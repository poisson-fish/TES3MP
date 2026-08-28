#include <tes3mp/observability.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    static_assert(!std::is_constructible_v<MetricObservation, std::string_view>);
    static_assert(!std::is_constructible_v<StructuredEvent, std::string_view>);
    static_assert(std::variant_size_v<StructuredEventPayload> == 5);

    MetricObservation metric(MetricKey key, MetricValue value, MetricDimensionValue outcome)
    {
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::ContractOutcome, outcome },
        };
        return MetricObservation::create(key, value, dimensions).value();
    }

    StructuredEvent event(ObservationResult result, std::uint64_t tick)
    {
        return StructuredEvent::create(
            EventSeverity::Warning, ServerTick::fromValue(tick), ContractObservationEvent{ result })
            .value();
    }

    bool metric_contract_is_closed_typed_and_dimension_bounded()
    {
        const auto counter = metric(MetricKey::ContractCounter, CounterAddition{ 7 }, MetricDimensionValue::Accepted);
        const auto gauge = metric(MetricKey::ContractGauge, GaugeValue{ -3 }, MetricDimensionValue::Dropped);
        const auto distribution
            = metric(MetricKey::ContractDistribution, DistributionSample{ 19 }, MetricDimensionValue::Accepted);
        if (counter.operation() != MetricOperation::CounterAdd || counter.unit() != MetricUnit::Count
            || std::get<CounterAddition>(counter.value()).amount != 7 || counter.dimensions().size() != 1
            || gauge.operation() != MetricOperation::GaugeSet || std::get<GaugeValue>(gauge.value()).value != -3
            || distribution.operation() != MetricOperation::DistributionObserve
            || distribution.unit() != MetricUnit::Nanoseconds)
            return false;

        if (MetricObservation::create(MetricKey::ContractCounter, GaugeValue{}))
            return false;
        if (MetricObservation::create(static_cast<MetricKey>(0), CounterAddition{}))
            return false;

        const std::array duplicateDimensions{
            MetricDimension{ MetricDimensionKey::ContractOutcome, MetricDimensionValue::Accepted },
            MetricDimension{ MetricDimensionKey::ContractOutcome, MetricDimensionValue::Dropped },
        };
        if (MetricObservation::create(MetricKey::ContractCounter, CounterAddition{}, duplicateDimensions))
            return false;

        const std::array<MetricDimension, MaximumMetricDimensions + 1> excessiveDimensions{};
        if (MetricObservation::create(MetricKey::ContractCounter, CounterAddition{}, excessiveDimensions))
            return false;

        const std::array invalidDimensions{
            MetricDimension{ static_cast<MetricDimensionKey>(0), MetricDimensionValue::Accepted },
        };
        return !MetricObservation::create(MetricKey::ContractCounter, CounterAddition{}, invalidDimensions);
    }

    bool structured_events_use_typed_payloads_and_semantic_time_only()
    {
        const auto observation = event(ObservationResult::Dropped, 42);
        if (observation.kind() != EventKind::ContractObservation || observation.severity() != EventSeverity::Warning
            || !observation.tick() || observation.tick()->value() != 42
            || std::get<ContractObservationEvent>(observation.payload()).result != ObservationResult::Dropped)
            return false;

        const auto lifecycle = StructuredEvent::create(EventSeverity::Info, std::nullopt,
            SessionLifecycleEvent{ SessionObservationRole::Server, SessionObservationOutcome::AuthenticationSucceeded,
                SessionObservationStage::Terminal });
        if (!lifecycle || lifecycle->kind() != EventKind::SessionLifecycle)
            return false;
        const auto* payload = std::get_if<SessionLifecycleEvent>(&lifecycle->payload());
        if (payload == nullptr || payload->role != SessionObservationRole::Server
            || payload->outcome != SessionObservationOutcome::AuthenticationSucceeded)
            return false;

        return !StructuredEvent::create(static_cast<EventSeverity>(255), std::nullopt, ContractObservationEvent{})
            && !StructuredEvent::create(EventSeverity::Info, std::nullopt,
                SessionLifecycleEvent{ static_cast<SessionObservationRole>(255),
                    SessionObservationOutcome::TransitionAccepted, SessionObservationStage::TransportAndNegotiation })
            && !StructuredEvent::create(EventSeverity::Info, std::nullopt,
                CommandReductionEvent{ static_cast<CommandReductionObservationOutcome>(255), 0 });
    }

    bool explicit_no_op_sinks_require_no_global_registry()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        const auto counter = metric(MetricKey::ContractCounter, CounterAddition{ 1 }, MetricDimensionValue::Accepted);
        const auto diagnostic = event(ObservationResult::Accepted, 0);
        return &observability.metrics() == &metrics && &observability.events() == &events
            && observability.metrics().tryRecord(counter) == ObservationResult::Accepted
            && observability.events().tryRecord(diagnostic) == ObservationResult::Accepted;
    }

    bool metric_recorder_has_exact_capacity_overflow_clear_and_reuse()
    {
        const auto first = metric(MetricKey::ContractCounter, CounterAddition{ 1 }, MetricDimensionValue::Accepted);
        const auto second = metric(MetricKey::ContractCounter, CounterAddition{ 2 }, MetricDimensionValue::Dropped);

        auto zero = RecordingMetricSink::create(0);
        if (!zero || zero->tryRecord(first) != ObservationResult::Dropped || zero->droppedCount() != 1
            || !zero->observations().empty())
            return false;

        auto one = RecordingMetricSink::create(1);
        if (!one || one->tryRecord(first) != ObservationResult::Accepted
            || one->tryRecord(second) != ObservationResult::Dropped || one->observations().size() != 1
            || one->observations().front() != first || one->droppedCount() != 1)
            return false;
        one->clear();
        if (!one->observations().empty() || one->droppedCount() != 0
            || one->tryRecord(second) != ObservationResult::Accepted || one->observations().front() != second)
            return false;

        return RecordingMetricSink::create(MaximumRecordedObservations)
            && !RecordingMetricSink::create(MaximumRecordedObservations + 1);
    }

    bool event_recorder_preserves_fifo_and_rejects_newest()
    {
        const auto first = event(ObservationResult::Accepted, 1);
        const auto second = event(ObservationResult::Dropped, 2);
        const auto rejected = event(ObservationResult::Accepted, 3);
        auto recorder = RecordingStructuredEventSink::create(2);
        if (!recorder || recorder->tryRecord(first) != ObservationResult::Accepted
            || recorder->tryRecord(second) != ObservationResult::Accepted
            || recorder->tryRecord(rejected) != ObservationResult::Dropped || recorder->events().size() != 2
            || recorder->events()[0] != first || recorder->events()[1] != second || recorder->droppedCount() != 1)
            return false;

        recorder->clear();
        return recorder->events().empty() && recorder->droppedCount() == 0
            && recorder->tryRecord(rejected) == ObservationResult::Accepted && recorder->events().front() == rejected;
    }

    bool identical_calls_produce_identical_observation_evidence()
    {
        auto firstMetrics = RecordingMetricSink::create(3);
        auto secondMetrics = RecordingMetricSink::create(3);
        auto firstEvents = RecordingStructuredEventSink::create(3);
        auto secondEvents = RecordingStructuredEventSink::create(3);
        const std::array metrics{
            metric(MetricKey::ContractCounter, CounterAddition{ 5 }, MetricDimensionValue::Accepted),
            metric(MetricKey::ContractGauge, GaugeValue{ -2 }, MetricDimensionValue::Dropped),
            metric(MetricKey::ContractDistribution, DistributionSample{ 99 }, MetricDimensionValue::Accepted),
        };
        const std::array events{
            event(ObservationResult::Accepted, 11),
            event(ObservationResult::Dropped, 12),
        };
        for (const auto& observation : metrics)
        {
            firstMetrics->tryRecord(observation);
            secondMetrics->tryRecord(observation);
        }
        for (const auto& observation : events)
        {
            firstEvents->tryRecord(observation);
            secondEvents->tryRecord(observation);
        }
        return std::equal(firstMetrics->observations().begin(), firstMetrics->observations().end(),
                   secondMetrics->observations().begin(), secondMetrics->observations().end())
            && std::equal(firstEvents->events().begin(), firstEvents->events().end(), secondEvents->events().begin(),
                secondEvents->events().end());
    }

    bool observation_failure_cannot_change_canonical_result()
    {
        auto accepted = RecordingMetricSink::create(1);
        auto dropped = RecordingMetricSink::create(0);
        const auto observation
            = metric(MetricKey::ContractCounter, CounterAddition{ 1 }, MetricDimensionValue::Accepted);
        const auto apply = [&observation](MetricSink& sink) {
            sink.tryRecord(observation);
            return std::uint64_t{ 0x5a17 };
        };
        return apply(*accepted) == apply(*dropped) && accepted->observations().size() == 1
            && dropped->observations().empty() && dropped->droppedCount() == 1;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "metric_contract_is_closed_typed_and_dimension_bounded",
            &metric_contract_is_closed_typed_and_dimension_bounded },
        std::pair{ "structured_events_use_typed_payloads_and_semantic_time_only",
            &structured_events_use_typed_payloads_and_semantic_time_only },
        std::pair{
            "explicit_no_op_sinks_require_no_global_registry", &explicit_no_op_sinks_require_no_global_registry },
        std::pair{ "metric_recorder_has_exact_capacity_overflow_clear_and_reuse",
            &metric_recorder_has_exact_capacity_overflow_clear_and_reuse },
        std::pair{
            "event_recorder_preserves_fifo_and_rejects_newest", &event_recorder_preserves_fifo_and_rejects_newest },
        std::pair{ "identical_calls_produce_identical_observation_evidence",
            &identical_calls_produce_identical_observation_evidence },
        std::pair{
            "observation_failure_cannot_change_canonical_result", &observation_failure_cannot_change_canonical_result },
    };
    for (const auto& [name, test] : tests)
    {
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
