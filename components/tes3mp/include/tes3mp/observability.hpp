#ifndef TES3MP_OBSERVABILITY_HPP
#define TES3MP_OBSERVABILITY_HPP

#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace TES3MP
{
    inline constexpr std::size_t MaximumMetricDimensions = 4;

    enum class ObservationResult : std::uint8_t
    {
        Accepted,
        Dropped,
    };

    enum class MetricOperation : std::uint8_t
    {
        CounterAdd,
        GaugeSet,
        DistributionObserve,
    };

    enum class MetricUnit : std::uint8_t
    {
        Count,
        Nanoseconds,
    };

    // Slice 3.7 contract fixtures. Real metric keys land with their owning behavior.
    enum class MetricKey : std::uint16_t
    {
        ContractCounter = 1,
        ContractGauge = 2,
        ContractDistribution = 3,
    };

    struct MetricDefinition
    {
        MetricOperation operation;
        MetricUnit unit;

        friend constexpr bool operator==(MetricDefinition, MetricDefinition) noexcept = default;
    };

    constexpr std::optional<MetricDefinition> metricDefinition(MetricKey key) noexcept
    {
        switch (key)
        {
            case MetricKey::ContractCounter:
                return MetricDefinition{ MetricOperation::CounterAdd, MetricUnit::Count };
            case MetricKey::ContractGauge:
                return MetricDefinition{ MetricOperation::GaugeSet, MetricUnit::Count };
            case MetricKey::ContractDistribution:
                return MetricDefinition{ MetricOperation::DistributionObserve, MetricUnit::Nanoseconds };
        }
        return std::nullopt;
    }

    enum class MetricDimensionKey : std::uint8_t
    {
        ContractOutcome = 1,
    };

    enum class MetricDimensionValue : std::uint8_t
    {
        Accepted = 1,
        Dropped = 2,
    };

    struct MetricDimension
    {
        MetricDimensionKey key = MetricDimensionKey::ContractOutcome;
        MetricDimensionValue value = MetricDimensionValue::Accepted;

        friend constexpr bool operator==(MetricDimension, MetricDimension) noexcept = default;
    };

    struct CounterAddition
    {
        std::uint64_t amount = 0;

        friend constexpr bool operator==(CounterAddition, CounterAddition) noexcept = default;
    };

    struct GaugeValue
    {
        std::int64_t value = 0;

        friend constexpr bool operator==(GaugeValue, GaugeValue) noexcept = default;
    };

    struct DistributionSample
    {
        std::uint64_t value = 0;

        friend constexpr bool operator==(DistributionSample, DistributionSample) noexcept = default;
    };

    using MetricValue = std::variant<CounterAddition, GaugeValue, DistributionSample>;

    class MetricObservation
    {
    public:
        static std::optional<MetricObservation> create(
            MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions = {}) noexcept;

        constexpr MetricKey key() const noexcept { return mKey; }
        constexpr MetricOperation operation() const noexcept { return metricDefinition(mKey)->operation; }
        constexpr MetricUnit unit() const noexcept { return metricDefinition(mKey)->unit; }
        constexpr const MetricValue& value() const noexcept { return mValue; }
        constexpr std::span<const MetricDimension> dimensions() const noexcept
        {
            return { mDimensions.data(), mDimensionCount };
        }

        friend constexpr bool operator==(const MetricObservation&, const MetricObservation&) noexcept = default;

    private:
        MetricObservation(MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions) noexcept;

        MetricKey mKey;
        MetricValue mValue;
        std::array<MetricDimension, MaximumMetricDimensions> mDimensions{};
        std::size_t mDimensionCount = 0;
    };

    enum class EventSeverity : std::uint8_t
    {
        Debug,
        Info,
        Warning,
        Error,
    };

    enum class EventKind : std::uint16_t
    {
        ContractObservation = 1,
    };

    struct ContractObservationEvent
    {
        ObservationResult result = ObservationResult::Accepted;

        friend constexpr bool operator==(ContractObservationEvent, ContractObservationEvent) noexcept = default;
    };

    using StructuredEventPayload = std::variant<ContractObservationEvent>;

    class StructuredEvent
    {
    public:
        static std::optional<StructuredEvent> create(
            EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept;

        constexpr EventKind kind() const noexcept { return EventKind::ContractObservation; }
        constexpr EventSeverity severity() const noexcept { return mSeverity; }
        constexpr std::optional<ServerTick> tick() const noexcept { return mTick; }
        constexpr const StructuredEventPayload& payload() const noexcept { return mPayload; }

        friend constexpr bool operator==(const StructuredEvent&, const StructuredEvent&) noexcept = default;

    private:
        StructuredEvent(
            EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept;

        EventSeverity mSeverity;
        std::optional<ServerTick> mTick;
        StructuredEventPayload mPayload;
    };

    class MetricSink
    {
    public:
        virtual ~MetricSink() = default;
        virtual ObservationResult tryRecord(const MetricObservation& observation) noexcept = 0;
    };

    class StructuredEventSink
    {
    public:
        virtual ~StructuredEventSink() = default;
        virtual ObservationResult tryRecord(const StructuredEvent& event) noexcept = 0;
    };

    class NullMetricSink final : public MetricSink
    {
    public:
        ObservationResult tryRecord(const MetricObservation&) noexcept override { return ObservationResult::Accepted; }
    };

    class NullStructuredEventSink final : public StructuredEventSink
    {
    public:
        ObservationResult tryRecord(const StructuredEvent&) noexcept override { return ObservationResult::Accepted; }
    };

    class Observability
    {
    public:
        constexpr Observability(MetricSink& metrics, StructuredEventSink& events) noexcept
            : mMetrics(metrics)
            , mEvents(events)
        {
        }

        constexpr MetricSink& metrics() const noexcept { return mMetrics; }
        constexpr StructuredEventSink& events() const noexcept { return mEvents; }

    private:
        MetricSink& mMetrics;
        StructuredEventSink& mEvents;
    };
}

#endif
