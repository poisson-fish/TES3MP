#ifndef OPENMW_TES3MP_REMOTE_MOTION_HPP
#define OPENMW_TES3MP_REMOTE_MOTION_HPP

#include <tes3mp/command_primitives.hpp>
#include <tes3mp/monotonic_clock.hpp>
#include <tes3mp/observability.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace TES3MP::OpenMWAdapter
{
    inline constexpr std::size_t MaximumRemoteMotionSamples = 4;
    inline constexpr std::uint64_t RemotePlaybackDelayFloorTicks = 2;
    inline constexpr std::uint64_t RemotePlaybackDelayCeilingTicks = 3;
    inline constexpr std::size_t RemotePlaybackStableSamples = MaximumRemoteMotionSamples;
    inline constexpr std::uint64_t MaximumRemoteExtrapolationTicks = 3;
    inline constexpr std::uint64_t RemoteCorrectionBlendNanoseconds = 66'666'667;
    inline constexpr std::uint64_t RemoteHardSnapDistanceQuanta = 16 * 1024;

    enum class MovementMetricKey : std::uint8_t
    {
        CommandAcknowledgementNanoseconds,
        StopAcknowledgementNanoseconds,
        LocalCorrectionDistanceQuanta,
        SnapshotAgeNanoseconds,
        BufferDepth,
        ExtrapolationNanoseconds,
        CorrectionDistanceQuanta,
        HardSnaps,
        PoseAgeNanoseconds,
        PoseLostSamples,
        Count,
    };

    struct MovementMetric
    {
        MovementMetricKey key;
        std::uint64_t value;

        friend constexpr bool operator==(MovementMetric, MovementMetric) noexcept = default;
    };

    class MovementMetricSink
    {
    public:
        virtual ~MovementMetricSink() = default;
        virtual ObservationResult tryRecord(MovementMetric metric) noexcept = 0;
    };

    class NullMovementMetricSink final : public MovementMetricSink
    {
    public:
        ObservationResult tryRecord(MovementMetric) noexcept override { return ObservationResult::Accepted; }
    };

    inline constexpr std::size_t MaximumMovementEvidenceObservations = 16'384;

    struct MovementMetricSummary
    {
        std::uint64_t samples = 0;
        std::uint64_t minimum = 0;
        std::uint64_t maximum = 0;
        std::uint64_t total = 0;

        friend constexpr bool operator==(MovementMetricSummary, MovementMetricSummary) noexcept = default;
    };

    class BoundedMovementMetricSink final : public MovementMetricSink
    {
    public:
        explicit constexpr BoundedMovementMetricSink(
            std::size_t capacity = MaximumMovementEvidenceObservations) noexcept
            : mCapacity(std::min(capacity, MaximumMovementEvidenceObservations))
        {
        }

        ObservationResult tryRecord(MovementMetric metric) noexcept override;
        constexpr const MovementMetricSummary& summary(MovementMetricKey key) const noexcept
        {
            return mSummaries[static_cast<std::size_t>(key)];
        }
        constexpr std::size_t acceptedCount() const noexcept { return mAccepted; }
        constexpr std::size_t droppedCount() const noexcept { return mDropped; }

    private:
        std::array<MovementMetricSummary, static_cast<std::size_t>(MovementMetricKey::Count)> mSummaries{};
        std::size_t mCapacity = 0;
        std::size_t mAccepted = 0;
        std::size_t mDropped = 0;
    };

    const char* movementMetricName(MovementMetricKey key) noexcept;

    using RemoteMotionMetricKey = MovementMetricKey;
    using RemoteMotionMetric = MovementMetric;
    using RemoteMotionMetricSink = MovementMetricSink;
    using NullRemoteMotionMetricSink = NullMovementMetricSink;

    struct RemoteMotionPose
    {
        CellId cell;
        double x;
        double y;
        double z;
        Orientation3 orientation;
        LinearVelocity3 velocity;
        LocomotionMode locomotionMode;
    };

    enum class RemoteLocomotionAnimation : std::uint8_t
    {
        Idle,
        SneakIdle,
        WalkForward,
        WalkBack,
        WalkLeft,
        WalkRight,
        RunForward,
        RunBack,
        RunLeft,
        RunRight,
        SneakForward,
        SneakBack,
        SneakLeft,
        SneakRight,
        Jump,
    };

    RemoteLocomotionAnimation remoteLocomotionAnimation(const RemoteMotionPose& pose) noexcept;

    class RemoteMotionBuffer
    {
    public:
        explicit RemoteMotionBuffer(RemoteMotionMetricSink& metrics) noexcept;

        bool observe(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept;
        std::optional<RemoteMotionPose> advance(MonotonicInstant now) noexcept;
        void clear() noexcept;
        std::size_t sampleCount() const noexcept { return mSampleCount; }
        std::uint64_t playbackDelayTicks() const noexcept { return mPlaybackDelayTicks; }

    private:
        struct Sample
        {
            SpatialEntitySnapshot snapshot;
            MonotonicInstant receivedAt;
        };

        struct ResolvedPose
        {
            RemoteMotionPose pose;
            std::uint64_t extrapolationNanoseconds;
        };

        void resetTo(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept;
        void adaptPlaybackDelay(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept;
        void advanceCursor(MonotonicInstant now) noexcept;
        std::optional<ResolvedPose> resolve() const noexcept;
        RemoteMotionPose applyCorrection(RemoteMotionPose pose, MonotonicInstant now) noexcept;
        void record(RemoteMotionMetricKey key, std::uint64_t value) noexcept;

        RemoteMotionMetricSink& mMetrics;
        std::array<std::optional<Sample>, MaximumRemoteMotionSamples> mSamples{};
        std::size_t mSampleCount = 0;
        bool mStarted = false;
        std::uint64_t mCursorTick = 0;
        std::uint64_t mCursorFraction = 0;
        std::uint64_t mPlaybackDelayTicks = RemotePlaybackDelayFloorTicks;
        std::size_t mStableArrivalSamples = 0;
        std::uint64_t mDelayDebtNanoseconds = 0;
        std::optional<MonotonicInstant> mLastAdvance;
        std::optional<MonotonicInstant> mLastSnapshot;
        std::array<double, 3> mCorrection{};
        std::optional<MonotonicInstant> mCorrectionEnds;
    };
}

#endif
