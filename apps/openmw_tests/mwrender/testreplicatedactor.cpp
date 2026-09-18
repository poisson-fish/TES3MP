#include <gtest/gtest.h>

#include <components/esm/position.hpp>
#include <osg/Quat>

#include "../../openmw/mwrender/actorutil.hpp"
#include "../../openmw/mwrender/replicatedactor.hpp"
#include "../../openmw/mwrender/vismask.hpp"

#include <cmath>
#include <limits>

namespace
{
    TEST(ReplicatedActor, RootStaysUprightWhenLookingAround)
    {
        for (const float yaw : { 0.f, 1.5707963f, -1.5707963f, 3.1415927f })
            for (const float pitch : { -1.55f, 0.f, 1.55f })
                for (const float roll : { -0.75f, 0.f, 0.75f })
                {
                    ESM::Position position{};
                    position.rot[0] = pitch;
                    position.rot[1] = roll;
                    position.rot[2] = yaw;
                    const auto rotation = MWRender::makeActorRootRotation(position);
                    const auto up = rotation * osg::Vec3f(0.f, 0.f, 1.f);
                    EXPECT_NEAR(up.x(), 0.f, 1e-6f);
                    EXPECT_NEAR(up.y(), 0.f, 1e-6f);
                    EXPECT_NEAR(up.z(), 1.f, 1e-6f);
                    const auto forward = rotation * osg::Vec3f(0.f, 1.f, 0.f);
                    EXPECT_NEAR(forward.x(), std::sin(yaw), 1e-6f);
                    EXPECT_NEAR(forward.y(), std::cos(yaw), 1e-6f);
                    EXPECT_NEAR(forward.z(), 0.f, 1e-6f);
                }
    }

    TEST(ReplicatedActor, AcceptsFinitePose)
    {
        ESM::Position position{};
        position.pos[0] = 1.f;
        position.pos[1] = -2.f;
        position.pos[2] = 3.f;
        position.rot[2] = 0.5f;
        EXPECT_TRUE(MWRender::isValidReplicatedActorPose(position));
    }

    TEST(ReplicatedActor, RejectsNonFinitePositionOrRotation)
    {
        ESM::Position position{};
        position.pos[1] = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(MWRender::isValidReplicatedActorPose(position));
        position.pos[1] = 0.f;
        position.rot[0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(MWRender::isValidReplicatedActorPose(position));
    }

    TEST(ReplicatedActor, DefinesBoundedAndNonInteractiveVisibilityRole)
    {
        EXPECT_EQ(MWRender::MaximumReplicatedActors, 255u);
        EXPECT_EQ(MWRender::replicatedActorCapacityResult(254), MWRender::ReplicatedActorResult::Accepted);
        EXPECT_EQ(
            MWRender::replicatedActorCapacityResult(255), MWRender::ReplicatedActorResult::CapacityExceeded);
        EXPECT_NE(MWRender::Mask_ReplicatedActor & MWRender::sToggleWorldMask, 0u);
        EXPECT_EQ(MWRender::Mask_ReplicatedActor & MWRender::Mask_Actor, 0u);
        EXPECT_EQ(MWRender::Mask_ReplicatedActor & MWRender::Mask_Player, 0u);
    }

    TEST(ReplicatedActor, AnimationFallbackIsAUsableCreationResult)
    {
        EXPECT_TRUE(MWRender::replicatedActorResultAccepted(MWRender::ReplicatedActorResult::Accepted));
        EXPECT_TRUE(MWRender::replicatedActorResultAccepted(MWRender::ReplicatedActorResult::AnimationFallback));
        EXPECT_FALSE(MWRender::replicatedActorResultAccepted(MWRender::ReplicatedActorResult::CapacityExceeded));
        EXPECT_FALSE(MWRender::replicatedActorResultAccepted(MWRender::ReplicatedActorResult::LifecycleViolation));
    }

    TEST(ReplicatedActor, CanonicalLocomotionMapsOnlyToLoopingPresentationGroups)
    {
        EXPECT_EQ(MWRender::replicatedActorAnimationGroup(MWRender::ReplicatedActorLocomotion::Idle), "idle");
        EXPECT_EQ(MWRender::replicatedActorAnimationGroup(MWRender::ReplicatedActorLocomotion::SneakIdle), "idlesneak");
        EXPECT_EQ(MWRender::replicatedActorAnimationGroup(MWRender::ReplicatedActorLocomotion::RunLeft), "runleft");
        EXPECT_EQ(MWRender::replicatedActorAnimationGroup(MWRender::ReplicatedActorLocomotion::Jump), "jump");
    }
}
