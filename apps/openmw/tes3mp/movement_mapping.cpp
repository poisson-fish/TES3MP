#include "movement_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        std::int64_t roundTiesToEven(double value) noexcept
        {
            const double lower = std::floor(value);
            const double fraction = value - lower;
            if (fraction < 0.5)
                return static_cast<std::int64_t>(lower);
            if (fraction > 0.5)
                return static_cast<std::int64_t>(lower + 1.0);
            const auto lowerInteger = static_cast<std::int64_t>(lower);
            return lowerInteger % 2 == 0 ? lowerInteger : lowerInteger + 1;
        }
    }

    PlayerMotionIntent mapPlanarMovement(double right, double forward, double yawRadians) noexcept
    {
        if (!std::isfinite(right) || !std::isfinite(forward) || !std::isfinite(yawRadians))
            return PlayerMotionIntent(LinearVelocity3(0, 0, 0));
        right = std::clamp(right, -1.0, 1.0);
        forward = std::clamp(forward, -1.0, 1.0);
        const double length = std::hypot(right, forward);
        if (length > 1.0)
        {
            right /= length;
            forward /= length;
        }
        const double sine = std::sin(yawRadians);
        const double cosine = std::cos(yawRadians);
        const double worldX = cosine * right + sine * forward;
        const double worldY = -sine * right + cosine * forward;
        return PlayerMotionIntent(LinearVelocity3(
            roundTiesToEven(worldX * DesktopFixtureSpeedQuantaPerTick),
            roundTiesToEven(worldY * DesktopFixtureSpeedQuantaPerTick), 0));
    }

    void MotionIntentTracker::observeAcknowledgement(
        std::optional<CommandSequence> acknowledgement) noexcept
    {
        if (mPending && acknowledgement && *acknowledgement >= *mPending)
            mPending.reset();
    }

    std::optional<PlayerMotionIntent> MotionIntentTracker::next(
        LinearVelocity3 authoritativeVelocity) const noexcept
    {
        if (mPending || !mDesired || mDesired->desiredVelocity() == authoritativeVelocity)
            return std::nullopt;
        return mDesired;
    }

    bool MotionIntentTracker::markQueued(CommandSequence sequence) noexcept
    {
        if (mPending)
            return false;
        mPending = sequence;
        return true;
    }
}
