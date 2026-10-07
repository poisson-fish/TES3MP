#ifndef TES3MP_MOVEMENT_POLICY_HPP
#define TES3MP_MOVEMENT_POLICY_HPP

#include "spatial_types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <array>
#include <cmath>

namespace TES3MP
{
    // Compatibility envelope for the version 1.2 fixture intent. The extra
    // radial quantum admits ties-to-even planar component rounding at any yaw;
    // it is not a production locomotion speed profile.
    inline constexpr std::int64_t LegacyMotionAxisQuantaPerTick = 4096;
    inline constexpr std::uint64_t LegacyMotionRadiusQuantaPerTick = 4097;
    inline constexpr std::uint64_t MaximumMovementProfileQuantaPerTick = 65536;
    inline constexpr std::uint64_t MaximumLocomotionInputOrdinal
        = std::numeric_limits<std::uint32_t>::max();
    inline constexpr std::size_t MaximumRetainedLocomotionInputs = 128;

    enum class LocomotionMode : std::uint8_t
    {
        Sneak,
        Walk,
        Run,
        Jump,
    };

    // Dimensionless local controls. The engine owns speed, gravity and collision.
    struct PlayerMovementControls
    {
        std::int16_t right = 0, forward = 0, vertical = 0;
        Turn32 pitch = Turn32::fromValue(0);
        bool jump = false;
        constexpr bool valid() const noexcept
        {
            return right >= -1024 && right <= 1024 && forward >= -1024 && forward <= 1024
                && vertical >= -1024 && vertical <= 1024;
        }
        friend constexpr bool operator==(PlayerMovementControls, PlayerMovementControls) noexcept = default;
    };

    struct PlayerPhysicsBaseline
    {
        std::array<float, 3> inertia{}, lastStuckPosition{};
        float fallHeight = 0, breath = 0;
        std::uint32_t stuckFrames = 0;
        bool grounded = false, slope = false, walkingOnWater = false;
        std::uint64_t consumedCommand = 0;
        bool jumpHeld = false, blocked = false;
        bool valid() const noexcept
        {
            const auto bounded = [](float value) { return std::isfinite(value) && std::abs(value) <= 1e7f; };
            for (float value : inertia) if (!bounded(value)) return false;
            for (float value : lastStuckPosition) if (!bounded(value)) return false;
            return bounded(fallHeight) && fallHeight >= 0 && bounded(breath) && breath >= -1;
        }
        friend constexpr bool operator==(const PlayerPhysicsBaseline&, const PlayerPhysicsBaseline&) noexcept = default;
    };

    class LocomotionInputTick
    {
    public:
        static constexpr std::optional<LocomotionInputTick> fromValue(std::uint64_t value) noexcept
        {
            if (value == 0 || value > MaximumLocomotionInputOrdinal)
                return std::nullopt;
            return LocomotionInputTick(value);
        }

        constexpr std::uint64_t value() const noexcept { return mValue; }
        constexpr std::optional<LocomotionInputTick> next() const noexcept { return fromValue(mValue + 1); }
        friend constexpr bool operator==(LocomotionInputTick, LocomotionInputTick) noexcept = default;
        friend constexpr auto operator<=>(LocomotionInputTick, LocomotionInputTick) noexcept = default;

    private:
        constexpr explicit LocomotionInputTick(std::uint64_t value) noexcept : mValue(value) {}
        std::uint64_t mValue;
    };

    class LocomotionInputSequence
    {
    public:
        static constexpr std::optional<LocomotionInputSequence> fromValue(std::uint64_t value) noexcept
        {
            if (value == 0 || value > MaximumLocomotionInputOrdinal)
                return std::nullopt;
            return LocomotionInputSequence(value);
        }

        static constexpr LocomotionInputSequence initial() noexcept { return LocomotionInputSequence(1); }
        constexpr std::uint64_t value() const noexcept { return mValue; }
        constexpr std::optional<LocomotionInputSequence> next() const noexcept { return fromValue(mValue + 1); }
        friend constexpr bool operator==(LocomotionInputSequence, LocomotionInputSequence) noexcept = default;
        friend constexpr auto operator<=>(LocomotionInputSequence, LocomotionInputSequence) noexcept = default;

    private:
        constexpr explicit LocomotionInputSequence(std::uint64_t value) noexcept : mValue(value) {}
        std::uint64_t mValue;
    };

    class LocomotionIntent
    {
    public:
        constexpr LocomotionIntent(LocomotionMode mode, Turn32 rootFacing, LinearVelocity3 desiredVelocity,
            std::optional<Position3> position = std::nullopt,
            std::optional<Orientation3> orientation = std::nullopt,
            std::optional<PlayerMovementControls> controls = std::nullopt) noexcept
            : mMode(mode)
            , mRootFacing(rootFacing)
            , mDesiredVelocity(desiredVelocity)
            , mPosition(position)
            , mOrientation(orientation)
            , mControls(controls)
        {
        }

        constexpr LocomotionMode mode() const noexcept { return mMode; }
        constexpr Turn32 rootFacing() const noexcept { return mRootFacing; }
        constexpr LinearVelocity3 desiredVelocity() const noexcept { return mDesiredVelocity; }
        constexpr const std::optional<Position3>& position() const noexcept { return mPosition; }
        constexpr const std::optional<Orientation3>& orientation() const noexcept { return mOrientation; }
        constexpr const std::optional<PlayerMovementControls>& controls() const noexcept { return mControls; }
        friend constexpr bool operator==(const LocomotionIntent&, const LocomotionIntent&) noexcept = default;

    private:
        LocomotionMode mMode;
        Turn32 mRootFacing;
        LinearVelocity3 mDesiredVelocity;
        std::optional<Position3> mPosition;
        std::optional<Orientation3> mOrientation;
        std::optional<PlayerMovementControls> mControls;
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
