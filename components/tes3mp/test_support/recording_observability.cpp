#include <tes3mp/test_support/recording_observability.hpp>

#include <limits>

namespace
{
    void saturatingIncrement(std::uint64_t& value) noexcept
    {
        if (value != std::numeric_limits<std::uint64_t>::max())
            ++value;
    }
}

namespace TES3MP::TestSupport
{
    std::unique_ptr<RecordingMetricSink> RecordingMetricSink::create(std::size_t capacity)
    {
        if (capacity > MaximumRecordedObservations)
            return {};
        return std::unique_ptr<RecordingMetricSink>(new RecordingMetricSink(capacity));
    }

    RecordingMetricSink::RecordingMetricSink(std::size_t capacity)
        : mCapacity(capacity)
    {
        mObservations.reserve(capacity);
    }

    ObservationResult RecordingMetricSink::tryRecord(const MetricObservation& observation) noexcept
    {
        if (mObservations.size() == mCapacity)
        {
            saturatingIncrement(mDroppedCount);
            return ObservationResult::Dropped;
        }
        mObservations.push_back(observation);
        return ObservationResult::Accepted;
    }

    void RecordingMetricSink::clear() noexcept
    {
        mObservations.clear();
        mDroppedCount = 0;
    }

    std::unique_ptr<RecordingStructuredEventSink> RecordingStructuredEventSink::create(std::size_t capacity)
    {
        if (capacity > MaximumRecordedObservations)
            return {};
        return std::unique_ptr<RecordingStructuredEventSink>(new RecordingStructuredEventSink(capacity));
    }

    RecordingStructuredEventSink::RecordingStructuredEventSink(std::size_t capacity)
        : mCapacity(capacity)
    {
        mEvents.reserve(capacity);
    }

    ObservationResult RecordingStructuredEventSink::tryRecord(const StructuredEvent& event) noexcept
    {
        if (mEvents.size() == mCapacity)
        {
            saturatingIncrement(mDroppedCount);
            return ObservationResult::Dropped;
        }
        mEvents.push_back(event);
        return ObservationResult::Accepted;
    }

    void RecordingStructuredEventSink::clear() noexcept
    {
        mEvents.clear();
        mDroppedCount = 0;
    }
}
