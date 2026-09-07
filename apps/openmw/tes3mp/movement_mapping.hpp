#ifndef OPENMW_TES3MP_MOVEMENT_MAPPING_HPP
#define OPENMW_TES3MP_MOVEMENT_MAPPING_HPP

#include "remote_motion.hpp"

#include <tes3mp/protocol_exchange.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace TES3MP::OpenMWAdapter
{
    constexpr std::int64_t DesktopFixtureSpeedQuantaPerTick = 4096;

    LocomotionIntent mapPlanarMovement(double right, double forward, double yawRadians,
        LocomotionMode mode = LocomotionMode::Walk) noexcept;
    std::uint64_t movementCorrectionDistanceQuanta(
        Position3 authoritative, double localXQuanta, double localYQuanta, double localZQuanta) noexcept;

    class MotionIntentTracker
    {
    public:
        explicit MotionIntentTracker(MovementMetricSink* metrics = nullptr) noexcept
            : mMetrics(metrics)
        {
        }

        void sample(LocomotionIntent intent, MonotonicInstant sampledAt) noexcept;
        void observeAcknowledgement(
            std::optional<CommandSequence> acknowledgement, MonotonicInstant observedAt) noexcept;
        std::optional<LocomotionIntent> next(LinearVelocity3 authoritativeVelocity) const noexcept;
        bool markQueued(CommandSequence sequence, LocomotionIntent intent, MonotonicInstant queuedAt) noexcept;
        bool pending() const noexcept { return mPending.has_value(); }

    private:
        struct PendingIntent
        {
            CommandSequence sequence;
            MonotonicInstant queuedAt;
            std::optional<MonotonicInstant> stopObservedAt;
        };

        struct DesiredIntent
        {
            LocomotionIntent intent;
            MonotonicInstant sampledAt;
        };

        MovementMetricSink* mMetrics = nullptr;
        std::optional<DesiredIntent> mDesired;
        std::optional<PendingIntent> mPending;
    };

    inline constexpr std::size_t MaximumPoseEvidenceSources = 256;

    class PoseEvidenceTracker
    {
    public:
        explicit PoseEvidenceTracker(MovementMetricSink* metrics = nullptr) noexcept
            : mMetrics(metrics)
        {
        }

        void observe(EntityId source, AuthorityEpoch epoch, PoseSampleSequence sequence,
            MonotonicInstant receivedAt) noexcept;
        void retain(std::span<const SpatialEntitySnapshot> visible) noexcept;
        void advance(MonotonicInstant now) noexcept;
        void clear() noexcept;

    private:
        struct Source
        {
            EntityId entity;
            AuthorityEpoch epoch;
            PoseSampleSequence sequence;
            MonotonicInstant receivedAt;
        };

        MovementMetricSink* mMetrics = nullptr;
        std::array<std::optional<Source>, MaximumPoseEvidenceSources> mSources{};
    };
}

#endif
