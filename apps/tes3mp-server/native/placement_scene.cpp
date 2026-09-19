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
#include <components/esmterrain/storage.hpp>
#include <components/terrain/buffercache.hpp>
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
#include <osg/Geometry>

namespace TES3MP::Native
{
    namespace
    {
        // Adapt the retained ESM store to OpenMW's terrain conversion. No global
        // World/renderer, terrain textures, or alternate height interpolation.
        class TerrainStorage final : public ESMTerrain::Storage
        {
            const MWWorld::ESMStore& mStore;
        public:
            TerrainStorage(const MWWorld::ESMStore& store, const VFS::Manager& vfs)
                : ESMTerrain::Storage(&vfs), mStore(store) {}
            osg::ref_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cell) override
            {
                const auto* land = mStore.get<ESM::Land>().search(cell.mX, cell.mY);
                return land ? new ESMTerrain::LandObject(*land,
                    ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VCLR) : nullptr;
            }
            const std::string* getLandTexture(uint16_t index, int plugin) override
            { return mStore.get<ESM::LandTexture>().search(index, plugin); }
            void getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId) override
            {
                minX = maxX = minY = maxY = 0;
                for (const auto& land : mStore.get<ESM::Land>())
                {
                    minX = std::min(minX, float(land.mX)); maxX = std::max(maxX, float(land.mX));
                    minY = std::min(minY, float(land.mY)); maxY = std::max(maxY, float(land.mY));
                }
                ++maxX; ++maxY;
            }
        };

        std::string addTerrain(osg::Group& scene, Loadout& loadout, const VFS::Manager& vfs,
            const ESM::ESM3ExteriorCellRefId& cell)
        {
            TerrainStorage terrain(loadout.store(), vfs);
            const auto worldspace = ESM::Cell::sDefaultWorldspaceId;
            const osg::Vec2f center(cell.getX() + .5f, cell.getY() + .5f);
            osg::ref_ptr<osg::Vec3Array> positions = new osg::Vec3Array, normals = new osg::Vec3Array;
            osg::ref_ptr<osg::Vec4ubArray> colours = new osg::Vec4ubArray;
            terrain.fillVertexBuffers(0, 1.f, center, worldspace, *positions, *normals, *colours);
            std::string heights;
            for (const auto& position : *positions)
            {
                if (!std::isfinite(position.z())) throw std::invalid_argument("Native terrain height is not finite");
                const float height = position.z();
                heights.append(reinterpret_cast<const char*>(&height), sizeof(height));
            }
            Terrain::BufferCache buffers;
            osg::ref_ptr<osg::Geometry> mesh = new osg::Geometry;
            mesh->setVertexArray(positions);
            mesh->addPrimitiveSet(buffers.getIndexBuffer(terrain.getCellVertices(worldspace), 0));
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node = new SceneUtil::PositionAttitudeTransform;
            node->setPosition({center.x() * terrain.getCellWorldSize(worldspace),
                center.y() * terrain.getCellWorldSize(worldspace), 0});
            node->setNodeMask(MWRender::Mask_Terrain);
            node->addChild(mesh);
            scene.addChild(node);
            std::istringstream input(heights);
            const auto digest = Files::getHash("native-terrain", input);
            return "\nstock-terrain-1:" + std::to_string(cell.getX()) + ':' + std::to_string(cell.getY())
                + ':' + std::to_string(digest[0]) + ':' + std::to_string(digest[1]) + '\n';
        }
    }
    struct PlacementScene::Impl
    {
        Loadout& mLoadout;
        ESM::RefId mCell;
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

        Impl(Loadout& loadout, ESM::RefId cell, std::span<const ESM::CellRef> worldDomain)
            : mLoadout(loadout), mCell(cell), mResources(&mVfs, 0, &loadout.encoder()),
              mWorld(loadout.store(), loadout.readers(), 1)
        {
            validateCell(cell);
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
            mWorld.getCell(cell).forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                    || ptr.getClass().isActor() || suppressed.contains(ptr.getCellRef().getRefNum())
                    || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId())) return true;
                if (++count > 8192) throw std::invalid_argument("Native placement scene exceeds reference budget");
                if (!ptr.getClass().getScript(ptr).empty())
                    throw std::invalid_argument("Native placement scene requires unavailable script services in "
                        + cell.toDebugString() + " for " + ptr.getCellRef().getRefId().toDebugString()
                        + " (" + ptr.getClass().getScript(ptr).toDebugString() + ")");
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
            if (const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>())
                mFingerprint += addTerrain(*mStatic, loadout, mVfs, *exterior);
        }

        ESM::Position resolve(const ESM::Position& actor, const MWWorld::Ptr& item,
            const DropPlacementView& input, std::span<const ESM::ObjectState> worldItems)
        {
            if (!validDropPlacementView(input) || worldItems.size() > 192)
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
            // Until adjacent-cell streaming owns membership, an exterior drop
            // cannot create a reference in a different cell's coordinate range.
            if (const auto* exterior = mCell.getIf<ESM::ESM3ExteriorCellRefId>())
                if (std::floor(double(position.pos[0]) / ESM::Cell::sSize) != exterior->getX()
                    || std::floor(double(position.pos[1]) / ESM::Cell::sSize) != exterior->getY())
                    throw std::invalid_argument("Native exterior drop crosses the bound cell");
            return position;
        }
    };
    PlacementScene::PlacementScene(Loadout& loadout, ESM::RefId cell, std::span<const ESM::CellRef> worldDomain)
        : mImpl(std::make_unique<Impl>(loadout, cell, worldDomain)) {}
    PlacementScene::~PlacementScene() = default;
    const std::string& PlacementScene::fingerprint() const { return mImpl->mFingerprint; }
    ESM::Position PlacementScene::resolve(const ESM::Position& actor, const MWWorld::Ptr& item,
        const DropPlacementView& view, std::span<const ESM::ObjectState> worldItems)
    {
        return mImpl->resolve(actor, item, view, worldItems);
    }
}
