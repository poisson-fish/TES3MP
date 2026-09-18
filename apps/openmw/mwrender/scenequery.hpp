#ifndef OPENMW_MWRENDER_SCENEQUERY_H
#define OPENMW_MWRENDER_SCENEQUERY_H

#include "../mwworld/ptr.hpp"
#include <osg/Camera>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>
#include <span>

namespace MWRender
{
    struct SceneRayResult
    {
        bool mHit = false;
        osg::Vec3f mHitNormalWorld;
        osg::Vec3f mHitPointWorld;
        MWWorld::Ptr mHitObject;
        ESM::RefNum mHitRefnum;
        float mRatio = 0;
    };

    // Shared stock scene picking. No viewer, Environment, UI or graphics context required.
    SceneRayResult getIntersectionResult(osgUtil::LineSegmentIntersector* intersector,
        std::span<const MWWorld::Ptr> ignoreList = {});
    unsigned int sceneQueryMask(bool ignorePlayer, bool ignoreActors);
    osg::ref_ptr<osgUtil::LineSegmentIntersector> cameraRayIntersector(
        const osg::Matrixd& projection, float x, float y, float maxDistance);
    SceneRayResult castSceneRay(osg::Node& root, const osg::Vec3f& from, const osg::Vec3f& to);
    SceneRayResult castSceneCameraRay(osg::Camera& camera, float x, float y, float maxDistance);

    class IntersectionVisitorWithIgnoreList : public osgUtil::IntersectionVisitor
    {
    public:
        bool skipTransform(osg::Transform& transform);
        void apply(osg::Transform& transform) override;
        void setIgnoreList(std::span<const MWWorld::Ptr> ignoreList) { mIgnoreList = ignoreList; }
        void setContainsPagedRefs(bool contains) { mContainsPagedRefs = contains; }
    private:
        std::span<const MWWorld::Ptr> mIgnoreList;
        bool mContainsPagedRefs = false;
    };
}
#endif
