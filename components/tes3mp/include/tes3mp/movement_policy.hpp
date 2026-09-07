#ifndef TES3MP_MOVEMENT_POLICY_HPP
#define TES3MP_MOVEMENT_POLICY_HPP

#include "spatial_types.hpp"

#include <cstdint>
#include <optional>

namespace TES3MP
{
    // Compatibility envelope for the version 1.2 fixture intent. The extra
    // radial quantum admits ties-to-even planar component rounding at any yaw;
    // it is not a production locomotion speed profile.
    inline constexpr std::int64_t LegacyMotionAxisQuantaPerTick = 4096;
    inline constexpr std::uint64_t LegacyMotionRadiusQuantaPerTick = 4097;
    inline constexpr std::uint64_t MaximumMovementProfileQuantaPerTick = 65536;

    enum class LocomotionMode : std::uint8_t
    {
        Sneak,
        Walk,
        Run,
        Jump,
    };

    class MovementProfile
    {
    public:
        static constexpr std::optional<MovementProfile> create(std::uint64_t sneakQuantaPerTick,
            std::uint64_t walkQuantaPerTick, std::uint64_t runQuantaPerTick,
            std::uint64_t jumpQuantaPerTick) noexcept
        {
            if (sneakQuantaPerTick == 0 || sneakQuantaPerTick > walkQuantaPerTick
                || walkQuantaPerTick > runQuantaPerTick || runQuantaPerTick > MaximumMovementProfileQuantaPerTick
                || jumpQuantaPerTick == 0 || jumpQuantaPerTick > MaximumMovementProfileQuantaPerTick)
                return std::nullopt;
            return MovementProfile(sneakQuantaPerTick, walkQuantaPerTick, runQuantaPerTick, jumpQuantaPerTick);
        }

        constexpr std::uint64_t speed(LocomotionMode mode) const noexcept
        {
            switch (mode)
            {
                case LocomotionMode::Sneak: return mSneakQuantaPerTick;
                case LocomotionMode::Walk: return mWalkQuantaPerTick;
                case LocomotionMode::Run: return mRunQuantaPerTick;
                case LocomotionMode::Jump: return mJumpQuantaPerTick;
            }
            return 0;
        }

        constexpr bool allows(LocomotionMode mode, LinearVelocity3 velocity) const noexcept
        {
            const auto limit = speed(mode);
            if (limit == 0)
                return false;
            const auto signedLimit = static_cast<std::int64_t>(limit);
            if (velocity.x() < -signedLimit || velocity.x() > signedLimit
                || velocity.y() < -signedLimit || velocity.y() > signedLimit
                || velocity.z() < -signedLimit || velocity.z() > signedLimit)
                return false;
            const auto magnitude
                = [](std::int64_t value) constexpr { return static_cast<std::uint64_t>(value < 0 ? -value : value); };
            const auto x = magnitude(velocity.x());
            const auto y = magnitude(velocity.y());
            const auto z = magnitude(velocity.z());
            return x * x + y * y + z * z <= limit * limit;
        }

        friend constexpr bool operator==(MovementProfile, MovementProfile) noexcept = default;

    private:
        constexpr MovementProfile(std::uint64_t sneakQuantaPerTick, std::uint64_t walkQuantaPerTick,
            std::uint64_t runQuantaPerTick, std::uint64_t jumpQuantaPerTick) noexcept
            : mSneakQuantaPerTick(sneakQuantaPerTick)
            , mWalkQuantaPerTick(walkQuantaPerTick)
            , mRunQuantaPerTick(runQuantaPerTick)
            , mJumpQuantaPerTick(jumpQuantaPerTick)
        {
        }

        std::uint64_t mSneakQuantaPerTick;
        std::uint64_t mWalkQuantaPerTick;
        std::uint64_t mRunQuantaPerTick;
        std::uint64_t mJumpQuantaPerTick;
    };

    constexpr bool isLegacyMotionVelocitySafe(LinearVelocity3 velocity) noexcept
    {
        if (velocity.x() < -LegacyMotionAxisQuantaPerTick || velocity.x() > LegacyMotionAxisQuantaPerTick
            || velocity.y() < -LegacyMotionAxisQuantaPerTick || velocity.y() > LegacyMotionAxisQuantaPerTick
            || velocity.z() < -LegacyMotionAxisQuantaPerTick || velocity.z() > LegacyMotionAxisQuantaPerTick)
            return false;

        const auto magnitude
            = [](std::int64_t value) constexpr { return static_cast<std::uint64_t>(value < 0 ? -value : value); };
        const std::uint64_t x = magnitude(velocity.x());
        const std::uint64_t y = magnitude(velocity.y());
        const std::uint64_t z = magnitude(velocity.z());
        return x * x + y * y + z * z <= LegacyMotionRadiusQuantaPerTick * LegacyMotionRadiusQuantaPerTick;
    }

    constexpr MovementProfile testMovementProfile() noexcept
    {
        return *MovementProfile::create(LegacyMotionRadiusQuantaPerTick, LegacyMotionRadiusQuantaPerTick,
            LegacyMotionRadiusQuantaPerTick, LegacyMotionRadiusQuantaPerTick);
    }
}

#endif
