#include <apps/openmw/mwmechanics/dooravoidance.hpp>
#include <apps/openmw/mwmechanics/steering.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/movementdata.hpp>
#include <apps/openmw/mwphysics/movementsolver.hpp>
#include <apps/openmw/mwphysics/actorconvexcallback.hpp>
#include <apps/openmw/mwphysics/projectileconvexcallback.hpp>
#include <apps/openmw/mwphysics/actorshape.hpp>
#include <apps/openmw/mwphysics/doorcontact.hpp>

#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    // Synthetic collision geometry only. No Environment, World, player,
    // renderer, inventory or independent movement implementation is installed.
    class Scene
    {
        struct Body
        {
            std::unique_ptr<btCollisionShape> mShape;
            btCollisionObject mObject;
        };
        btDefaultCollisionConfiguration mConfiguration;
        btCollisionDispatcher mDispatcher{ &mConfiguration };
        btDbvtBroadphase mBroadphase;
        btCollisionWorld mWorld{ &mDispatcher, &mBroadphase, &mConfiguration };
        std::vector<std::unique_ptr<Body>> mBodies;

        btCollisionObject* add(std::unique_ptr<btCollisionShape> shape, const btVector3& center, int group)
        {
            auto body = std::make_unique<Body>();
            body->mShape = std::move(shape);
            if (group == MWPhysics::CollisionType_Actor)
            {
                body->mShape->setMargin(.001);
                body->mObject.setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
                body->mObject.setActivationState(DISABLE_DEACTIVATION);
            }
            body->mObject.setCollisionShape(body->mShape.get());
            body->mObject.setWorldTransform(btTransform(btQuaternion::getIdentity(), center));
            auto* object = &body->mObject;
            mBodies.push_back(std::move(body));
            mWorld.addCollisionObject(object, group, MWPhysics::CollisionType_World | MWPhysics::CollisionType_Actor);
            return object;
        }

    public:
        Scene() { box({ 1000, 1000, 10 }, { 0, 0, -10 }); }
        ~Scene()
        {
            for (auto& body : mBodies)
                mWorld.removeCollisionObject(&body->mObject);
        }

        void box(const btVector3& halfExtents, const btVector3& center)
        {
            add(std::make_unique<btBoxShape>(halfExtents), center, MWPhysics::CollisionType_World);
        }

        MWPhysics::ActorFrameData actor(const osg::Vec3f& position = { 0, 0, 1 }, float slowFall = 1.f)
        {
            auto* object = add(std::make_unique<btCylinderShapeZ>(btVector3(16, 16, 32)),
                { position.x(), position.y(), position.z() + 32 }, MWPhysics::CollisionType_Actor);
            return { .mPosition = position,
                .mIsOnGround = position.z() == 1.f,
                .mCollisionObject = object,
                .mSlowFall = slowFall,
                .mHalfExtentsZ = 32.f,
                .mWasOnGround = position.z() == 1.f };
        }

        void step(MWPhysics::ActorFrameData& actor, const MWPhysics::WorldFrameData& world = {},
            MWPhysics::CollisionEffects& effects = MWPhysics::stockCollisionEffects())
        {
            // Same ordering and 60 Hz step as stock physics. Installation here
            // updates only this fixture's body; it is not a server transaction.
            MWPhysics::MovementSolver::unstuck(actor, &mWorld);
            MWPhysics::MovementSolver::move(actor, 1.f / 60.f, &mWorld, world, effects);
            auto transform = actor.mCollisionObject->getWorldTransform();
            transform.setOrigin({ actor.mPosition.x(), actor.mPosition.y(), actor.mPosition.z() + actor.mHalfExtentsZ });
            actor.mCollisionObject->setWorldTransform(transform);
            mWorld.updateSingleAabb(actor.mCollisionObject);
            require(std::isfinite(actor.mPosition.x()) && std::isfinite(actor.mPosition.y())
                    && std::isfinite(actor.mPosition.z()),
                "Stock movement produced nonfinite position");
        }
    };

    class RecordedEffects final : public MWPhysics::CollisionEffects
    {
    public:
        std::vector<const btCollisionObject*> mContacts;
        std::vector<std::pair<const btCollisionObject*, const btCollisionObject*>> mHits;
        const btCollisionObject* mInvalid = nullptr;
        const btCollisionObject* mCaster = nullptr;
        bool mActive = true;
        bool mWater = false;
        void objectCollision(const btCollisionObject* object, bool player) override
        {
            require(!player, "NPC collision was attributed to the player");
            mContacts.push_back(object);
        }
        bool projectileActive(const btCollisionObject*) const override { return mActive; }
        bool validProjectileTarget(const btCollisionObject*, const btCollisionObject* target) const override
        { return target != mInvalid; }
        const btCollisionObject* projectileCaster(const btCollisionObject*) const override { return mCaster; }
        void hit(const btCollisionObject* projectile, const btCollisionObject* target, const btVector3&, const btVector3&) override
        { mHits.emplace_back(projectile, target); }
        void hitWater(const btCollisionObject*) override { mWater = true; }
    };

    void collisionEffects()
    {
        Scene wall;
        wall.box({500,20,150}, {0,180,150});
        auto actor = wall.actor();
        RecordedEffects journal;
        actor.mMovement = {0,120,0};
        for (int i = 0; i < 180; ++i) wall.step(actor, {}, journal);
        require(!journal.mContacts.empty() && actor.mPosition.y() < 145,
            "Detached movement did not journal its object contacts");

        btCollisionObject source, target, caster;
        btBroadphaseProxy proxy;
        target.setBroadphaseHandle(&proxy);
        // Opaque non-engine user data must never be interpreted by either callback.
        int sentinel = 17;
        source.setUserPointer(&sentinel);
        target.setUserPointer(&sentinel);
        proxy.m_collisionFilterGroup = MWPhysics::CollisionType_Projectile;
        btCollisionWorld::LocalConvexResult hit(&target, nullptr, {0,-1,0}, {0,10,0}, .5f);
        MWPhysics::ActorConvexCallback sweep(&source, {0,-1,0}, 0, nullptr, journal);
        sweep.addSingleResult(hit, true);
        require(journal.mHits.size() == 1 && journal.mHits.back() == std::pair(&target, &source),
            "Actor sweep did not route projectile contact");
        journal.mActive = false;
        sweep.addSingleResult(hit, true);
        journal.mActive = true;
        journal.mInvalid = &source;
        sweep.addSingleResult(hit, true);
        require(journal.mHits.size() == 1, "Inactive or invalid projectile contact leaked an effect");
        journal.mInvalid = nullptr;
        journal.mCaster = &caster;
        MWPhysics::ProjectileConvexCallback projectile(&caster, &source, {0,0,0}, {0,20,0}, journal);
        projectile.addSingleResult(hit, true);
        require(journal.mHits.size() == 3 && journal.mHits.back() == std::pair(&source, &target),
            "Projectile pair did not route both hits");
        proxy.m_collisionFilterGroup = MWPhysics::CollisionType_Water;
        projectile.addSingleResult(hit, true);
        require(journal.mWater && sentinel == 17, "Water contact or collision isolation failed");
        target.setBroadphaseHandle(nullptr);

        const auto cylinder = MWPhysics::makeActorShape({16,16,32}, {0,0,32}, DetourNavigator::CollisionShapeType::Cylinder);
        const auto box = MWPhysics::makeActorShape({16,32,32}, {0,0,32}, DetourNavigator::CollisionShapeType::Cylinder);
        const auto offset = MWPhysics::makeActorShape({16,16,32}, {1,0,32}, DetourNavigator::CollisionShapeType::Cylinder);
        require(cylinder.mShape->getShapeType() == CYLINDER_SHAPE_PROXYTYPE && cylinder.mRotationallyInvariant
            && box.mShape->getShapeType() == BOX_SHAPE_PROXYTYPE && !box.mRotationallyInvariant
            && !offset.mRotationallyInvariant, "Stock actor hull selection changed");
    }

    void collision()
    {
        Scene wall;
        wall.box({ 500, 20, 150 }, { 0, 180, 150 });
        auto blocked = wall.actor();
        blocked.mMovement = { 0, 120, 0 };
        for (int i = 0; i < 180; ++i)
            wall.step(blocked);
        require(blocked.mPosition.y() > 135 && blocked.mPosition.y() < 145,
            "Actor failed to approach/stop at the wall");
        require(blocked.mIsOnGround && std::abs(blocked.mPosition.z() - 1.f) < 1.f,
            "Wall contact lost grounded state");
        blocked.mMovement = { 60, 120, 0 };
        for (int i = 0; i < 120; ++i)
            wall.step(blocked);
        require(blocked.mPosition.x() > 100 && blocked.mPosition.y() < 145,
            "Actor did not slide along the wall");

        Scene stairs;
        stairs.box({ 100, 30, 10 }, { 0, 90, 10 });
        auto walker = stairs.actor();
        walker.mMovement = { 0, 120, 0 };
        float highest = walker.mPosition.z();
        for (int i = 0; i < 120; ++i)
        {
            stairs.step(walker);
            highest = std::max(highest, walker.mPosition.z());
        }
        require(highest > 20 && highest < 23 && walker.mPosition.y() > 220,
            "Stock step-up failed to cross a walkable obstacle");
        require(walker.mIsOnGround && walker.mPosition.z() < 2,
            "Stock step-down failed to return to the floor");

        Scene actors;
        const auto obstacle = actors.actor({ 0, 180, 1 });
        auto approaching = actors.actor();
        approaching.mMovement = { 0, 120, 0 };
        for (int i = 0; i < 180; ++i)
            actors.step(approaching);
        require(approaching.mPosition.y() > 138 && approaching.mPosition.y() < 149
                && approaching.mPosition.z() < 2,
            "Actor collision passed through or climbed another actor");
        require(obstacle.mPosition == osg::Vec3f(0, 180, 1), "Collision moved the stationary actor frame");
    }

    float travel(const MWPhysics::WorldFrameData& weather)
    {
        Scene scene;
        auto actor = scene.actor();
        actor.mMovement = { 0, 120, 0 };
        for (int i = 0; i < 60; ++i)
            scene.step(actor, weather);
        return actor.mPosition.y();
    }

    void environment()
    {
        require(std::abs(travel({}) - 120.f) < .01f, "Calm movement changed");
        require(std::abs(travel({ true, { 0, 1, 0 }, .5f }) - 180.f) < .01f,
            "Storm multiplier was not supplied by the caller");
        require(std::abs(travel({ true, { 0, -1, 0 }, .5f }) - 60.f) < .01f,
            "Movement against the storm did not slow down");
        require(std::abs(travel({ true, { 0, 1, 0 }, .25f }) - 150.f) < .01f,
            "Movement retained another world's storm setting");
        require(std::abs(travel({ false, { 0, 1, 0 }, .5f }) - 120.f) < .01f,
            "Inactive storm affected movement");

        Scene falling;
        auto normal = falling.actor({ -100, 0, 200 });
        auto slow = falling.actor({ 100, 0, 200 }, .5f);
        falling.step(normal);
        falling.step(slow);
        require(normal.mInertia.z() < 0 && std::abs(slow.mInertia.z() * 2 - normal.mInertia.z()) < .001f,
            "Caller-provided slow fall did not affect stock gravity");
        for (int i = 0; i < 120; ++i)
            falling.step(normal);
        require(normal.mIsOnGround && std::abs(normal.mPosition.z() - 1.f) < 1.f,
            "Falling actor did not land on the floor");
    }

    void doorAvoidance()
    {
        Misc::Rng::Generator random(7), expected(7);
        MWMechanics::DoorAvoidance state;
        const osg::Vec3f actor(60, -32, 1), door(0, 0, 0);
        const auto angle = state.update(actor, door, true, .5f, random);
        require(angle && std::abs(*angle - std::atan2(60.f, -32.f)) < .0001f,
            "Avoidance did not turn away from the door pivot");
        require(random == expected, "Unstuck avoidance consumed randomness");
        require(!state.update(actor + osg::Vec3f(11, 0, 0), door, true, .51f, random),
            "Moving NPC did not finish its one-second retreat");
        state = {};
        state.update(actor, door, true, .5f, random);
        const int direction = Misc::Rng::rollDice(4, expected);
        require(state.update(actor, door, true, .51f, random).has_value()
            && state.mDuration == 1 && state.mDirection == direction && random == expected,
            "Stuck avoidance did not use the caller's stock random stream");
        require(!state.update(actor, door, false, 1.f / 60, random), "Idle door did not end avoidance");
        const auto turn = MWMechanics::smoothTurnStep(0, osg::PIf, 120, 1.f / 60, false, .01f);
        require(!turn.mComplete && std::abs(std::abs(turn.mRotation) - osg::DegreesToRadians(15.f)) < .0001f,
            "Avoidance steering lost stock angular speed");
        require(MWMechanics::smoothTurnStep(0, .01f, 120, 1.f / 60, false, .02f).mComplete,
            "Stock turning tolerance did not complete");
    }

    void doorContact()
    {
        btDefaultCollisionConfiguration configuration;
        btCollisionDispatcher dispatcher(&configuration);
        btDbvtBroadphase broadphase;
        btCollisionWorld world(&dispatcher, &broadphase, &configuration);
        btBoxShape doorShape({40, 2, 70});
        btCylinderShapeZ actorShape({16, 16, 32});
        btCollisionObject door, actor, unrelated;
        door.setCollisionShape(&doorShape);
        actor.setCollisionShape(&actorShape);
        door.setWorldTransform(btTransform(btQuaternion::getIdentity(), {40, 0, 70}));
        actor.setWorldTransform(btTransform(btQuaternion::getIdentity(), {60, -16, 32}));
        const auto priorDoor = door.getWorldTransform(), priorActor = actor.getWorldTransform();
        const auto blocked = [&](float delta, bool reverse, const btCollisionObject* selected) {
            MWPhysics::DoorContactResult query(&door, selected, {0, 0, 0}, delta);
            if (reverse) world.contactPairTest(&actor, &door, query);
            else world.contactPairTest(&door, &actor, query);
            return query.mBlocked;
        };
        require(blocked(.05f, false, &actor), "Door failed to detect an approaching NPC contact");
        require(blocked(.05f, true, &actor), "Reversed Bullet pair lost the NPC contact");
        require(!blocked(-.05f, false, &actor), "Door contact blocked retreating rotation");
        require(!blocked(.05f, false, &unrelated), "Door query accepted another actor's contact");
        require(door.getWorldTransform() == priorDoor && actor.getWorldTransform() == priorActor,
            "Door sensing mutated collision transforms");
        actor.setWorldTransform(btTransform(btQuaternion::getIdentity(), {60, -80, 32}));
        require(!blocked(.05f, false, &actor), "Separated NPC blocked the door");
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Select movement-collision, movement-environment or collision-effects");
        const std::string_view filter = argv[1];
        if (filter == "movement-collision")
            collision();
        else if (filter == "movement-environment")
            environment();
        else if (filter == "collision-effects")
            collisionEffects();
        else if (filter == "door-contact")
            doorContact();
        else if (filter == "door-avoidance")
            doorAvoidance();
        else
            throw std::invalid_argument("Unknown actor physics filter");
        std::cout << "PASS " << filter << " (synthetic geometry, stock OpenMW solver, no engine environment)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL actor physics: " << error.what() << '\n';
        return 1;
    }
}
