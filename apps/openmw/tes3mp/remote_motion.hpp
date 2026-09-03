#ifndef OPENMW_TES3MP_REMOTE_MOTION_HPP
#define OPENMW_TES3MP_REMOTE_MOTION_HPP

#include <tes3mp/command_primitives.hpp>
#include <tes3mp/monotonic_clock.hpp>
#include <tes3mp/observability.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace TES3MP::OpenMWAdapter
{
    inline constexpr std::size_t MaximumRemoteMotionSamples = 4;
    inline constexpr std::uint64_t RemotePlaybackDelayTicks = 2;
    inline constexpr std::uint64_t MaximumRemoteExtrapolationTicks = 3;
    inline constexpr std::uint64_t RemoteCorrectionBlendNanoseconds = 66'666'667;
    inline constexpr std::uint64_t RemoteHardSnapDistanceQuanta = 16 * 1024;

    enum class RemoteMotionMetricKey : std::uint8_t
    {
        SnapshotAgeNanoseconds,
        BufferDepth,
        ExtrapolationNanoseconds,
        CorrectionDistanceQuanta,
        HardSnaps,
    };

    struct RemoteMotionMetric
    {
        RemoteMotionMetricKey key;
        std::uint64_t value;

        friend constexpr bool operator==(RemoteMotionMetric, RemoteMotionMetric) noexcept = default;
    };

    class RemoteMotionMetricSink
    {
    public:
        virtual ~RemoteMotionMetricSink() = default;
        virtual ObservationResult tryRecord(RemoteMotionMetric metric) noexcept = 0;
    };

    class NullRemoteMotionMetricSink final : public RemoteMotionMetricSink
    {
    public:
        ObservationResult tryRecord(RemoteMotionMetric) noexcept override { return ObservationResult::Accepted; }
    };

    struct RemoteMotionPose
    {
        CellId cell;
        double x;
        double y;
        double z;
        Orientation3 orientation;
    };

    class RemoteMotionBuffer
    {
    public:
        explicit RemoteMotionBuffer(RemoteMotionMetricSink& metrics) noexcept;

        bool observe(const SpatialEntitySnapshot& sample, MonotonicInstant receivedAt) noexcept;
        std::optional<RemoteMotionPose> advance(MonotonicInstant now) noexcept;
        void clear() noexcept;
        std::size_t sampleCount() const noexcept { return mSampleCount; }

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
        std::optional<MonotonicInstant> mLastAdvance;
        std::optional<MonotonicInstant> mLastSnapshot;
        std::array<double, 3> mCorrection{};
        std::optional<MonotonicInstant> mCorrectionEnds;
    };
}

#endif
