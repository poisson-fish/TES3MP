#include <apps/openmw/mwmechanics/jump.hpp>
#include "actor_scene.hpp"
#include "actor_processing_regions.hpp"
#include "stock_actor_script.hpp"
#include <components/sceneutil/animationkeys.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadbody.hpp>
#include "actor_inventory.hpp"
#include "loadout.hpp"
#include "actor_spawns.hpp"
#include <apps/openmw/mwworld/manualref.hpp>
#include <bit>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwclass/npcmodel.hpp>
#include <apps/openmw/mwphysics/actorshape.hpp>
#include <apps/openmw/mwphysics/collisioneffects.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/doorcontact.hpp>
#include <apps/openmw/mwphysics/movementdata.hpp>
#include <apps/openmw/mwphysics/movementsolver.hpp>
#include <apps/openmw/mwphysics/trace.h>
#include <apps/openmw/mwmechanics/pathfinding.hpp>
#include <apps/openmw/mwmechanics/pathgrid.hpp>
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
#include <components/misc/coordinateconverter.hpp>
#include <components/misc/pathgridutils.hpp>
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
        constexpr uint64_t NeighborImageMagic = 0x31524f424847494e;

        std::vector<char> joinActorImages(std::span<const char> primary, std::span<const char> neighbor)
        {
            if (primary.empty() || neighbor.empty() || primary.size() + neighbor.size() > 65536 - 24)
                throw std::invalid_argument("Neighbor actor image exceeds bound");
            std::vector<char> result;
            result.reserve(24 + primary.size() + neighbor.size());
            putAreaWord(result, NeighborImageMagic);
            putAreaWord(result, primary.size());
            putAreaWord(result, neighbor.size());
            result.insert(result.end(), primary.begin(), primary.end());
            result.insert(result.end(), neighbor.begin(), neighbor.end());
            return result;
        }

        std::pair<std::span<const char>, std::span<const char>> splitActorImages(std::span<const char> bytes)
        {
            if (bytes.size() < 24 || bytes.size() > 65536)
                throw std::invalid_argument("Neighbor actor image size invalid");
            size_t offset = 0;
            if (getAreaWord(bytes, offset) != NeighborImageMagic)
                throw std::invalid_argument("Neighbor actor image version invalid");
            const auto first = getAreaWord(bytes, offset), second = getAreaWord(bytes, offset);
            if (!first || !second || first > bytes.size() - offset
                || second != bytes.size() - offset - first)
                throw std::invalid_argument("Neighbor actor image lengths invalid");
            return {bytes.subspan(offset, size_t(first)), bytes.subspan(offset + size_t(first), size_t(second))};
        }

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
            uint64_t mId = 0;
            ESM::RefId mCell;
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
        struct Collision
        {
            btDefaultCollisionConfiguration configuration;
            btCollisionDispatcher dispatcher{&configuration};
            btDbvtBroadphase broadphase;
            btCollisionWorld world{&dispatcher, &broadphase, &configuration};
        };
        std::shared_ptr<Collision> mCollision;
        btCollisionWorld& mWorld;
        Impl* mSharedParent = nullptr;
        std::shared_ptr<btStaticPlaneShape> mWaterShape;
        std::shared_ptr<btCollisionObject> mWaterObject;
        std::optional<float> mInteriorWater;
        std::vector<std::unique_ptr<Body>> mBodies;
        struct Terrain
        {
            btCollisionWorld& world;
            uint64_t identity = 0;
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
        std::map<ESM::RefId, float> mCellWater;
        bool mMultipleDomains = false;
        bool mExpandedExterior = false;
        std::vector<ActorSceneDoor> localDoors(std::span<const ActorSceneDoor> doors) const
        {
            std::vector<ActorSceneDoor> result(doors.begin(), doors.end());
            if (mMultipleDomains)
                std::erase_if(result, [&](const auto& door) { return !mOrdinaryDoors.contains(door.mId); });
            return result;
        }
        std::map<const btCollisionObject*, uint64_t> mIdentities;
        std::map<uint64_t, btCollisionObject*> mActorObstacles;
        std::unique_ptr<MWPhysics::ActorFrameData> mActor;
        osg::Vec3f mActorOffset;
        bool mActorRotating = false;
        uint64_t mActorId;
        std::vector<uint64_t> mContacts;
        std::string mFingerprint;
        ESM::RefId mActorBase;
        VFS::Path::Normalized mBaseAnimation, mBeastAnimation;
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
        bool mNormaliseRaceSpeed = false;
        bool mWaterNavigation = false;
        bool mIntrinsicFlying = false;
        bool mDynamicDestination = false;
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
            uint8_t jumpFlags = 0;
            float fallHeight = 0, landingFall = 0;
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
        { if (mWaterObject && !mSharedParent) mWorld.removeCollisionObject(mWaterObject.get()); }

        float waterAt(const osg::Vec3f& position) const
        {
            if (mInteriorWater) return *mInteriorWater;
            const int x = int(std::floor(double(position.x()) / ESM::Land::REAL_SIZE));
            const int y = int(std::floor(double(position.y()) / ESM::Land::REAL_SIZE));
            const auto cell = mCellWater.find(ESM::RefId::esm3ExteriorCell(x, y));
            if (cell != mCellWater.end()) return cell->second;
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
                .mFlying = movement.levitating || mIntrinsicFlying, .mWasOnGround = frame.mIsOnGround,
                .mWaterCollision = movement.waterWalking && water > -1e30f, .mIsPlayer = frame.mIsPlayer};
        }

        float movementSpeed(const MWPhysics::ActorFrameData& frame, const ActorMovement& movement) const
        {
            if (mIntrinsicFlying) return movement.walkSpeed;
            if (movement.levitating) return movement.flySpeed;
            if (frame.mPosition.z() < frame.mSwimLevel) return movement.swimSpeed;
            return movement.walkSpeed;
        }

        void validateMovement(const ActorMovement& movement) const
        {
            const auto speed = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 4096.f; };
            const auto fraction = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; };
            if (!speed(movement.walkSpeed) || (movement.enabled && (!speed(movement.swimSpeed)
                    || !speed(movement.flySpeed) || !speed(movement.jumpSpeed)
                    || !fraction(movement.slowFall) || !fraction(movement.airControl)))
                || !std::isfinite(movement.xyScale) || movement.xyScale <= 0.f || movement.xyScale > 100.f)
                throw std::invalid_argument("Actor movement outside bounds");
        }

        void move(MWPhysics::ActorFrameData& frame, std::vector<uint64_t>& contacts,
            const osg::Vec3f& velocity, const ActorMovement& movement, Travel& travel)
        {
            const bool wasGrounded = frame.mIsOnGround;
            const float oldHeight = frame.mPosition.z();
            const bool swimming = frame.mPosition.z() < frame.mSwimLevel;
            const bool requested = movement.jumpRequested || MWMechanics::forceJumpRequested(
                travel.jumpFlags & 1, travel.jumpFlags & 2, velocity.x() != 0.f || velocity.y() != 0.f,
                wasGrounded, swimming, frame.mFlying);
            const auto input = movement.enabled ? MWMechanics::jumpMovement(velocity, movement.jumpSpeed,
                requested, wasGrounded, swimming, frame.mFlying, false, movement.airControl) : velocity;
            simulate(frame, contacts, {input.x() * movement.xyScale, input.y() * movement.xyScale, input.z()});
            if (!movement.enabled) return;
            // Stock MTPhysics resets fall height in water/flight/SlowFall; otherwise
            // descending physics contributes until CharacterController lands.
            if ((wasGrounded && frame.mIsOnGround) || frame.mFlying
                || frame.mPosition.z() < frame.mSwimLevel || frame.mSlowFall < 1.f)
                travel.fallHeight = 0.f;
            else if (frame.mPosition.z() < oldHeight) travel.fallHeight += oldHeight - frame.mPosition.z();
            if (!std::isfinite(travel.fallHeight) || travel.fallHeight > 1e7f)
                throw std::invalid_argument("Actor fall height outside bounds");
            if (!wasGrounded && frame.mIsOnGround)
            {
                travel.landingFall += travel.fallHeight;
                travel.fallHeight = 0.f;
            }
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

        void syncNavigationPosition(const osg::Vec3f& position)
        {
            if (!mExpandedExterior || !mNavigator) return;
            // The tile cache follows this actor, not the region's initial anchor.
            // It is derived state: every query recenters even after rejection.
            mNavigator->updateBounds(ESM::RefId::stringRefId("native-interior"), {}, position, nullptr);
            mNavigator->update(position, nullptr);
            mNavigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
        }

        void rebuildPath(MWMechanics::PathFinder& path, const osg::Vec3f& position, const Travel& travel)
        {
            path.clearPath();
            if (!travel.hasDestination) return;
            syncNavigationPosition(position);
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
            if (doors.size() != mDoors.size()) throw std::invalid_argument("Actor door domain mismatch: actor="
                + std::to_string(mActorId) + " received=" + std::to_string(doors.size())
                + " expected=" + std::to_string(mDoors.size()));
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
            if (mSharedParent) { mSharedParent->applyDoors(doors); return; }
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
            const std::string& baseAnimation, const std::string& beastAnimation, Impl* sharedParent = nullptr,
            std::span<const DynamicActorBody> dynamic = {}, bool scenery = false, bool regions = false)
            : mStore(loadout.store()), mResources(&mVfs, 0, &loadout.encoder()),
              mShapes(new Resource::BulletShapeManager(&mVfs, mResources.getSceneManager(),
                  mResources.getNifFileManager(), 0)),
              mReferences(loadout.store(), loadout.readers(), 1),
              mCollision(sharedParent ? sharedParent->mCollision : std::make_shared<Collision>()),
              mWorld(mCollision->world), mSharedParent(sharedParent), mActorId(actor),
              mBaseAnimation(baseAnimation), mBeastAnimation(beastAnimation)
        {
            if (cells.empty() || cells.size() > (regions ? MaxActorProcessingCells : 9) || (!actor && !scenery)
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
                    || (!regions && (std::abs(int64_t(exterior->getX()) - anchor->getX()) > 1
                        || std::abs(int64_t(exterior->getY()) - anchor->getY()) > 1))) : cells.size() != 1))
                    throw std::invalid_argument("Actor processing cells must fit one unique 3x3 neighborhood");
            }
            mExpandedExterior = anchor && std::ranges::any_of(cells, [&](auto cell) {
                const auto& exterior = *cell.template getIf<ESM::ESM3ExteriorCellRefId>();
                return std::abs(int64_t(exterior.getX()) - anchor->getX()) > 1
                    || std::abs(int64_t(exterior.getY()) - anchor->getY()) > 1;
            });
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
            ESM::RefId bodyCell;
            const auto addBody = [&](const MWWorld::Ptr& ptr, uint64_t id) {
                if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                    || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId())) return true;
                if (++references > MaxBodies) throw std::length_error("Interior collision reference budget exceeded");
                const auto& cls = ptr.getClass();
                if (!identities.insert(id).second) throw std::invalid_argument("Duplicate collision placement across cells");
                const bool npc = ptr.getType() == ESM::NPC::sRecordId;
                const bool actorBody = npc || ptr.getType() == ESM::Creature::sRecordId;
                if (ptr.getType() == ESM::CreatureLevList::sRecordId)
                    throw std::invalid_argument("Unresolved leveled actor in interior collision domain");
                if (id == actor && (!actorBody || initialCorpse(ptr)
                    || (!cls.getScript(ptr).empty()
                        && !(npc && mStore.get<ESM::Script>().search(cls.getScript(ptr)))
                        && !(std::ranges::any_of(dynamic, [id](const auto& body) { return body.actor == id; })
                            && !stockActorSpawnDisease(cls.getScript(ptr), mStore).empty()))))
                    throw std::invalid_argument("Selected actor requires unsupported corpse/script services");
                if (actorBody && initialCorpse(ptr))
                    throw std::invalid_argument("Corpse collision requires gameplay animation");
                const auto& position = ptr.getRefData().getPosition();
                float scale = ptr.getCellRef().getScale();
                if (actorBody && !npc)
                {
                    osg::Vec3f adjusted(scale, scale, scale);
                    cls.adjustScale(ptr, adjusted, false);
                    scale = adjusted.x();
                }
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
                body->mId = id;
                body->mCell = bodyCell;
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
                        mIntrinsicFlying = !npc && (ptr.get<ESM::Creature>()->mBase->mFlags & ESM::Creature::Flies);
                        mActorOffset = shape->mCollisionBox.mCenter * scale;
                        mActorRotating = !hull.mRotationallyInvariant;
                        mAgentBounds = {hull.mType, extents * scale};
                        mActor = std::make_unique<MWPhysics::ActorFrameData>(MWPhysics::ActorFrameData{
                            .mPosition = position.asVec3(),
                            .mCollisionObject = body->mObject.get(),
                            .mRotation = { position.rot[0], position.rot[2] },
                            .mHalfExtentsZ = extents.z() * scale,
                            .mIsPlayer = std::ranges::any_of(dynamic, [id](const auto& body) {
                                return body.actor == id && bool(body.playerBody);
                            }) });
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
                if (actorBody && id != actor)
                {
                    mActorObstacles.emplace(id, object);
                }
                if (ptr.getType() == ESM::Door::sRecordId && !ptr.getCellRef().getTeleport())
                    mOrdinaryDoors.emplace(id, mBodies.size());
                mTransforms.push_back({position, scale});
                mBodies.push_back(std::move(body));
                mIdentities.emplace(object, id);
                if (!mSharedParent)
                    mWorld.addCollisionObject(object, actorBody ? MWPhysics::CollisionType_Actor : MWPhysics::CollisionType_World,
                        actorBody ? (MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Actor) : MWPhysics::CollisionType_Actor);
                return true;
            };
            for (auto cellId : cells)
            {
            bodyCell = cellId;
            auto& cell = mReferences.getCell(cellId);
            mCellWater.emplace(cellId, cell.getWaterLevel());
            if (!cell.isExterior() && cell.getCell()->hasWater())
            {
                const float level = cell.getWaterLevel();
                if (!std::isfinite(level)) throw std::invalid_argument("Actor water level outside bounds");
                mInteriorWater = level;
            }
            cell.forEach([&](const MWWorld::Ptr& ptr) {
                const auto id = MWWorld::placedRefId(ptr.getCellRef().getRefNum(), loadout.options().mContent);
                if (!id) throw std::invalid_argument("Collision reference identity invalid");
                return addBody(ptr, *id);
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
                terrain->identity = mTerrain.size() + 1;
                mIdentities.emplace(terrain->object.get(), terrain->identity);
                if (!mSharedParent)
                    mWorld.addCollisionObject(terrain->object.get(), MWPhysics::CollisionType_HeightMap, MWPhysics::CollisionType_Actor);
                mTerrain.push_back(std::move(terrain));
            }
            }
            if (dynamic.size() > 32) throw std::invalid_argument("Dynamic collision capacity exceeded");
            for (const auto& value : dynamic)
            {
                bodyCell = value.cell;
                MWWorld::ManualRef reference(mStore, value.record);
                auto ptr = reference.getPtr();
                if (value.playerBody)
                {
                    if (ptr.getType() != ESM::NPC::sRecordId)
                        throw std::invalid_argument("Player collision requires an NPC body");
                    ptr.get<ESM::NPC>()->mBase = value.playerBody.get();
                }
                ESM::Position position{};
                std::copy(value.position.begin(), value.position.end(), position.pos);
                position.rot[2] = value.yaw;
                ptr.getRefData().setPosition(position);
                addBody(ptr, value.actor);
            }
            if (actor && !mActor) throw std::invalid_argument("Selected NPC absent from interior collision scene");
            if (mSharedParent)
            {
                auto shared = mSharedParent->mActorObstacles.find(actor);
                if (shared == mSharedParent->mActorObstacles.end() && mActor->mIsPlayer)
                {
                    // The player child owns the hull; its lifetime is nested in
                    // the regional owner and ends before that collision world.
                    mWorld.addCollisionObject(mActor->mCollisionObject, MWPhysics::CollisionType_Actor,
                        MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
                            | MWPhysics::CollisionType_Actor);
                    mSharedParent->mIdentities.emplace(mActor->mCollisionObject, actor);
                    shared = mSharedParent->mActorObstacles.emplace(actor, mActor->mCollisionObject).first;
                }
                if (shared == mSharedParent->mActorObstacles.end())
                    throw std::invalid_argument("Shared NPC collision body absent");
                mActor->mCollisionObject = shared->second;
                mIdentities = mSharedParent->mIdentities;
            }
            if (mInteriorWater || !mTerrain.empty())
            {
                if (mSharedParent)
                {
                    mWaterShape = mSharedParent->mWaterShape;
                    mWaterObject = mSharedParent->mWaterObject;
                    if (!mWaterObject) throw std::invalid_argument("Shared actor water domain differs");
                }
                else
                {
                    mWaterShape = std::make_shared<btStaticPlaneShape>(btVector3(0, 0, 1), 0);
                    mWaterObject = std::make_shared<btCollisionObject>();
                    mWaterObject->setCollisionShape(mWaterShape.get());
                    mWorld.addCollisionObject(mWaterObject.get(), MWPhysics::CollisionType_Water,
                        MWPhysics::CollisionType_Actor);
                }
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
                    world.updateSingleAabb(&object);
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
            struct RestoreActor
            {
                btCollisionWorld& world;
                btCollisionObject* body;
                btTransform transform;
                ~RestoreActor() { body->setWorldTransform(transform); world.updateSingleAabb(body); }
            } restoreActor{mWorld, frame.mCollisionObject, frame.mCollisionObject->getWorldTransform()};
            frame.mCollisionObject->setWorldTransform(actorTransform(frame));
            mWorld.updateSingleAabb(frame.mCollisionObject);
            MWPhysics::MovementSolver::unstuck(frame, &mWorld);
            MWPhysics::MovementSolver::move(frame, 1.f/60.f, &mWorld, {}, effects);
            if (!std::isfinite(frame.mPosition.length2()) || frame.mPosition.length2()>3e14f)
                throw std::runtime_error("Interior NPC solver output outside bounds");
            contacts.swap(effects.mObjects);
        }
        void navigate(MWPhysics::ActorFrameData& frame, MWMechanics::PathFinder& path,
            std::vector<uint64_t>& contacts, float speed, const ActorMovement& movement, Travel& travel)
        {
            if (!mNavigator || !std::isfinite(speed) || speed<0 || speed>4096)
                throw std::invalid_argument("Interior navigation speed outside bounds");
            path.update(frame.mPosition, 8, 8, 0, mAgentBounds,
                navigationFlags(), *mNavigator);
            if (path.isPathConstructed()) frame.mRotation.y()=path.getZAngleToNext(frame.mPosition.x(), frame.mPosition.y());
            move(frame, contacts, {0, path.isPathConstructed() ? speed : 0, 0}, movement, travel);
        }
        btTransform actorTransform(const MWPhysics::ActorFrameData& frame) const noexcept
        {
            const osg::Quat rotation = mActorRotating ? osg::Quat(frame.mRotation.y(), osg::Vec3f(0, 0, -1)) : osg::Quat();
            return btTransform(Misc::Convert::toBullet(rotation), Misc::Convert::toBullet(frame.mPosition + rotation * mActorOffset));
        }
        void updateTransform() noexcept
        {
            auto transform=actorTransform(*mActor);
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
            if (frame.mFlying && travel.hasDestination)
            {
                // Reuse the stock route around floor-level obstructions when
                // available. Elevated destinations can still fly directly.
                path.update(frame.mPosition, 8, 8, 0, mAgentBounds, navigationFlags(), *mNavigator);
                MWPhysics::ActorTracer tracer;
                const auto offset = Misc::Convert::toOsg(actorTransform(frame).getOrigin()) - frame.mPosition;
                tracer.doTrace(frame.mCollisionObject, frame.mPosition + offset,
                    travel.destination + offset, &mWorld);
                const bool routed = path.isPathConstructed() && tracer.mFraction < 1.f;
                const auto delta = travel.destination - frame.mPosition;
                const float horizontal = std::sqrt(delta.x()*delta.x() + delta.y()*delta.y());
                frame.mRotation.y() = routed ? path.getZAngleToNext(frame.mPosition.x(), frame.mPosition.y())
                    : std::atan2(delta.x(), delta.y());
                frame.mRotation.x() = routed ? path.getXAngleToNext(frame.mPosition.x(), frame.mPosition.y(), frame.mPosition.z())
                    : -std::atan2(delta.z(), horizontal);
                move(frame, contacts, {0, delta.length2() > 64.f ? speed : 0.f, 0}, movement, travel);
                updateBreath(frame, travel, movement);
                return;
            }
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
                    move(frame, contacts, {0, turn.mComplete ? speed : 0, 0}, movement, travel);
                    updateBreath(frame, travel, movement);
                    return;
                }
                travel.door = 0;
                travel.avoidance = {};
                rebuildPath(path, frame.mPosition, travel);
            }
            navigate(frame, path, contacts, speed, movement, travel);
            updateBreath(frame, travel, movement);
        }

        ActorSceneSnapshot snapshot() const
        {
            return {mActorId, {mActor->mPosition.x(), mActor->mPosition.y(), mActor->mPosition.z()},
                mActor->mIsOnGround, mContacts, mActor->mRotation.y(), mTravel.drowning, mTravel.jumpFlags, 0,
                {mActor->mInertia.x(), mActor->mInertia.y(), mActor->mInertia.z()},
                {mActor->mLastStuckPosition.x(), mActor->mLastStuckPosition.y(), mActor->mLastStuckPosition.z()},
                mTravel.fallHeight, mTravel.breath, mActor->mStuckFrames, mActor->mIsOnSlope, mActor->mWalkingOnWater};
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
        const std::string& baseAnimation, const std::string& beastAnimation, uint64_t neighbor)
        : InteriorActorScene(loadout, cells, actor, baseAnimation, beastAnimation,
            neighbor ? std::span<const uint64_t>(&neighbor, 1) : std::span<const uint64_t>{}, nullptr) {}
    InteriorActorScene::InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation,
        std::span<const uint64_t> neighbors)
        : InteriorActorScene(loadout, cells, actor, baseAnimation, beastAnimation, neighbors, nullptr) {}
    InteriorActorScene::InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation,
        std::span<const uint64_t> neighbors, Impl* sharedParent, std::span<const DynamicActorBody> dynamic, bool scenery, bool regions)
        : mImpl(std::make_unique<Impl>(loadout, cells, actor, baseAnimation, beastAnimation, sharedParent, dynamic, scenery, regions))
    {
        if (actor && !contains(snapshot().mPosition))
            throw std::invalid_argument("Actor start outside dry processing neighborhood");
        if (!neighbors.empty())
        {
            if (neighbors.size() > 36 || !neighbors.front()
                || std::ranges::find(neighbors, actor) != neighbors.end()
                || std::ranges::find(neighbors.begin() + 1, neighbors.end(), neighbors.front()) != neighbors.end())
                throw std::invalid_argument("Neighbor actor placements invalid");
            mNeighbor.reset(new InteriorActorScene(loadout, cells, neighbors.front(),
                baseAnimation, beastAnimation, neighbors.subspan(1), sharedParent ? sharedParent : mImpl.get(), dynamic));
            mNeighbor->mImpl->mDynamicDestination = true;
        }
    }
    InteriorActorScene::InteriorActorScene(Loadout& loadout, std::span<const ESM::RefId> cells, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation,
        std::span<const uint64_t> neighbors, std::span<const DynamicActorBody> dynamic)
        : InteriorActorScene(loadout, cells, actor, baseAnimation, beastAnimation, neighbors, nullptr, dynamic) {}

    InteriorActorScene::InteriorActorScene(Loadout& loadout,
        const std::vector<std::pair<uint64_t, std::vector<ESM::RefId>>>& actors,
        const std::string& baseAnimation, const std::string& beastAnimation,
        std::span<const DynamicActorBody> dynamic, std::span<const ESM::RefId> allCells)
    {
        if (actors.empty() || actors.size() > 37)
            throw std::invalid_argument("Actor processing domain capacity exceeded");
        std::set<uint64_t> identities;
        std::vector<ESM::RefId> boundCells;
        const auto addCell = [&](ESM::RefId cell) {
            if (std::ranges::find(boundCells, cell) != boundCells.end()) return;
            if (boundCells.size() == MaxActorProcessingCells)
                throw std::invalid_argument("Actor processing cell capacity exceeded");
            boundCells.push_back(cell);
        };
        if (allCells.size() > MaxActorProcessingCells)
            throw std::invalid_argument("Actor processing cell capacity exceeded");
        for (const auto& [id, cells] : actors)
        {
            if (!id || !identities.insert(id).second || cells.empty() || cells.size() > MaxActorProcessingCells
                || std::set<ESM::RefId>(cells.begin(), cells.end()).size() != cells.size())
                throw std::invalid_argument("Actor processing identity/domain invalid");
            for (auto cell : cells) addCell(cell);
        }
        if (std::set<ESM::RefId>(allCells.begin(), allCells.end()).size() != allCells.size())
            throw std::invalid_argument("Duplicate bound processing cell");
        for (auto cell : allCells) addCell(cell);
        const auto regions = actorProcessingRegions(boundCells);
        std::vector<size_t> actorRegions;
        for (const auto& [id, cells] : actors)
        {
            const auto region = std::ranges::find_if(regions, [&](const auto& value) {
                return std::ranges::find(value, cells.front()) != value.end();
            });
            if (!std::ranges::all_of(cells, [&](auto cell) {
                    return std::ranges::find(*region, cell) != region->end(); }))
                throw std::invalid_argument("Actor domain spans separate processing regions");
            actorRegions.push_back(size_t(region - regions.begin()));
        }
        std::map<std::vector<ESM::RefId>, Impl*> parents;
        InteriorActorScene* previous = nullptr;
        for (size_t i = 0; i < actors.size(); ++i)
        {
            const auto id = actors[i].first;
            const auto& cells = regions[actorRegions[i]];
            std::vector<DynamicActorBody> bodies;
            for (const auto& body : dynamic)
                if (std::ranges::find(cells, body.cell) != cells.end()) bodies.push_back(body);
            auto next = std::unique_ptr<InteriorActorScene>(new InteriorActorScene(loadout, cells, id,
                baseAnimation, beastAnimation, {}, parents[cells], bodies, false, true));
            next->mImpl->mMultipleDomains = true;
            next->mImpl->mDynamicDestination = previous != nullptr;
            if (!parents[cells]) parents[cells] = next->mImpl.get();
            if (!previous) { mImpl.swap(next->mImpl); previous = this; }
            else { previous->mNeighbor = std::move(next); previous = previous->mNeighbor.get(); }
        }
        for (size_t i = 0; i < regions.size(); ++i)
        {
            if (std::ranges::find(actorRegions, i) != actorRegions.end()) continue;
            auto scenery = std::unique_ptr<InteriorActorScene>(new InteriorActorScene(loadout,
                regions[i], 0, baseAnimation, beastAnimation, {}, nullptr, {}, true, true));
            scenery->mImpl->mMultipleDomains = true;
            mImpl->mFingerprint += scenery->fingerprint();
            mScenery.push_back(std::move(scenery));
        }
    }

    const InteriorActorScene& InteriorActorScene::forActor(uint64_t actor) const
    {
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->actorId() == actor) return *scene;
        throw std::invalid_argument("Collision actor outside processing domain");
    }
    InteriorActorScene& InteriorActorScene::forActor(uint64_t actor)
    { return const_cast<InteriorActorScene&>(std::as_const(*this).forActor(actor)); }
    const InteriorActorScene& InteriorActorScene::forCell(ESM::RefId cell) const
    {
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->mImpl && std::ranges::find(scene->mImpl->mCells, cell) != scene->mImpl->mCells.end()) return *scene;
        for (const auto& scene : mScenery)
            if (std::ranges::find(scene->mImpl->mCells, cell) != scene->mImpl->mCells.end()) return *scene;
        throw std::invalid_argument("Collision cell outside active processing domain");
    }

    void InteriorActorScene::installActorSet(InteriorActorScene& candidate) noexcept
    {
        mImpl.swap(candidate.mImpl);
        mNeighbor.swap(candidate.mNeighbor);
        mScenery.swap(candidate.mScenery);
        mGeometryDemand.swap(candidate.mGeometryDemand);
        mDormant.swap(candidate.mDormant);
        mPlayers.swap(candidate.mPlayers);
        mPlayerBindings.swap(candidate.mPlayerBindings);
        mUnloadedPlayerImages.swap(candidate.mUnloadedPlayerImages);
    }

    void InteriorActorScene::bindPlayers(Loadout& loadout, std::span<const DynamicActorBody> players)
    {
        if (!mImpl || !mPlayers.empty() || players.size() > 2)
            throw std::invalid_argument("Player collision domain invalid");
        std::set<uint64_t> unique;
        for (const auto& player : players)
            if (!player.actor || !player.playerBody || !unique.insert(player.actor).second)
                throw std::invalid_argument("Player collision identity invalid");
        for (const auto& player : players)
        {
            auto& parent = *const_cast<InteriorActorScene&>(forCell(player.cell)).mImpl;
            auto* regional = &parent;
            while (regional->mSharedParent) regional = regional->mSharedParent;
            auto child = std::unique_ptr<InteriorActorScene>(new InteriorActorScene(loadout, regional->mCells,
                player.actor, "meshes/base_anim.nif", "meshes/base_animkna.nif", {}, regional,
                std::span(&player, 1), false, true));
            child->mImpl->mMultipleDomains = true;
            std::vector<uint64_t> doors;
            for (const auto& door : regional->mDoors) doors.push_back(door.mId);
            if (!doors.empty()) child->bindDoors(doors, false);
            child->enableMovementEffects();
            mPlayers.push_back(std::move(child));
        }
        // Shared actor readers must see the same identities as the owner.
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->mImpl->mSharedParent) scene->mImpl->mIdentities = scene->mImpl->mSharedParent->mIdentities;
        for (auto& scene : mPlayers) scene->mImpl->mIdentities = scene->mImpl->mSharedParent->mIdentities;
        mPlayerBindings.assign(players.begin(), players.end());
    }

    const InteriorActorScene& InteriorActorScene::forPlayer(uint64_t actor) const
    {
        for (const auto& scene : mPlayers) if (scene->actorId() == actor) return *scene;
        throw std::invalid_argument("Player outside collision domain");
    }
    InteriorActorScene& InteriorActorScene::forPlayer(uint64_t actor)
    { return const_cast<InteriorActorScene&>(std::as_const(*this).forPlayer(actor)); }
    bool InteriorActorScene::hasPlayer(uint64_t actor) const noexcept
    { return std::ranges::any_of(mPlayers, [actor](const auto& scene) { return scene->actorId() == actor; }); }
    float InteriorActorScene::playerMovementScale(uint64_t actor) const
    {
        const auto found = std::ranges::find(mPlayerBindings, actor, &DynamicActorBody::actor);
        if (!mImpl || found == mPlayerBindings.end()) throw std::invalid_argument("Player speed body unavailable");
        if (mImpl->mNormaliseRaceSpeed) return 1.f;
        const auto& npc = *found->playerBody;
        const auto& race = *mImpl->mStore.get<ESM::Race>().find(npc.mRace);
        const float scale = npc.isMale() ? race.mData.mMaleWeight : race.mData.mFemaleWeight;
        if (!std::isfinite(scale) || scale <= 0.f || scale > 100.f)
            throw std::invalid_argument("Player movement scale invalid");
        return scale;
    }
    void InteriorActorScene::copyPlayersTo(Loadout& loadout, InteriorActorScene& fresh) const
    {
        if (mPlayerBindings.empty()) return;
        auto bindings = mPlayerBindings;
        if (mImpl) for (size_t i = 0; i < bindings.size(); ++i)
        {
            const auto frame = mPlayers[i]->snapshot();
            bindings[i].position = frame.mPosition; bindings[i].yaw = frame.mYaw;
            if (bindings[i].cell.is<ESM::ESM3ExteriorCellRefId>())
                bindings[i].cell = ESM::RefId::esm3ExteriorCell(int(std::floor(frame.mPosition[0] / ESM::Land::REAL_SIZE)),
                    int(std::floor(frame.mPosition[1] / ESM::Land::REAL_SIZE)));
        }
        fresh.bindPlayers(loadout, bindings);
        for (size_t i = 0; i < mPlayerBindings.size(); ++i)
        {
            auto& player = fresh.forPlayer(mPlayerBindings[i].actor);
            const auto bytes = mImpl ? mPlayers[i]->image() : mUnloadedPlayerImages.at(i);
            auto restored = player.prepareRestore(bytes, player.mImpl->mDoors);
            player.install(*restored);
        }
    }
    void InteriorActorScene::replaceWorld(const ActorSceneWorld& world)
    {
        if (!mImpl || world.placements.size() > MaxBodies || world.objects.size() > MaxBodies)
            throw std::invalid_argument("Collision world image outside bounds");
        auto& scene = *mImpl;
        const std::set<uint64_t> placements(world.placements.begin(), world.placements.end());
        if (placements.size() != world.placements.size() || placements.contains(0))
            throw std::invalid_argument("Ground collision placement domain invalid");
        std::set<uint64_t> live;
        for (size_t i = scene.mBodies.size(); i-- > 0;)
        {
            auto* object = scene.mBodies[i]->mObject.get();
            if (!placements.contains(scene.mBodies[i]->mId)) continue;
            if (scene.mBodies[i]->mHull || scene.mOrdinaryDoors.contains(scene.mBodies[i]->mId))
                throw std::invalid_argument("Ground collision domain contains actor/door");
            if (scene.mNavigator) scene.mNavigator->removeObject(DetourNavigator::ObjectId(object), nullptr);
            scene.mIdentities.erase(object);
            scene.mBodies.erase(scene.mBodies.begin() + i);
            scene.mTransforms.erase(scene.mTransforms.begin() + i);
            for (auto& [id, index] : scene.mOrdinaryDoors) if (index > i) --index;
        }
        for (const auto& [id, state] : world.objects)
        {
            if (scene.mMultipleDomains && (!world.cells.contains(id)
                || std::ranges::find(scene.mCells, world.cells.at(id)) == scene.mCells.end())) continue;
            if (!id || !live.insert(id).second || state.mRef.mCount <= 0
                || scene.mBodies.size() == MaxBodies || (!scene.mSharedParent
                    && std::ranges::any_of(scene.mIdentities, [id](const auto& entry) { return entry.second == id; })))
                throw std::invalid_argument("Ground collision identity/count outside bounds");
            validatePosition(state.mPosition, state.mRef.mScale);
            MWWorld::ManualRef reference(scene.mStore, state.mRef.mRefID);
            const auto ptr = reference.getPtr();
            const auto model = ptr.getClass().getCorrectedModel(ptr);
            if (model.empty()) continue;
            if (!scene.mVfs.exists(model)) throw std::invalid_argument("Missing ground collision model");
            auto shape = scene.mShapes->getInstance(model);
            if (!shape || !shape->mCollisionShape) continue;
            if (shape->isAnimated()) throw std::invalid_argument("Animated ground collision requires CPU evaluation");
            auto body = std::make_unique<Impl::Body>(scene.mWorld);
            body->mId = id;
            body->mCell = scene.mMultipleDomains ? world.cells.at(id) : scene.mCells.front();
            body->mResource = shape;
            shape->setLocalScaling(btVector3(state.mRef.mScale, state.mRef.mScale, state.mRef.mScale));
            body->mObject = BulletHelpers::makeCollisionObject(shape->mCollisionShape.get(),
                Misc::Convert::toBullet(state.mPosition.asVec3()),
                Misc::Convert::toBullet(Misc::Convert::makeOsgQuat(state.mPosition)));
            auto* object = body->mObject.get();
            if (!scene.mSharedParent)
                scene.mWorld.addCollisionObject(object, MWPhysics::CollisionType_World, MWPhysics::CollisionType_Actor);
            scene.mIdentities.emplace(object, id);
            scene.mTransforms.push_back({state.mPosition, state.mRef.mScale});
            scene.mBodies.push_back(std::move(body));
            if (scene.mNavigator) scene.mNavigator->addObject(DetourNavigator::ObjectId(object),
                DetourNavigator::ObjectShapes(shape, scene.mTransforms.back()), object->getWorldTransform(), nullptr);
        }
        // Neighbor solvers query the parent's Bullet objects, using the parent's identity map.
        if (scene.mSharedParent) scene.mIdentities = scene.mSharedParent->mIdentities;
        if (scene.mNavigator)
        {
            scene.mNavigator->update(scene.mActor->mPosition, nullptr);
            scene.mNavigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
        }
        if (mNeighbor) mNeighbor->replaceWorld(world);
        for (const auto& scene : mScenery) scene->replaceWorld(world);
    }
    size_t InteriorActorScene::terrainCount() const noexcept
    {
        size_t count = 0;
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->mImpl && !scene->mImpl->mSharedParent) count += scene->mImpl->mTerrain.size();
        for (const auto& scene : mScenery) count += scene->terrainCount();
        return count;
    }

    void InteriorActorScene::retainGeometry(std::span<const ESM::RefId> demanded)
    {
        if (!mImpl || demanded.size() > MaxActorProcessingCells)
            throw std::invalid_argument("Geometry demand capacity exceeded");
        std::vector<Impl*> scenes;
        for (auto* scene = this; scene; scene = scene->mNeighbor.get()) scenes.push_back(scene->mImpl.get());
        for (const auto& scene : mScenery) scenes.push_back(scene->mImpl.get());
        for (const auto& scene : mPlayers) scenes.push_back(scene->mImpl.get());
        std::set<ESM::RefId> unique;
        for (auto cell : demanded)
            if (!unique.insert(cell).second || !std::ranges::any_of(scenes, [&](const auto* scene) {
                return std::ranges::find(scene->mCells, cell) != scene->mCells.end();
            })) throw std::invalid_argument("Geometry demand identity outside bound domain");
        // A frozen actor still owns its exact support/contact/avoidance image.
        // Keep those witnesses alive until stock movement replaces them.
        std::map<const Impl::Collision*, std::set<uint64_t>> witnesses;
        for (const auto* scene : scenes) if (scene->mActor)
        {
            auto& pinned = witnesses[scene->mCollision.get()];
            pinned.insert(scene->mContacts.begin(), scene->mContacts.end());
            if (scene->mTravel.door) pinned.insert(scene->mTravel.door);
            if (scene->mActor->mStandingOn)
                pinned.insert(scene->mIdentities.at(scene->mActor->mStandingOn));
        }
        for (auto* scene : scenes)
        {
            const auto& pinned = witnesses[scene->mCollision.get()];
            for (size_t i = scene->mBodies.size(); i-- > 0;)
            {
                const auto& body = *scene->mBodies[i];
                if (body.mHull || unique.contains(body.mCell) || pinned.contains(body.mId)) continue;
                auto* object = body.mObject.get();
                if (scene->mNavigator) scene->mNavigator->removeObject(DetourNavigator::ObjectId(object), nullptr);
                scene->mOrdinaryDoors.erase(body.mId);
                std::erase_if(scene->mDoors, [&](const auto& door) { return door.mId == body.mId; });
                scene->mIdentities.erase(object);
                scene->mBodies.erase(scene->mBodies.begin() + i);
                scene->mTransforms.erase(scene->mTransforms.begin() + i);
                for (auto& [id, index] : scene->mOrdinaryDoors) if (index > i) --index;
            }
            std::erase_if(scene->mTerrain, [&](const auto& terrain) {
                if (unique.contains(ESM::RefId::esm3ExteriorCell(terrain->cell.x(), terrain->cell.y()))
                    || pinned.contains(terrain->identity)) return false;
                if (scene->mNavigator)
                {
                    scene->mNavigator->removeHeightfield(terrain->cell, nullptr);
                    if (scene->mWaterNavigation) scene->mNavigator->removeWater(terrain->cell, nullptr);
                }
                scene->mIdentities.erase(terrain->object.get());
                return true;
            });
            scene->mNavigationValid = false;
            scene->mNavigationDoors.clear();
            if (scene->mNavigator)
            {
                scene->mNavigator->update(scene->mActor->mPosition, nullptr);
                scene->mNavigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
            }
            scene->mReferences.clear();
            scene->mShapes->clearCache();
            scene->mResources.clearCache();
        }
        for (auto* scene : scenes) if (scene->mSharedParent)
            scene->mIdentities = scene->mSharedParent->mIdentities;
        mGeometryDemand.assign(demanded.begin(), demanded.end());
    }

    void InteriorActorScene::rebuildTravel()
    {
        if (mImpl && mImpl->mTravel.hasDestination)
        {
            mImpl->syncNavigation(mImpl->mDoors);
            mImpl->rebuildPath(mImpl->mPath, mImpl->mActor->mPosition, mImpl->mTravel);
        }
        if (mNeighbor) mNeighbor->rebuildTravel();
    }
    InteriorActorScene::~InteriorActorScene() = default;
    ActorSceneSnapshot InteriorActorScene::snapshot() const { return mImpl ? mImpl->snapshot() : mDormant->snapshot; }
    std::optional<ActorSceneSnapshot> InteriorActorScene::neighborSnapshot() const
    { return mNeighbor ? std::optional(mNeighbor->snapshot()) : std::nullopt; }
    std::vector<ActorSceneSnapshot> InteriorActorScene::neighborSnapshots() const
    {
        std::vector<ActorSceneSnapshot> result;
        for (auto* adjacent = mNeighbor.get(); adjacent; adjacent = adjacent->mNeighbor.get())
            result.push_back(adjacent->snapshot());
        return result;
    }
    std::array<float, 4> InteriorActorScene::transform() const noexcept
    {
        if (!mImpl) return mDormant->transform;
        const auto& frame=*mImpl->mActor;
        return {frame.mPosition.x(),frame.mPosition.y(),frame.mPosition.z(),frame.mRotation.y()};
    }
    uint64_t InteriorActorScene::actorId() const noexcept { return mImpl ? mImpl->mActorId : mDormant->snapshot.mActor; }
    float InteriorActorScene::actorHalfExtentY(uint64_t actor) const
    {
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->actorId() == actor && scene->mImpl) return scene->mImpl->mAgentBounds.mHalfExtents.y();
        throw std::invalid_argument("Actor hull identity absent");
    }

    float InteriorActorScene::selectedActorHalfExtentY() const
    {
        if (!mImpl) throw std::invalid_argument("Flee hull scene is unloaded");
        return mImpl->mAgentBounds.mHalfExtents.y();
    }
    float InteriorActorScene::selectedActorHorizontalRadius() const
    {
        if (!mImpl) throw std::invalid_argument("Return hull scene is unloaded");
        const auto& extent = mImpl->mAgentBounds.mHalfExtents;
        return std::hypot(extent.x(), extent.y());
    }
    std::array<float, 3> InteriorActorScene::selectedActorHalfExtents() const
    {
        if (!mImpl) throw std::invalid_argument("Activity hull scene is unloaded");
        const auto& extent = mImpl->mAgentBounds.mHalfExtents;
        return {extent.x(), extent.y(), extent.z()};
    }
    float InteriorActorScene::npcHalfExtentY(ESM::RefId race, float scale) const
    {
        if (!mImpl || !std::isfinite(scale) || scale <= 0.f || scale > 100.f)
            throw std::invalid_argument("Flee target hull input invalid");
        const auto* record = mImpl->mStore.get<ESM::Race>().search(race);
        if (!record) throw std::invalid_argument("Flee target race absent from content");
        auto model = VFS::Path::Normalized(MWClass::npcModel(*record,
            mImpl->mBaseAnimation, mImpl->mBeastAnimation));
        model = Misc::ResourceHelpers::correctActorModelPath(model, &mImpl->mVfs);
        if (!mImpl->mVfs.exists(model)) throw std::invalid_argument("Flee target collision model missing");
        const auto shape = mImpl->mShapes->getInstance(model);
        const float halfExtent = shape->mCollisionBox.mExtents.y() * scale;
        if (!std::isfinite(halfExtent) || halfExtent <= 0.f || halfExtent > 1e7f)
            throw std::invalid_argument("Flee target hull outside bounds");
        return halfExtent;
    }
    size_t InteriorActorScene::bodyCount() const { return mImpl ? mImpl->mBodies.size() : 0; }
    bool InteriorActorScene::sharesCollisionWorld(const InteriorActorScene& other) const noexcept
    { return mImpl && other.mImpl && mImpl->mCollision == other.mImpl->mCollision; }
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
    std::optional<std::array<float, 3>> InteriorActorScene::objectCenter(uint64_t id) const
    {
        if (!mImpl) throw std::invalid_argument("Object contact scene is unloaded");
        for (const auto& [object, identity] : mImpl->mIdentities)
            if (identity == id)
            {
                btVector3 low, high;
                object->getCollisionShape()->getAabb(object->getWorldTransform(), low, high);
                const auto center = (low + high) * .5f;
                return std::array<float, 3>{float(center.x()), float(center.y()), float(center.z())};
            }
        return {};
    }
    bool InteriorActorScene::lineOfSightToObject(const std::array<float, 3>& from,
        const std::array<float, 3>& to, uint64_t id) const
    {
        if (!mImpl) throw std::invalid_argument("Object contact scene is unloaded");
        for (const auto& point : {from, to}) for (float value : point)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Object contact endpoint invalid");
        const btVector3 start(from[0], from[1], from[2]), end(to[0], to[1], to[2]);
        btCollisionWorld::ClosestRayResultCallback hit(start, end);
        hit.m_collisionFilterGroup = MWPhysics::CollisionType_AnyPhysical;
        hit.m_collisionFilterMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;
        mImpl->mWorld.rayTest(start, end, hit);
        return !hit.hasHit() || (mImpl->mIdentities.contains(hit.m_collisionObject)
            && mImpl->mIdentities.at(hit.m_collisionObject) == id);
    }
    bool InteriorActorScene::lineOfSightToDoor(const std::array<float, 3>& from,
        const std::array<float, 3>& to, uint64_t door) const
    {
        if (!mImpl) throw std::invalid_argument("Door contact scene is unloaded");
        const auto found = mImpl->mOrdinaryDoors.find(door);
        if (found == mImpl->mOrdinaryDoors.end()) return false;
        for (const auto& point : {from, to}) for (float value : point)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Door contact endpoint invalid");
        const btVector3 start(from[0], from[1], from[2]), end(to[0], to[1], to[2]);
        btCollisionWorld::ClosestRayResultCallback hit(start, end);
        hit.m_collisionFilterGroup = MWPhysics::CollisionType_AnyPhysical;
        hit.m_collisionFilterMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;
        mImpl->mWorld.rayTest(start, end, hit);
        return hit.hasHit() && hit.m_collisionObject == mImpl->mBodies[found->second]->mObject.get();
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
        } hit(start, end, [&]() -> const btCollisionObject* {
            for (const auto& [object, id] : mImpl->mIdentities)
                if (id == casterActor) return object;
            return nullptr;
        }());
        hit.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
        hit.m_collisionFilterMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Actor | MWPhysics::CollisionType_Door;
        mImpl->mWorld.convexSweepTest(&sphere, startFrame, endFrame, hit);
        if (!hit.hasHit()) return {};
        const auto& point = hit.m_hitPointWorld;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 1e7)
                throw std::invalid_argument("Projectile impact position invalid");
        uint64_t actor = 0;
        if (hit.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup == MWPhysics::CollisionType_Actor
            && mImpl->mIdentities.contains(hit.m_hitCollisionObject))
            actor = mImpl->mIdentities.at(hit.m_hitCollisionObject);
        return ActorProjectileContact{actor, {float(point.x()), float(point.y()), float(point.z())},
            mImpl->mIdentities.contains(hit.m_hitCollisionObject) && (mImpl->mIdentities.at(hit.m_hitCollisionObject) >> 63)
                ? mImpl->mIdentities.at(hit.m_hitCollisionObject) : 0};
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

    BoundCastAnimations InteriorActorScene::bindCastAnimations(ESM::RefId actor, const ESM::NPC* playerBody)
    {
        if (!mImpl)
            throw std::invalid_argument("Selected NPC has no supported cast animation source");
        const auto [sources, identity] = bindAnimationSources(actor.empty() ? mImpl->mActorBase : actor, playerBody);
        for (auto source = sources.rbegin(); source != sources.rend(); ++source)
        {
            if (!(*source)->hasGroupStart("spellcast")) continue;
            return {readCastAnimations(**source), identity};
        }
        if (const auto* creature = mImpl->mStore.get<ESM::Creature>().search(actor);
            creature && !(creature->mFlags & ESM::Creature::Bipedal))
            for (auto source = sources.rbegin(); source != sources.rend(); ++source)
            {
                SceneUtil::AnimationKeys clip;
                if (!SceneUtil::findAnimationKeys(**source, "attack1", "start", "stop", clip)) continue;
                const auto stop = uint32_t(std::ceil((clip.mStop->first - clip.mStart->first) * 30.f));
                if (stop < 2 || stop > 1800) throw std::invalid_argument("Creature cast duration invalid");
                // CharacterController casts at start for random attack groups.
                return {{{CastAnimation{1, stop}, CastAnimation{1, stop}, CastAnimation{1, stop}}}, identity};
            }
        throw std::invalid_argument("Selected NPC lacks spellcast animation keys");
    }

    void InteriorActorScene::validateActorAnimations(ESM::RefId actor)
    {
        const auto [sources, identity] = bindAnimationSources(actor);
        for (const auto group : {"idle", "walkforward", "death1"})
        {
            const auto source = std::find_if(sources.rbegin(), sources.rend(), [&](const auto& keys) {
                return keys->hasGroupStart(group);
            });
            SceneUtil::AnimationKeys clip;
            if (source == sources.rend() || !SceneUtil::findAnimationKeys(**source, group, "start", "stop", clip)
                || clip.mStop->first <= clip.mStart->first || clip.mStop->first - clip.mStart->first > 60)
                throw std::invalid_argument("Actor movement/death animation absent or invalid: " + std::string(group));
        }
        for (const auto mode : {"chop", "slash", "thrust"}) (void)bindWeaponMeleeAnimation(actor, nullptr, mode);
    }

    std::pair<std::vector<std::shared_ptr<const SceneUtil::TextKeyMap>>, std::string>
    InteriorActorScene::bindAnimationSources(ESM::RefId actor, const ESM::NPC* playerBody)
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
            const auto& npc = playerBody ? *playerBody : *mImpl->mStore.get<ESM::NPC>().find(actor);
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
            if (playerBody)
                for (auto partId : {npc.mHead, npc.mHair})
                {
                    const auto& part = *mImpl->mStore.get<ESM::BodyPart>().find(partId);
                    const auto mesh = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(part.mModel));
                    if (part.mModel.empty() || mesh.value().size() > 512 || !mImpl->mVfs.exists(mesh))
                        throw std::invalid_argument("Character appearance mesh unavailable or outside bounds");
                    auto stream = mImpl->mVfs.get(mesh);
                    const auto hash = Files::getHash(mesh.value(), *stream);
                    identity << partId << ':' << mesh.value() << ':' << hash[0] << ':' << hash[1] << '\n';
                }
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



    BoundHitAnimations InteriorActorScene::bindHitAnimations(ESM::RefId actor, bool knockout, const ESM::NPC* playerBody)
    {
        const auto [owned, identity] = bindAnimationSources(actor, playerBody);
        std::vector<const SceneUtil::TextKeyMap*> sources;
        for (const auto& source : owned) sources.push_back(source.get());
        return {readHitAnimations(sources), identity,
            knockout ? readKnockoutAnimation(sources) : KnockoutAnimation{},
            knockout ? readKnockoutAnimation(sources, "knockdown") : KnockoutAnimation{}, readDeathAnimations(sources)};
    }



    MeleeAnimation InteriorActorScene::bindWeaponMeleeAnimation(ESM::RefId actor,
        const ESM::Weapon* weapon, const std::string& attack, const ESM::NPC* playerBody)
    {
        const auto type = weapon ? weapon->mData.mType : ESM::Weapon::HandToHand;
        const auto* info = MWMechanics::getWeaponType(type);
        if (info->mWeaponClass != ESM::WeaponType::Melee
            && !((info->mWeaponClass == ESM::WeaponType::Ranged || info->mWeaponClass == ESM::WeaponType::Thrown)
                && attack == "shoot"))
            throw std::invalid_argument("Native ranged execution is not bound");
        const auto [sources, resourceIdentity] = bindAnimationSources(actor, playerBody);
        std::string group(info->mLongGroup);
        const auto findGroup = [&](const std::string& name) -> const SceneUtil::TextKeyMap* {
            for (auto it = sources.rbegin(); it != sources.rend(); ++it)
                if ((*it)->hasGroupStart(name)) return it->get();
            return nullptr;
        };
        auto* keys = findGroup(group);
        if (!weapon)
            if (const auto* creature = mImpl->mStore.get<ESM::Creature>().search(actor);
                creature && !(creature->mFlags & ESM::Creature::Bipedal))
            {
                const auto number = attack == "slash" ? 2 : attack == "thrust" ? 3 : 1;
                group = "attack" + std::to_string(number);
                keys = findGroup(group);
                if (!keys) throw std::invalid_argument("Native creature attack group absent");
            }
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
        auto doors = mImpl->mDoors;
        if (mImpl->mMultipleDomains)
        {
            for (const auto& scene : mScenery)
                doors.insert(doors.end(), scene->mImpl->mDoors.begin(), scene->mImpl->mDoors.end());
            for (auto* scene = mNeighbor.get(); scene; scene = scene->mNeighbor.get())
                if (scene->mImpl) doors.insert(doors.end(), scene->mImpl->mDoors.begin(), scene->mImpl->mDoors.end());
            std::ranges::sort(doors, {}, &ActorSceneDoor::mId);
            doors.erase(std::unique(doors.begin(), doors.end(), [](const auto& a, const auto& b) {
                return a.mId == b.mId;
            }), doors.end());
        }
        if (mNeighbor) mNeighbor->unload();
        auto dormant = std::make_unique<Dormant>(Dormant{
            snapshot(), transform(), fingerprint(),
            mImpl->encode(*mImpl->mActor, mImpl->mPath, mImpl->mContacts, mImpl->mTravel),
            std::move(doors), arrived()});
        mDormant.swap(dormant);
        mUnloadedPlayerImages.clear();
        for (size_t i = 0; i < mPlayers.size(); ++i)
        {
            const auto& player = mPlayers[i];
            mUnloadedPlayerImages.push_back(player->image());
            const auto frame = player->snapshot();
            mPlayerBindings[i].position = frame.mPosition; mPlayerBindings[i].yaw = frame.mYaw;
            if (mPlayerBindings[i].cell.is<ESM::ESM3ExteriorCellRefId>())
                mPlayerBindings[i].cell = ESM::RefId::esm3ExteriorCell(int(std::floor(frame.mPosition[0] / ESM::Land::REAL_SIZE)),
                    int(std::floor(frame.mPosition[1] / ESM::Land::REAL_SIZE)));
        }
        mPlayers.clear();
        mImpl.reset();
        mScenery.clear();
    }

    void InteriorActorScene::reload(InteriorActorScene& fresh)
    {
        if (mImpl || !fresh.mImpl || fresh.fingerprint() != fingerprint())
            throw std::invalid_argument("Actor reload scene differs from bound resources");
        if (bool(mNeighbor) != bool(fresh.mNeighbor)
            || (mNeighbor && fresh.mNeighbor->fingerprint() != mNeighbor->fingerprint()))
            throw std::invalid_argument("Neighbor reload scene differs from bound resources");
        const auto saved = image();
        auto restored = fresh.prepareRestore(saved, mDormant->doors);
        fresh.install(*restored);
        mImpl.swap(fresh.mImpl);
        mDormant.swap(fresh.mDormant);
        mNeighbor.swap(fresh.mNeighbor);
        mScenery.swap(fresh.mScenery);
        mPlayers.swap(fresh.mPlayers);
        mPlayerBindings.swap(fresh.mPlayerBindings);
        mUnloadedPlayerImages.swap(fresh.mUnloadedPlayerImages);
    }

    void InteriorActorScene::enableMovementEffects()
    {
        if (!mImpl || mImpl->mNavigator) throw std::logic_error("Actor movement binding must precede navigation");
        mImpl->mWaterNavigation = true;
        mImpl->mFingerprint += "npc-jump-fall-1\n";
        if (mNeighbor) mNeighbor->enableMovementEffects();
    }
    bool InteriorActorScene::isSwimming(uint64_t actor) const
    {
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            if (scene->mImpl && scene->mImpl->mActorId == actor)
            {
                const auto& frame = *scene->mImpl->mActor;
                const float scale = scene->mImpl->mStore.get<ESM::GameSetting>().find("fSwimHeightScale")->mValue.getFloat();
                return frame.mPosition.z() + 2.f * frame.mHalfExtentsZ * scale < scene->mImpl->waterAt(frame.mPosition);
            }
        throw std::invalid_argument("Swimming actor outside bound scene");
    }
    bool InteriorActorScene::isSwimming(const std::array<float, 3>& position, ESM::RefId race, float scale, float water) const
    {
        if (!mImpl || !std::isfinite(scale) || scale <= 0 || scale > 100
            || std::ranges::any_of(position, [](float v) { return !std::isfinite(v) || std::abs(v) > 1e7f; })
            || !std::isfinite(water)) throw std::invalid_argument("Swimming player context invalid");
        const auto* record = mImpl->mStore.get<ESM::Race>().find(race);
        auto model = VFS::Path::Normalized(MWClass::npcModel(*record, mImpl->mBaseAnimation, mImpl->mBeastAnimation));
        model = Misc::ResourceHelpers::correctActorModelPath(model, &mImpl->mVfs);
        const auto shape = mImpl->mShapes->getInstance(model);
        const float halfHeight = shape->mCollisionBox.mExtents.z() * scale;
        if (!std::isfinite(halfHeight) || halfHeight <= 0 || halfHeight > 10000)
            throw std::invalid_argument("Swimming player hull invalid");
        const float swimScale = mImpl->mStore.get<ESM::GameSetting>().find("fSwimHeightScale")->mValue.getFloat();
        return position[2] + 2.f * halfHeight * swimScale < water;
    }
    bool InteriorActorScene::waterWalkingCastable(uint64_t actor) const
    {
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
        {
            if (!scene->mImpl || scene->mImpl->mActorId != actor) continue;
            const auto& frame = *scene->mImpl->mActor;
            const float water = scene->mImpl->waterAt(frame.mPosition);
            if (water <= -1e30f) return true;
            const float swimScale = scene->mImpl->mStore.get<ESM::GameSetting>()
                .find("fSwimHeightScale")->mValue.getFloat();
            const float height = 2.f * frame.mHalfExtentsZ;
            if (frame.mPosition.z() + (swimScale + 1.f) * height < water) return false;
            if (frame.mPosition.z() >= water) return true;
            const osg::Vec3f start = frame.mPosition + osg::Vec3f(0, 0, frame.mHalfExtentsZ);
            const osg::Vec3f end(frame.mPosition.x(), frame.mPosition.y(), water + frame.mHalfExtentsZ);
            MWPhysics::ActorTracer tracer;
            tracer.doTrace(frame.mCollisionObject, start, end, &scene->mImpl->mWorld);
            return tracer.mFraction >= 1.f;
        }
        throw std::invalid_argument("WaterWalking actor is outside the bound scene");
    }

    bool InteriorActorScene::waterWalkingCastable(const std::array<float, 3>& position,
        ESM::RefId race, float scale) const
    {
        if (!mImpl || !std::isfinite(scale) || scale <= 0.f || scale > 100.f
            || std::ranges::any_of(position, [](float value) {
                return !std::isfinite(value) || std::abs(value) > 1e7f;
            }) || !contains(position))
            throw std::invalid_argument("Player WaterWalking position outside bound scene");
        const auto* record = mImpl->mStore.get<ESM::Race>().search(race);
        if (!record) throw std::invalid_argument("Player WaterWalking race absent from content");
        auto model = VFS::Path::Normalized(MWClass::npcModel(*record,
            mImpl->mBaseAnimation, mImpl->mBeastAnimation));
        model = Misc::ResourceHelpers::correctActorModelPath(model, &mImpl->mVfs);
        if (!mImpl->mVfs.exists(model))
            throw std::invalid_argument("Player WaterWalking collision model missing");
        const auto shape = mImpl->mShapes->getInstance(model);
        const auto extents = shape->mCollisionBox.mExtents;
        if (!(extents.x() > 0 && extents.y() > 0 && extents.z() > 0)
            || !std::isfinite(extents.length2()) || extents.length2() > 1e8f)
            throw std::invalid_argument("Player WaterWalking collision hull invalid");
        const osg::Vec3f point(position[0], position[1], position[2]);
        const float water = mImpl->waterAt(point);
        if (water <= -1e30f || point.z() >= water) return true;
        const float height = 2.f * extents.z() * scale;
        const float swimScale = mImpl->mStore.get<ESM::GameSetting>()
            .find("fSwimHeightScale")->mValue.getFloat();
        if (point.z() + (swimScale + 1.f) * height < water) return false;

        auto hull = MWPhysics::makeActorShape(extents, shape->mCollisionBox.mCenter,
            DetourNavigator::CollisionShapeType::Cylinder);
        hull.mShape->setLocalScaling(btVector3(scale, scale, scale));
        auto body = BulletHelpers::makeCollisionObject(hull.mShape.get(),
            Misc::Convert::toBullet(point + shape->mCollisionBox.mCenter * scale), btQuaternion::getIdentity());
        auto& world = mImpl->mWorld;
        world.addCollisionObject(body.get(), MWPhysics::CollisionType_Actor,
            MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Actor);
        struct RemoveBody
        {
            btCollisionWorld& world;
            btCollisionObject& body;
            ~RemoveBody() { world.removeCollisionObject(&body); }
        } remove{world, *body};
        MWPhysics::ActorTracer tracer;
        tracer.doTrace(body.get(), point + osg::Vec3f(0, 0, height / 2.f),
            osg::Vec3f(point.x(), point.y(), water + height / 2.f), &world);
        return tracer.mFraction >= 1.f;
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
        mImpl->mNormaliseRaceSpeed = Settings::Manager::getBool("normalise race speed", "Game");
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
        if (mNeighbor)
        {
            mNeighbor->enableNavigation(settingsFile);
            mImpl->mFingerprint += "neighbor-scene-1:" + mNeighbor->fingerprint();
        }
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
        mImpl->syncNavigationPosition(mImpl->mActor->mPosition);
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

    std::vector<std::array<float, 3>> InteriorActorScene::fleePathgridDestinations() const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        const auto& position = mImpl->mActor->mPosition;
        ESM::RefId cellId = mImpl->mCells.front();
        if (mImpl->mCells.size() > 1)
        {
            const int x = int(std::floor(position.x() / 8192.f));
            const int y = int(std::floor(position.y() / 8192.f));
            const auto found = std::ranges::find_if(mImpl->mCells, [&](ESM::RefId id) {
                const auto* exterior = id.getIf<ESM::ESM3ExteriorCellRefId>();
                return exterior && exterior->getX() == x && exterior->getY() == y;
            });
            if (found == mImpl->mCells.end()) return {};
            cellId = *found;
        }
        const auto* cell = mImpl->mReferences.getCell(cellId).getCell();
        const auto* pathgrid = mImpl->mStore.get<ESM::Pathgrid>().search(*cell);
        if (!pathgrid || pathgrid->mPoints.empty()) return {};
        if (pathgrid->mPoints.size() > 2048)
            throw std::length_error("Flee pathgrid exceeds bound");
        MWMechanics::PathgridGraph graph(*pathgrid);
        const auto converter = Misc::makeCoordinateConverter(*cell);
        osg::Vec3f local = position;
        converter.toLocal(local);
        const size_t closest = Misc::getClosestPoint(*pathgrid, local);
        std::vector<std::array<float, 3>> result;
        result.reserve(pathgrid->mPoints.size() - 1);
        for (size_t i = 0; i < pathgrid->mPoints.size(); ++i)
        {
            if (i == closest || !graph.isPointConnected(closest, i)) continue;
            auto point = pathgrid->mPoints[i];
            converter.toWorld(point);
            const std::array<float, 3> candidate{float(point.mX), float(point.mY), float(point.mZ)};
            if (candidate != std::array<float, 3>{} && contains(candidate)) result.push_back(candidate);
        }
        return result;
    }

    std::vector<std::array<float, 3>> InteriorActorScene::neighborFleePathgridDestinations(size_t index) const
    {
        const auto* neighbor = mNeighbor.get();
        while (neighbor && index--) neighbor = neighbor->mNeighbor.get();
        if (!neighbor) throw std::out_of_range("Neighbor flee pathgrid index outside bound actors");
        return neighbor->fleePathgridDestinations();
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
        if (!mImpl->mCells.front().is<ESM::ESM3ExteriorCellRefId>()) return true;
        const auto x = std::floor(double(position[0]) / ESM::Land::REAL_SIZE);
        const auto y = std::floor(double(position[1]) / ESM::Land::REAL_SIZE);
        return std::ranges::any_of(mImpl->mCells, [&](const auto& cell) {
            const auto* exterior = cell.template getIf<ESM::ESM3ExteriorCellRefId>();
            return x == exterior->getX() && y == exterior->getY()
                && (mImpl->mWaterNavigation || position[2] >= mImpl->mCellWater.at(cell));
        });
    }
    bool InteriorActorScene::pathUnavailable() const
    { return mImpl && !mImpl->mPath.isPathConstructed() && !mImpl->mPath.checkPathCompleted(); }

    void InteriorActorScene::bindDoors(std::span<const uint64_t> doors, bool avoidance)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        if (mImpl->mNavigator || !mImpl->mDoors.empty() || (doors.empty() && !avoidance)
            || doors.size() > (mImpl->mMultipleDomains ? 4096 : 128))
            throw std::invalid_argument("Actor door binding outside startup bounds");
        std::vector<ActorSceneDoor> bound;
        auto fingerprint = mImpl->mFingerprint + (avoidance ? "npc-door-avoidance-1\n" : "npc-door-contact-1\n");
        for (auto id : doors)
        {
            const auto found = mImpl->mOrdinaryDoors.find(id);
            if (mImpl->mMultipleDomains && found == mImpl->mOrdinaryDoors.end()) continue;
            if (found == mImpl->mOrdinaryDoors.end() || (!bound.empty() && bound.back().mId >= id))
                throw std::invalid_argument("Actor door collision identity absent or unordered");
            bound.push_back({id, mImpl->mTransforms[found->second].mPosition.rot[2]});
            if (bound.size() > 128) throw std::invalid_argument("Local actor door capacity exceeded");
            fingerprint += std::to_string(id) + '\n';
        }
        mImpl->mDoors.swap(bound);
        mImpl->mFingerprint.swap(fingerprint);
        mImpl->mAvoidanceEnabled = avoidance;
        if (mNeighbor) mNeighbor->bindDoors(doors, avoidance);
        for (const auto& scene : mScenery) scene->bindDoors(doors, avoidance);
    }

    ActorDoorContact InteriorActorScene::doorContact(uint64_t door, float proposedAngle, float delta) const
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        const auto bound = std::ranges::find(mImpl->mDoors, door, &ActorSceneDoor::mId);
        if (mImpl->mMultipleDomains && bound == mImpl->mDoors.end())
            for (const auto& scene : mScenery)
                if (scene->mImpl->mOrdinaryDoors.contains(door)) return scene->doorContact(door, proposedAngle, delta);
        if (mImpl->mMultipleDomains && bound == mImpl->mDoors.end() && mNeighbor)
            return mNeighbor->doorContact(door, proposedAngle, delta);
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
            result.mSelectedActor |= contact.mBlocked && mImpl->mActor
                && body->mObject.get() == mImpl->mActor->mCollisionObject;
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
        std::vector<std::pair<std::shared_ptr<const char>, std::vector<ActorSceneDoor>>> sceneryDoors;
    };
    InteriorActorScene::Prepared::Prepared(std::unique_ptr<State> state) : mState(std::move(state)) {}
    InteriorActorScene::Prepared::~Prepared() = default;
    void InteriorActorScene::withPlayerHulls(
        std::span<const std::pair<uint64_t, const Prepared*>> frames, const std::function<void()>& operation)
    {
        struct Restore
        {
            struct Body { btCollisionWorld* world; btCollisionObject* body; btTransform transform; };
            std::vector<Body> bodies;
            ~Restore() { for (auto& value : bodies) {
                value.body->setWorldTransform(value.transform); value.world->updateSingleAabb(value.body);
            } }
        } restore;
        restore.bodies.reserve(frames.size());
        for (const auto& [id, frame] : frames)
        {
            auto& scene = *forPlayer(id).mImpl;
            if (!frame || frame->mState->lifetime != scene.mLifetime)
                throw std::invalid_argument("Player hull candidate differs from scene");
            auto* body = scene.mActor->mCollisionObject;
            restore.bodies.push_back({&scene.mWorld, body, body->getWorldTransform()});
            body->setWorldTransform(scene.actorTransform(*frame->mState->frame));
            scene.mWorld.updateSingleAabb(body);
        }
        operation();
    }
    bool InteriorActorScene::actorPoseClear(uint64_t actor, const std::array<float, 3>& position, float yaw)
    {
        struct Contact final : btCollisionWorld::ContactResultCallback
        {
            bool clear = true;
            btScalar addSingleResult(btManifoldPoint& point, const btCollisionObjectWrapper*, int, int,
                const btCollisionObjectWrapper*, int, int) override
            { if (point.getDistance() < -.1f) clear = false; return 0; }
        } contact;
        contact.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
        contact.m_collisionFilterMask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
            | MWPhysics::CollisionType_Actor;
        withActorPose(actor, position, yaw, [&] {
            auto& scene = *forActor(actor).mImpl;
            scene.mWorld.contactTest(scene.mActor->mCollisionObject, contact);
        });
        return contact.clear;
    }

    void InteriorActorScene::withActorPose(uint64_t actor, const std::array<float, 3>& position, float yaw,
        const std::function<void()>& operation)
    {
        auto& scene = *forActor(actor).mImpl;
        if (!std::ranges::all_of(position, [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; })
            || !std::isfinite(yaw) || std::abs(yaw) > 6.283186f || !contains(position))
            throw std::invalid_argument("Scoped actor pose outside bounds");
        auto* body = scene.mActor->mCollisionObject;
        struct Restore
        {
            btCollisionWorld& world; btCollisionObject* body; btTransform transform;
            ~Restore() { body->setWorldTransform(transform); world.updateSingleAabb(body); }
        } restore{scene.mWorld, body, body->getWorldTransform()};
        auto frame = *scene.mActor;
        frame.mPosition = {position[0], position[1], position[2]}; frame.mRotation.y() = yaw;
        body->setWorldTransform(scene.actorTransform(frame)); scene.mWorld.updateSingleAabb(body);
        operation();
    }

    void InteriorActorScene::withHiddenActors(std::span<const uint64_t> actors, const std::function<void()>& operation)
    {
        struct Restore
        {
            struct Body { btBroadphaseProxy* proxy; int group, mask; };
            std::vector<Body> bodies;
            ~Restore() { for (auto it = bodies.rbegin(); it != bodies.rend(); ++it) {
                it->proxy->m_collisionFilterGroup = it->group;
                it->proxy->m_collisionFilterMask = it->mask;
            } }
        } restore;
        restore.bodies.reserve(actors.size());
        for (auto id : actors)
        {
            auto& scene = hasPlayer(id) ? forPlayer(id) : forActor(id);
            if (!scene.mImpl || !scene.mImpl->mActor) continue;
            auto* body = scene.mImpl->mActor->mCollisionObject;
            auto* proxy = body->getBroadphaseHandle();
            if (!proxy) throw std::invalid_argument("Scoped hull has no collision witness");
            restore.bodies.push_back({proxy, proxy->m_collisionFilterGroup, proxy->m_collisionFilterMask});
            proxy->m_collisionFilterGroup = 0;
            proxy->m_collisionFilterMask = 0;
        }
        operation();
    }
    void InteriorActorScene::prepareSceneryDoors(Prepared& prepared, std::span<const ActorSceneDoor> doors) const
    {
        auto& frames = prepared.mState->sceneryDoors;
        frames.clear();
        for (const auto& scene : mScenery)
        {
            auto local = scene->mImpl->localDoors(doors);
            scene->mImpl->validateDoors(local);
            frames.emplace_back(scene->mImpl->mLifetime, std::move(local));
        }
    }
    ActorSceneSnapshot InteriorActorScene::Prepared::snapshot() const
    {
        const auto& frame = *mState->frame;
        return {mState->actor, {frame.mPosition.x(), frame.mPosition.y(), frame.mPosition.z()},
            frame.mIsOnGround, mState->contacts, frame.mRotation.y(), mState->travel.drowning,
            mState->travel.jumpFlags, mState->travel.landingFall,
            {frame.mInertia.x(), frame.mInertia.y(), frame.mInertia.z()},
            {frame.mLastStuckPosition.x(), frame.mLastStuckPosition.y(), frame.mLastStuckPosition.z()},
            mState->travel.fallHeight, mState->travel.breath, frame.mStuckFrames, frame.mIsOnSlope, frame.mWalkingOnWater};
    }
    std::optional<ActorSceneSnapshot> InteriorActorScene::Prepared::neighborSnapshot() const
    { return mNeighbor ? std::optional(mNeighbor->snapshot()) : std::nullopt; }
    std::vector<ActorSceneSnapshot> InteriorActorScene::Prepared::neighborSnapshots() const
    {
        std::vector<ActorSceneSnapshot> result;
        for (auto* adjacent = mNeighbor.get(); adjacent; adjacent = adjacent->mNeighbor.get())
            result.push_back(adjacent->snapshot());
        return result;
    }
    std::span<const char> InteriorActorScene::Prepared::selectedImage() const
    {
        return mNeighbor ? splitActorImages(mState->bytes).first : std::span<const char>(mState->bytes);
    }
    std::optional<std::array<float, 3>> InteriorActorScene::Prepared::destination() const
    {
        if (!mState->travel.hasDestination) return {};
        const auto& point = mState->travel.destination;
        return std::array<float, 3>{point.x(), point.y(), point.z()};
    }
    std::span<const char> InteriorActorScene::Prepared::neighborImage(size_t index) const
    {
        auto* next = mNeighbor.get();
        while (next && index--) next = next->mNeighbor.get();
        if (!next) throw std::invalid_argument("Prepared actor index absent");
        return next->selectedImage();
    }

    void InteriorActorScene::setFacing(Prepared& prepared, float yaw) const
    {
        if (!std::isfinite(yaw) || !mImpl || prepared.mState->lifetime != mImpl->mLifetime)
            throw std::invalid_argument("Invalid prepared facing");
        auto& state = *prepared.mState;
        state.frame->mRotation.y() = yaw;
        state.bytes = mImpl->encode(*state.frame, state.path, state.contacts, state.travel);
        if (prepared.mNeighbor) state.bytes = joinActorImages(state.bytes, prepared.mNeighbor->image());
    }
    std::span<const char> InteriorActorScene::Prepared::image() const { return mState->bytes; }
    bool InteriorActorScene::Prepared::pathUnavailable() const
    { return !mState->path.isPathConstructed() && !mState->path.checkPathCompleted(); }
    bool InteriorActorScene::Prepared::pathCompleted() const
    { return mState->path.checkPathCompleted(); }

    std::vector<char> InteriorActorScene::selectedImage() const
    {
        return mImpl ? mImpl->encode(*mImpl->mActor, mImpl->mPath, mImpl->mContacts, mImpl->mTravel)
            : mDormant->image;
    }
    std::vector<char> InteriorActorScene::neighborImage() const
    { return mNeighbor ? mNeighbor->selectedImage() : std::vector<char>{}; }
    std::vector<char> InteriorActorScene::neighborImage(size_t index) const
    {
        auto* adjacent = mNeighbor.get();
        while (index-- && adjacent) adjacent = adjacent->mNeighbor.get();
        if (!adjacent) throw std::invalid_argument("Neighbor image index outside bound scene");
        return adjacent->selectedImage();
    }
    std::vector<char> InteriorActorScene::image() const
    {
        auto primary = selectedImage();
        return mNeighbor ? joinActorImages(primary, mNeighbor->image()) : primary;
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareSelectedRestore(
        std::span<const char> bytes, std::span<const ActorSceneDoor> doors, std::span<const uint64_t> removed)
    {
        if (!mNeighbor) return prepareRestore(bytes, doors, removed);
        const auto combined = joinActorImages(bytes, mNeighbor->image());
        return prepareRestore(combined, doors, removed);
    }
    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNeighborRestore(
        std::span<const char> bytes, std::span<const ActorSceneDoor> doors)
    {
        if (!mNeighbor) throw std::invalid_argument("Neighbor restore has no bound actor");
        return prepareRestore(joinActorImages(selectedImage(), bytes), doors);
    }
    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNeighborRestore(
        size_t index, std::span<const char> bytes, std::span<const ActorSceneDoor> doors, std::span<const uint64_t> removed)
    {
        const auto count = neighborSnapshots().size();
        if (index >= count) throw std::invalid_argument("Neighbor restore index outside bound scene");
        std::vector<std::vector<char>> images;
        for (size_t i = 0; i < count; ++i) images.push_back(neighborImage(i));
        images[index].assign(bytes.begin(), bytes.end());
        auto combined = std::move(images.back());
        for (size_t i = count - 1; i-- > 0; ) combined = joinActorImages(images[i], combined);
        return prepareRestore(joinActorImages(selectedImage(), combined), doors, removed);
    }

    std::vector<char> InteriorActorScene::Impl::encode(const MWPhysics::ActorFrameData& frame,
        const MWMechanics::PathFinder& path, const std::vector<uint64_t>& contacts, const Travel& travel) const
    {
        std::vector<char> bytes;
        const auto word = [&](uint64_t value) { putAreaWord(bytes, value); };
        const auto real = [&](float value) { word(std::bit_cast<uint32_t>(value)); };
        const auto vector = [&](const osg::Vec3f& value) { for (int i=0; i<3; ++i) real(value[i]); };
        word(frame.mIsPlayer ? 4 : mWaterNavigation ? 3 : mAvoidanceEnabled ? 2 : 1); word(mActorId);
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
        if (mWaterNavigation)
        {
            real(travel.breath); word(travel.jumpFlags); real(travel.fallHeight);
        }
        if (frame.mIsPlayer) word(frame.mWalkingOnWater);
        return bytes;
    }

    void InteriorActorScene::restore(std::span<const char> bytes)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        std::vector<ActorSceneDoor> doors;
        for (auto* scene = this; scene; scene = scene->mNeighbor.get())
            doors.insert(doors.end(), scene->mImpl->mDoors.begin(), scene->mImpl->mDoors.end());
        for (const auto& scene : mScenery)
            doors.insert(doors.end(), scene->mImpl->mDoors.begin(), scene->mImpl->mDoors.end());
        std::ranges::sort(doors, {}, &ActorSceneDoor::mId);
        doors.erase(std::unique(doors.begin(), doors.end(), [](const auto& a, const auto& b) {
            return a.mId == b.mId; }), doors.end());
        auto prepared = prepareRestore(bytes, doors);
        install(*prepared);
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareRestore(
        std::span<const char> bytes, std::span<const ActorSceneDoor> doors, std::span<const uint64_t> removed)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        const auto allDoors = std::vector<ActorSceneDoor>(doors.begin(), doors.end());
        std::unique_ptr<Prepared> neighbor;
        if (mNeighbor)
        {
            const auto [primary, adjacent] = splitActorImages(bytes);
            neighbor = mNeighbor->prepareRestore(adjacent, doors, removed);
            bytes = primary;
        }
        const auto localDoors = mImpl->localDoors(doors);
        doors = localDoors;
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
        const auto version = word(), actor = word();
        if (version != (mImpl->mActor->mIsPlayer ? 4 : mImpl->mWaterNavigation ? 3 : mImpl->mAvoidanceEnabled ? 2 : 1) || actor != mImpl->mActorId)
            throw std::invalid_argument("Actor image identity mismatch: version=" + std::to_string(version)
                + " actor=" + std::to_string(actor) + " expected=" + std::to_string(mImpl->mActorId));
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(*mImpl->mActor);
        frame->mPosition = vector(); frame->mInertia = vector(); frame->mLastStuckPosition = vector();
        if (!contains({frame->mPosition.x(), frame->mPosition.y(), frame->mPosition.z()}))
            throw std::invalid_argument("Recovered traveler outside processing neighborhood");
        frame->mRotation.x() = real(); frame->mRotation.y() = real(); frame->mOldHeight = real();
        const auto stuck = word();
        if (stuck > UINT32_MAX) throw std::invalid_argument("Actor stuck counter outside bounds");
        frame->mStuckFrames = unsigned(stuck); frame->mIsOnGround = boolean(); frame->mIsOnSlope = boolean();
        const auto standing = word(); frame->mStandingOn = nullptr;
        if (standing && std::ranges::find(removed, standing) != removed.end()) frame->mIsOnGround = false;
        else if (standing)
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
            if (std::ranges::find(removed, id) != removed.end()) continue;
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
            if (!mImpl->mDynamicDestination && travel.hasDestination != mImpl->mTravel.hasDestination)
                throw std::invalid_argument("Actor destination presence outside binding");
            if (travel.destination != mImpl->mTravel.destination
                    && (!mImpl->mDynamicDestination && !mImpl->mWaterNavigation
                        || (travel.hasDestination
                            && !contains({travel.destination.x(), travel.destination.y(), travel.destination.z()}))))
                throw std::invalid_argument("Actor destination outside binding: actor=" + std::to_string(actor)
                    + " has_destination=" + std::to_string(travel.hasDestination));
            if (travel.door && std::ranges::find(doors, travel.door, &ActorSceneDoor::mId) == doors.end())
                throw std::invalid_argument("Actor avoidance door outside binding");
            if (travel.avoidance.mDuration < 0 || travel.avoidance.mDuration > 1 || direction > 3
                || random < Misc::Rng::Generator::min() || random > Misc::Rng::Generator::max())
                throw std::invalid_argument("Actor avoidance image outside bounds");
            travel.avoidance.mDirection = int(direction);
            Misc::Rng::deserialize(std::to_string(random), travel.random);
        }
        if (mImpl->mWaterNavigation)
        {
            travel.breath = real();
            const float hold = mImpl->mStore.get<ESM::GameSetting>().find("fHoldBreathTime")->mValue.getFloat();
            if (!(hold > 0.f) || travel.breath < -1.f || travel.breath > hold)
                throw std::invalid_argument("Actor breath timer outside stock bounds");
            travel.drowning = false;
            const auto flags = word();
            if (flags > 3) throw std::invalid_argument("Actor jump flags outside bounds");
            travel.jumpFlags = uint8_t(flags);
            travel.fallHeight = real();
            if (travel.fallHeight < 0.f) throw std::invalid_argument("Actor fall height outside bounds");
        }
        if (frame->mIsPlayer) frame->mWalkingOnWater = boolean();
        if (offset != bytes.size()) throw std::invalid_argument("Trailing actor image data");
        auto retained = removed.empty() ? std::vector<char>(bytes.begin(), bytes.end())
            : mImpl->encode(*frame, path, ids, travel);
        auto prepared = std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::State>(Prepared::State{
            mImpl->mLifetime, mImpl->mActorId, std::move(frame), std::move(path), std::move(ids), std::move(retained),
            {doors.begin(), doors.end()}, std::move(travel)})));
        if (neighbor)
        {
            prepared->mState->bytes = joinActorImages(prepared->mState->bytes, neighbor->image());
            prepared->mNeighbor = std::move(neighbor);
        }
        prepareSceneryDoors(*prepared, allDoors);
        return prepared;
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNavigation(
        float speed, std::span<const ActorSceneDoor> doors)
    { return prepareNavigation(ActorMovement{.walkSpeed = speed}, doors); }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNavigation(
        const ActorMovement& movement, std::span<const ActorSceneDoor> doors,
        std::optional<std::array<float, 3>> destination)
    { return prepareNavigationFrom(nullptr, movement, doors, destination); }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareScopedNavigation(
        std::span<const char> image, const ActorMovement& movement, const std::array<float, 3>& destination)
    {
        if (mNeighbor || !mScenery.empty()) throw std::invalid_argument("Activity exceeds one actor/interior");
        auto before = prepareRestore(image);
        return prepareNavigationFrom(before.get(), movement, {}, destination);
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareNavigationFrom(
        const Prepared* before, const ActorMovement& movement, std::span<const ActorSceneDoor> doors,
        std::optional<std::array<float, 3>> destination)
    {
        if (!mImpl) throw std::logic_error("Actor scene is unloaded");
        const auto localDoors = mImpl->localDoors(doors);
        doors = localDoors;
        mImpl->validateDoors(doors);
        if (!mImpl->mNavigator) throw std::logic_error("Actor navigation unavailable");
        mImpl->validateMovement(movement);
        const auto& initial = before ? *before->mState->frame : *mImpl->mActor;
        mImpl->syncNavigationPosition(initial.mPosition);
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(movement.enabled
            ? mImpl->movementFrame(initial, movement) : initial);
        auto path = before ? before->mState->path : mImpl->mPath;
        auto travel = before ? before->mState->travel : mImpl->mTravel;
        travel.landingFall = 0.f;
        if (movement.jumpFlags)
        {
            if (!mImpl->mWaterNavigation || *movement.jumpFlags > 3)
                throw std::invalid_argument("Actor jump control outside movement domain");
            travel.jumpFlags = *movement.jumpFlags;
        }
        if (destination)
        {
            for (float value : *destination)
                if (!std::isfinite(value) || std::abs(value) > 1e7f)
                    throw std::invalid_argument("Interior follow destination outside bounds");
            if (!contains(*destination))
                throw std::invalid_argument("Interior follow destination outside processing neighborhood");
            const osg::Vec3f selected((*destination)[0], (*destination)[1], (*destination)[2]);
            if (!travel.hasDestination || (before ? travel.destination != selected
                    : (travel.destination - selected).length2() > 32.f * 32.f))
            {
                travel.hasDestination = true;
                travel.destination = selected;
                mImpl->syncNavigation(doors);
                mImpl->rebuildPath(path, frame->mPosition, travel);
            }
        }
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

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareControlled(
        const ActorMovement& movement, const std::array<float, 3>& axes, float pitch, float yaw,
        std::span<const ActorSceneDoor> doors)
    {
        if (!mImpl || !movement.enabled) throw std::invalid_argument("Controlled movement unavailable");
        mImpl->validateMovement(movement);
        for (float axis : axes)
            if (!std::isfinite(axis) || std::abs(axis) > 1.f)
                throw std::invalid_argument("Movement controls outside bounds");
        if (!std::isfinite(pitch) || !std::isfinite(yaw) || std::abs(pitch) > 6.283186f
            || std::abs(yaw) > 6.283186f) throw std::invalid_argument("Movement facing outside bounds");
        const auto localDoors = mImpl->localDoors(doors);
        mImpl->validateDoors(localDoors);
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(mImpl->movementFrame(*mImpl->mActor, movement));
        frame->mRotation = {pitch, yaw};
        auto travel = mImpl->mTravel;
        travel.landingFall = 0.f;
        auto path = mImpl->mPath;
        std::vector<uint64_t> contacts;
        struct RestoreDoors { Impl& scene; ~RestoreDoors() { scene.applyDoors(scene.mDoors); } } restore{*mImpl};
        mImpl->applyDoors(localDoors);
        osg::Vec3f direction(axes[0], axes[1], axes[2]);
        if (direction.length2() > 1.f) direction.normalize();
        for (int substep = 0; substep < 2; ++substep)
        {
            auto controls = movement;
            // A release edge launches at most once in this durable tick.
            if (substep) controls.jumpRequested = false;
            mImpl->move(*frame, contacts, direction * mImpl->movementSpeed(*frame, movement), controls, travel);
            mImpl->updateBreath(*frame, travel, movement);
        }
        auto bytes = mImpl->encode(*frame, path, contacts, travel);
        return std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::State>(Prepared::State{
            mImpl->mLifetime, mImpl->mActorId, std::move(frame), std::move(path), std::move(contacts),
            std::move(bytes), {localDoors.begin(), localDoors.end()}, std::move(travel)})));
    }

    void InteriorActorScene::prepareNeighborNavigation(Prepared& prepared, float speed,
        std::span<const ActorSceneDoor> doors, std::optional<std::array<float, 3>> destination)
    {
        if (!mNeighbor || !mImpl || prepared.mState->lifetime != mImpl->mLifetime)
            throw std::invalid_argument("Neighbor navigation binding invalid");
        // Both sweeps use the same collision world. The neighbor must see the
        // selected actor's candidate position, while a rejected write must
        // leave the committed broadphase untouched.
        auto* body = mImpl->mActor->mCollisionObject;
        const auto committed = body->getWorldTransform();
        struct RestoreActor
        {
            btCollisionWorld& world;
            btCollisionObject& body;
            btTransform transform;
            ~RestoreActor() { body.setWorldTransform(transform); world.updateSingleAabb(&body); }
        } restore{mImpl->mWorld, *body, committed};
        auto candidate = committed;
        candidate.setOrigin(Misc::Convert::toBullet(prepared.mState->frame->mPosition + mImpl->mActorOffset));
        body->setWorldTransform(candidate);
        mImpl->mWorld.updateSingleAabb(body);
        auto neighbor = mNeighbor->prepareNavigation(ActorMovement{.walkSpeed = speed}, doors, destination);
        prepared.mState->bytes = joinActorImages(
            mImpl->encode(*prepared.mState->frame, prepared.mState->path,
                prepared.mState->contacts, prepared.mState->travel), neighbor->image());
        prepared.mNeighbor = std::move(neighbor);
    }

    void InteriorActorScene::prepareNeighborNavigation(Prepared& prepared, std::span<const ActorMovement> movements,
        std::span<const ActorSceneDoor> doors,
        std::span<const std::optional<std::array<float, 3>>> destinations,
        std::span<const std::optional<std::array<float, 3>>> fleeEnemies, std::span<const uint8_t> active)
    {
        if (!mImpl || prepared.mState->lifetime != mImpl->mLifetime
            || movements.size() != destinations.size() || movements.size() != neighborSnapshots().size()
            || (!fleeEnemies.empty() && fleeEnemies.size() != movements.size())
            || (!active.empty() && active.size() != movements.size()))
            throw std::invalid_argument("Neighbor set navigation binding invalid");
        std::vector<InteriorActorScene*> scenes{this};
        for (auto* next = mNeighbor.get(); next; next = next->mNeighbor.get()) scenes.push_back(next);
        struct RestoreBodies
        {
            struct Body { btCollisionWorld* world; btCollisionObject* body; btTransform transform; };
            std::vector<Body> bodies;
            ~RestoreBodies()
            {
                for (auto& [world, body, transform] : bodies)
                { body->setWorldTransform(transform); world->updateSingleAabb(body); }
            }
        } restore;
        Prepared* parent = &prepared;
        for (size_t i = 0; i < movements.size(); ++i)
        {
            auto* previous = scenes[i]->mImpl->mActor->mCollisionObject;
            restore.bodies.push_back({&scenes[i]->mImpl->mWorld, previous, previous->getWorldTransform()});
            auto candidate = scenes[i]->mImpl->actorTransform(*parent->mState->frame);
            previous->setWorldTransform(candidate);
            scenes[i]->mImpl->mWorld.updateSingleAabb(previous);
            parent->mNeighbor = !active.empty() && !active[i]
                ? scenes[i + 1]->prepareRestore(scenes[i + 1]->image(), doors)
                : scenes[i + 1]->prepareNavigation(movements[i], doors, destinations[i]);
            if ((active.empty() || active[i]) && !fleeEnemies.empty() && fleeEnemies[i]
                && (!destinations[i] || parent->mNeighbor->pathUnavailable()))
                parent->mNeighbor = scenes[i + 1]->prepareBlindRun(movements[i], doors, *fleeEnemies[i]);
            parent = parent->mNeighbor.get();
        }
        std::vector<Prepared*> frames{&prepared};
        for (auto* next = prepared.mNeighbor.get(); next; next = next->mNeighbor.get())
            frames.push_back(next);
        for (size_t i = frames.size(); i-- > 1; )
        {
            auto* current = frames[i - 1];
            // A restored frame already contains its neighbor image. Rebuild
            // the actor's own bytes before composing the candidate chain.
            const auto& state = *current->mState;
            current->mState->bytes = joinActorImages(
                scenes[i - 1]->mImpl->encode(*state.frame, state.path, state.contacts, state.travel),
                frames[i]->image());
        }
    }

    std::unique_ptr<InteriorActorScene::Prepared> InteriorActorScene::prepareBlindRun(
        const ActorMovement& movement, std::span<const ActorSceneDoor> doors,
        const std::array<float, 3>& enemy)
    {
        if (!mImpl || !mImpl->mNavigator) throw std::logic_error("Flee movement unavailable");
        const auto localDoors = mImpl->localDoors(doors);
        doors = localDoors;
        mImpl->validateDoors(doors);
        for (float value : enemy)
            if (!std::isfinite(value) || std::abs(value) > 1e7f)
                throw std::invalid_argument("Flee enemy position outside bounds");
        if (!movement.enabled) throw std::invalid_argument("Flee movement disabled");
        mImpl->validateMovement(movement);
        auto frame = std::make_unique<MWPhysics::ActorFrameData>(mImpl->movementFrame(*mImpl->mActor, movement));
        auto path = mImpl->mPath;
        auto travel = mImpl->mTravel;
        travel.landingFall = 0.f;
        if (movement.jumpFlags)
        {
            if (!mImpl->mWaterNavigation || *movement.jumpFlags > 3)
                throw std::invalid_argument("Actor jump control outside movement domain");
            travel.jumpFlags = *movement.jumpFlags;
        }
        std::vector<uint64_t> contacts;
        struct RestoreDoors
        {
            Impl& scene;
            ~RestoreDoors() { scene.applyDoors(scene.mDoors); }
        } restore{*mImpl};
        mImpl->applyDoors(doors);
        for (int step = 0; step < 2; ++step)
        {
            frame->mRotation.y() = std::atan2(frame->mPosition.x() - enemy[0],
                frame->mPosition.y() - enemy[1]);
            mImpl->move(*frame, contacts, {0, mImpl->movementSpeed(*frame, movement), 0}, movement, travel);
            mImpl->updateBreath(*frame, travel, movement);
        }
        auto bytes = mImpl->encode(*frame, path, contacts, travel);
        return std::unique_ptr<Prepared>(new Prepared(std::make_unique<Prepared::State>(Prepared::State{
            mImpl->mLifetime, mImpl->mActorId, std::move(frame), std::move(path), std::move(contacts),
            std::move(bytes), {doors.begin(), doors.end()}, std::move(travel)})));
    }
    bool InteriorActorScene::canInstall(const Prepared& prepared) const noexcept
    {
        if (!mImpl || prepared.mState->lifetime != mImpl->mLifetime
            || prepared.mState->sceneryDoors.size() != mScenery.size()
            || (mNeighbor && (!prepared.mNeighbor || !mNeighbor->canInstall(*prepared.mNeighbor)))) return false;
        for (size_t i = 0; i < mScenery.size(); ++i)
            if (prepared.mState->sceneryDoors[i].first != mScenery[i]->mImpl->mLifetime) return false;
        return true;
    }

    void InteriorActorScene::install(Prepared& prepared) noexcept
    {
        auto& state = *prepared.mState;
        assert(canInstall(prepared));
        mImpl->mActor.swap(state.frame); std::swap(mImpl->mPath, state.path); mImpl->mContacts.swap(state.contacts);
        mImpl->mDoors.swap(state.doors);
        std::swap(mImpl->mTravel, state.travel);
        mImpl->applyDoors(mImpl->mDoors);
        mImpl->updateTransform();
        if (mNeighbor) mNeighbor->install(*prepared.mNeighbor);
        for (size_t i = 0; i < mScenery.size(); ++i)
        {
            auto& scene = *mScenery[i]->mImpl;
            scene.mDoors.swap(state.sceneryDoors[i].second);
            scene.applyDoors(scene.mDoors);
        }
    }

}
