#include "placement_scene.hpp"
#include "loadout.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/itemplacement.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwrender/scenequery.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/files/hash.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/nifosg/nifloader.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>
#include <cmath>
#include <set>
#include <sstream>

namespace TES3MP::Native
{
    struct PlacementScene::Impl
    {
        Loadout& mLoadout;
        VFS::Manager mVfs;
        Resource::ResourceSystem mResources;
        MWWorld::WorldModel mWorld;
        osg::ref_ptr<osg::Group> mStatic = new osg::Group;
        std::string mFingerprint;

        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node(const MWWorld::Ptr& ptr, const ESM::Position& pos)
        {
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> result = new SceneUtil::PositionAttitudeTransform;
            result->setNodeMask(MWRender::Mask_Object);
            result->setPosition(pos.asVec3());
            result->setAttitude(Misc::Convert::makeOsgQuat(pos));
            const float scale = ptr.getCellRef().getScale();
            result->setScale({scale,scale,scale});
            result->getOrCreateUserDataContainer()->addUserObject(new MWRender::PtrHolder(ptr));
            const auto model = ptr.getClass().getCorrectedModel(ptr);
            if (!model.empty()) result->addChild(mResources.getSceneManager()->getInstance(model));
            return result;
        }

        Impl(Loadout& loadout, std::string_view cell, std::span<const ESM::CellRef> worldDomain)
            : mLoadout(loadout), mResources(&mVfs, 0, &loadout.encoder()),
              mWorld(loadout.store(), loadout.readers(), 1)
        {
            MWClass::registerClasses();
            VFS::registerArchives(&mVfs, Files::Collections(loadout.options().mDataPaths),
                loadout.options().mArchives, true, &loadout.encoder());
            mResources.getSceneManager()->setShaderGenerationEnabled(false);
            mResources.getSceneManager()->setParticleSystemMask(MWRender::Mask_ParticleSystem);
            NifOsg::Loader::setHiddenNodeMask(MWRender::Mask_UpdateVisitor);
            NifOsg::Loader::setIntersectionDisabledNodeMask(MWRender::Mask_Effect);
            std::set<ESM::RefNum> suppressed;
            for (const auto& ref : worldDomain) suppressed.insert(ref.mRefNum);
            std::set<std::string> meshes;
            // Bind potential dropped-item meshes as well as the supporting scene.
            // Textures remain presentation-only; these are the resolved model bytes.
            const auto remember = [&](const MWWorld::Ptr& ptr) {
                const auto mesh = ptr.getClass().getCorrectedModel(ptr);
                if (!mesh.empty()) meshes.emplace(mesh.value());
            };
            for (const auto& [id, record] : MWWorld::inventoryRecords(loadout.store()))
            {
                MWWorld::ManualRef item(loadout.store(), record);
                remember(item.getPtr());
            }
            size_t count = 0;
            mWorld.getInterior(cell).forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                    || ptr.getClass().isActor() || suppressed.contains(ptr.getCellRef().getRefNum())
                    || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId())) return true;
                if (++count > 8192) throw std::invalid_argument("Native placement scene exceeds reference budget");
                if (!ptr.getClass().getScript(ptr).empty())
                    throw std::invalid_argument("Native placement scene requires unavailable script services");
                remember(ptr);
                mStatic->addChild(node(ptr, ptr.getRefData().getPosition()));
                return true;
            });
            if (meshes.size() > 32768) throw std::invalid_argument("Native placement model budget exceeded");
            meshes.emplace("meshes/marker_error.nif");
            std::ostringstream fingerprint;
            fingerprint << "stock-placement-scene-1\n";
            for (const auto& mesh : meshes)
            {
                auto stream = mVfs.find(VFS::Path::toNormalized(mesh));
                fingerprint << mesh.size() << ':' << mesh << ':';
                if (stream)
                {
                    const auto digest = Files::getHash(mesh, *stream);
                    fingerprint << digest[0] << ':' << digest[1];
                }
                else fingerprint << "missing";
                fingerprint << '\n';
            }
            mFingerprint = fingerprint.str();
        }

        ESM::Position resolve(const ESM::Position& actor, const MWWorld::Ptr& item,
            const DropPlacementView& input, std::span<const ESM::ObjectState> worldItems)
        {
            if (!validDropPlacementView(input) || worldItems.size() > 64)
                throw std::invalid_argument("Placement input outside representation bounds");
            osg::Matrixd view(input.view.data()), projection(input.projection.data()), inverse;
            for (const auto& matrix : {view,projection})
            {
                if (!inverse.invert(matrix))
                    throw std::invalid_argument("Placement camera matrix is singular");
                for (unsigned i = 0; i < 16; ++i)
                    if (!std::isfinite(inverse.ptr()[i]))
                        throw std::invalid_argument("Placement camera inverse is not finite");
            }
            // Reject malformed camera matrices; retain stock perspective and camera distance semantics.
            if (view(0,3) != 0 || view(1,3) != 0 || view(2,3) != 0 || view(3,3) != 1
                || projection(2,3) != -1 || projection(3,3) != 0)
                throw std::invalid_argument("Placement requires a perspective camera and affine view");
            for (int i = 0; i < 3; ++i)
                for (int j = i; j < 3; ++j)
                {
                    double dot = 0;
                    for (int k = 0; k < 3; ++k) dot += view(i,k)*view(j,k);
                    if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 1e-4)
                        throw std::invalid_argument("Placement camera view is not rigid");
                }
            osg::ref_ptr<osg::Group> scene = new osg::Group;
            scene->addChild(mStatic);
            std::vector<std::unique_ptr<MWWorld::ManualRef>> references;
            for (const auto& state : worldItems)
            {
                auto& ref = references.emplace_back(std::make_unique<MWWorld::ManualRef>(mLoadout.store(), state.mRef.mRefID));
                auto ptr = ref->getPtr();
                ptr.getCellRef() = MWWorld::CellRef(state.mRef);
                scene->addChild(node(ptr, state.mRef.mPos));
            }
            osg::ref_ptr<osg::Camera> camera = new osg::Camera;
            camera->setViewMatrix(view);
            camera->setProjectionMatrix(projection);
            camera->addChild(scene);
            const auto hit = MWRender::castSceneCameraRay(*camera, input.cursorX, input.cursorY, MWWorld::ItemPlacementDistance);
            const MWWorld::ItemPlacementHit cursor{hit.mHit, hit.mHitPointWorld, hit.mHitNormalWorld};
            auto position = MWWorld::canPlaceItem(cursor) ? MWWorld::cursorItemPlacement(actor, cursor)
                : MWWorld::groundItemPlacement(actor, [&](const auto& from, const auto& to) {
                    const auto ground = MWRender::castSceneRay(*scene, from, to);
                    return MWWorld::ItemPlacementHit{ground.mHit, ground.mHitPointWorld, ground.mHitNormalWorld};
                });
            const auto model = node(item, position);
            position = MWWorld::adjustItemPlacement(position, *model).value_or(position);
            // Commit at the baseline's precision so both clients and recovery share one position.
            for (float& value : position.pos)
            {
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Resolved placement outside world bounds");
                value = float(double(std::llround(double(value) * 1024)) / 1024);
            }
            return position;
        }
    };
    PlacementScene::PlacementScene(Loadout& loadout, std::string_view cell, std::span<const ESM::CellRef> worldDomain)
        : mImpl(std::make_unique<Impl>(loadout, cell, worldDomain)) {}
    PlacementScene::~PlacementScene() = default;
    const std::string& PlacementScene::fingerprint() const { return mImpl->mFingerprint; }
    ESM::Position PlacementScene::resolve(const ESM::Position& actor, const MWWorld::Ptr& item,
        const DropPlacementView& view, std::span<const ESM::ObjectState> worldItems)
    {
        return mImpl->resolve(actor, item, view, worldItems);
    }
}
