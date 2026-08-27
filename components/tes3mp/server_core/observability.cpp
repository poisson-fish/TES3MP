#include <tes3mp/observability.hpp>

#include <algorithm>

namespace
{
    constexpr bool validDimension(TES3MP::MetricDimension dimension) noexcept
    {
        if (dimension.key != TES3MP::MetricDimensionKey::ContractOutcome)
            return false;
        return dimension.value == TES3MP::MetricDimensionValue::Accepted
            || dimension.value == TES3MP::MetricDimensionValue::Dropped;
    }

    constexpr bool valueMatches(TES3MP::MetricOperation operation, const TES3MP::MetricValue& value) noexcept
    {
        switch (operation)
        {
            case TES3MP::MetricOperation::CounterAdd:
                return std::holds_alternative<TES3MP::CounterAddition>(value);
            case TES3MP::MetricOperation::GaugeSet:
                return std::holds_alternative<TES3MP::GaugeValue>(value);
            case TES3MP::MetricOperation::DistributionObserve:
                return std::holds_alternative<TES3MP::DistributionSample>(value);
        }
        return false;
    }

    constexpr bool validSeverity(TES3MP::EventSeverity severity) noexcept
    {
        using TES3MP::EventSeverity;
        return severity == EventSeverity::Debug || severity == EventSeverity::Info || severity == EventSeverity::Warning
            || severity == EventSeverity::Error;
    }
}

namespace TES3MP
{
    std::optional<MetricObservation> MetricObservation::create(
        MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions) noexcept
    {
        const auto definition = metricDefinition(key);
        if (!definition || !valueMatches(definition->operation, value) || dimensions.size() > MaximumMetricDimensions)
            return std::nullopt;

        for (std::size_t index = 0; index < dimensions.size(); ++index)
        {
            if (!validDimension(dimensions[index]))
                return std::nullopt;
            if (std::find_if(dimensions.begin(), dimensions.begin() + index,
                    [key = dimensions[index].key](MetricDimension dimension) { return dimension.key == key; })
                != dimensions.begin() + index)
                return std::nullopt;
        }
        return MetricObservation(key, value, dimensions);
    }

    MetricObservation::MetricObservation(
        MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions) noexcept
        : mKey(key)
        , mValue(value)
        , mDimensionCount(dimensions.size())
    {
        std::copy(dimensions.begin(), dimensions.end(), mDimensions.begin());
    }

    std::optional<StructuredEvent> StructuredEvent::create(
        EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept
    {
        if (!validSeverity(severity))
            return std::nullopt;
        return StructuredEvent(severity, tick, payload);
    }

    StructuredEvent::StructuredEvent(
        EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept
        : mSeverity(severity)
        , mTick(tick)
        , mPayload(payload)
    {
    }
}
