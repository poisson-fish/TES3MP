#include <apps/openmw/mwphysics/collisiontype.hpp>
#include <apps/openmw/mwphysics/movementdata.hpp>
#include <apps/openmw/mwphysics/movementsolver.hpp>

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

        void step(MWPhysics::ActorFrameData& actor, const MWPhysics::WorldFrameData& world = {})
        {
            // Same ordering and 60 Hz step as stock physics. Installation here
            // updates only this fixture's body; it is not a server transaction.
            MWPhysics::MovementSolver::unstuck(actor, &mWorld);
            MWPhysics::MovementSolver::move(actor, 1.f / 60.f, &mWorld, world);
            auto transform = actor.mCollisionObject->getWorldTransform();
            transform.setOrigin({ actor.mPosition.x(), actor.mPosition.y(), actor.mPosition.z() + actor.mHalfExtentsZ });
            actor.mCollisionObject->setWorldTransform(transform);
            mWorld.updateSingleAabb(actor.mCollisionObject);
            require(std::isfinite(actor.mPosition.x()) && std::isfinite(actor.mPosition.y())
                    && std::isfinite(actor.mPosition.z()),
                "Stock movement produced nonfinite position");
        }
    };

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
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Select movement-collision or movement-environment");
        const std::string_view filter = argv[1];
        if (filter == "movement-collision")
            collision();
        else if (filter == "movement-environment")
            environment();
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
