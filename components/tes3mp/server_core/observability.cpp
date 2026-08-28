#include <tes3mp/observability.hpp>

#include <algorithm>

namespace
{
    constexpr bool validDimension(TES3MP::MetricDimension dimension) noexcept
    {
        using TES3MP::MetricDimensionKey;
        using TES3MP::MetricDimensionValue;
        switch (dimension.key)
        {
            case MetricDimensionKey::ContractOutcome:
                return dimension.value == MetricDimensionValue::Accepted
                    || dimension.value == MetricDimensionValue::Dropped;
            case MetricDimensionKey::SessionOutcome:
                return dimension.value == MetricDimensionValue::TransitionAccepted
                    || dimension.value == MetricDimensionValue::IllegalTransition
                    || dimension.value == MetricDimensionValue::AuthenticationSucceeded
                    || dimension.value == MetricDimensionValue::AuthenticationRejected
                    || dimension.value == MetricDimensionValue::TimedOut
                    || dimension.value == MetricDimensionValue::Cancelled
                    || dimension.value == MetricDimensionValue::StaleCompletion;
            case MetricDimensionKey::CommandIntakeOutcome:
                return dimension.value == MetricDimensionValue::CommandAccepted
                    || dimension.value == MetricDimensionValue::CommandPerSessionPendingLimit
                    || dimension.value == MetricDimensionValue::CommandGlobalPendingLimit
                    || dimension.value == MetricDimensionValue::CommandCoordinatorTerminated
                    || dimension.value == MetricDimensionValue::CommandDeferredByTickBudget
                    || dimension.value == MetricDimensionValue::CommandClockMovedBackwards
                    || dimension.value == MetricDimensionValue::CommandDeadlineOverflow
                    || dimension.value == MetricDimensionValue::CommandTickExhausted
                    || dimension.value == MetricDimensionValue::CommandIngressOrdinalExhausted;
            case MetricDimensionKey::CommandReductionOutcome:
                return dimension.value >= MetricDimensionValue::CommandReductionApplied
                    && dimension.value <= MetricDimensionValue::CommandReductionStateVersionCapacityExceeded;
            case MetricDimensionKey::CanonicalSinkRole:
                return dimension.value >= MetricDimensionValue::CanonicalSinkPersistence
                    && dimension.value <= MetricDimensionValue::CanonicalSinkMetrics;
            case MetricDimensionKey::CanonicalSinkDeliveryOutcome:
                return dimension.value >= MetricDimensionValue::CanonicalSinkAccepted
                    && dimension.value <= MetricDimensionValue::CanonicalSinkFailed;
        }
        return false;
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

    constexpr bool dimensionAllowed(TES3MP::MetricKey key, TES3MP::MetricDimensionKey dimension) noexcept
    {
        using TES3MP::MetricDimensionKey;
        using TES3MP::MetricKey;
        switch (key)
        {
            case MetricKey::ContractCounter:
            case MetricKey::ContractGauge:
            case MetricKey::ContractDistribution:
                return dimension == MetricDimensionKey::ContractOutcome;
            case MetricKey::SessionTransitions:
            case MetricKey::SessionAuthenticationOutcomes:
            case MetricKey::SessionTimeouts:
            case MetricKey::SessionStaleCompletions:
            case MetricKey::SessionCancellations:
                return dimension == MetricDimensionKey::SessionOutcome;
            case MetricKey::CommandIntakeOutcomes:
                return dimension == MetricDimensionKey::CommandIntakeOutcome;
            case MetricKey::CommandIntakePending:
                return false;
            case MetricKey::CommandReductionOutcomes:
                return dimension == MetricDimensionKey::CommandReductionOutcome;
            case MetricKey::CanonicalSinkDeliveries:
                return dimension == MetricDimensionKey::CanonicalSinkRole
                    || dimension == MetricDimensionKey::CanonicalSinkDeliveryOutcome;
        }
        return false;
    }

    constexpr bool validSeverity(TES3MP::EventSeverity severity) noexcept
    {
        using TES3MP::EventSeverity;
        return severity == EventSeverity::Debug || severity == EventSeverity::Info || severity == EventSeverity::Warning
            || severity == EventSeverity::Error;
    }

    constexpr bool validSessionEvent(TES3MP::SessionLifecycleEvent event) noexcept
    {
        using TES3MP::SessionObservationOutcome;
        using TES3MP::SessionObservationRole;
        using TES3MP::SessionObservationStage;
        const bool validRole
            = event.role == SessionObservationRole::Client || event.role == SessionObservationRole::Server;
        const bool validOutcome = event.outcome == SessionObservationOutcome::TransitionAccepted
            || event.outcome == SessionObservationOutcome::IllegalTransition
            || event.outcome == SessionObservationOutcome::AuthenticationSucceeded
            || event.outcome == SessionObservationOutcome::AuthenticationRejected
            || event.outcome == SessionObservationOutcome::TimedOut
            || event.outcome == SessionObservationOutcome::Cancelled
            || event.outcome == SessionObservationOutcome::StaleCompletion;
        const bool validStage = event.stage == SessionObservationStage::TransportAndNegotiation
            || event.stage == SessionObservationStage::AuthenticationInput
            || event.stage == SessionObservationStage::AuthenticationProvider
            || event.stage == SessionObservationStage::Terminal;
        return validRole && validOutcome && validStage;
    }

    constexpr bool validCommandIntakeEvent(TES3MP::CommandIntakeEvent event) noexcept
    {
        using TES3MP::CommandIntakeObservationOutcome;
        const auto outcome = event.outcome;
        return outcome == CommandIntakeObservationOutcome::Accepted
            || outcome == CommandIntakeObservationOutcome::PerSessionPendingLimit
            || outcome == CommandIntakeObservationOutcome::GlobalPendingLimit
            || outcome == CommandIntakeObservationOutcome::CoordinatorTerminated
            || outcome == CommandIntakeObservationOutcome::DeferredByTickBudget
            || outcome == CommandIntakeObservationOutcome::ClockMovedBackwards
            || outcome == CommandIntakeObservationOutcome::DeadlineOverflow
            || outcome == CommandIntakeObservationOutcome::TickExhausted
            || outcome == CommandIntakeObservationOutcome::IngressOrdinalExhausted;
    }

    constexpr bool validCommandReductionEvent(TES3MP::CommandReductionEvent event) noexcept
    {
        using TES3MP::CommandReductionObservationOutcome;
        return event.outcome >= CommandReductionObservationOutcome::Applied
            && event.outcome <= CommandReductionObservationOutcome::StateVersionCapacityExceeded;
    }

    constexpr bool validCanonicalSinkDeliveryEvent(TES3MP::CanonicalSinkDeliveryEvent event) noexcept
    {
        using TES3MP::CanonicalSinkObservationOutcome;
        using TES3MP::CanonicalSinkObservationRole;
        const bool validRole = event.role >= CanonicalSinkObservationRole::Persistence
            && event.role <= CanonicalSinkObservationRole::Metrics;
        const bool validOutcome = event.outcome >= CanonicalSinkObservationOutcome::Accepted
            && event.outcome <= CanonicalSinkObservationOutcome::Failed;
        return validRole && validOutcome;
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

        if (key == MetricKey::CanonicalSinkDeliveries)
        {
            if (dimensions.size() != 2)
                return std::nullopt;
            const bool hasRole = std::any_of(dimensions.begin(), dimensions.end(),
                [](MetricDimension dimension) { return dimension.key == MetricDimensionKey::CanonicalSinkRole; });
            const bool hasOutcome = std::any_of(dimensions.begin(), dimensions.end(), [](MetricDimension dimension) {
                return dimension.key == MetricDimensionKey::CanonicalSinkDeliveryOutcome;
            });
            if (!hasRole || !hasOutcome)
                return std::nullopt;
        }

        for (std::size_t index = 0; index < dimensions.size(); ++index)
        {
            if (!validDimension(dimensions[index]) || !dimensionAllowed(key, dimensions[index].key))
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
        if (const auto* session = std::get_if<SessionLifecycleEvent>(&payload); session && !validSessionEvent(*session))
            return std::nullopt;
        if (const auto* intake = std::get_if<CommandIntakeEvent>(&payload); intake && !validCommandIntakeEvent(*intake))
            return std::nullopt;
        if (const auto* reduction = std::get_if<CommandReductionEvent>(&payload);
            reduction && !validCommandReductionEvent(*reduction))
            return std::nullopt;
        if (const auto* delivery = std::get_if<CanonicalSinkDeliveryEvent>(&payload);
            delivery && !validCanonicalSinkDeliveryEvent(*delivery))
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
