#include "movement_mapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

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

        Turn32 rootFacing(double yawRadians) noexcept
        {
            constexpr double Tau = 2.0 * std::numbers::pi;
            double turns = std::fmod(yawRadians / Tau, 1.0);
            if (turns < 0.0)
                turns += 1.0;
            return Turn32::fromUnnormalized(static_cast<std::uint64_t>(
                std::floor(turns * 4294967296.0 + 0.5)));
        }
    }

    LocomotionIntent mapPlanarMovement(
        double right, double forward, double yawRadians, LocomotionMode mode) noexcept
    {
        if (!std::isfinite(right) || !std::isfinite(forward) || !std::isfinite(yawRadians))
            return LocomotionIntent(mode, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));
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
        return LocomotionIntent(mode, rootFacing(yawRadians),
            LinearVelocity3(roundTiesToEven(worldX * DesktopFixtureSpeedQuantaPerTick),
                roundTiesToEven(worldY * DesktopFixtureSpeedQuantaPerTick), 0));
    }

    std::uint64_t movementCorrectionDistanceQuanta(
        Position3 authoritative, double localXQuanta, double localYQuanta, double localZQuanta) noexcept
    {
        const double distance = std::hypot(static_cast<double>(authoritative.x()) - localXQuanta,
            static_cast<double>(authoritative.y()) - localYQuanta,
            static_cast<double>(authoritative.z()) - localZQuanta);
        if (!std::isfinite(distance) || distance >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
            return std::numeric_limits<std::uint64_t>::max();
        return static_cast<std::uint64_t>(std::floor(distance + 0.5));
    }

    void MotionIntentTracker::sample(LocomotionIntent intent, MonotonicInstant sampledAt) noexcept
    {
        if (!mDesired || mDesired->intent != intent)
            mDesired.emplace(DesiredIntent{ intent, sampledAt });
    }

    void MotionIntentTracker::observeAcknowledgement(
        std::optional<CommandSequence> acknowledgement, MonotonicInstant observedAt) noexcept
    {
        if (mPending && acknowledgement && *acknowledgement >= mPending->sequence)
        {
            if (mMetrics && observedAt >= mPending->queuedAt)
            {
                const std::uint64_t elapsed = observedAt.nanoseconds() - mPending->queuedAt.nanoseconds();
                (void)mMetrics->tryRecord({ MovementMetricKey::CommandAcknowledgementNanoseconds, elapsed });
                if (mPending->stopObservedAt && observedAt >= *mPending->stopObservedAt)
                    (void)mMetrics->tryRecord({ MovementMetricKey::StopAcknowledgementNanoseconds,
                        observedAt.nanoseconds() - mPending->stopObservedAt->nanoseconds() });
            }
            mPending.reset();
        }
    }

    std::optional<LocomotionIntent> MotionIntentTracker::next(LinearVelocity3 authoritativeVelocity) const noexcept
    {
        if (mPending || !mDesired || mDesired->intent.desiredVelocity() == authoritativeVelocity)
            return std::nullopt;
        return mDesired->intent;
    }

    bool MotionIntentTracker::markQueued(
        CommandSequence sequence, LocomotionIntent intent, MonotonicInstant queuedAt) noexcept
    {
        if (mPending)
            return false;
        std::optional<MonotonicInstant> stopObservedAt;
        if (intent.desiredVelocity() == LinearVelocity3(0, 0, 0) && mDesired
            && mDesired->intent.desiredVelocity() == intent.desiredVelocity())
            stopObservedAt = mDesired->sampledAt;
        mPending.emplace(PendingIntent{ sequence, queuedAt, stopObservedAt });
        return true;
    }

    void PoseEvidenceTracker::observe(EntityId source, AuthorityEpoch epoch, PoseSampleSequence sequence,
        MonotonicInstant receivedAt) noexcept
    {
        auto found = std::find_if(mSources.begin(), mSources.end(),
            [&](const auto& value) { return value && value->entity == source && value->epoch == epoch; });
        if (found == mSources.end())
            found = std::find(mSources.begin(), mSources.end(), std::nullopt);
        if (found == mSources.end())
            return;
        if (*found)
        {
            if (sequence <= (*found)->sequence)
                return;
            const std::uint64_t previous = (*found)->sequence.value();
            if (sequence.value() > previous + 1 && mMetrics)
                (void)mMetrics->tryRecord({ MovementMetricKey::PoseLostSamples, sequence.value() - previous - 1 });
        }
        found->emplace(Source{ source, epoch, sequence, receivedAt });
    }

    void PoseEvidenceTracker::retain(std::span<const SpatialEntitySnapshot> visible) noexcept
    {
        for (auto& source : mSources)
            if (source && std::find_if(visible.begin(), visible.end(), [&](const auto& snapshot) {
                              return snapshot.entityId() == source->entity
                                  && snapshot.authorityEpoch() == source->epoch;
                          }) == visible.end())
                source.reset();
    }

    void PoseEvidenceTracker::advance(MonotonicInstant now) noexcept
    {
        if (!mMetrics)
            return;
        for (const auto& source : mSources)
            if (source)
            {
                const std::uint64_t age
                    = now >= source->receivedAt ? now.nanoseconds() - source->receivedAt.nanoseconds() : 0;
                (void)mMetrics->tryRecord({ MovementMetricKey::PoseAgeNanoseconds, age });
            }
    }

    double PoseEvidenceTracker::poseWeight(EntityId source, AuthorityEpoch epoch, MonotonicInstant now) const noexcept
    {
        const auto found = std::find_if(mSources.begin(), mSources.end(),
            [&](const auto& value) { return value && value->entity == source && value->epoch == epoch; });
        if (found == mSources.end())
            return 0.0;
        const std::uint64_t age
            = now >= (*found)->receivedAt ? now.nanoseconds() - (*found)->receivedAt.nanoseconds() : 0;
        if (age <= RemotePoseFreshNanoseconds)
            return 1.0;
        const std::uint64_t blendAge = age - RemotePoseFreshNanoseconds;
        if (blendAge >= RemotePoseFallbackBlendNanoseconds)
            return 0.0;
        return 1.0 - static_cast<double>(blendAge) / static_cast<double>(RemotePoseFallbackBlendNanoseconds);
    }

    void PoseEvidenceTracker::clear() noexcept
    {
        for (auto& source : mSources)
            source.reset();
    }
}
