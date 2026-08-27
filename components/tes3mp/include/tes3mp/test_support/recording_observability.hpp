#ifndef TES3MP_TEST_SUPPORT_RECORDING_OBSERVABILITY_HPP
#define TES3MP_TEST_SUPPORT_RECORDING_OBSERVABILITY_HPP

#include <tes3mp/observability.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace TES3MP::TestSupport
{
    inline constexpr std::size_t MaximumRecordedObservations = 1024;

    class RecordingMetricSink final : public MetricSink
    {
    public:
        static std::unique_ptr<RecordingMetricSink> create(std::size_t capacity);

        ObservationResult tryRecord(const MetricObservation& observation) noexcept override;
        std::span<const MetricObservation> observations() const noexcept { return mObservations; }
        std::size_t capacity() const noexcept { return mCapacity; }
        std::uint64_t droppedCount() const noexcept { return mDroppedCount; }
        void clear() noexcept;

    private:
        explicit RecordingMetricSink(std::size_t capacity);

        std::vector<MetricObservation> mObservations;
        std::size_t mCapacity;
        std::uint64_t mDroppedCount = 0;
    };

    class RecordingStructuredEventSink final : public StructuredEventSink
    {
    public:
        static std::unique_ptr<RecordingStructuredEventSink> create(std::size_t capacity);

        ObservationResult tryRecord(const StructuredEvent& event) noexcept override;
        std::span<const StructuredEvent> events() const noexcept { return mEvents; }
        std::size_t capacity() const noexcept { return mCapacity; }
        std::uint64_t droppedCount() const noexcept { return mDroppedCount; }
        void clear() noexcept;

    private:
        explicit RecordingStructuredEventSink(std::size_t capacity);

        std::vector<StructuredEvent> mEvents;
        std::size_t mCapacity;
        std::uint64_t mDroppedCount = 0;
    };
}

#endif
