#include "actor_scene.hpp"
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <components/esm3/loadweap.hpp>
#include "actor_inventory.hpp"
#include "loadout.hpp"
#include "actor_spawns.hpp"
#include <bit>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwclass/npcmodel.hpp>
#include <apps/openmw/mwphysics/actorshape.hpp>
#include <apps/openmw/mwphysics/collisioneffects.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/doorcontact.hpp>
#include <apps/openmw/mwphysics/movementdata.hpp>
#include <apps/openmw/mwphysics/movementsolver.hpp>
#include <apps/openmw/mwmechanics/pathfinding.hpp>
#include <apps/openmw/mwmechanics/breathing.hpp>
#include <apps/openmw/mwmechanics/dooravoidance.hpp>
#include <apps/openmw/mwmechanics/steering.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/bullethelpers/collisionobject.hpp>
#include <components/files/hash.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/flags.hpp>
#include <components/detournavigator/navigatorutils.hpp>
#include <components/settings/categories/navigator.hpp>
#include <components/settings/parser.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadland.hpp>
#include <components/bullethelpers/heightfield.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/resource/keyframemanager.hpp>
#include <components/sceneutil/keyframe.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/vfs/registerarchives.hpp>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <fstream>

namespace TES3MP::Native
{
    namespace
    {
        constexpr size_t MaxBodies = 8192;
        constexpr size_t MaxContacts = 64;

        void validatePosition(const ESM::Position& position, float scale)
        {
            if (!std::isfinite(scale) || scale <= 0.f || scale > 100.f)
                throw std::invalid_argument("Collision reference scale outside bounds");
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(position.pos[i]) || std::abs(position.pos[i]) > 1e7f
                    || !std::isfinite(position.rot[i]) || std::abs(position.rot[i]) > 1e4f)
                    throw std::invalid_argument("Collision reference transform outside bounds");
        }

        // A bounded per-step journal. This scene contains no projectiles: seeing
        // one is an unsupported-domain error, never a discarded gameplay effect.
        class Contacts final : public MWPhysics::CollisionEffects
        {
            const std::map<const btCollisionObject*, uint64_t>& mIdentities;
            [[noreturn]] static void unsupported() { throw std::logic_error("Projectile outside interior NPC domain"); }
        public:
            std::vector<uint64_t> mObjects;
            explicit Contacts(const std::map<const btCollisionObject*, uint64_t>& identities) : mIdentities(identities)
            { mObjects.reserve(MaxContacts); }
            void objectCollision(const btCollisionObject* object, bool) override
            {
                if (object->getBroadphaseHandle()->m_collisionFilterGroup == MWPhysics::CollisionType_Water) return;
                const auto id = mIdentities.at(object);
                if (std::find(mObjects.begin(), mObjects.end(), id) != mObjects.end()) return;
                if (mObjects.size() == MaxContacts) throw std::length_error("Interior contact budget exceeded");
                mObjects.push_back(id);
            }
            bool projectileActive(const btCollisionObject*) const override { unsupported(); }
            bool validProjectileTarget(const btCollisionObject*, const btCollisionObject*) const override { unsupported(); }
            const btCollisionObject* projectileCaster(const btCollisionObject*) const override { unsupported(); }
            void hit(const btCollisionObject*, const btCollisionObject*, const btVector3&, const btVector3&) override
            { unsupported(); }
            void hitWater(const btCollisionObject*) override { unsupported(); }
        };
    }

    struct InteriorActorScene::Impl
    {
        const MWWorld::ESMStore& mStore;
        // Prepared frames cannot outlive and later match a reallocated scene.
        std::shared_ptr<const char> mLifetime = std::make_shared<const char>(0);
        struct Body
        {
            btCollisionWorld& mWorld;
            osg::ref_ptr<Resource::BulletShapeInstance> mResource;
            std::unique_ptr<btConvexShape> mHull;
            std::unique_ptr<btCollisionObject> mObject;
            explicit Body(btCollisionWorld& world) : mWorld(world) {}
            ~Body() { if (mObject && mObject->getBroadphaseHandle()) mWorld.removeCollisionObject(mObject.get()); }
        };
        VFS::Manager mVfs;
        Resource::ResourceSystem mResources;
        std::unique_ptr<Resource::BulletShapeManager> mShapes;
        MWWorld::WorldModel mReferences;
        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher{ &mConfiguration };
        btDbvtBroadphase mBroadphase;
        btCollisionWorld mWorld{ &mDispatcher, &mBroadphase, &mConfiguration };
        std::unique_ptr<btStaticPlaneShape> mWaterShape;
        std::unique_ptr<btCollisionObject> mWaterObject;
        std::optional<float> mInteriorWater;
        std::vector<std::unique_ptr<Body>> mBodies;
        struct Terrain
        {
            btCollisionWorld& world;
            osg::Vec2i cell;
            std::array<float, ESM::Land::LAND_NUM_VERTS> heights;
            std::vector<btScalar> collisionHeights;
            float minimum, maximum, water;
            std::unique_ptr<btHeightfieldTerrainShape> shape;
            std::unique_ptr<btCollisionObject> object;
            explicit Terrain(btCollisionWorld& value) : world(value) {}
            ~Terrain() { if (object && object->getBroadphaseHandle()) world.removeCollisionObject(object.get()); }
        };
        std::vector<std::unique_ptr<Terrain>> mTerrain;
        std::vector<ESM::RefId> mCells;
        std::map<const btCollisionObject*, uint64_t> mIdentities;
        std::unique_ptr<MWPhysics::ActorFrameData> mActor;
        osg::Vec3f mActorOffset;
        uint64_t mActorId;
        std::vector<uint64_t> mContacts;
        std::string mFingerprint;
        ESM::RefId mActorBase;
        // Stock third-person base, male, female, beast and Argonian swim layers.
        std::array<VFS::Path::Normalized, 5> mHitModels;
        bool mAdditionalHitSources = false;
        DetourNavigator::AgentBounds mAgentBounds;
        std::unique_ptr<DetourNavigator::Navigator> mNavigator;
        MWMechanics::PathFinder mPath;
        std::vector<DetourNavigator::ObjectTransform> mTransforms;
        std::map<uint64_t, size_t> mOrdinaryDoors;
        std::vector<ActorSceneDoor> mDoors;
        bool mAvoidanceEnabled = false;
        bool mSmoothMovement = false;
        bool mWaterNavigation = false;
        bool mEnchantedWeaponsAreMagical = false;
        bool mOnlyAppropriateAmmunitionBypassesResistance = false;
        bool mUncappedDamageFatigue = false;
        bool mClassicReflectedAbsorb = true;
        struct Travel
        {
            osg::Vec3f destination;
            bool hasDestination = false;
            uint64_t door = 0;
            MWMechanics::DoorAvoidance avoidance;
            Misc::Rng::Generator random;
            float breath = -1.f;
            bool drowning = false;
        } mTravel;
        // Derived cache only. A rejected candidate may warm it; every query
        // synchronizes it to its own complete angle image before using it.
        std::vector<ActorSceneDoor> mNavigationDoors;
        bool mNavigationValid = false;

        DetourNavigator::Flags navigationFlags() const
        { return mWaterNavigation && (mInteriorWater || !mTerrain.empty())
            ? DetourNavigator::Flags(DetourNavigator::Flag_walk | DetourNavigator::Flag_swim)
            : DetourNavigator::Flag_walk; }

        ~Impl()
        { if (mWaterObject) mWorld.removeCollisionObject(mWaterObject.get()); }

        float waterAt(const osg::Vec3f& position) const
        {
            if (mInteriorWater) return *mInteriorWater;
            const int x = int(std::floor(double(position.x()) / ESM::Land::REAL_SIZE));
            const int y = int(std::floor(double(position.y()) / ESM::Land::REAL_SIZE));
            for (const auto& terrain : mTerrain)
                if (terrain->cell.x() == x && terrain->cell.y() == y) return terrain->water;
            return -std::numeric_limits<float>::max();
        }

        MWPhysics::ActorFrameData movementFrame(const MWPhysics::ActorFrameData& frame,
            const ActorMovement& movement) const
        {
            const float water = waterAt(frame.mPosition);
            const float swimScale = mStore.get<ESM::GameSetting>().find("fSwimHeightScale")->mValue.getFloat();
            return {.mPosition = frame.mPosition, .mInertia = frame.mInertia,
                .mStandingOn = frame.mStandingOn, .mIsOnGround = frame.mIsOnGround,
                .mIsOnSlope = frame.mIsOnSlope, .mWalkingOnWater = frame.mWalkingOnWater,
                .mCollisionObject = frame.mCollisionObject,
                .mSwimLevel = water - frame.mHalfExtentsZ * 2.f * swimScale,
                .mSlowFall = movement.slowFall, .mRotation = frame.mRotation,
                .mMovement = frame.mMovement, .mLastStuckPosition = frame.mLastStuckPosition,
                .mWaterlevel = water, .mHalfExtentsZ = frame.mHalfExtentsZ,
                .mOldHeight = frame.mOldHeight, .mStuckFrames = frame.mStuckFrames,
                .mFlying = movement.levitating, .mWasOnGround = frame.mIsOnGround,
                .mWaterCollision = movement.waterWalking && water > -1e30f};
        }

        float movementSpeed(const MWPhysics::ActorFrameData& frame, const ActorMovement& movement) const
        {
            if (movement.levitating) return movement.flySpeed;
            if (frame.mPosition.z() < frame.mSwimLevel) return movement.swimSpeed;
            return movement.walkSpeed;
        }

        float jumpVelocity(const MWPhysics::ActorFrameData& frame, const ActorMovement& movement) const
        {
            return movement.enabled && movement.jumpRequested && frame.mIsOnGround
                && !movement.levitating && frame.mPosition.z() >= frame.mSwimLevel
                ? movement.jumpSpeed : 0.f;
        }

        void updateBreath(const MWPhysics::ActorFrameData& frame, Travel& travel,
            const ActorMovement& movement) const
        {
            if (!movement.enabled) return;
            const float water = waterAt(frame.mPosition);
            const float swimScale = mStore.get<ESM::GameSetting>().find("fSwimHeightScale")->mValue.getFloat();
            const float hold = mStore.get<ESM::GameSetting>().find("fHoldBreathTime")->mValue.getFloat();
            if (!(swimScale > 0.f) || !(hold > 0.f))
                throw std::invalid_argument("Native breath settings outside bounds");
            const bool submerged = water > frame.mPosition.z() + 2.f * frame.mHalfExtentsZ / swimScale;
            const bool knockedOutUnderwater = movement.unconscious && water > frame.mPosition.z();
            const auto next = MWMechanics::advanceBreath(travel.breath, hold, 1.f / 60.f,
                submerged, knockedOutUnderwater, movement.waterBreathing);
            travel.breath = next.remaining;
            travel.drowning = next.drowning;
        }

        void syncNavigation(std::span<const ActorSceneDoor> doors)
        {
            if (!mAvoidanceEnabled || !mNavigator) return;
            bool changed = !mNavigationValid;
            for (size_t i = 0; i < doors.size(); ++i)
                changed |= !mNavigationValid || doors[i].mAngle != mNavigationDoors[i].mAngle;
            if (!changed) return;
            mNavigationValid = false;
            {
                auto guard = mNavigator->makeUpdateGuard();
                for (const auto& door : doors)
                {
                    const auto index = mOrdinaryDoors.at(door.mId);
                    auto transform = mTransforms[index];
                    transform.mPosition.rot[2] = door.mAngle;
                    const auto& body = *mBodies[index];
                    mNavigator->updateObject(DetourNavigator::ObjectId(body.mObject.get()),
                        DetourNavigator::ObjectShapes(body.mResource, transform),
                        btTransform(Misc::Convert::toBullet(Misc::Convert::makeOsgQuat(transform.mPosition)),
                            Misc::Convert::toBullet(transform.mPosition.asVec3())), guard.get());
                }
                mNavigator->update(mActor->mPosition, guard.get());
            }
            mNavigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
            mNavigationDoors.assign(doors.begin(), doors.end());
            mNavigationValid = true;
        }

        void rebuildPath(MWMechanics::PathFinder& path, const osg::Vec3f& position, const Travel& travel)
        {
            path.clearPath();
            if (!travel.hasDestination) return;
            std::vector<osg::Vec3f> points;
            const auto status = DetourNavigator::findPath(*mNavigator, mAgentBounds, position,
                travel.destination, navigationFlags(),
                {}, 0, {}, std::back_inserter(points));
            if (points.size() > 2048) throw std::length_error("Interior path exceeds bound");
            // An obstructed destination remains pending, never a completed trip.
            if (status == DetourNavigator::Status::Success)
                for (const auto& point : points) path.addPointToPath(point);
        }

        void validateDoors(std::span<const ActorSceneDoor> doors) const
        {
            if (doors.size() != mDoors.size()) throw std::invalid_argument("Actor door domain mismatch");
            for (size_t i = 0; i < doors.size(); ++i)
            {
                if (doors[i].mId != mDoors[i].mId) throw std::invalid_argument("Actor door identity mismatch");
                const auto closed = mTransforms[mOrdinaryDoors.at(doors[i].mId)].mPosition.rot[2];
                const auto opened = MWWorld::doorMotion(MWWorld::DoorState::Opening, closed, closed, 1).mTargetAngle;
                if (!std::isfinite(doors[i].mAngle) || doors[i].mAngle < closed || doors[i].mAngle > opened)
                    throw std::invalid_argument("Actor door angle outside authored swing");
                if (doors[i].mAvoid && !doors[i].mMoving)
                    throw std::invalid_argument("Idle door requested NPC avoidance");
            }
        }
        void applyDoors(std::span<const ActorSceneDoor> doors) noexcept
        {
            for (const auto& door : doors)
            {
                const auto index = mOrdinaryDoors.find(door.mId)->second;
                auto position = mTransforms[index].mPosition;
                position.rot[2] = door.mAngle;
                auto* object = mBodies[index]->mObject.get();
                object->setWorldTransform(btTransform(Misc::Convert::toBullet(Misc::Convert::makeOsgQuat(position)),
                    Misc::Convert::toBullet(position.asVec3())));
                mWorld.updateSingleAabb(object);
            }
        }

        Impl(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation)
            : mStore(loadout.store()), mResources(&mVfs, 0, &loadout.encoder()),
              mShapes(new Resource::BulletShapeManager(&mVfs, mResources.getSceneManager(),
                  mResources.getNifFileManager(), 0)),
              mReferences(loadout.store(), loadout.readers(), 1), mActorId(actor)
        {
            if (cells.empty() || cells.size() > 9 || !actor
                || baseAnimation.empty() || baseAnimation.size() > 1024
                || beastAnimation.empty() || beastAnimation.size() > 1024)
                throw std::invalid_argument("Interior NPC binding outside bounds");
            mCells.assign(cells.begin(), cells.end());
            std::set<ESM::RefId> unique;
            const auto* anchor = cells.front().getIf<ESM::ESM3ExteriorCellRefId>();
            for (auto cell : cells)
            {
                const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>();
                if (!unique.insert(cell).second || (anchor ? (!exterior
                    || std::abs(int64_t(exterior->getX()) - anchor->getX()) > 1
                    || std::abs(int64_t(exterior->getY()) - anchor->getY()) > 1) : cells.size() != 1))
                    throw std::invalid_argument("Actor processing cells must fit one unique 3x3 neighborhood");
            }
            MWClass::registerClasses();
            // Non-NIF collision meshes use the stock scene importer; shader
            // generation is presentation and must not initialize here.
            mResources.getSceneManager()->setShaderGenerationEnabled(false);
            VFS::registerArchives(&mVfs, Files::Collections(loadout.options().mDataPaths),
                loadout.options().mArchives, true, &loadout.encoder());
            const VFS::Path::Normalized base(baseAnimation), beast(beastAnimation);
            std::set<std::string> meshes;
            std::set<uint64_t> identities;
            size_t references = 0;
            for (auto cellId : cells)
            {
            auto& cell = mReferences.getCell(cellId);
            if (!cell.isExterior() && cell.getCell()->hasWater())
            {
                const float level = cell.getWaterLevel();
                if (!std::isfinite(level)) throw std::invalid_argument("Actor water level outside bounds");
                mInteriorWater = level;
            }
            cell.forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                    || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId())) return true;
                if (++references > MaxBodies) throw std::length_error("Interior collision reference budget exceeded");
                const auto& cls = ptr.getClass();
                const auto resolvedId = MWWorld::placedRefId(ptr.getCellRef().getRefNum(), loadout.options().mContent);
                if (!resolvedId) throw std::invalid_argument("Collision reference identity invalid");
                const auto id = *resolvedId;
                if (!identities.insert(id).second) throw std::invalid_argument("Duplicate collision placement across cells");
                const bool npc = ptr.getType() == ESM::NPC::sRecordId;
                const bool actorBody = npc || ptr.getType() == ESM::Creature::sRecordId;
                if (actorBody && !npc && (!(ptr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Bipedal)
                    || (ptr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Flies)))
                    throw std::invalid_argument("Creature collision currently requires a walking biped");
                if (ptr.getType() == ESM::CreatureLevList::sRecordId)
                    throw std::invalid_argument("Unresolved leveled actor in interior collision domain");
                if (id == actor && (!actorBody || initialCorpse(ptr) || !cls.getScript(ptr).empty()))
                    throw std::invalid_argument("Selected actor must be a living unscripted native NPC");
                if (actorBody && initialCorpse(ptr))
                    throw std::invalid_argument("Corpse collision requires gameplay animation");
                const auto& position = ptr.getRefData().getPosition();
                const float scale = ptr.getCellRef().getScale();
                validatePosition(position, scale);
                auto model = npc
                    ? VFS::Path::Normalized(MWClass::npcModel(*loadout.store().get<ESM::Race>().find(
                        ptr.get<ESM::NPC>()->mBase->mRace), base, beast))
                    : cls.getCorrectedModel(ptr);
                if (model.empty()) return true;
                if (actorBody) model = Misc::ResourceHelpers::correctActorModelPath(model, &mVfs);
                if (!mVfs.exists(model)) throw std::invalid_argument("Missing collision model: " + model.value());
                meshes.emplace(model.value());
                auto shape = mShapes->getInstance(model);
                if (!shape) return true;
                // Stock NPC collision uses the resource's fixed bounding hull,
                // not its animated child shapes. Objects need CPU evaluation.
                if (!actorBody && shape->isAnimated())
                    throw std::invalid_argument("Animated collision requires CPU evaluation: " + model.value());
                auto body = std::make_unique<Body>(mWorld);
                body->mResource = shape;
                const auto rotation = Misc::Convert::makeOsgQuat(position);
                if (actorBody)
                {
                    const auto extents = shape->mCollisionBox.mExtents;
                    if (!(extents.x() > 0 && extents.y() > 0 && extents.z() > 0)
                        || !std::isfinite(extents.length2()) || extents.length2() > 1e8f
                        || !std::isfinite(shape->mCollisionBox.mCenter.length2())
                        || shape->mCollisionBox.mCenter.length2() > 1e8f)
                        throw std::invalid_argument("NPC model has invalid collision bounds");
                    auto hull = MWPhysics::makeActorShape(extents, shape->mCollisionBox.mCenter,
                        DetourNavigator::CollisionShapeType::Cylinder);
                    body->mHull = std::move(hull.mShape);
                    body->mHull->setLocalScaling(btVector3(scale, scale, scale));
                    const auto hullRotation = hull.mRotationallyInvariant ? osg::Quat() : rotation;
                    const auto offset = hullRotation * (shape->mCollisionBox.mCenter * scale);
                    body->mObject = BulletHelpers::makeCollisionObject(body->mHull.get(),
                        Misc::Convert::toBullet(position.asVec3() + offset),
                        Misc::Convert::toBullet(hullRotation));
                    body->mObject->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
                    body->mObject->setActivationState(DISABLE_DEACTIVATION);
                    if (id == actor)
                    {
                        mActorBase = ptr.getCellRef().getRefId();
                        mActorOffset = offset;
                        mAgentBounds = {DetourNavigator::CollisionShapeType::Cylinder, extents * scale};
                        mActor = std::make_unique<MWPhysics::ActorFrameData>(MWPhysics::ActorFrameData{
                            .mPosition = position.asVec3(),
                            .mCollisionObject = body->mObject.get(),
                            .mRotation = { position.rot[0], position.rot[2] },
                            .mHalfExtentsZ = extents.z() * scale });
                    }
                }
                else
                {
                    if (!shape->mCollisionShape) return true;
                    body->mResource->setLocalScaling(btVector3(scale, scale, scale));
                    body->mObject = BulletHelpers::makeCollisionObject(shape->mCollisionShape.get(),
                        Misc::Convert::toBullet(position.asVec3()), Misc::Convert::toBullet(rotation));
                }
                auto* object = body->mObject.get();
                if (ptr.getType() == ESM::Door::sRecordId && !ptr.getCellRef().getTeleport())
                    mOrdinaryDoors.emplace(id, mBodies.size());
                mTransforms.push_back({position, scale});
                mBodies.push_back(std::move(body));
                mIdentities.emplace(object, id);
                mWorld.addCollisionObject(object, actorBody ? MWPhysics::CollisionType_Actor : MWPhysics::CollisionType_World,
                    actorBody ? (MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Actor) : MWPhysics::CollisionType_Actor);
                return true;
            });
            if (const auto* exterior = cellId.getIf<ESM::ESM3ExteriorCellRefId>())
            {
                auto terrain = std::make_unique<Terrain>(mWorld);
                terrain->cell = {exterior->getX(), exterior->getY()};
                terrain->water = cell.getWaterLevel();
                if (!std::isfinite(terrain->water)) throw std::invalid_argument("Actor water level outside bounds");
                const auto* land = loadout.store().get<ESM::Land>().search(exterior->getX(), exterior->getY());
                const auto* data = land ? land->getLandData(ESM::Land::DATA_VHGT) : nullptr;
                if (data) terrain->heights = data->mHeights;
                else terrain->heights.fill(ESM::Land::DEFAULT_HEIGHT);
                for (float value : terrain->heights)
                    if (!std::isfinite(value) || std::abs(value) > 1e7f)
                        throw std::invalid_argument("Actor terrain height outside bounds");
                const auto [lo, hi] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
                terrain->minimum = *lo; terrain->maximum = *hi;
                // Stock HeightField parameters and diamond subdivision. The owned
                // buffer also supports Bullet builds using double precision.
                terrain->collisionHeights.assign(terrain->heights.begin(), terrain->heights.end());
                terrain->shape = std::make_unique<btHeightfieldTerrainShape>(ESM::Land::LAND_SIZE,
                    ESM::Land::LAND_SIZE, terrain->collisionHeights.data(), 1, *lo, *hi, 2,
                    sizeof(btScalar) == sizeof(float) ? PHY_FLOAT : PHY_DOUBLE, false);
                terrain->shape->setUseDiamondSubdivision(true);
                const float scale = float(ESM::Land::REAL_SIZE) / (ESM::Land::LAND_SIZE - 1);
                terrain->shape->setLocalScaling({scale, scale, 1});
                terrain->object = BulletHelpers::makeCollisionObject(terrain->shape.get(),
                    BulletHelpers::getHeightfieldShift(exterior->getX(), exterior->getY(), ESM::Land::REAL_SIZE, *lo, *hi),
                    btQuaternion::getIdentity());
                mIdentities.emplace(terrain->object.get(), mTerrain.size() + 1);
                mWorld.addCollisionObject(terrain->object.get(), MWPhysics::CollisionType_HeightMap, MWPhysics::CollisionType_Actor);
                mTerrain.push_back(std::move(terrain));
            }
            }
            if (!mActor) throw std::invalid_argument("Selected NPC absent from interior collision scene");
            if (mInteriorWater || !mTerrain.empty())
            {
                mWaterShape = std::make_unique<btStaticPlaneShape>(btVector3(0, 0, 1), 0);
                mWaterObject = std::make_unique<btCollisionObject>();
                mWaterObject->setCollisionShape(mWaterShape.get());
                mWorld.addCollisionObject(mWaterObject.get(), MWPhysics::CollisionType_Water,
                    MWPhysics::CollisionType_Actor);
            }
            std::ostringstream fingerprint;
            fingerprint << "native-interior-collision-1\n" << loadout.contentFingerprint() << '\n'
                << (anchor ? "exterior-neighborhood-1" : std::string(cells.front().getRefIdString())) << '\n' << actor << '\n';
            if (anchor) for (auto cell : cells) fingerprint << cell.serializeText() << '\n';
            for (const auto& mesh : meshes)
            {
                auto stream = mVfs.get(VFS::Path::toNormalized(mesh));
                const auto hash = Files::getHash(mesh, *stream);
                fingerprint << mesh.size() << ':' << mesh << ':' << hash[0] << ':' << hash[1] << '\n';
            }
            mFingerprint = fingerprint.str();
        }

        void simulate(MWPhysics::ActorFrameData& frame, std::vector<uint64_t>& contacts,
            const std::array<float,3>& velocity)
        {
            for (float value : velocity) if (!std::isfinite(value) || std::abs(value)>4096)
                throw std::invalid_argument("Interior NPC velocity outside bounds");
            frame.mMovement = {velocity[0],velocity[1],velocity[2]};
            Contacts effects(mIdentities);
            struct RestoreTransform
            {
                btCollisionObject& object;
                btTransform transform;
                btCollisionWorld& world;
                btCollisionObject* water;
                btTransform waterTransform;
                int mask;
                ~RestoreTransform()
                {
                    object.setWorldTransform(transform);
                    object.getBroadphaseHandle()->m_collisionFilterMask = mask;
                    if (water) { water->setWorldTransform(waterTransform); world.updateSingleAabb(water); }
                }
            } restore{*frame.mCollisionObject, frame.mCollisionObject->getWorldTransform(), mWorld,
                mWaterObject.get(), mWaterObject ? mWaterObject->getWorldTransform() : btTransform::getIdentity(),
                frame.mCollisionObject->getBroadphaseHandle()->m_collisionFilterMask};
            if (mWaterObject && frame.mWaterlevel > -1e30f)
            {
                auto transform = mWaterObject->getWorldTransform();
                transform.setOrigin(btVector3(0, 0, frame.mWaterlevel));
                mWaterObject->setWorldTransform(transform);
                mWorld.updateSingleAabb(mWaterObject.get());
            }
            if (frame.mWaterCollision)
                frame.mCollisionObject->getBroadphaseHandle()->m_collisionFilterMask |= MWPhysics::CollisionType_Water;
            MWPhysics::MovementSolver::unstuck(frame, &mWorld);
            MWPhysics::MovementSolver::move(frame, 1.f/60.f, &mWorld, {}, effects);
            if (!std::isfinite(frame.mPosition.length2()) || frame.mPosition.length2()>3e14f)
                throw std::runtime_error("Interior NPC solver output outside bounds");
            contacts.swap(effects.mObjects);
        }
        void navigate(MWPhysics::ActorFrameData& frame, MWMechanics::PathFinder& path,
            std::vector<uint64_t>& contacts, float speed, const ActorMovement& movement)
        {
            if (!mNavigator || !std::isfinite(speed) || speed<0 || speed>4096)
                throw std::invalid_argument("Interior navigation speed outside bounds");
            path.update(frame.mPosition, 8, 8, 0, mAgentBounds,
                navigationFlags(), *mNavigator);
            if (path.isPathConstructed()) frame.mRotation.y()=path.getZAngleToNext(frame.mPosition.x(), frame.mPosition.y());
            simulate(frame, contacts, {0, path.isPathConstructed() ? speed : 0,
                jumpVelocity(frame, movement)});
        }
        void updateTransform() noexcept
        {
            auto transform=mActor->mCollisionObject->getWorldTransform();
            transform.setOrigin(Misc::Convert::toBullet(mActor->mPosition+mActorOffset));
            mActor->mCollisionObject->setWorldTransform(transform);
            mWorld.updateSingleAabb(mActor->mCollisionObject);
        }
        std::vector<char> encode(const MWPhysics::ActorFrameData& frame,
            const MWMechanics::PathFinder& path, const std::vector<uint64_t>& contacts, const Travel& travel) const;

        void advance(MWPhysics::ActorFrameData& frame, MWMechanics::PathFinder& path,
            std::vector<uint64_t>& contacts, Travel& travel, const ActorMovement& movement,
            std::span<const ActorSceneDoor> doors)
        {
            const float speed = movement.enabled ? movementSpeed(frame, movement) : movement.walkSpeed;
            if (travel.door)
            {
                const auto door = std::ranges::find(doors, travel.door, &ActorSceneDoor::mId);
                const auto angle = travel.avoidance.update(frame.mPosition,
                    mTransforms[mOrdinaryDoors.at(travel.door)].mPosition.asVec3(),
                    door != doors.end() && door->mMoving, 1.f / 60, travel.random);
                if (angle)
                {
                    const auto turn = MWMechanics::smoothTurnStep(frame.mRotation.y(), *angle, speed,
                        1.f / 60, mSmoothMovement, osg::DegreesToRadians(5.f));
                    frame.mRotation.y() += turn.mRotation;
                    simulate(frame, contacts, {0, turn.mComplete ? speed : 0,
                        jumpVelocity(frame, movement)});
                    updateBreath(frame, travel, movement);
                    return;
                }
                travel.door = 0;
                travel.avoidance = {};
                rebuildPath(path, frame.mPosition, travel);
            }
            navigate(frame, path, contacts, speed, movement);
            updateBreath(frame, travel, movement);
        }

        ActorSceneSnapshot snapshot() const
        {
            return {mActorId, {mActor->mPosition.x(), mActor->mPosition.y(), mActor->mPosition.z()},
                mActor->mIsOnGround, mContacts, mActor->mRotation.y(), mTravel.drowning};
        }
    };

    struct InteriorActorScene::Dormant
    {
        ActorSceneSnapshot snapshot;
        std::array<float, 4> transform;
        std::string fingerprint;
        std::vector<char> image;
        std::vector<ActorSceneDoor> doors;
        bool arrived;
    };

    InteriorActorScene::InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation)
        : InteriorActorScene(loadout, std::array{interiorCell(cell)}, actor, baseAnimation, beastAnimation) {}
    InteriorActorScene::InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation)
        : mImpl(std::make_unique<Impl>(loadout, cells, actor, baseAnimation, beastAnimation))
    {
        if (!contains(snapshot().mPosition))
            throw std::invalid_argument("Actor start outside dry processing neighborhood");
    }
    InteriorActorScene::~InteriorActorScene() = default;
    ActorSceneSnapshot InteriorActorScene::snapshot() const { return mImpl ? mImpl->snapshot() : mDormant->snapshot; }
    std::array<float, 4> InteriorActorScene::transform() const noexcept
    {
        if (!mImpl) return mDormant->transform;
        const auto& frame=*mImpl->mActor;
        return {frame.mPosition.x(),frame.mPosition.y(),frame.mPosition.z(),frame.mRotation.y()};
    }
    uint64_t InteriorActorScene::actorId() const noexcept { return mImpl ? mImpl->mActorId : mDormant->snapshot.mActor; }
    size_t InteriorActorScene::bodyCount() const { return mImpl ? mImpl->mBodies.size() : 0; }
    bool InteriorActorScene::lineOfSight(const std::array<float, 3>& from, const std::array<float, 3>& to) const
    {
        if (!mImpl) throw std::invalid_argument("Visibility scene is unloaded");
        for (const auto& point : {from, to}) for (float value : point)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Visibility endpoint invalid");
        const btVector3 start(from[0], from[1], from[2]), end(to[0], to[1], to[2]);
        btCollisionWorld::ClosestRayResultCallback hit(start, end);
        hit.m_collisionFilterGroup = MWPhysics::CollisionType_AnyPhysical;
        hit.m_collisionFilterMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;
        mImpl->mWorld.rayTest(start, end, hit);
        return !hit.hasHit();
    }
    std::optional<ActorProjectileContact> InteriorActorScene::projectileContact(const std::array<float, 3>& from,
        const std::array<float, 3>& to, uint64_t casterActor) const
    {
        if (!mImpl) throw std::invalid_argument("Projectile scene is unloaded");
        for (float value : from)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Projectile origin invalid");
        for (float value : to)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Projectile endpoint invalid");
        const btVector3 start(from[0], from[1], from[2]), end(to[0], to[1], to[2]);
        btSphereShape sphere(4.f);
        const btTransform startFrame(btQuaternion::getIdentity(), start);
        const btTransform endFrame(btQuaternion::getIdentity(), end);
        struct Contact final : btCollisionWorld::ClosestConvexResultCallback
        {
            const btCollisionObject* ignored;
            Contact(const btVector3& from, const btVector3& to, const btCollisionObject* caster)
                : ClosestConvexResultCallback(from, to), ignored(caster) {}
            bool needsCollision(btBroadphaseProxy* proxy) const override
            { return proxy->m_clientObject != ignored && ClosestConvexResultCallback::needsCollision(proxy); }
        } hit(start, end, casterActor == mImpl->mActorId ? mImpl->mActor->mCollisionObject : nullptr);
        hit.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
        hit.m_collisionFilterMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Actor | MWPhysics::CollisionType_Door;
        mImpl->mWorld.convexSweepTest(&sphere, startFrame, endFrame, hit);
        if (!hit.hasHit()) return {};
        const auto& point = hit.m_hitPointWorld;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 1e7)
                throw std::invalid_argument("Projectile impact position invalid");
        return ActorProjectileContact{
            hit.m_hitCollisionObject == mImpl->mActor->mCollisionObject ? mImpl->mActorId : 0,
            {float(point.x()), float(point.y()), float(point.z())}};
    }
    const std::string& InteriorActorScene::fingerprint() const { return mImpl ? mImpl->mFingerprint : mDormant->fingerprint; }
    bool InteriorActorScene::enchantedWeaponsAreMagical() const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        return mImpl->mEnchantedWeaponsAreMagical;
    }
    bool InteriorActorScene::classicReflectedAbsorb() const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        return mImpl->mClassicReflectedAbsorb;
    }
    bool InteriorActorScene::uncappedDamageFatigue() const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        return mImpl->mUncappedDamageFatigue;
    }
    bool InteriorActorScene::onlyAppropriateAmmunitionBypassesResistance() const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        return mImpl->mOnlyAppropriateAmmunitionBypassesResistance;
    }

    BoundMeleeAnimation InteriorActorScene::bindMeleeAnimation(
        std::string group, std::string attack, float speed)
    {
        if (!mImpl)
            throw std::invalid_argument("Selected NPC has no supported live melee animation source");
        const auto [sources, identity] = bindAnimationSources(mImpl->mActorBase);
        for (auto source = sources.rbegin(); source != sources.rend(); ++source)
        {
            if (!(*source)->hasGroupStart(group)) continue;
            return {MeleeAnimation(**source, std::move(group), std::move(attack), speed), identity};
        }
        throw std::invalid_argument("Selected NPC lacks the requested melee animation group");
    }

    BoundCastAnimations InteriorActorScene::bindCastAnimations(ESM::RefId actor)
    {
        if (!mImpl)
            throw std::invalid_argument("Selected NPC has no supported cast animation source");
        const auto [sources, identity] = bindAnimationSources(actor.empty() ? mImpl->mActorBase : actor);
        for (auto source = sources.rbegin(); source != sources.rend(); ++source)
        {
            if (!(*source)->hasGroupStart("spellcast")) continue;
            return {readCastAnimations(**source), identity};
        }
        throw std::invalid_argument("Selected NPC lacks spellcast animation keys");
    }

    std::pair<std::vector<std::shared_ptr<const SceneUtil::TextKeyMap>>, std::string>
    InteriorActorScene::bindAnimationSources(ESM::RefId actor)
    {
        if (!mImpl || !mImpl->mNavigator)
            throw std::invalid_argument("Native hit resources require initialized actor settings");
        // NpcAnimation/ReplicatedActor load a base, sex/race skeleton, custom
        // skeleton, then the Argonian swim layer. Resolve each group's winning
        // source independently, as Animation::play does.
        const auto& models = mImpl->mHitModels;
        std::vector<VFS::Path::Normalized> paths;
        std::ostringstream identity;
        if (const auto* creature = mImpl->mStore.get<ESM::Creature>().search(actor))
        {
            identity << "native-creature-resources-1\n" << creature->mId << ':' << creature->mFlags << '\n';
            if (creature->mFlags & ESM::Creature::Bipedal) paths.push_back(models[0]);
            paths.emplace_back(Misc::ResourceHelpers::correctActorModelPath(
                Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(creature->mModel)), &mImpl->mVfs));
        }
        else
        {
            const auto& npc = *mImpl->mStore.get<ESM::NPC>().find(actor);
            const auto& race = *mImpl->mStore.get<ESM::Race>().find(npc.mRace);
            const bool beast = (race.mData.mFlags & ESM::Race::Beast) != 0;
            const auto& normal = models[beast ? 3 : npc.isMale() ? 1 : 2];
            paths.push_back(models[0]);
            const VFS::Path::Normalized skeleton(Misc::ResourceHelpers::correctActorModelPath(normal, &mImpl->mVfs));
            if (skeleton != models[0]) paths.push_back(skeleton);
            if (!npc.mModel.empty())
            {
                const auto model = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(npc.mModel));
                if (model != models[1] && model != models[2] && model != models[3])
                    paths.emplace_back(Misc::ResourceHelpers::correctActorModelPath(model, &mImpl->mVfs));
            }
            if (beast && npc.mRace.contains("argonian")) paths.push_back(models[4]);
            identity << "native-hit-resources-1\n" << npc.mId << ':' << npc.mRace << ':' << npc.isMale() << '\n';
        }
        std::vector<std::shared_ptr<const SceneUtil::TextKeyMap>> sources;
        size_t sourceCount = 0;
        const auto appendSource = [&](const VFS::Path::Normalized& path) {
            if (++sourceCount > MaximumHitSources || path.value().size() > 512)
                throw std::invalid_argument("Native hit resource path budget exceeded");
            identity << path.value() << ':';
            if (!mImpl->mVfs.exists(path)) { identity << "absent\n"; return; }
            auto stream = mImpl->mVfs.get(path);
            const auto hash = Files::getHash(path.value(), *stream);
            identity << hash[0] << ':' << hash[1] << '\n';
            if (identity.tellp() > std::streamoff(MaximumHitResourceIdentity))
                throw std::invalid_argument("Native hit resource identity too large");
            const auto holder = mImpl->mResources.getKeyframeManager()->get(path);
            if (!holder || holder->mTextKeys.empty() || holder->mKeyframeControllers.empty()) return;
            sources.emplace_back(&holder->mTextKeys, [holder](const SceneUtil::TextKeyMap*) {});
        };
        for (auto path : paths)
        {
            if (path.extension() == VFS::Path::ExtensionView("nif"))
                path.changeExtension(VFS::Path::ExtensionView("kf"));
            appendSource(path);
            // Animation::loadAdditionalAnimations inserts this directory's KF
            // files immediately after their base source, in VFS order.
            if (!mImpl->mAdditionalHitSources || !path.value().starts_with("meshes/")) continue;
            std::string directory(path.value());
            directory.replace(0, 7, "animations/");
            const auto extension = directory.find_last_of(VFS::Path::extensionSeparator);
            if (extension == std::string::npos) continue;
            directory.replace(extension, directory.size() - extension, "/");
            for (const auto& extra : mImpl->mVfs.getRecursiveDirectoryIterator(directory))
                if (extra.extension() == VFS::Path::ExtensionView("kf")) appendSource(extra);
        }
        if (identity.tellp() > std::streamoff(MaximumHitResourceIdentity))
            throw std::invalid_argument("Native hit resource identity too large");
        return {std::move(sources), identity.str()};
    }



    BoundHitAnimations InteriorActorScene::bindHitAnimations(ESM::RefId actor, bool knockout)
    {
        const auto [owned, identity] = bindAnimationSources(actor);
        std::vector<const SceneUtil::TextKeyMap*> sources;
        for (const auto& source : owned) sources.push_back(source.get());
        return {readHitAnimations(sources), identity,
            knockout ? readKnockoutAnimation(sources) : KnockoutAnimation{},
            knockout ? readKnockoutAnimation(sources, "knockdown") : KnockoutAnimation{}};
    }



    MeleeAnimation InteriorActorScene::bindWeaponMeleeAnimation(ESM::RefId actor,
        const ESM::Weapon* weapon, const std::string& attack)
    {
        const auto type = weapon ? weapon->mData.mType : ESM::Weapon::HandToHand;
        const auto* info = MWMechanics::getWeaponType(type);
        if (info->mWeaponClass != ESM::WeaponType::Melee
            && !((info->mWeaponClass == ESM::WeaponType::Ranged || info->mWeaponClass == ESM::WeaponType::Thrown)
                && attack == "shoot"))
            throw std::invalid_argument("Native ranged execution is not bound");
        const auto [sources, resourceIdentity] = bindAnimationSources(actor);
        std::string group(info->mLongGroup);
        const auto findGroup = [&](const std::string& name) -> const SceneUtil::TextKeyMap* {
            for (auto it = sources.rbegin(); it != sources.rend(); ++it)
                if ((*it)->hasGroupStart(name)) return it->get();
            return nullptr;
        };
        auto* keys = findGroup(group);
        // CharacterController's real-weapon fallback, using the same layered sources.
        if (!keys && weapon)
        {
            group = MWMechanics::getWeaponType(info->mFlags & ESM::WeaponType::TwoHanded
                ? ESM::Weapon::LongBladeTwoHand : ESM::Weapon::LongBladeOneHand)->mLongGroup;
            keys = findGroup(group);
        }
        if (!keys) throw std::invalid_argument("Native selected weapon has no melee animation");
        const float speed = weapon ? weapon->mData.mSpeed : 1.f;
        // Resource bytes already belong to the participant campaign fingerprint.
        // Keep the persisted clip recipe small and distinguish speed exactly.
        const std::string identity = "native-weapon-melee-1:" + group + ':' + attack + ':'
            + std::to_string(std::bit_cast<uint32_t>(speed));
        return MeleeAnimation(*keys, group, attack, speed, identity);
    }

    void InteriorActorScene::unload()
    {
        if (!mImpl) return;
        auto dormant = std::make_unique<Dormant>(Dormant{
            snapshot(), transform(), fingerprint(), image(), mImpl->mDoors, arrived()});
        mDormant.swap(dormant);
        mImpl.reset();
    }

    void InteriorActorScene::reload(InteriorActorScene& fresh)
    {
        if (mImpl || !fresh.mImpl || fresh.fingerprint() != fingerprint())
            throw std::invalid_argument("Actor reload scene differs from bound resources");
        auto restored = fresh.prepareRestore(mDormant->image, mDormant->doors);
        fresh.install(*restored);
        mImpl.swap(fresh.mImpl);
        mDormant.swap(fresh.mDormant);
    }

    void InteriorActorScene::enableMovementEffects()
    {
        if (!mImpl || mImpl->mNavigator) throw std::logic_error("Actor movement binding must precede navigation");
        mImpl->mWaterNavigation = true;
    }

    void InteriorActorScene::enableNavigation(const std::string& settingsFile)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (mImpl->mNavigator) throw std::logic_error("Interior navigation already initialized");
        if (mImpl->mInteriorWater && !mImpl->mWaterNavigation)
            throw std::invalid_argument("Interior NPC slice requires a dry interior");
        if (std::filesystem::file_size(settingsFile) > 1024 * 1024)
            throw std::invalid_argument("Navigation settings exceed startup bound");
        // Engine settings are read during serialized startup. Restore the globals
        // even on failure; only the owned navigator settings snapshot survives.
        struct RestoreSettings
        {
            Settings::CategorySettingValueMap defaults = std::move(Settings::Manager::mDefaultSettings);
            Settings::CategorySettingValueMap user = std::move(Settings::Manager::mUserSettings);
            Settings::CategorySettingVector changed = std::move(Settings::Manager::mChangedSettings);
            ~RestoreSettings()
            {
                Settings::Manager::mDefaultSettings = std::move(defaults);
                Settings::Manager::mUserSettings = std::move(user);
                Settings::Manager::mChangedSettings = std::move(changed);
            }
        } restore;
        Settings::SettingsFileParser().loadSettingsFile(settingsFile, Settings::Manager::mDefaultSettings);
        constexpr std::array modelNames{"xbaseanim", "baseanim", "baseanimfemale", "baseanimkna", "xargonianswimkna"};
        for (size_t i = 0; i < modelNames.size(); ++i)
            mImpl->mHitModels[i] = VFS::Path::Normalized(Settings::Manager::getString(modelNames[i], "Models"));
        mImpl->mAdditionalHitSources = Settings::Manager::getBool("use additional anim sources", "Game");
        mImpl->mSmoothMovement = Settings::Manager::getBool("smooth movement", "Game");
        mImpl->mEnchantedWeaponsAreMagical = Settings::Manager::getBool("enchanted weapons are magical", "Game");
        mImpl->mOnlyAppropriateAmmunitionBypassesResistance
            = Settings::Manager::getBool("only appropriate ammunition bypasses resistance", "Game");
        mImpl->mUncappedDamageFatigue = Settings::Manager::getBool("uncapped damage fatigue", "Game");
        mImpl->mClassicReflectedAbsorb = Settings::Manager::getBool("classic reflected absorb spells behavior", "Game");
        Settings::Index index;
        Settings::NavigatorCategory category(index);
        auto settings = DetourNavigator::makeSettings(category, Debug::Error);
        // Execution budgets/cache policy, not alternative navigation rules.
        settings.mAsyncNavMeshUpdaterThreads = 1;
        // Candidate queries must see this tick's geometry, without the stock
        // background worker's wall-clock debounce (250 ms by default).
        if (mImpl->mAvoidanceEnabled) settings.mMinUpdateInterval = std::chrono::milliseconds(0);
        settings.mMaxTilesNumber = std::min(settings.mMaxTilesNumber, 256);
        settings.mDetour.mMaxSmoothPathSize = std::min<size_t>(settings.mDetour.mMaxSmoothPathSize, 2048);
        settings.mEnableNavMeshDiskCache = settings.mWriteToNavMeshDb = false;
        settings.mEnableWriteRecastMeshToFile = settings.mEnableWriteNavMeshToFile = false;
        auto navigator = DetourNavigator::makeNavigator(settings, {});
        if (!navigator->addAgent(mImpl->mAgentBounds))
            throw std::invalid_argument("Interior NPC navigation bounds unsupported");
        navigator->updateBounds(ESM::RefId::stringRefId("native-interior"), {}, mImpl->mActor->mPosition, nullptr);
        for (const auto& terrain : mImpl->mTerrain)
        {
            navigator->addHeightfield(terrain->cell, ESM::Land::REAL_SIZE,
                DetourNavigator::HeightfieldSurface{terrain->heights.data(), ESM::Land::LAND_SIZE,
                    terrain->minimum, terrain->maximum}, nullptr);
            if (mImpl->mWaterNavigation)
                navigator->addWater(terrain->cell, ESM::Land::REAL_SIZE, terrain->water, nullptr);
        }
        if (mImpl->mWaterNavigation && mImpl->mInteriorWater)
            navigator->addWater({0, 0}, std::numeric_limits<int>::max(), *mImpl->mInteriorWater, nullptr);
        for (size_t i = 0; i < mImpl->mBodies.size(); ++i)
        {
            const auto& body = *mImpl->mBodies[i];
            // Stock navigator excludes actors; physics still collides with them.
            if (body.mHull) continue;
            navigator->addObject(DetourNavigator::ObjectId(body.mObject.get()),
                DetourNavigator::ObjectShapes(body.mResource, mImpl->mTransforms[i]),
                body.mObject->getWorldTransform(), nullptr);
        }
        navigator->update(mImpl->mActor->mPosition, nullptr);
        navigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
        std::ifstream input(settingsFile, std::ios::binary);
        const auto hash = Files::getHash(settingsFile, input);
        auto fingerprint = mImpl->mFingerprint + "navigation-1:" + std::to_string(hash[0]) + ':' + std::to_string(hash[1]) + '\n';
        mImpl->mFingerprint.swap(fingerprint);
        mImpl->mNavigator.swap(navigator);
    }

    std::vector<std::array<float, 3>> InteriorActorScene::pathTo(const std::array<float, 3>& destination) const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (!mImpl->mNavigator) throw std::logic_error("Interior navigation unavailable");
        for (float value : destination)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Interior destination outside bounds");
        if (!contains(destination)) throw std::invalid_argument("Travel destination outside processing neighborhood");
        mImpl->syncNavigation(mImpl->mDoors);
        std::vector<osg::Vec3f> path;
        const auto status = DetourNavigator::findPath(*mImpl->mNavigator, mImpl->mAgentBounds,
            mImpl->mActor->mPosition, {destination[0], destination[1], destination[2]},
            mImpl->navigationFlags(),
            {}, 0, {}, std::back_inserter(path));
        if (status != DetourNavigator::Status::Success || path.empty() || path.size() > 2048)
            throw std::invalid_argument("No complete interior navigation path: " + std::string(DetourNavigator::getMessage(status)));
        std::vector<std::array<float, 3>> result;
        for (const auto& point : path) result.push_back({point.x(), point.y(), point.z()});
        return result;
    }

    void InteriorActorScene::travelTo(const std::array<float, 3>& destination, bool retainUnavailable)
    {
        if (retainUnavailable)
        {
            if (!mImpl || !mImpl->mNavigator) throw std::logic_error("Travel navigation unavailable");
            for (float value : destination)
                if (!std::isfinite(value) || std::abs(value) > 1e7f)
                    throw std::invalid_argument("Travel destination outside bounds");
            if (!contains(destination)) throw std::invalid_argument("Travel destination outside processing neighborhood");
            auto travel = mImpl->mTravel;
            travel.hasDestination = true;
            travel.destination = {destination[0], destination[1], destination[2]};
            MWMechanics::PathFinder path;
            mImpl->syncNavigation(mImpl->mDoors);
            mImpl->rebuildPath(path, mImpl->mActor->mPosition, travel);
            mImpl->mPath = std::move(path);
            mImpl->mTravel = std::move(travel);
            return;
        }
        const auto path = pathTo(destination);
        MWMechanics::PathFinder next;
        for (const auto& point : path) next.addPointToPath({point[0], point[1], point[2]});
        mImpl->mPath = std::move(next);
        mImpl->mTravel.destination = {destination[0], destination[1], destination[2]};
        mImpl->mTravel.hasDestination = true;
    }

    bool InteriorActorScene::arrived() const
    { return mImpl ? !mImpl->mTravel.door && mImpl->mPath.checkPathCompleted() : mDormant->arrived; }

    bool InteriorActorScene::contains(const std::array<float, 3>& position) const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (mImpl->mTerrain.empty()) return true;
        const auto x = std::floor(double(position[0]) / ESM::Land::REAL_SIZE);
        const auto y = std::floor(double(position[1]) / ESM::Land::REAL_SIZE);
        return std::ranges::any_of(mImpl->mTerrain, [&](const auto& terrain) {
            return x == terrain->cell.x() && y == terrain->cell.y()
                && (mImpl->mWaterNavigation || position[2] >= terrain->water);
        });
    }
    bool InteriorActorScene::pathUnavailable() const
    { return mImpl && !mImpl->mPath.isPathConstructed() && !mImpl->mPath.checkPathCompleted(); }

    void InteriorActorScene::bindDoors(std::span<const uint64_t> doors, bool avoidance)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (mImpl->mNavigator || !mImpl->mDoors.empty() || (doors.empty() && !avoidance) || doors.size() > 128)
            throw std::invalid_argument("Actor door binding outside startup bounds");
        std::vector<ActorSceneDoor> bound;
        auto fingerprint = mImpl->mFingerprint + (avoidance ? "npc-door-avoidance-1\n" : "npc-door-contact-1\n");
        for (auto id : doors)
        {
            const auto found = mImpl->mOrdinaryDoors.find(id);
            if (found == mImpl->mOrdinaryDoors.end() || (!bound.empty() && bound.back().mId >= id))
                throw std::invalid_argument("Actor door collision identity absent or unordered");
            bound.push_back({id, mImpl->mTransforms[found->second].mPosition.rot[2]});
            fingerprint += std::to_string(id) + '\n';
        }
        mImpl->mDoors.swap(bound);
        mImpl->mFingerprint.swap(fingerprint);
        mImpl->mAvoidanceEnabled = avoidance;
    }

    ActorDoorContact InteriorActorScene::doorContact(uint64_t door, float proposedAngle, float delta) const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        const auto bound = std::ranges::find(mImpl->mDoors, door, &ActorSceneDoor::mId);
        if (bound == mImpl->mDoors.end() || !std::isfinite(delta) || std::abs(delta) > osg::PIf / 2)
            throw std::invalid_argument("Actor door query outside domain");
        const auto index = mImpl->mOrdinaryDoors.at(door);
        auto position = mImpl->mTransforms[index].mPosition;
        const auto opened = MWWorld::doorMotion(MWWorld::DoorState::Opening, position.rot[2], position.rot[2], 1).mTargetAngle;
        if (!std::isfinite(proposedAngle) || proposedAngle < position.rot[2] || proposedAngle > opened)
            throw std::invalid_argument("Actor door query angle outside authored swing");
        position.rot[2] = proposedAngle;
        btCollisionObject query;
        query.setCollisionShape(mImpl->mBodies[index]->mObject->getCollisionShape());
        query.setWorldTransform(btTransform(Misc::Convert::toBullet(Misc::Convert::makeOsgQuat(position)),
            Misc::Convert::toBullet(position.asVec3())));
        // All retained NPC hulls obstruct doors, including frozen background NPCs.
        ActorDoorContact result;
        for (const auto& body : mImpl->mBodies)
        {
            if (!body->mHull) continue;
            MWPhysics::DoorContactResult contact(&query, body->mObject.get(), position.asVec3(), delta);
            mImpl->mWorld.contactPairTest(&query, body->mObject.get(), contact);
            result.mBlocked |= contact.mBlocked;
            result.mSelectedActor |= contact.mBlocked && body->mObject.get() == mImpl->mActor->mCollisionObject;
        }
        return result;
    }

    ActorSceneSnapshot InteriorActorScene::navigate(float speed)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (!mImpl->mNavigator || !std::isfinite(speed) || speed <= 0 || speed > 4096)
            throw std::invalid_argument("Interior navigation speed outside bounds");
        auto frame=std::make_unique<MWPhysics::ActorFrameData>(*mImpl->mActor);
        auto path=mImpl->mPath;
        auto travel=mImpl->mTravel;
        std::vector<uint64_t> contacts;
        mImpl->syncNavigation(mImpl->mDoors);
        mImpl->advance(*frame,path,contacts,travel,ActorMovement{.walkSpeed = speed},mImpl->mDoors);
        ActorSceneSnapshot result{mImpl->mActorId,{frame->mPosition.x(),frame->mPosition.y(),frame->mPosition.z()},
            frame->mIsOnGround,contacts,frame->mRotation.y()};
        mImpl->mActor.swap(frame); std::swap(mImpl->mPath,path); mImpl->mContacts.swap(contacts);
        std::swap(mImpl->mTravel,travel);
        mImpl->updateTransform();
        return result;
    }
    ActorSceneSnapshot InteriorActorScene::step(const std::array<float,3>& velocity)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        auto frame=std::make_unique<MWPhysics::ActorFrameData>(*mImpl->mActor);
        std::vector<uint64_t> contacts;
        mImpl->simulate(*frame,contacts,velocity);
        ActorSceneSnapshot result{mImpl->mActorId,{frame->mPosition.x(),frame->mPosition.y(),frame->mPosition.z()},
            frame->mIsOnGround,contacts,frame->mRotation.y()};
        mImpl->mActor.swap(frame); mImpl->mContacts.swap(contacts); mImpl->updateTransform();
        return result;
    }

    struct InteriorActorScene::Prepared::State
    {
        std::shared_ptr<const char> lifetime;
        uint64_t actor;
        std::unique_ptr<MWPhysics::ActorFrameData> frame;
        MWMechanics::PathFinder path;
        std::vector<uint64_t> contacts;
        std::vector<char> bytes;
        std::vector<ActorSceneDoor> doors;
        Impl::Travel travel;
    };
    InteriorActorScene::Prepared::Prepared(std::unique_ptr<State> state) : mState(std::move(state)) {}
    InteriorActorScene::Prepared::~Prepared() = default;
    ActorSceneSnapshot InteriorActorScene::Prepared::snapshot() const
    {
        const auto& frame = *mState->frame;
        return {mState->actor, {frame.mPosition.x(), frame.mPosition.y(), frame.mPosition.z()},
            frame.mIsOnGround, mState->contacts, frame.mRotation.y(), mState->travel.drowning};
    }
    void InteriorActorScene::setFacing(Prepared& prepared, float yaw) const
    {
        if (!std::isfinite(yaw) || !canInstall(prepared)) throw std::invalid_argument("Invalid prepared facing");
        auto& state = *prepared.mState;
        state.frame->mRotation.y() = yaw;
        state.bytes = mImpl->encode(*state.frame, state.path, state.contacts, state.travel);
    }
    std::span<const char> InteriorActorScene::Prepared::image() const { return mState->bytes; }
    bool InteriorActorScene::Prepared::pathUnavailable() const
    { return !mState->path.isPathConstructed() && !mState->path.checkPathCompleted(); }

    std::vector<char> InteriorActorScene::image() const
    { return mImpl ? mImpl->encode(*mImpl->mActor, mImpl->mPath, mImpl->mContacts, mImpl->mTravel) : mDormant->image; }

    std::vector<char> InteriorActorScene::Impl::encode(const MWPhysics::ActorFrameData& frame,
        const MWMechanics::PathFinder& path, const std::vector<uint64_t>& contacts, const Travel& travel) const
    {
        std::vector<char> bytes;
        const auto word = [&](uint64_t value) { putAreaWord(bytes, value); };
        const auto real = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
        const auto vector = [&](const osg::Vec3f& value) { for (int i=0; i<3; ++i) real(value[i]); };
        word(mAvoidanceEnabled ? 2 : 1); word(mActorId);
        vector(frame.mPosition); vector(frame.mInertia); vector(frame.mLastStuckPosition);
        real(frame.mRotation.x()); real(frame.mRotation.y()); real(frame.mOldHeight);
        word(frame.mStuckFrames); word(frame.mIsOnGround); word(frame.mIsOnSlope);
        word(frame.mStandingOn == mWaterObject.get() && mWaterObject ? UINT64_MAX
            : frame.mStandingOn ? mIdentities.at(frame.mStandingOn) : 0);
        word(path.checkPathCompleted()); word(path.getPathSize());
        for (const auto& point : path.getPath()) vector(point);
        word(contacts.size());
        for (auto contact : contacts) word(contact);
        if (mAvoidanceEnabled)
        {
            word(travel.hasDestination); vector(travel.destination);
            word(travel.door); real(travel.avoidance.mDuration); vector(travel.avoidance.mLastPos);
            word(travel.avoidance.mDirection);
            word(std::stoull(Misc::Rng::serialize(travel.random)));
        }
        if (mWaterNavigation) real(travel.breath);
        return bytes;
    }

    void InteriorActorScene::restore(std::span<const char> bytes)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        auto prepared = prepareRestore(bytes, mImpl->mDoors);
        install(*prepared);
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareRestore(
        std::span<const char> bytes, std::span<const ActorSceneDoor> doors)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        mImpl->validateDoors(doors);
        if (bytes.size() > 64 * 1024) throw std::invalid_argument("Actor image exceeds bound");
        size_t offset = 0;
        const auto word = [&]() { return getAreaWord(bytes, offset); };
        const auto real = [&]() {
            const auto bits = word();
            const float value = std::bit_cast<float>(uint32_t(bits));
            if (bits > UINT32_MAX || !std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Invalid actor image float");
            return value;
        };
        const auto vector = [&]() { const auto x=real(), y=real(), z=real(); return osg::Vec3f(x,y,z); };
        const auto boolean = [&]() { const auto value=word(); if (value>1) throw std::invalid_argument("Invalid actor image boolean"); return bool(value); };
        if (word() != (mImpl->mAvoidanceEnabled ? 2 : 1) || word() != mImpl->mActorId)
            throw std::invalid_argument("Actor image identity mismatch");
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(*mImpl->mActor);
        frame->mPosition = vector(); frame->mInertia = vector(); frame->mLastStuckPosition = vector();
        if (!contains({frame->mPosition.x(), frame->mPosition.y(), frame->mPosition.z()}))
            throw std::invalid_argument("Recovered traveler outside processing neighborhood");
        frame->mRotation.x() = real(); frame->mRotation.y() = real(); frame->mOldHeight = real();
        const auto stuck = word();
        if (stuck > UINT32_MAX) throw std::invalid_argument("Actor stuck counter outside bounds");
        frame->mStuckFrames = unsigned(stuck); frame->mIsOnGround = boolean(); frame->mIsOnSlope = boolean();
        const auto standing = word(); frame->mStandingOn = nullptr;
        if (standing)
        {
            if (standing == UINT64_MAX && mImpl->mWaterNavigation)
                frame->mStandingOn = mImpl->mWaterObject.get();
            else
                for (const auto& [object, id] : mImpl->mIdentities) if (id == standing) frame->mStandingOn = object;
            if (!frame->mStandingOn || standing == mImpl->mActorId) throw std::invalid_argument("Actor support outside scene");
        }
        const bool completed = boolean();
        const auto count = word();
        if (count > 2048 || count > (bytes.size()-offset)/24 || (completed && count))
            throw std::invalid_argument("Actor path outside bounds");
        MWMechanics::PathFinder path;
        // Preserve a constructed empty path through the stock completion rule.
        if (completed)
        {
            if (!mImpl->mNavigator) throw std::invalid_argument("Actor navigation unavailable");
            path.addPointToPath(frame->mPosition);
            path.update(frame->mPosition, 8, 8, 0, mImpl->mAgentBounds, DetourNavigator::Flag_walk, *mImpl->mNavigator);
        }
        for (size_t i=0; i<count; ++i) path.addPointToPath(vector());
        const auto contacts = word();
        if (contacts > MaxContacts || contacts > (bytes.size()-offset)/8) throw std::invalid_argument("Actor contacts outside bounds");
        std::vector<uint64_t> ids;
        for (size_t i=0; i<contacts; ++i)
        {
            const auto id=word();
            if (!std::ranges::any_of(mImpl->mIdentities, [id](const auto& pair) { return pair.second == id; })
                || std::ranges::find(ids, id) != ids.end()) throw std::invalid_argument("Actor contact outside scene");
            ids.push_back(id);
        }
        auto travel = mImpl->mTravel;
        if (mImpl->mAvoidanceEnabled)
        {
            travel.hasDestination = boolean(); travel.destination = vector();
            travel.door = word(); travel.avoidance.mDuration = real(); travel.avoidance.mLastPos = vector();
            const auto direction = word(), random = word();
            if (travel.hasDestination != mImpl->mTravel.hasDestination || travel.destination != mImpl->mTravel.destination
                || (travel.door && std::ranges::find(doors, travel.door, &ActorSceneDoor::mId) == doors.end())
                || travel.avoidance.mDuration < 0 || travel.avoidance.mDuration > 1 || direction > 3
                || random < Misc::Rng::Generator::min() || random > Misc::Rng::Generator::max())
                throw std::invalid_argument("Actor avoidance image outside bounds");
            travel.avoidance.mDirection = int(direction);
            Misc::Rng::deserialize(std::to_string(random), travel.random);
        }
        // Earlier V56 images ended after avoidance. Restore those at full
        // breath, then include the timer on the next committed frame.
        if (mImpl->mWaterNavigation && offset < bytes.size())
        {
            travel.breath = real();
            const float hold = mImpl->mStore.get<ESM::GameSetting>().find("fHoldBreathTime")->mValue.getFloat();
            if (!(hold > 0.f) || travel.breath < -1.f || travel.breath > hold)
                throw std::invalid_argument("Actor breath timer outside stock bounds");
            travel.drowning = false;
        }
        if (offset != bytes.size()) throw std::invalid_argument("Trailing actor image data");
        return std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::State>(Prepared::State{
            mImpl->mLifetime, mImpl->mActorId, std::move(frame), std::move(path), std::move(ids), {bytes.begin(), bytes.end()},
            {doors.begin(), doors.end()}, std::move(travel)})));
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNavigation(
        float speed, std::span<const ActorSceneDoor> doors)
    { return prepareNavigation(ActorMovement{.walkSpeed = speed}, doors); }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNavigation(
        const ActorMovement& movement, std::span<const ActorSceneDoor> doors)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        mImpl->validateDoors(doors);
        if (!mImpl->mNavigator || !std::isfinite(movement.walkSpeed)
            || movement.walkSpeed < 0 || movement.walkSpeed > 4096
            || (movement.enabled && (!std::isfinite(movement.swimSpeed) || movement.swimSpeed < 0
                || movement.swimSpeed > 4096 || !std::isfinite(movement.flySpeed)
                || movement.flySpeed < 0 || movement.flySpeed > 4096
                || !std::isfinite(movement.jumpSpeed) || movement.jumpSpeed < 0
                || movement.jumpSpeed > 4096 || !std::isfinite(movement.slowFall)
                || movement.slowFall < 0 || movement.slowFall > 1)))
            throw std::invalid_argument("Interior navigation speed outside bounds");
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(movement.enabled
            ? mImpl->movementFrame(*mImpl->mActor, movement) : *mImpl->mActor);
        auto path = mImpl->mPath;
        auto travel = mImpl->mTravel;
        std::vector<uint64_t> contacts;
        mImpl->syncNavigation(doors);
        if (mImpl->mAvoidanceEnabled)
        {
            if (!travel.door)
                for (const auto& door : doors)
                    if (door.mAvoid) { travel.door = door.mId; travel.avoidance = {}; break; }
            bool changed = false;
            for (size_t i = 0; i < doors.size(); ++i)
                changed |= doors[i].mAngle != mImpl->mDoors[i].mAngle;
            if (!travel.door && (changed || (!path.isPathConstructed() && !path.checkPathCompleted())))
                mImpl->rebuildPath(path, frame->mPosition, travel);
        }
        // The staged angles participate in stock sweeps/stepping. Restore the
        // committed broadphase even when a solver/allocation exception escapes.
        struct RestoreDoors
        {
            Impl& scene;
            ~RestoreDoors() { scene.applyDoors(scene.mDoors); }
        } restore{*mImpl};
        mImpl->applyDoors(doors);
        mImpl->advance(*frame,path,contacts,travel,movement,doors);
        mImpl->advance(*frame,path,contacts,travel,movement,doors);
        auto bytes = mImpl->encode(*frame,path,contacts,travel);
        return std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::State>(Prepared::State{
            mImpl->mLifetime, mImpl->mActorId, std::move(frame), std::move(path), std::move(contacts), std::move(bytes),
            {doors.begin(), doors.end()}, std::move(travel)})));
    }
    bool InteriorActorScene::canInstall(const Prepared& prepared) const noexcept
    { return mImpl && prepared.mState->lifetime == mImpl->mLifetime; }

    void InteriorActorScene::install(Prepared& prepared) noexcept
    {
        auto& state = *prepared.mState;
        assert(canInstall(prepared));
        mImpl->mActor.swap(state.frame); std::swap(mImpl->mPath, state.path); mImpl->mContacts.swap(state.contacts);
        mImpl->mDoors.swap(state.doors);
        std::swap(mImpl->mTravel, state.travel);
        mImpl->applyDoors(mImpl->mDoors);
        mImpl->updateTransform();
    }

}
