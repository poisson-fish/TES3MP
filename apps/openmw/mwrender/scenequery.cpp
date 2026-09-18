#include "scenequery.hpp"
#include "objects.hpp"
#include "objectpaging.hpp"
#include "vismask.hpp"
#include <osg/UserDataContainer>
#include <algorithm>

namespace MWRender
{
    SceneRayResult getIntersectionResult(osgUtil::LineSegmentIntersector* intersector,
        std::span<const MWWorld::Ptr> ignoreList)
    {
        constexpr auto nonObjectWorldMask = Mask_Terrain | Mask_Water;
        SceneRayResult result;
        result.mHit = false;
        result.mRatio = 0;

        if (!intersector->containsIntersections())
            return result;

        auto test = [&](const osgUtil::LineSegmentIntersector::Intersection& intersection) {
            PtrHolder* ptrHolder = nullptr;
            std::vector<RefnumMarker*> refnumMarkers;
            bool hitNonObjectWorld = false;
            for (osg::Node* node : intersection.nodePath)
            {
                const auto& nodeMask = node->getNodeMask();
                if (!hitNonObjectWorld)
                    hitNonObjectWorld = nodeMask & nonObjectWorldMask;

                osg::UserDataContainer* userDataContainer = node->getUserDataContainer();
                if (!userDataContainer)
                    continue;
                for (unsigned int i = 0; i < userDataContainer->getNumUserObjects(); ++i)
                {
                    if (PtrHolder* p = dynamic_cast<PtrHolder*>(userDataContainer->getUserObject(i)))
                    {
                        if (std::find(ignoreList.begin(), ignoreList.end(), p->mPtr) == ignoreList.end())
                        {
                            ptrHolder = p;
                        }
                    }
                    if (RefnumMarker* r = dynamic_cast<RefnumMarker*>(userDataContainer->getUserObject(i)))
                    {
                        refnumMarkers.push_back(r);
                    }
                }
            }

            if (ptrHolder)
                result.mHitObject = ptrHolder->mPtr;

            unsigned int vertexCounter = 0;
            for (unsigned int i = 0; i < refnumMarkers.size(); ++i)
            {
                unsigned int intersectionIndex = intersection.indexList.empty() ? 0 : intersection.indexList[0];
                if (!refnumMarkers[i]->mNumVertices
                    || (intersectionIndex >= vertexCounter
                        && intersectionIndex < vertexCounter + refnumMarkers[i]->mNumVertices))
                {
                    auto it = std::find_if(
                        ignoreList.begin(), ignoreList.end(), [target = refnumMarkers[i]->mRefnum](const auto& ptr) {
                            return target == ptr.getCellRef().getRefNum();
                        });

                    if (it == ignoreList.end())
                    {
                        result.mHitRefnum = refnumMarkers[i]->mRefnum;
                    }

                    break;
                }
                vertexCounter += refnumMarkers[i]->mNumVertices;
            }

            if (!result.mHitObject.isEmpty() || result.mHitRefnum.isSet() || hitNonObjectWorld)
            {
                result.mHit = true;
                result.mHitPointWorld = intersection.getWorldIntersectPoint();
                result.mHitNormalWorld = intersection.getWorldIntersectNormal();
                result.mRatio = static_cast<float>(intersection.ratio);
            }
        };

        if (ignoreList.empty() || intersector->getIntersectionLimit() != osgUtil::LineSegmentIntersector::NO_LIMIT)
        {
            test(intersector->getFirstIntersection());
        }
        else
        {
            for (const auto& intersection : intersector->getIntersections())
            {
                test(intersection);

                if (result.mHit)
                {
                    break;
                }
            }
        }

        return result;
    }

    bool IntersectionVisitorWithIgnoreList::skipTransform(osg::Transform& transform)
    {
        if (mContainsPagedRefs)
            return false;

        osg::UserDataContainer* userDataContainer = transform.getUserDataContainer();
        if (!userDataContainer)
            return false;

        for (unsigned int i = 0; i < userDataContainer->getNumUserObjects(); ++i)
        {
            if (PtrHolder* p = dynamic_cast<PtrHolder*>(userDataContainer->getUserObject(i)))
            {
                if (std::find(mIgnoreList.begin(), mIgnoreList.end(), p->mPtr) != mIgnoreList.end())
                {
                    return true;
                }
            }
        }

        return false;
    }

    void IntersectionVisitorWithIgnoreList::apply(osg::Transform& transform)
    {
        if (skipTransform(transform))
        {
            return;
        }
        osgUtil::IntersectionVisitor::apply(transform);
    }

    unsigned int sceneQueryMask(bool ignorePlayer, bool ignoreActors)
    {
        unsigned int mask = ~0u;
        mask &= ~(Mask_RenderToTexture | Mask_Sky | Mask_Debug | Mask_Effect | Mask_Water | Mask_SimpleWater
            | Mask_Groundcover | Mask_ReplicatedActor);
        if (ignorePlayer)
            mask &= ~(Mask_Player);
        if (ignoreActors)
            mask &= ~(Mask_Actor | Mask_Player);

        return mask;
    }
    osg::ref_ptr<osgUtil::LineSegmentIntersector> cameraRayIntersector(
        const osg::Matrixd& projection, float nX, float nY, float maxDistance)
    {
        osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector(new osgUtil::LineSegmentIntersector(
            osgUtil::LineSegmentIntersector::PROJECTION, nX * 2.f - 1.f, nY * (-2.f) + 1.f));

        osg::Vec3d dist(0.f, 0.f, -maxDistance);

        dist = dist * projection;

        osg::Vec3d end = intersector->getEnd();
        end.z() = dist.z();
        intersector->setEnd(end);
        intersector->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);

        return intersector;
    }
    SceneRayResult castSceneRay(osg::Node& root, const osg::Vec3f& from, const osg::Vec3f& to)
    {
        osg::ref_ptr<osgUtil::LineSegmentIntersector> ray = new osgUtil::LineSegmentIntersector(
            osgUtil::LineSegmentIntersector::MODEL, from, to);
        ray->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);
        osgUtil::IntersectionVisitor visitor(ray);
        visitor.setTraversalMask(sceneQueryMask(true, true));
        root.accept(visitor);
        return getIntersectionResult(ray);
    }
    SceneRayResult castSceneCameraRay(osg::Camera& camera, float x, float y, float maxDistance)
    {
        auto ray = cameraRayIntersector(camera.getProjectionMatrix(), x, y, maxDistance);
        osgUtil::IntersectionVisitor visitor(ray);
        visitor.setTraversalMask(sceneQueryMask(true, true));
        camera.accept(visitor);
        return getIntersectionResult(ray);
    }
}
