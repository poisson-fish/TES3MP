#ifndef OPENMW_TES3MP_MOVEMENT_MAPPING_HPP
#define OPENMW_TES3MP_MOVEMENT_MAPPING_HPP

#include <tes3mp/protocol_exchange.hpp>

#include <cstdint>
#include <optional>

namespace TES3MP::OpenMWAdapter
{
    constexpr std::int64_t DesktopFixtureSpeedQuantaPerTick = 4096;

    PlayerMotionIntent mapPlanarMovement(double right, double forward, double yawRadians) noexcept;

    class MotionIntentTracker
    {
    public:
        void sample(PlayerMotionIntent intent) noexcept { mDesired = intent; }
        void observeAcknowledgement(std::optional<CommandSequence> acknowledgement) noexcept;
        std::optional<PlayerMotionIntent> next(LinearVelocity3 authoritativeVelocity) const noexcept;
        bool markQueued(CommandSequence sequence) noexcept;
        bool pending() const noexcept { return mPending.has_value(); }

    private:
        std::optional<PlayerMotionIntent> mDesired;
        std::optional<CommandSequence> mPending;
    };
}

#endif
