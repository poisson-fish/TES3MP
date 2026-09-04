#include <gtest/gtest.h>

#include <components/esm/position.hpp>

#include "../../openmw/mwrender/replicatedactor.hpp"
#include "../../openmw/mwrender/vismask.hpp"

#include <limits>

namespace
{
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
}
