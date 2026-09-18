#include <apps/openmw/mwworld/itemplacement.hpp>
#include <apps/openmw/mwrender/scenequery.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <osg/Geometry>
#include <osg/ShapeDrawable>
#include <cmath>
#include <stdexcept>
#include "placement_tests.hpp"
#include <osgDB/WriteFile>

namespace TES3MP::Native::Testing
{
    void writePlacementFixtureModels(const std::filesystem::path& scratch)
    {
        std::filesystem::create_directories(scratch / "meshes");
        const auto write = [&](osg::Node* node, const char* name) {
            osg::ref_ptr<osg::Node> owned = node;
            if (!osgDB::writeNodeFile(*node, (scratch / "meshes" / name).string()))
                throw std::runtime_error("Could not write synthetic placement geometry");
        };
        write(new osg::ShapeDrawable(new osg::Box({3,4,5},2,4,6)), "placement-item.osgt");
        write(new osg::ShapeDrawable(new osg::Box({8,10,5},8,12,6)), "placement-gold.osgt");
        const auto plane = [](float half) {
            const osg::Vec3f vertices[]{{-half,-half,0},{half,-half,0},{half,half,0},{-half,half,0}};
            auto* mesh = new osg::Geometry;
            mesh->setVertexArray(new osg::Vec3Array(std::begin(vertices),std::end(vertices)));
            mesh->addPrimitiveSet(new osg::DrawArrays(GL_QUADS,0,4));
            return mesh;
        };
        write(plane(500), "placement-floor.osgt");
        write(plane(35), "placement-table.osgt");
    }
    DropPlacementView placementTestView(float x, float z, bool miss)
    {
        const auto view = osg::Matrixd::lookAt(osg::Vec3d(x,-120,100),osg::Vec3d(x,0,miss ? 200 : z),osg::Vec3d(0,0,1));
        const auto projection = osg::Matrixd::perspective(60,1,5,1000);
        DropPlacementView result;
        std::copy_n(view.ptr(),16,result.view.begin());
        std::copy_n(projection.ptr(),16,result.projection.begin());
        return result;
    }
    void checkItemPlacement()
    {
        const auto require = [](bool value, const char* message) {
            if (!value) throw std::runtime_error(message);
        };
        osg::ref_ptr<osg::Geometry> floor = new osg::Geometry;
        floor->setNodeMask(MWRender::Mask_Terrain);
        const osg::Vec3f vertices[]{{-500,-500,0},{500,-500,0},{500,500,0},{-500,500,0}};
        floor->setVertexArray(new osg::Vec3Array(std::begin(vertices), std::end(vertices)));
        floor->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));
        osg::ref_ptr<osg::Group> scene = new osg::Group;
        scene->addChild(floor);
        const auto ray = [&](const osg::Vec3f& from, const osg::Vec3f& to) {
            const auto hit = MWRender::castSceneRay(*scene, from, to);
            return MWWorld::ItemPlacementHit{hit.mHit, hit.mHitPointWorld, hit.mHitNormalWorld};
        };
        ESM::Position actor{{0,0,60},{0.4f,0.2f,0.9f}};
        const auto ground = MWWorld::groundItemPlacement(actor, ray);
        require(ground.asVec3() == osg::Vec3f(0,0,0) && ground.rot[0] == 0
            && ground.rot[1] == 0 && ground.rot[2] == actor.rot[2], "Ground placement or upright yaw changed");
        const auto miss = MWWorld::groundItemPlacement(actor, [&](auto from, auto to) {
            require(from == osg::Vec3f(0,0,80) && to == osg::Vec3f(0,0,-999920), "Stock downward ray changed");
            return MWWorld::ItemPlacementHit{};
        });
        require(miss.asVec3() == actor.asVec3(), "Ground miss must keep actor position");
        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setViewMatrixAsLookAt({0,-100,100}, {0,0,0}, {0,0,1});
        camera->setProjectionMatrixAsPerspective(60, 1, 5, 1000);
        camera->addChild(scene);
        auto hit = MWRender::castSceneCameraRay(*camera, .5f, .5f, MWWorld::ItemPlacementDistance);
        require(hit.mHit && hit.mHitPointWorld.length() < .001f, "Stock camera floor ray missed");
        require(MWWorld::canPlaceItem({hit.mHit, hit.mHitPointWorld, hit.mHitNormalWorld}), "Floor rejected");
        camera->setViewMatrixAsLookAt({0,-300,300}, {0,0,0}, {0,0,1});
        require(!MWRender::castSceneCameraRay(*camera,.5f,.5f,MWWorld::ItemPlacementDistance).mHit,
            "Camera query exceeded stock distance");
        for (float degrees : {0.f, 29.f, 30.f, 31.f, 90.f})
        {
            const float angle = osg::DegreesToRadians(degrees);
            const bool allowed = MWWorld::canPlaceItem({true, {}, {std::sin(angle),0,std::cos(angle)}});
            require(allowed == (degrees < 30), "Stock slope boundary changed");
        }
        require(!MWWorld::canPlaceItem({}), "Cursor miss must select ground fallback");
        osg::ref_ptr<osg::Group> empty = new osg::Group;
        require(!MWWorld::adjustItemPlacement(actor,*empty), "Missing bounds must preserve the stock no-adjustment path");
        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> item = new SceneUtil::PositionAttitudeTransform;
        item->setPosition({100,200,40});
        item->addChild(new osg::ShapeDrawable(new osg::Box({3,4,5},2,4,6)));
        ESM::Position position{{100,200,40},{0,0,0}};
        require(MWWorld::adjustItemPlacement(position,*item)->asVec3() == osg::Vec3f(97,196,38),
            "Item bounds must center XY and align the bottom to the surface");
        require(item->getPosition() == position.asVec3(), "Placement query mutated its scene");
        item->setScale({2,2,2});
        require(MWWorld::adjustItemPlacement(position,*item)->asVec3() == osg::Vec3f(94,192,36),
            "Scaled item bounds were ignored");
        osg::ref_ptr<osg::ShapeDrawable> particle = new osg::ShapeDrawable(new osg::Box({0,0,-1000},100));
        particle->setNodeMask(MWRender::Mask_ParticleSystem);
        item->addChild(particle);
        require(MWWorld::adjustItemPlacement(position,*item)->asVec3() == osg::Vec3f(94,192,36),
            "Particles altered placement bounds");
    }
}
