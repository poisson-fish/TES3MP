#include "actor_scene.hpp"
#include "actor_inventory.hpp"
#include "loadout.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwclass/npcmodel.hpp>
#include <apps/openmw/mwphysics/actorshape.hpp>
#include <apps/openmw/mwphysics/collisioneffects.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/movementdata.hpp>
#include <apps/openmw/mwphysics/movementsolver.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/bullethelpers/collisionobject.hpp>
#include <components/files/hash.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

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
        std::vector<std::unique_ptr<Body>> mBodies;
        std::map<const btCollisionObject*, uint64_t> mIdentities;
        std::unique_ptr<MWPhysics::ActorFrameData> mActor;
        osg::Vec3f mActorOffset;
        uint64_t mActorId;
        std::vector<uint64_t> mContacts;
        std::string mFingerprint;

        Impl(Loadout& loadout, const std::string& cellName, uint64_t actor,
            const std::string& baseAnimation, const std::string& beastAnimation)
            : mResources(&mVfs, 0, &loadout.encoder()),
              mShapes(new Resource::BulletShapeManager(&mVfs, mResources.getSceneManager(),
                  mResources.getNifFileManager(), 0)),
              mReferences(loadout.store(), loadout.readers(), 1), mActorId(actor)
        {
            if (cellName.empty() || cellName.size() > 256 || !actor
                || baseAnimation.empty() || baseAnimation.size() > 1024
                || beastAnimation.empty() || beastAnimation.size() > 1024)
                throw std::invalid_argument("Interior NPC binding outside bounds");
            MWClass::registerClasses();
            VFS::registerArchives(&mVfs, Files::Collections(loadout.options().mDataPaths),
                loadout.options().mArchives, true, &loadout.encoder());
            auto& cell = mReferences.getCell(interiorCell(cellName));
            if (cell.isExterior() || cell.getCell()->hasWater())
                throw std::invalid_argument("Interior NPC slice requires a dry interior");
            const VFS::Path::Normalized base(baseAnimation), beast(beastAnimation);
            std::set<std::string> meshes;
            size_t references = 0;
            cell.forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                    || Misc::ResourceHelpers::isHiddenMarker(ptr.getCellRef().getRefId())) return true;
                if (++references > MaxBodies) throw std::length_error("Interior collision reference budget exceeded");
                const auto& cls = ptr.getClass();
                const auto resolvedId = MWWorld::placedRefId(ptr.getCellRef().getRefNum(), loadout.options().mContent);
                if (!resolvedId) throw std::invalid_argument("Collision reference identity invalid");
                const auto id = *resolvedId;
                const bool npc = ptr.getType() == ESM::NPC::sRecordId;
                if (cls.isActor() && !npc)
                    throw std::invalid_argument("Non-NPC actor in interior collision domain");
                if (ptr.getType() == ESM::CreatureLevList::sRecordId)
                    throw std::invalid_argument("Unresolved leveled actor in interior collision domain");
                if (id == actor && (!npc || initialCorpse(ptr) || !cls.getScript(ptr).empty()))
                    throw std::invalid_argument("Selected actor must be a living unscripted native NPC");
                if (npc && initialCorpse(ptr))
                    throw std::invalid_argument("Corpse collision requires gameplay animation");
                const auto& position = ptr.getRefData().getPosition();
                const float scale = ptr.getCellRef().getScale();
                validatePosition(position, scale);
                auto model = npc
                    ? VFS::Path::Normalized(MWClass::npcModel(*loadout.store().get<ESM::Race>().find(
                        ptr.get<ESM::NPC>()->mBase->mRace), base, beast))
                    : cls.getCorrectedModel(ptr);
                if (model.empty()) return true;
                if (npc) model = Misc::ResourceHelpers::correctActorModelPath(model, &mVfs);
                if (!mVfs.exists(model)) throw std::invalid_argument("Missing collision model: " + model.value());
                meshes.emplace(model.value());
                auto shape = mShapes->getInstance(model);
                if (!shape) return true;
                // Stock NPC collision uses the resource's fixed bounding hull,
                // not its animated child shapes. Objects need CPU evaluation.
                if (!npc && shape->isAnimated())
                    throw std::invalid_argument("Animated collision requires CPU evaluation: " + model.value());
                auto body = std::make_unique<Body>(mWorld);
                body->mResource = shape;
                const auto rotation = Misc::Convert::makeOsgQuat(position);
                if (npc)
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
                        mActorOffset = offset;
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
                mBodies.push_back(std::move(body));
                mIdentities.emplace(object, id);
                mWorld.addCollisionObject(object, npc ? MWPhysics::CollisionType_Actor : MWPhysics::CollisionType_World,
                    npc ? (MWPhysics::CollisionType_World | MWPhysics::CollisionType_Actor) : MWPhysics::CollisionType_Actor);
                return true;
            });
            if (!mActor) throw std::invalid_argument("Selected NPC absent from interior collision scene");
            std::ostringstream fingerprint;
            fingerprint << "native-interior-collision-1\n" << loadout.contentFingerprint() << '\n'
                << cellName << '\n' << actor << '\n';
            for (const auto& mesh : meshes)
            {
                auto stream = mVfs.get(VFS::Path::toNormalized(mesh));
                const auto hash = Files::getHash(mesh, *stream);
                fingerprint << mesh.size() << ':' << mesh << ':' << hash[0] << ':' << hash[1] << '\n';
            }
            mFingerprint = fingerprint.str();
        }

        ActorSceneSnapshot snapshot() const
        {
            return {mActorId, {mActor->mPosition.x(), mActor->mPosition.y(), mActor->mPosition.z()},
                mActor->mIsOnGround, mContacts};
        }
    };

    InteriorActorScene::InteriorActorScene(Loadout& loadout, const std::string& cell, uint64_t actor,
        const std::string& baseAnimation, const std::string& beastAnimation)
        : mImpl(std::make_unique<Impl>(loadout, cell, actor, baseAnimation, beastAnimation)) {}
    InteriorActorScene::~InteriorActorScene() = default;
    ActorSceneSnapshot InteriorActorScene::snapshot() const { return mImpl->snapshot(); }
    size_t InteriorActorScene::bodyCount() const { return mImpl->mBodies.size(); }
    const std::string& InteriorActorScene::fingerprint() const { return mImpl->mFingerprint; }

    ActorSceneSnapshot InteriorActorScene::step(const std::array<float, 3>& velocity)
    {
        for (const float value : velocity)
            if (!std::isfinite(value) || std::abs(value) > 4096.f)
                throw std::invalid_argument("Interior NPC velocity outside bounds");
        auto next = std::make_unique<MWPhysics::ActorFrameData>(*mImpl->mActor);
        next->mMovement = {velocity[0], velocity[1], velocity[2]};
        Contacts effects(mImpl->mIdentities);
        MWPhysics::MovementSolver::unstuck(*next, &mImpl->mWorld);
        MWPhysics::MovementSolver::move(*next, 1.f / 60.f, &mImpl->mWorld, {}, effects);
        if (!std::isfinite(next->mPosition.length2()) || next->mPosition.length2() > 3e14f)
            throw std::runtime_error("Interior NPC solver output outside bounds");
        ActorSceneSnapshot result{mImpl->mActorId,
            {next->mPosition.x(), next->mPosition.y(), next->mPosition.z()}, next->mIsOnGround, effects.mObjects};
        auto transform = next->mCollisionObject->getWorldTransform();
        transform.setOrigin(Misc::Convert::toBullet(next->mPosition + mImpl->mActorOffset));
        next->mCollisionObject->setWorldTransform(transform);
        mImpl->mWorld.updateSingleAabb(next->mCollisionObject);
        mImpl->mActor = std::move(next);
        mImpl->mContacts = std::move(effects.mObjects);
        return result;
    }
}
