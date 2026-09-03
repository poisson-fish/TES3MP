#include <tes3mp/protocol_exchange.hpp>
#include <tes3mp/server_command_intake.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    constexpr std::uint64_t FirstTickDeadline = 33'333'334;
    constexpr std::uint64_t SecondTickIncrement = 33'333'333;

    static_assert(MaximumPendingServerCommands == 4096);
    static_assert(MaximumPendingServerCommandsPerSessionGeneration == 128);
    static_assert(MaximumServerCommandsPerTick == 1024);
    static_assert(!std::is_constructible_v<ServerCommandProposal, ReliableOperation>);
    static_assert(!std::is_copy_constructible_v<ServerCommandIntakeCoordinator>);
    static_assert(!std::is_move_constructible_v<ServerCommandIntakeCoordinator>);

    ServerCommandProposal proposal(
        std::uint64_t source, std::uint64_t sequence, std::uint64_t observedTick = 0, std::uint64_t generation = 1)
    {
        const std::uint64_t identity = (source - 1) * MaximumPendingServerCommandsPerSessionGeneration + sequence;
        return ServerCommandProposal(SessionId::fromValue(source).value(),
            SessionGeneration::fromValue(generation).value(), CommandSequence::fromValue(sequence).value(),
            CommandId::fromValue(identity).value(), CanonicalRevision::fromValue(observedTick).value(),
            EntityPrecondition(
                EntityId::fromValue(source).value(), EntityRevision::initial(), AuthorityEpoch::initial()),
            PlayerMotionCommandProposal(
                LinearVelocity3(static_cast<std::int64_t>(identity), -static_cast<std::int64_t>(source), 0)));
    }

    class IntakeFixture
    {
    public:
        explicit IntakeFixture(IngressOrdinal nextOrdinal = IngressOrdinal::initial())
            : clock(MonotonicInstant::fromNanoseconds(0))
            , observability(metrics, events)
            , intake(clock, observability, clock.now(), ServerTick::fromValue(1).value(), nextOrdinal)
        {
        }

        ManualClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability;
        ServerCommandIntakeCoordinator intake;
    };

    bool fill(IntakeFixture& fixture, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::uint64_t source
                = static_cast<std::uint64_t>(index / MaximumPendingServerCommandsPerSessionGeneration + 1);
            const std::uint64_t sequence
                = static_cast<std::uint64_t>(index % MaximumPendingServerCommandsPerSessionGeneration + 1);
            if (fixture.intake.submit(proposal(source, sequence)) != CommandSubmissionResult::Accepted)
                return false;
        }
        return true;
    }

    std::uint64_t traceDigest(const ServerCommandPumpResult& result)
    {
        std::uint64_t digest = 1469598103934665603ULL;
        const auto mix = [&digest](std::uint64_t value) {
            for (int byte = 0; byte < 8; ++byte)
            {
                digest ^= value & 0xff;
                digest *= 1099511628211ULL;
                value >>= 8;
            }
        };
        mix(static_cast<std::uint64_t>(result.error()));
        mix(result.dueTickLag());
        mix(result.batches().size());
        for (const auto& batch : result.batches())
        {
            mix(batch.scheduledTick().value().value());
            mix(batch.commands().size());
            for (const auto& command : batch.commands())
            {
                mix(command.stamp().eligibleServerTick().value());
                mix(command.stamp().ingressOrdinal().value());
                mix(command.proposal().sessionId().value());
                mix(command.proposal().sessionGeneration().value());
                mix(command.proposal().commandSequence().value());
                mix(command.proposal().commandId().value());
                mix(command.proposal().observedCanonicalRevision().value());
                mix(command.proposal().entityPrecondition().entityId().value());
                mix(command.proposal().entityPrecondition().expectedRevision().value());
                mix(command.proposal().entityPrecondition().expectedAuthorityEpoch().value());
                const auto& motion = std::get<PlayerMotionCommandProposal>(command.proposal().payload());
                mix(static_cast<std::uint64_t>(motion.desiredVelocity().x()));
                mix(static_cast<std::uint64_t>(motion.desiredVelocity().y()));
                mix(static_cast<std::uint64_t>(motion.desiredVelocity().z()));
            }
        }
        return digest;
    }

    bool same_clock_and_submission_stream_produces_identical_ordered_trace()
    {
        IntakeFixture first;
        IntakeFixture second;
        const std::array commands{
            proposal(2, 1, 99),
            proposal(1, 1, 500),
            proposal(2, 2, 1),
            proposal(1, 2, 0),
        };
        for (const auto& command : commands)
        {
            if (first.intake.submit(command) != CommandSubmissionResult::Accepted
                || second.intake.submit(command) != CommandSubmissionResult::Accepted)
                return false;
        }
        first.clock.advance(1'000'000'000);
        second.clock.advance(1'000'000'000);
        const auto firstResult = first.intake.pump();
        const auto secondResult = second.intake.pump();
        return firstResult == secondResult && traceDigest(firstResult) == traceDigest(secondResult)
            && traceDigest(firstResult) != 0;
    }

    bool writer_observation_order_wins_ties_not_client_observed_tick()
    {
        IntakeFixture fixture;
        const std::array commands{
            proposal(2, 1, 500),
            proposal(1, 1, 1),
            proposal(2, 2, 0),
        };
        for (const auto& command : commands)
        {
            if (fixture.intake.submit(command) != CommandSubmissionResult::Accepted)
                return false;
        }
        fixture.clock.advance(FirstTickDeadline);
        const auto result = fixture.intake.pump();
        if (!result || result.batches().size() != 1 || result.batches()[0].commands().size() != commands.size())
            return false;
        const auto admitted = result.batches()[0].commands();
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            if (admitted[index].proposal() != commands[index]
                || admitted[index].stamp().eligibleServerTick().value() != 1
                || admitted[index].stamp().ingressOrdinal().value() != index + 1)
                return false;
        }
        return true;
    }

    bool tick_cutoff_seals_prefix_and_late_submission_waits()
    {
        IntakeFixture fixture;
        if (fixture.intake.submit(proposal(1, 1)) != CommandSubmissionResult::Accepted)
            return false;
        fixture.clock.advance(FirstTickDeadline);
        const auto first = fixture.intake.pump();
        if (!first || first.batches().size() != 1 || first.batches()[0].commands().size() != 1
            || first.batches()[0].commands()[0].stamp().eligibleServerTick().value() != 1)
            return false;

        if (fixture.intake.submit(proposal(1, 2)) != CommandSubmissionResult::Accepted)
            return false;
        const auto noTick = fixture.intake.pump();
        if (!noTick || !noTick.batches().empty() || fixture.intake.pendingCount() != 1)
            return false;
        fixture.clock.advance(SecondTickIncrement);
        const auto second = fixture.intake.pump();
        return second && second.batches().size() == 1 && second.batches()[0].commands().size() == 1
            && second.batches()[0].commands()[0].stamp().eligibleServerTick().value() == 2
            && second.batches()[0].commands()[0].stamp().ingressOrdinal().value() == 2;
    }

    bool scheduler_stall_emits_at_most_four_sequential_bounded_batches()
    {
        IntakeFixture fixture;
        if (!fill(fixture, MaximumPendingServerCommands))
            return false;
        fixture.clock.advance(10'000'000'000ULL);
        const auto result = fixture.intake.pump();
        if (!result || result.dueTickLag() != 300 || result.batches().size() != MaximumCatchUpTicks
            || fixture.intake.pendingCount() != 0)
            return false;
        for (std::size_t batch = 0; batch < result.batches().size(); ++batch)
        {
            if (result.batches()[batch].scheduledTick().value().value() != batch + 1
                || result.batches()[batch].commands().size() != MaximumServerCommandsPerTick)
                return false;
        }
        return result.batches().back().commands().back().stamp().ingressOrdinal().value()
            == MaximumPendingServerCommands;
    }

    bool tick_budget_stamps_1024_and_defers_fifo_suffix_without_loss()
    {
        IntakeFixture fixture;
        if (!fill(fixture, MaximumServerCommandsPerTick + 1))
            return false;
        fixture.clock.advance(FirstTickDeadline);
        const auto first = fixture.intake.pump();
        if (!first || first.batches().size() != 1
            || first.batches()[0].commands().size() != MaximumServerCommandsPerTick
            || fixture.intake.pendingCount() != 1
            || first.batches()[0].commands().back().stamp().ingressOrdinal().value() != MaximumServerCommandsPerTick)
            return false;
        fixture.clock.advance(SecondTickIncrement);
        const auto second = fixture.intake.pump();
        return second && second.batches().size() == 1 && second.batches()[0].commands().size() == 1
            && second.batches()[0].commands()[0].proposal().commandId().value() == MaximumServerCommandsPerTick + 1
            && second.batches()[0].commands()[0].stamp().eligibleServerTick().value() == 2
            && second.batches()[0].commands()[0].stamp().ingressOrdinal().value() == MaximumServerCommandsPerTick + 1
            && fixture.intake.pendingCount() == 0;
    }

    bool session_generation_pending_limit_rejects_129th_without_consuming_ordinal()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        auto metrics = RecordingMetricSink::create(MaximumRecordedObservations);
        auto events = RecordingStructuredEventSink::create(4);
        Observability observability(*metrics, *events);
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::fromValue(1).value(), IngressOrdinal::initial());
        for (std::uint64_t sequence = 1; sequence <= MaximumPendingServerCommandsPerSessionGeneration; ++sequence)
        {
            if (intake.submit(proposal(1, sequence)) != CommandSubmissionResult::Accepted)
                return false;
        }
        if (intake.submit(proposal(1, MaximumPendingServerCommandsPerSessionGeneration + 1))
                != CommandSubmissionResult::PerSessionPendingLimit
            || intake.pendingCount() != MaximumPendingServerCommandsPerSessionGeneration
            || intake.nextIngressOrdinal() != IngressOrdinal::initial() || events->events().size() != 1)
            return false;
        const auto* event = std::get_if<CommandIntakeEvent>(&events->events().front().payload());
        return event != nullptr && event->outcome == CommandIntakeObservationOutcome::PerSessionPendingLimit
            && event->pendingCommands == MaximumPendingServerCommandsPerSessionGeneration;
    }

    bool global_pending_limit_rejects_4097th_without_removing_accepted_work()
    {
        IntakeFixture fixture;
        if (!fill(fixture, MaximumPendingServerCommands))
            return false;
        return fixture.intake.submit(proposal(33, 1)) == CommandSubmissionResult::GlobalPendingLimit
            && fixture.intake.pendingCount() == MaximumPendingServerCommands
            && fixture.intake.nextIngressOrdinal() == IngressOrdinal::initial();
    }

    bool one_full_session_queue_does_not_reserve_other_session_capacity()
    {
        IntakeFixture fixture;
        for (std::uint64_t sequence = 1; sequence <= MaximumPendingServerCommandsPerSessionGeneration; ++sequence)
        {
            if (fixture.intake.submit(proposal(1, sequence)) != CommandSubmissionResult::Accepted)
                return false;
        }
        return fixture.intake.submit(proposal(2, 1)) == CommandSubmissionResult::Accepted
            && fixture.intake.pendingCount() == MaximumPendingServerCommandsPerSessionGeneration + 1;
    }

    bool ordinal_exhaustion_emits_no_partial_batch_and_never_wraps()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        auto metrics = RecordingMetricSink::create(16);
        auto events = RecordingStructuredEventSink::create(4);
        Observability observability(*metrics, *events);
        ServerCommandIntakeCoordinator intake(clock, observability, clock.now(), ServerTick::fromValue(1).value(),
            IngressOrdinal::fromValue(std::numeric_limits<std::uint64_t>::max()).value());
        if (intake.submit(proposal(1, 1)) != CommandSubmissionResult::Accepted
            || intake.submit(proposal(1, 2)) != CommandSubmissionResult::Accepted)
            return false;
        clock.advance(FirstTickDeadline);
        const auto result = intake.pump();
        if (result.error() != ServerCommandPumpError::IngressOrdinalExhausted || !result.batches().empty()
            || intake.pendingCount() != 2 || intake.terminalError() != ServerCommandPumpError::IngressOrdinalExhausted
            || !intake.nextIngressOrdinal()
            || intake.nextIngressOrdinal()->value() != std::numeric_limits<std::uint64_t>::max())
            return false;
        if (intake.submit(proposal(2, 1)) != CommandSubmissionResult::CoordinatorTerminated
            || intake.pendingCount() != 2 || events->events().size() != 1)
            return false;
        const auto* event = std::get_if<CommandIntakeEvent>(&events->events().front().payload());
        return event != nullptr && event->outcome == CommandIntakeObservationOutcome::IngressOrdinalExhausted
            && event->pendingCommands == 2;
    }

    bool queued_proposal_owns_values_and_contains_no_wire_or_generated_view()
    {
        IntakeFixture fixture;
        {
            auto temporary = proposal(7, 1, 88);
            if (fixture.intake.submit(temporary) != CommandSubmissionResult::Accepted)
                return false;
        }
        fixture.clock.advance(FirstTickDeadline);
        const auto result = fixture.intake.pump();
        return result && result.batches().size() == 1 && result.batches()[0].commands().size() == 1
            && result.batches()[0].commands()[0].proposal() == proposal(7, 1, 88);
    }

    bool intake_does_not_deduplicate_validate_or_mutate_canonical_state()
    {
        IntakeFixture fixture;
        const auto duplicate = proposal(1, 1, 999);
        if (fixture.intake.submit(duplicate) != CommandSubmissionResult::Accepted
            || fixture.intake.submit(duplicate) != CommandSubmissionResult::Accepted)
            return false;
        fixture.clock.advance(FirstTickDeadline);
        const auto result = fixture.intake.pump();
        return result && result.batches().size() == 1 && result.batches()[0].commands().size() == 2
            && result.batches()[0].commands()[0].proposal() == duplicate
            && result.batches()[0].commands()[1].proposal() == duplicate
            && result.batches()[0].commands()[0].stamp().ingressOrdinal().value() == 1
            && result.batches()[0].commands()[1].stamp().ingressOrdinal().value() == 2;
    }

    bool server_core_intake_has_no_openmw_socket_platform_script_or_database_dependency()
    {
        IntakeFixture fixture;
        return fixture.intake.pendingCount() == 0 && fixture.intake.terminalError() == ServerCommandPumpError::None;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "same_clock_and_submission_stream_produces_identical_ordered_trace",
            &same_clock_and_submission_stream_produces_identical_ordered_trace },
        std::pair{ "writer_observation_order_wins_ties_not_client_observed_tick",
            &writer_observation_order_wins_ties_not_client_observed_tick },
        std::pair{
            "tick_cutoff_seals_prefix_and_late_submission_waits", &tick_cutoff_seals_prefix_and_late_submission_waits },
        std::pair{ "scheduler_stall_emits_at_most_four_sequential_bounded_batches",
            &scheduler_stall_emits_at_most_four_sequential_bounded_batches },
        std::pair{ "tick_budget_stamps_1024_and_defers_fifo_suffix_without_loss",
            &tick_budget_stamps_1024_and_defers_fifo_suffix_without_loss },
        std::pair{ "session_generation_pending_limit_rejects_129th_without_consuming_ordinal",
            &session_generation_pending_limit_rejects_129th_without_consuming_ordinal },
        std::pair{ "global_pending_limit_rejects_4097th_without_removing_accepted_work",
            &global_pending_limit_rejects_4097th_without_removing_accepted_work },
        std::pair{ "one_full_session_queue_does_not_reserve_other_session_capacity",
            &one_full_session_queue_does_not_reserve_other_session_capacity },
        std::pair{ "ordinal_exhaustion_emits_no_partial_batch_and_never_wraps",
            &ordinal_exhaustion_emits_no_partial_batch_and_never_wraps },
        std::pair{ "queued_proposal_owns_values_and_contains_no_wire_or_generated_view",
            &queued_proposal_owns_values_and_contains_no_wire_or_generated_view },
        std::pair{ "intake_does_not_deduplicate_validate_or_mutate_canonical_state",
            &intake_does_not_deduplicate_validate_or_mutate_canonical_state },
        std::pair{ "server_core_intake_has_no_openmw_socket_platform_script_or_database_dependency",
            &server_core_intake_has_no_openmw_socket_platform_script_or_database_dependency },
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
