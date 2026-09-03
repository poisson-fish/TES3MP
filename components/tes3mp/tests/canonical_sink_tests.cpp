#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    constexpr std::uint64_t FirstTickDeadline = 33'333'334;
    constexpr std::uint64_t NextTickIncrement = 33'333'333;

    PlayerId playerId(std::uint64_t value)
    {
        return PlayerId::fromValue(value).value();
    }

    EntityId entityId(std::uint64_t value)
    {
        return EntityId::fromValue(value).value();
    }

    SessionId sessionId(std::uint64_t value)
    {
        return SessionId::fromValue(value).value();
    }

    Transform transform(std::uint64_t cell, std::int64_t position)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(CellSpaceId::fromValue(cell).value()),
            Position3(position, position + 1, position + 2), Orientation3(zero, zero, zero));
    }

    CanonicalPlayerEntityState player()
    {
        return CanonicalPlayerEntityState(playerId(1), entityId(101), transform(1, 100), LinearVelocity3(0, 0, 0),
            EntityRevision::fromValue(1).value(), AuthorityEpoch::fromValue(1).value(), ServerTick::initial());
    }

    CanonicalSessionProgress session()
    {
        return CanonicalSessionProgress(
            sessionId(10), SessionGeneration::fromValue(1).value(), playerId(1), entityId(101), std::nullopt);
    }

    CanonicalServerState initialState()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
    }

    ServerCommandProposal proposal(std::uint64_t sequence, std::uint64_t commandId, std::uint64_t revision,
        std::uint64_t session = 10, std::uint64_t epoch = 1)
    {
        return ServerCommandProposal(sessionId(session), SessionGeneration::fromValue(1).value(),
            CommandSequence::fromValue(sequence).value(), CommandId::fromValue(commandId).value(),
            CanonicalRevision::initial(),
            EntityPrecondition(
                entityId(101), EntityRevision::fromValue(revision).value(), AuthorityEpoch::fromValue(epoch).value()),
            PlayerMotionCommandProposal(
                LinearVelocity3(static_cast<std::int64_t>(sequence), -static_cast<std::int64_t>(sequence), 0)));
    }

    class IntakeFixture
    {
    public:
        IntakeFixture()
            : mClock(MonotonicInstant::fromNanoseconds(0))
            , mObservability(mMetrics, mEvents)
            , mIntake(mClock, mObservability, mClock.now(), ServerTick::fromValue(1).value(), IngressOrdinal::initial())
        {
        }

        CommandBatchReductionResult reduce(
            CanonicalCommandReducer& reducer, std::span<const ServerCommandProposal> proposals)
        {
            if (!std::all_of(proposals.begin(), proposals.end(), [this](const ServerCommandProposal& value) {
                    return mIntake.submit(value) == CommandSubmissionResult::Accepted;
                }))
                return {};
            mClock.advance(mFirstPump ? FirstTickDeadline : NextTickIncrement);
            mFirstPump = false;
            const auto pumped = mIntake.pump();
            if (!pumped || pumped.batches().size() != 1)
                return {};
            return reducer.apply(pumped.batches().front());
        }

    private:
        ManualClock mClock;
        NullMetricSink mMetrics;
        NullStructuredEventSink mEvents;
        Observability mObservability;
        ServerCommandIntakeCoordinator mIntake;
        bool mFirstPump = true;
    };

    struct CallOrder
    {
        std::array<CanonicalSinkRole, MaximumCanonicalSinkAttempts * 8> roles{};
        std::size_t size = 0;

        void push(CanonicalSinkRole role) noexcept
        {
            if (size < roles.size())
                roles[size++] = role;
        }
    };

    struct SinkProbe
    {
        CanonicalSinkDeliveryResult result = CanonicalSinkDeliveryResult::Accepted;
        CallOrder* order = nullptr;
        CanonicalCommandReducer* reducer = nullptr;
        std::array<std::shared_ptr<const CanonicalStatePublication>, 8> publications{};
        std::size_t count = 0;
        bool installedBeforeDelivery = true;

        CanonicalSinkDeliveryResult record(
            CanonicalSinkRole role, const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
        {
            if (order != nullptr)
                order->push(role);
            if (reducer != nullptr && reducer->latestPublication().get() != publication.get())
                installedBeforeDelivery = false;
            if (count < publications.size())
                publications[count] = publication;
            ++count;
            return result;
        }
    };

    class PersistenceProbe final : public CanonicalPersistenceSink
    {
    public:
        explicit PersistenceProbe(SinkProbe& probe)
            : mProbe(probe)
        {
        }
        CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept override
        {
            return mProbe.record(CanonicalSinkRole::Persistence, publication);
        }

    private:
        SinkProbe& mProbe;
    };

    class ReplayProbe final : public CanonicalReplaySink
    {
    public:
        explicit ReplayProbe(SinkProbe& probe)
            : mProbe(probe)
        {
        }
        CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept override
        {
            return mProbe.record(CanonicalSinkRole::Replay, publication);
        }

    private:
        SinkProbe& mProbe;
    };

    class ScriptProbe final : public CanonicalScriptSink
    {
    public:
        explicit ScriptProbe(SinkProbe& probe)
            : mProbe(probe)
        {
        }
        CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept override
        {
            return mProbe.record(CanonicalSinkRole::Script, publication);
        }

    private:
        SinkProbe& mProbe;
    };

    class MetricsProbe final : public CanonicalMetricsSink
    {
    public:
        explicit MetricsProbe(SinkProbe& probe)
            : mProbe(probe)
        {
        }
        CanonicalSinkDeliveryResult tryConsume(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept override
        {
            return mProbe.record(CanonicalSinkRole::Metrics, publication);
        }

    private:
        SinkProbe& mProbe;
    };

    struct FourProbes
    {
        explicit FourProbes(CallOrder* order = nullptr)
            : persistencePort(persistence)
            , replayPort(replay)
            , scriptPort(script)
            , metricsPort(metrics)
        {
            persistence.order = order;
            replay.order = order;
            script.order = order;
            metrics.order = order;
        }

        CanonicalSinkBundle bundle()
        {
            return CanonicalSinkBundle(&persistencePort, &replayPort, &scriptPort, &metricsPort);
        }

        SinkProbe persistence;
        SinkProbe replay;
        SinkProbe script;
        SinkProbe metrics;
        PersistenceProbe persistencePort;
        ReplayProbe replayPort;
        ScriptProbe scriptPort;
        MetricsProbe metricsPort;
    };

    bool server_core_sink_ports_are_nominal_bounded_and_backend_free()
    {
        static_assert(MaximumCanonicalSinkAttempts == 4);
        static_assert(std::is_abstract_v<CanonicalPersistenceSink>);
        static_assert(std::is_abstract_v<CanonicalReplaySink>);
        static_assert(std::is_abstract_v<CanonicalScriptSink>);
        static_assert(std::is_abstract_v<CanonicalMetricsSink>);
        static_assert(!std::is_base_of_v<CanonicalPersistenceSink, CanonicalReplaySink>);
        static_assert(sizeof(CanonicalSinkBundle) == sizeof(void*) * MaximumCanonicalSinkAttempts);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalSinkBundle&>().persistence()),
            CanonicalPersistenceSink*>);
        return true;
    }

    bool construction_and_noncommitting_batches_deliver_nothing()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        const auto initial = reducer.latestPublication();
        if (probes.persistence.count != 0 || probes.replay.count != 0 || probes.script.count != 0
            || probes.metrics.count != 0)
            return false;
        IntakeFixture intake;
        const std::array unknown{ proposal(1, 1001, 1, 999) };
        const auto result = intake.reduce(reducer, unknown);
        return result && !result.sinkDeliveryReport().publicationOffered() && reducer.latestPublication() == initial
            && probes.persistence.count == 0 && probes.replay.count == 0 && probes.script.count == 0
            && probes.metrics.count == 0;
    }

    bool applied_commit_installs_latest_before_same_handle_reaches_all_roles()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        probes.persistence.reducer = &reducer;
        probes.replay.reducer = &reducer;
        probes.script.reducer = &reducer;
        probes.metrics.reducer = &reducer;
        IntakeFixture intake;
        const std::array command{ proposal(1, 1001, 1) };
        const auto result = intake.reduce(reducer, command);
        const auto latest = reducer.latestPublication();
        const auto report = result.sinkDeliveryReport();
        return result && report.publicationOffered()
            && report.result(CanonicalSinkRole::Persistence) == CanonicalSinkDeliveryResult::Accepted
            && report.result(CanonicalSinkRole::Replay) == CanonicalSinkDeliveryResult::Accepted
            && report.result(CanonicalSinkRole::Script) == CanonicalSinkDeliveryResult::Accepted
            && report.result(CanonicalSinkRole::Metrics) == CanonicalSinkDeliveryResult::Accepted
            && probes.persistence.publications[0].get() == latest.get()
            && probes.replay.publications[0].get() == latest.get()
            && probes.script.publications[0].get() == latest.get()
            && probes.metrics.publications[0].get() == latest.get() && probes.persistence.installedBeforeDelivery
            && probes.replay.installedBeforeDelivery && probes.script.installedBeforeDelivery
            && probes.metrics.installedBeforeDelivery;
    }

    bool acknowledgement_only_commit_uses_the_same_delivery_path()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array first{ proposal(1, 1001, 1) };
        const std::array duplicate{ proposal(2, 1001, 2) };
        if (!intake.reduce(reducer, first))
            return false;
        const CanonicalPlayerEntityState playerBefore = reducer.state().players().front();
        const auto result = intake.reduce(reducer, duplicate);
        const auto publication = reducer.latestPublication();
        return result && result.dispositions().front().disposition() == CommandDisposition::DuplicateCommandId
            && publication->changes().size() == 1
            && publication->changes().front().disposition() == CommandDisposition::DuplicateCommandId
            && !publication->changes().front().playerReplacement() && reducer.state().players().front() == playerBefore
            && probes.persistence.count == 2 && probes.replay.count == 2 && probes.script.count == 2
            && probes.metrics.count == 2 && probes.persistence.publications[1].get() == publication.get()
            && probes.metrics.publications[1].get() == publication.get();
    }

    bool multiple_commits_produce_one_exact_ordered_batch_delivery()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array commands{ proposal(1, 1001, 1), proposal(2, 1002, 2) };
        const auto result = intake.reduce(reducer, commands);
        const auto publication = reducer.latestPublication();
        return result && probes.persistence.count == 1 && probes.replay.count == 1 && probes.script.count == 1
            && probes.metrics.count == 1 && publication->changes().size() == 2
            && publication->changes()[0].stateVersion().value() == 1
            && publication->changes()[1].stateVersion().value() == 2 && publication->stateVersion().value() == 2
            && publication->state() == reducer.state()
            && publication->checksum()
            == canonicalStateChecksumV1(publication->stateVersion(), publication->checkpointTick(), reducer.state());
    }

    bool fanout_order_is_persistence_replay_script_metrics()
    {
        CallOrder order;
        FourProbes probes(&order);
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array command{ proposal(1, 1001, 1) };
        return intake.reduce(reducer, command) && order.size == 4 && order.roles[0] == CanonicalSinkRole::Persistence
            && order.roles[1] == CanonicalSinkRole::Replay && order.roles[2] == CanonicalSinkRole::Script
            && order.roles[3] == CanonicalSinkRole::Metrics;
    }

    bool mixed_results_are_complete_and_do_not_short_circuit_later_roles()
    {
        CallOrder order;
        FourProbes probes(&order);
        probes.persistence.result = CanonicalSinkDeliveryResult::Accepted;
        probes.replay.result = CanonicalSinkDeliveryResult::Backpressured;
        probes.script.result = CanonicalSinkDeliveryResult::Failed;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalSinkBundle bundle(&probes.persistencePort, &probes.replayPort, &probes.scriptPort, nullptr);
        CanonicalCommandReducer reducer(initialState(), observability, bundle);
        IntakeFixture intake;
        const std::array command{ proposal(1, 1001, 1) };
        const auto result = intake.reduce(reducer, command);
        const auto report = result.sinkDeliveryReport();
        return result && order.size == 3
            && report.result(CanonicalSinkRole::Persistence) == CanonicalSinkDeliveryResult::Accepted
            && report.result(CanonicalSinkRole::Replay) == CanonicalSinkDeliveryResult::Backpressured
            && report.result(CanonicalSinkRole::Script) == CanonicalSinkDeliveryResult::Failed
            && report.result(CanonicalSinkRole::Metrics) == CanonicalSinkDeliveryResult::NotConfigured
            && reducer.stateVersion().value() == 1;
    }

    bool invalid_configured_result_is_failed_and_later_roles_still_run()
    {
        CallOrder order;
        FourProbes probes(&order);
        probes.persistence.result = CanonicalSinkDeliveryResult::NotConfigured;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array command{ proposal(1, 1001, 1) };
        const auto result = intake.reduce(reducer, command);
        return result && order.size == 4
            && result.sinkDeliveryReport().result(CanonicalSinkRole::Persistence) == CanonicalSinkDeliveryResult::Failed
            && probes.metrics.count == 1;
    }

    bool sink_and_observability_outcomes_cannot_change_canonical_results()
    {
        FourProbes accepted;
        FourProbes failed;
        failed.persistence.result = CanonicalSinkDeliveryResult::Failed;
        failed.replay.result = CanonicalSinkDeliveryResult::Backpressured;
        failed.script.result = CanonicalSinkDeliveryResult::Failed;
        failed.metrics.result = CanonicalSinkDeliveryResult::Backpressured;
        auto acceptedMetrics = RecordingMetricSink::create(32);
        auto acceptedEvents = RecordingStructuredEventSink::create(32);
        auto droppedMetrics = RecordingMetricSink::create(0);
        auto droppedEvents = RecordingStructuredEventSink::create(0);
        Observability acceptedObservability(*acceptedMetrics, *acceptedEvents);
        Observability droppedObservability(*droppedMetrics, *droppedEvents);
        CanonicalCommandReducer first(initialState(), acceptedObservability, accepted.bundle());
        CanonicalCommandReducer second(initialState(), droppedObservability, failed.bundle());
        IntakeFixture firstIntake;
        IntakeFixture secondIntake;
        const std::array command{ proposal(1, 1001, 1) };
        const auto firstResult = firstIntake.reduce(first, command);
        const auto secondResult = secondIntake.reduce(second, command);
        return firstResult.error() == secondResult.error()
            && std::equal(firstResult.dispositions().begin(), firstResult.dispositions().end(),
                secondResult.dispositions().begin(), secondResult.dispositions().end())
            && first.state() == second.state() && first.stateVersion() == second.stateVersion()
            && *first.latestPublication() == *second.latestPublication() && acceptedMetrics->observations().size() == 5
            && acceptedEvents->events().size() == 5 && droppedMetrics->droppedCount() == 5
            && droppedEvents->droppedCount() == 5;
    }

    bool closed_sink_delivery_observations_preserve_role_and_outcome()
    {
        FourProbes probes;
        probes.persistence.result = CanonicalSinkDeliveryResult::Accepted;
        probes.replay.result = CanonicalSinkDeliveryResult::Backpressured;
        probes.script.result = CanonicalSinkDeliveryResult::Failed;
        auto metrics = RecordingMetricSink::create(32);
        auto events = RecordingStructuredEventSink::create(32);
        Observability observability(*metrics, *events);
        CanonicalCommandReducer reducer(initialState(), observability,
            CanonicalSinkBundle(&probes.persistencePort, &probes.replayPort, &probes.scriptPort, nullptr));
        IntakeFixture intake;
        const std::array command{ proposal(1, 1001, 1) };
        if (!intake.reduce(reducer, command) || metrics->observations().size() != 4 || events->events().size() != 4)
            return false;
        const auto& persistence = std::get<CanonicalSinkDeliveryEvent>(events->events()[1].payload());
        const auto& replay = std::get<CanonicalSinkDeliveryEvent>(events->events()[2].payload());
        const auto& script = std::get<CanonicalSinkDeliveryEvent>(events->events()[3].payload());
        return persistence.role == CanonicalSinkObservationRole::Persistence
            && persistence.outcome == CanonicalSinkObservationOutcome::Accepted
            && replay.role == CanonicalSinkObservationRole::Replay
            && replay.outcome == CanonicalSinkObservationOutcome::Backpressured
            && script.role == CanonicalSinkObservationRole::Script
            && script.outcome == CanonicalSinkObservationOutcome::Failed;
    }

    bool retained_old_handle_is_immutable_and_cannot_delay_latest_replacement()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array first{ proposal(1, 1001, 1) };
        const std::array second{ proposal(2, 1002, 2) };
        if (!intake.reduce(reducer, first))
            return false;
        const auto retained = probes.persistence.publications[0];
        if (!intake.reduce(reducer, second))
            return false;
        const auto latest = reducer.latestPublication();
        return retained->stateVersion().value() == 1 && latest->stateVersion().value() == 2
            && retained.get() != latest.get() && retained->changes().size() == 1 && latest->changes().size() == 1
            && probes.persistence.count == 2;
    }

    bool missed_publication_is_a_gap_without_core_retry_or_retention()
    {
        FourProbes probes;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initialState(), observability, probes.bundle());
        IntakeFixture intake;
        const std::array first{ proposal(1, 1001, 1) };
        const std::array second{ proposal(2, 1002, 2) };
        const std::array third{ proposal(3, 1003, 3) };
        if (!intake.reduce(reducer, first))
            return false;
        const auto firstPublication = reducer.latestPublication();
        probes.persistence.result = CanonicalSinkDeliveryResult::Backpressured;
        if (!intake.reduce(reducer, second))
            return false;
        probes.persistence.result = CanonicalSinkDeliveryResult::Accepted;
        if (!intake.reduce(reducer, third))
            return false;
        const auto latest = reducer.latestPublication();
        return probes.persistence.count == 3 && latest->changes().size() == 1
            && latest->changes().front().stateVersion().value() == 3
            && classifyCanonicalPublication(firstPublication->stateVersion(), *latest)
            == CanonicalPublicationReadAction::ReplaceFromSnapshot;
    }

    bool public_sink_surface_exposes_only_immutable_domain_values()
    {
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalSinkDeliveryReport&>().result(
                                         CanonicalSinkRole::Persistence)),
            CanonicalSinkDeliveryResult>);
        static_assert(std::is_same_v<decltype(std::declval<CanonicalPersistenceSink&>().tryConsume(
                                         std::declval<const std::shared_ptr<const CanonicalStatePublication>&>())),
            CanonicalSinkDeliveryResult>);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalStatePublication&>().state()),
            const CanonicalServerState&>);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalStatePublication&>().changes()),
            std::span<const CanonicalStateChangeRecord>>);
        static_assert(!std::is_default_constructible_v<CanonicalStatePublication>);
        return true;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "server_core_sink_ports_are_nominal_bounded_and_backend_free",
            &server_core_sink_ports_are_nominal_bounded_and_backend_free },
        std::pair{ "construction_and_noncommitting_batches_deliver_nothing",
            &construction_and_noncommitting_batches_deliver_nothing },
        std::pair{ "applied_commit_installs_latest_before_same_handle_reaches_all_roles",
            &applied_commit_installs_latest_before_same_handle_reaches_all_roles },
        std::pair{ "acknowledgement_only_commit_uses_the_same_delivery_path",
            &acknowledgement_only_commit_uses_the_same_delivery_path },
        std::pair{ "multiple_commits_produce_one_exact_ordered_batch_delivery",
            &multiple_commits_produce_one_exact_ordered_batch_delivery },
        std::pair{
            "fanout_order_is_persistence_replay_script_metrics", &fanout_order_is_persistence_replay_script_metrics },
        std::pair{ "mixed_results_are_complete_and_do_not_short_circuit_later_roles",
            &mixed_results_are_complete_and_do_not_short_circuit_later_roles },
        std::pair{ "invalid_configured_result_is_failed_and_later_roles_still_run",
            &invalid_configured_result_is_failed_and_later_roles_still_run },
        std::pair{ "sink_and_observability_outcomes_cannot_change_canonical_results",
            &sink_and_observability_outcomes_cannot_change_canonical_results },
        std::pair{ "closed_sink_delivery_observations_preserve_role_and_outcome",
            &closed_sink_delivery_observations_preserve_role_and_outcome },
        std::pair{ "retained_old_handle_is_immutable_and_cannot_delay_latest_replacement",
            &retained_old_handle_is_immutable_and_cannot_delay_latest_replacement },
        std::pair{ "missed_publication_is_a_gap_without_core_retry_or_retention",
            &missed_publication_is_a_gap_without_core_retry_or_retention },
        std::pair{ "public_sink_surface_exposes_only_immutable_domain_values",
            &public_sink_surface_exposes_only_immutable_domain_values },
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
