#ifndef TES3MP_MOVEMENT_POLICY_HPP
#define TES3MP_MOVEMENT_POLICY_HPP

#include "spatial_types.hpp"

#include <cstdint>

namespace TES3MP
{
    // Compatibility envelope for the version 1.2 fixture intent. The extra
    // radial quantum admits ties-to-even planar component rounding at any yaw;
    // it is not a production locomotion speed profile.
    inline constexpr std::int64_t LegacyMotionAxisQuantaPerTick = 4096;
    inline constexpr std::uint64_t LegacyMotionRadiusQuantaPerTick = 4097;

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
}

#endif
