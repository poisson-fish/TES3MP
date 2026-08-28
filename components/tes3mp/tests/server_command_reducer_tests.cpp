#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    constexpr std::uint64_t FirstTickDeadline = 33'333'334;
    constexpr std::uint64_t NextTickIncrement = 33'333'333;

    static_assert(!std::is_copy_constructible_v<CanonicalCommandReducer>);
    static_assert(!std::is_move_constructible_v<CanonicalCommandReducer>);
    static_assert(!std::is_constructible_v<ServerTickCommandBatch, ScheduledTick, std::vector<StampedServerCommand>>);

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

    CanonicalPlayerEntityState player(std::uint64_t player, std::uint64_t entity, std::uint64_t revision = 1,
        std::uint64_t tick = 0, std::uint64_t epoch = 1, LinearVelocity3 velocity = LinearVelocity3(0, 0, 0))
    {
        return CanonicalPlayerEntityState(playerId(player), entityId(entity), transform(player, player * 100), velocity,
            EntityRevision::fromValue(revision).value(), AuthorityEpoch::fromValue(epoch).value(),
            ServerTick::fromValue(tick).value());
    }

    CanonicalSessionProgress session(std::uint64_t session, std::uint64_t player, std::uint64_t entity,
        std::optional<std::uint64_t> acknowledgement = std::nullopt, std::uint64_t generation = 1)
    {
        const auto finalized = acknowledgement
            ? std::optional<CommandSequence>(CommandSequence::fromValue(*acknowledgement).value())
            : std::nullopt;
        return CanonicalSessionProgress(sessionId(session), SessionGeneration::fromValue(generation).value(),
            playerId(player), entityId(entity), finalized);
    }

    CanonicalServerState state(
        std::span<const CanonicalPlayerEntityState> players, std::span<const CanonicalSessionProgress> sessions)
    {
        return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
    }

    ServerCommandProposal proposal(std::uint64_t session, std::uint64_t sequence, std::uint64_t command,
        std::uint64_t entity, std::uint64_t revision, LinearVelocity3 velocity, std::uint64_t epoch = 1,
        std::uint64_t generation = 1, std::uint64_t observedTick = 0)
    {
        return ServerCommandProposal(sessionId(session), SessionGeneration::fromValue(generation).value(),
            CommandSequence::fromValue(sequence).value(), CommandId::fromValue(command).value(),
            ServerTick::fromValue(observedTick).value(),
            EntityPrecondition(entityId(entity), EntityRevision::fromValue(revision).value(),
                AuthorityEpoch::fromValue(epoch).value()),
            PlayerMotionCommandProposal(velocity));
    }

    class IntakeFixture
    {
    public:
        IntakeFixture()
            : clock(MonotonicInstant::fromNanoseconds(0))
            , observability(metrics, events)
            , intake(clock, observability, clock.now(), ServerTick::fromValue(1).value(), IngressOrdinal::initial())
        {
        }

        bool submit(std::span<const ServerCommandProposal> proposals)
        {
            return std::all_of(proposals.begin(), proposals.end(), [this](ServerCommandProposal value) {
                return intake.submit(value) == CommandSubmissionResult::Accepted;
            });
        }

        ServerCommandPumpResult pumpFirst()
        {
            clock.advance(FirstTickDeadline);
            return intake.pump();
        }

        ServerCommandPumpResult pumpNext()
        {
            clock.advance(NextTickIncrement);
            return intake.pump();
        }

        ManualClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability;
        ServerCommandIntakeCoordinator intake;
    };

    CommandBatchReductionResult reduceCommands(
        CanonicalCommandReducer& reducer, std::span<const ServerCommandProposal> values)
    {
        IntakeFixture fixture;
        if (!fixture.submit(values))
            return {};
        const auto pumped = fixture.pumpFirst();
        if (!pumped || pumped.batches().size() != 1)
            return {};
        return reducer.apply(pumped.batches().front());
    }

    bool valid_bound_next_command_atomically_replaces_player_and_ack()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        const auto initial = state(players, sessions);
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initial, observability);
        const std::array commands{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(10, -20, 30), 1, 1, 999) };
        const auto result = reduceCommands(reducer, commands);
        const auto& record = result.dispositions().front();
        const auto& currentPlayer = reducer.state().players().front();
        const auto& currentSession = reducer.state().activeSessions().front();
        return result && result.dispositions().size() == 1 && record.disposition() == CommandDisposition::Applied
            && record.acknowledgementAdvanced() && record.playerStateChanged()
            && currentPlayer.transform() == players.front().transform()
            && currentPlayer.linearVelocity() == LinearVelocity3(10, -20, 30)
            && currentPlayer.entityRevision().value() == 2 && currentPlayer.lastSpatialChangeTick().value() == 1
            && currentSession.highestContiguousFinalizedCommand()->value() == 1;
    }

    bool unknown_or_old_generation_session_cannot_mutate_or_ack_current_state()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        const auto initial = state(players, sessions);
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initial, observability);
        const std::array commands{
            proposal(11, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 1, 1002, 101, 1, LinearVelocity3(2, 0, 0), 1, 2),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().size() == 2
            && result.dispositions()[0].disposition() == CommandDisposition::UnknownSession
            && result.dispositions()[1].disposition() == CommandDisposition::SessionGenerationMismatch
            && reducer.state() == initial;
    }

    bool unbound_entity_revision_and_epoch_fail_in_closed_validation_order()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 999, 999, LinearVelocity3(1, 0, 0), 999),
            proposal(10, 2, 1002, 101, 999, LinearVelocity3(2, 0, 0), 999),
            proposal(10, 3, 1003, 101, 1, LinearVelocity3(3, 0, 0), 999),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().size() == 3
            && result.dispositions()[0].disposition() == CommandDisposition::EntityBindingMismatch
            && result.dispositions()[1].disposition() == CommandDisposition::EntityRevisionMismatch
            && result.dispositions()[2].disposition() == CommandDisposition::AuthorityEpochMismatch
            && reducer.state().players().front() == players.front()
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 3;
    }

    bool rejected_next_command_advances_ack_while_preserving_player_exactly()
    {
        const std::array players{ player(1, 101, 7, 5, 3, LinearVelocity3(8, 9, 10)) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 1001, 202, 7, LinearVelocity3(1, 2, 3), 3) };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().front().acknowledgementAdvanced()
            && !result.dispositions().front().playerStateChanged()
            && reducer.state().players().front() == players.front()
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 1;
    }

    bool already_finalized_and_sequence_gap_commands_change_no_state()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101, 1) };
        const auto initial = state(players, sessions);
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initial, observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 3, 1003, 101, 1, LinearVelocity3(3, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions()[0].disposition() == CommandDisposition::AlreadyFinalized
            && result.dispositions()[1].disposition() == CommandDisposition::SequenceGap && reducer.state() == initial;
    }

    bool same_batch_duplicate_id_or_sequence_cannot_commit_twice()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 2, 1001, 101, 2, LinearVelocity3(2, 0, 0)),
            proposal(10, 2, 1002, 101, 2, LinearVelocity3(3, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions()[0].disposition() == CommandDisposition::Applied
            && result.dispositions()[1].disposition() == CommandDisposition::DuplicateCommandId
            && result.dispositions()[2].disposition() == CommandDisposition::AlreadyFinalized
            && reducer.state().players().front().entityRevision().value() == 2
            && reducer.state().players().front().linearVelocity() == LinearVelocity3(1, 0, 0)
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 2;
    }

    bool two_same_revision_commands_commit_first_and_reject_second_stale()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(10, 0, 0)),
            proposal(10, 2, 1002, 101, 1, LinearVelocity3(20, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions()[0].disposition() == CommandDisposition::Applied
            && result.dispositions()[1].disposition() == CommandDisposition::EntityRevisionMismatch
            && reducer.state().players().front().linearVelocity() == LinearVelocity3(10, 0, 0)
            && reducer.state().players().front().entityRevision().value() == 2
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 2;
    }

    bool sealed_intake_batches_preserve_1025_as_1024_then_1_without_partial_command_state()
    {
        std::vector<CanonicalPlayerEntityState> players;
        std::vector<CanonicalSessionProgress> sessions;
        std::vector<ServerCommandProposal> commands;
        for (std::uint64_t source = 1; source <= 9; ++source)
        {
            players.push_back(player(source, source + 1000));
            sessions.push_back(session(source, source, source + 1000));
        }
        std::uint64_t commandId = 1;
        for (std::uint64_t source = 1; source <= 8; ++source)
        {
            for (std::uint64_t sequence = 1; sequence <= MaximumPendingServerCommandsPerSessionGeneration; ++sequence)
            {
                commands.push_back(proposal(source, sequence, commandId++, source + 1000, sequence,
                    LinearVelocity3(static_cast<std::int64_t>(commandId), 0, 0)));
            }
        }
        commands.push_back(proposal(9, 1, commandId, 1009, 1, LinearVelocity3(9, 0, 0)));

        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        IntakeFixture fixture;
        if (!fixture.submit(commands))
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1
            || first.batches().front().commands().size() != MaximumServerCommandsPerTick
            || fixture.intake.pendingCount() != 1)
            return false;
        const auto firstReduction = reducer.apply(first.batches().front());
        if (!firstReduction || firstReduction.dispositions().size() != MaximumServerCommandsPerTick
            || std::any_of(firstReduction.dispositions().begin(), firstReduction.dispositions().end(),
                [](CommandDispositionRecord value) { return value.disposition() != CommandDisposition::Applied; })
            || reducer.state().findActiveSession(sessionId(9))->highestContiguousFinalizedCommand())
            return false;

        const auto second = fixture.pumpNext();
        if (!second || second.batches().size() != 1 || second.batches().front().commands().size() != 1)
            return false;
        const auto secondReduction = reducer.apply(second.batches().front());
        return secondReduction && secondReduction.dispositions().size() == 1
            && secondReduction.dispositions().front().disposition() == CommandDisposition::Applied
            && reducer.state().findActiveSession(sessionId(9))->highestContiguousFinalizedCommand()->value() == 1;
    }

    bool revision_exhaustion_rejects_player_change_and_atomically_finalizes_ack()
    {
        const std::array players{ player(1, 101, std::numeric_limits<std::uint64_t>::max()) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(
            10, 1, 1001, 101, std::numeric_limits<std::uint64_t>::max(), LinearVelocity3(1, 2, 3)) };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().front().disposition() == CommandDisposition::EntityRevisionExhausted
            && result.dispositions().front().acknowledgementAdvanced()
            && !result.dispositions().front().playerStateChanged()
            && reducer.state().players().front() == players.front()
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 1;
    }

    bool tick_regression_rejects_player_change_and_finalizes_ack()
    {
        const std::array players{ player(1, 101, 1, 5) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 2, 3)) };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().front().disposition() == CommandDisposition::SpatialTickRegression
            && reducer.state().players().front() == players.front()
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 1;
    }

    bool accepted_dropped_and_null_observability_produce_identical_state_and_dispositions()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        const auto initial = state(players, sessions);
        auto acceptedMetrics = RecordingMetricSink::create(4);
        auto acceptedEvents = RecordingStructuredEventSink::create(4);
        auto droppedMetrics = RecordingMetricSink::create(0);
        auto droppedEvents = RecordingStructuredEventSink::create(0);
        NullMetricSink nullMetrics;
        NullStructuredEventSink nullEvents;
        Observability acceptedObservability(*acceptedMetrics, *acceptedEvents);
        Observability droppedObservability(*droppedMetrics, *droppedEvents);
        Observability nullObservability(nullMetrics, nullEvents);
        CanonicalCommandReducer accepted(initial, acceptedObservability);
        CanonicalCommandReducer dropped(initial, droppedObservability);
        CanonicalCommandReducer null(initial, nullObservability);
        const std::array commands{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 2, 3)) };
        IntakeFixture fixture;
        if (!fixture.submit(commands))
            return false;
        const auto pumped = fixture.pumpFirst();
        const auto acceptedResult = accepted.apply(pumped.batches().front());
        const auto droppedResult = dropped.apply(pumped.batches().front());
        const auto nullResult = null.apply(pumped.batches().front());
        return acceptedResult == droppedResult && droppedResult == nullResult && accepted.state() == dropped.state()
            && dropped.state() == null.state() && acceptedMetrics->observations().size() == 1
            && acceptedEvents->events().size() == 1 && droppedMetrics->droppedCount() == 1
            && droppedEvents->droppedCount() == 1;
    }

    bool zero_negative_vertical_and_extreme_representable_velocity_are_preserved()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(0, 0, 0)),
            proposal(10, 2, 1002, 101, 2, LinearVelocity3(-1, 0, 0)),
            proposal(10, 3, 1003, 101, 3, LinearVelocity3(0, 0, 1)),
            proposal(10, 4, 1004, 101, 4,
                LinearVelocity3(
                    std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), -9)),
        };
        const auto originalTransform = players.front().transform();
        const auto result = reduceCommands(reducer, commands);
        return result
            && std::all_of(result.dispositions().begin(), result.dispositions().end(),
                [](CommandDispositionRecord value) { return value.disposition() == CommandDisposition::Applied; })
            && reducer.state().players().front().linearVelocity()
            == LinearVelocity3(std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), -9)
            && reducer.state().players().front().transform() == originalTransform;
    }

    bool two_bound_players_change_only_their_own_entity_state()
    {
        const std::array players{ player(1, 101), player(2, 202) };
        const std::array sessions{ session(10, 1, 101), session(20, 2, 202) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(20, 1, 2001, 202, 1, LinearVelocity3(2, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && reducer.state().findPlayer(playerId(1))->linearVelocity() == LinearVelocity3(1, 0, 0)
            && reducer.state().findPlayer(playerId(2))->linearVelocity() == LinearVelocity3(2, 0, 0)
            && reducer.state().findPlayer(playerId(1))->entityRevision().value() == 2
            && reducer.state().findPlayer(playerId(2))->entityRevision().value() == 2;
    }

    bool reducer_exposes_no_mutable_state_wire_engine_socket_script_or_database_surface()
    {
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalCommandReducer&>().state()),
            const CanonicalServerState&>);
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        return reducer.state().players().size() == 1;
    }

    bool cross_batch_command_id_reuse_remains_an_explicit_slice55_online_gate()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        IntakeFixture fixture;
        const std::array firstCommand{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)) };
        if (!fixture.submit(firstCommand))
            return false;
        const auto firstBatch = fixture.pumpFirst();
        const auto first = reducer.apply(firstBatch.batches().front());
        const std::array reusedId{ proposal(10, 2, 1001, 101, 2, LinearVelocity3(2, 0, 0)) };
        if (!fixture.submit(reusedId))
            return false;
        const auto secondBatch = fixture.pumpNext();
        const auto second = reducer.apply(secondBatch.batches().front());
        return first && second && first.dispositions().front().disposition() == CommandDisposition::Applied
            && second.dispositions().front().disposition() == CommandDisposition::Applied
            && reducer.state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 2;
    }

    bool initial_publication_is_version_zero_complete_and_immutable()
    {
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalCommandReducer&>().latestPublication()),
            std::shared_ptr<const CanonicalStatePublication>>);
        const std::array players{ player(1, 101), player(2, 202) };
        const std::array sessions{ session(10, 1, 101), session(20, 2, 202) };
        const auto initial = state(players, sessions);
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(initial, observability);
        const auto publication = reducer.latestPublication();
        return publication && publication->stateVersion() == CanonicalStateVersion::initial()
            && publication->changes().empty() && publication->state() == initial && reducer.state() == initial;
    }

    bool accepted_command_publishes_player_and_session_replacements_at_one_version()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const auto initialPublication = reducer.latestPublication();
        const std::array commands{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(4, 5, 6)) };
        const auto result = reduceCommands(reducer, commands);
        const auto publication = reducer.latestPublication();
        if (!result || !publication || publication == initialPublication || publication->changes().size() != 1)
            return false;
        const auto& change = publication->changes().front();
        return publication->stateVersion().value() == 1 && reducer.stateVersion().value() == 1
            && change.stateVersion().value() == 1 && change.commitTick().value() == 1
            && change.disposition() == CommandDisposition::Applied
            && change.sessionReplacement().highestContiguousFinalizedCommand()->value() == 1
            && change.playerReplacement() && change.playerReplacement()->linearVelocity() == LinearVelocity3(4, 5, 6)
            && publication->state() == reducer.state() && initialPublication->state() != reducer.state();
    }

    bool rejected_next_command_publishes_ack_only_replacement()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 1001, 999, 1, LinearVelocity3(4, 5, 6)) };
        const auto result = reduceCommands(reducer, commands);
        const auto publication = reducer.latestPublication();
        const auto& change = publication->changes().front();
        return result && publication->stateVersion().value() == 1 && publication->changes().size() == 1
            && change.disposition() == CommandDisposition::EntityBindingMismatch && !change.playerReplacement()
            && change.sessionReplacement().highestContiguousFinalizedCommand()->value() == 1
            && publication->state().players().front() == players.front();
    }

    bool noncommitting_dispositions_do_not_advance_version_or_publish()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101, 1) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const auto before = reducer.latestPublication();
        const std::array commands{
            proposal(11, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 2, 1002, 101, 1, LinearVelocity3(2, 0, 0), 1, 2),
            proposal(10, 1, 1003, 101, 1, LinearVelocity3(3, 0, 0)),
            proposal(10, 3, 1004, 101, 1, LinearVelocity3(4, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        return result && result.dispositions().size() == 4
            && result.dispositions()[0].disposition() == CommandDisposition::UnknownSession
            && result.dispositions()[1].disposition() == CommandDisposition::SessionGenerationMismatch
            && result.dispositions()[2].disposition() == CommandDisposition::AlreadyFinalized
            && result.dispositions()[3].disposition() == CommandDisposition::SequenceGap
            && reducer.stateVersion() == CanonicalStateVersion::initial() && reducer.latestPublication() == before;
    }

    bool multiple_commits_publish_contiguous_versions_and_exact_final_snapshot()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 2, 1002, 101, 1, LinearVelocity3(2, 0, 0)),
            proposal(10, 3, 1003, 101, 2, LinearVelocity3(3, 0, 0)),
        };
        const auto result = reduceCommands(reducer, commands);
        const auto publication = reducer.latestPublication();
        const auto changes = publication->changes();
        return result && changes.size() == 3 && changes[0].stateVersion().value() == 1
            && changes[1].stateVersion().value() == 2 && changes[2].stateVersion().value() == 3
            && changes[0].disposition() == CommandDisposition::Applied
            && changes[1].disposition() == CommandDisposition::EntityRevisionMismatch
            && changes[2].disposition() == CommandDisposition::Applied && changes[0].playerReplacement()
            && !changes[1].playerReplacement() && changes[2].playerReplacement()
            && publication->stateVersion().value() == 3 && publication->state() == reducer.state()
            && publication->state().players().front().linearVelocity() == LinearVelocity3(3, 0, 0)
            && publication->state().activeSessions().front().highestContiguousFinalizedCommand()->value() == 3;
    }

    bool failed_batch_preflight_preserves_state_version_and_publication()
    {
        const auto exhausted = CanonicalStateVersion::fromValue(std::numeric_limits<std::uint64_t>::max()).value();
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const auto before = reducer.latestPublication();
        return !canReserveCanonicalStateVersions(exhausted, 1)
            && reducer.stateVersion() == CanonicalStateVersion::initial() && reducer.latestPublication() == before
            && reducer.state().players().front() == players.front();
    }

    bool version_capacity_preflight_fails_before_any_command_executes()
    {
        constexpr std::uint64_t Maximum = std::numeric_limits<std::uint64_t>::max();
        const auto exact = CanonicalStateVersion::fromValue(Maximum - MaximumServerCommandsPerTick).value();
        const auto shortByOne = CanonicalStateVersion::fromValue(Maximum - MaximumServerCommandsPerTick + 1).value();
        return canReserveCanonicalStateVersions(exact, MaximumServerCommandsPerTick)
            && !canReserveCanonicalStateVersions(shortByOne, MaximumServerCommandsPerTick)
            && canReserveCanonicalStateVersions(CanonicalStateVersion::initial(), MaximumServerCommandsPerTick);
    }

    bool reader_processes_contiguous_batch_or_detects_gap_and_replaces_from_snapshot()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        IntakeFixture fixture;
        const std::array firstCommand{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)) };
        if (!fixture.submit(firstCommand))
            return false;
        const auto firstBatch = fixture.pumpFirst();
        if (!reducer.apply(firstBatch.batches().front()))
            return false;
        const auto first = reducer.latestPublication();
        if (classifyCanonicalPublication(CanonicalStateVersion::initial(), *first)
            != CanonicalPublicationReadAction::ApplyContiguousChanges)
            return false;

        const std::array secondCommand{ proposal(10, 2, 1002, 101, 2, LinearVelocity3(2, 0, 0)) };
        if (!fixture.submit(secondCommand))
            return false;
        const auto secondBatch = fixture.pumpNext();
        if (!reducer.apply(secondBatch.batches().front()))
            return false;
        const auto second = reducer.latestPublication();
        return classifyCanonicalPublication(CanonicalStateVersion::initial(), *second)
            == CanonicalPublicationReadAction::ReplaceFromSnapshot
            && classifyCanonicalPublication(first->stateVersion(), *second)
            == CanonicalPublicationReadAction::ApplyContiguousChanges
            && classifyCanonicalPublication(second->stateVersion(), *second) == CanonicalPublicationReadAction::NoChange
            && classifyCanonicalPublication(second->stateVersion(), *first)
            == CanonicalPublicationReadAction::OlderPublication;
    }

    bool slow_reader_holding_old_publication_cannot_block_or_mutate_writer()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const auto oldPublication = reducer.latestPublication();
        for (std::uint64_t sequence = 1; sequence <= 16; ++sequence)
        {
            const std::array command{ proposal(10, sequence, 1000 + sequence, 101, sequence,
                LinearVelocity3(static_cast<std::int64_t>(sequence), 0, 0)) };
            if (!reduceCommands(reducer, command))
                return false;
        }
        return oldPublication->stateVersion() == CanonicalStateVersion::initial() && oldPublication->changes().empty()
            && reducer.latestPublication()->stateVersion().value() == 16
            && oldPublication->state().players().front() == players.front();
    }

    bool latest_slot_retains_one_bounded_batch_independent_of_reader_speed()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array firstCommands{
            proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)),
            proposal(10, 2, 1002, 101, 2, LinearVelocity3(2, 0, 0)),
            proposal(10, 3, 1003, 101, 3, LinearVelocity3(3, 0, 0)),
        };
        if (!reduceCommands(reducer, firstCommands))
            return false;
        const auto retainedOld = reducer.latestPublication();
        const std::array nextCommand{ proposal(10, 4, 1004, 101, 4, LinearVelocity3(4, 0, 0)) };
        if (!reduceCommands(reducer, nextCommand))
            return false;
        const auto latest = reducer.latestPublication();
        return retainedOld != latest && retainedOld->changes().size() == 3 && latest->changes().size() == 1
            && retainedOld->changes().size() <= MaximumServerCommandsPerTick
            && latest->changes().size() <= MaximumServerCommandsPerTick && latest->stateVersion().value() == 4;
    }

    bool accepted_dropped_and_null_observability_produce_identical_publications()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        const auto initial = state(players, sessions);
        auto acceptedMetrics = RecordingMetricSink::create(4);
        auto acceptedEvents = RecordingStructuredEventSink::create(4);
        auto droppedMetrics = RecordingMetricSink::create(0);
        auto droppedEvents = RecordingStructuredEventSink::create(0);
        NullMetricSink nullMetrics;
        NullStructuredEventSink nullEvents;
        Observability acceptedObservability(*acceptedMetrics, *acceptedEvents);
        Observability droppedObservability(*droppedMetrics, *droppedEvents);
        Observability nullObservability(nullMetrics, nullEvents);
        CanonicalCommandReducer accepted(initial, acceptedObservability);
        CanonicalCommandReducer dropped(initial, droppedObservability);
        CanonicalCommandReducer null(initial, nullObservability);
        const std::array commands{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 2, 3)) };
        IntakeFixture fixture;
        if (!fixture.submit(commands))
            return false;
        const auto pumped = fixture.pumpFirst();
        const auto acceptedResult = accepted.apply(pumped.batches().front());
        const auto droppedResult = dropped.apply(pumped.batches().front());
        const auto nullResult = null.apply(pumped.batches().front());
        return acceptedResult == droppedResult && droppedResult == nullResult
            && *accepted.latestPublication() == *dropped.latestPublication()
            && *dropped.latestPublication() == *null.latestPublication() && acceptedMetrics->observations().size() == 1
            && acceptedEvents->events().size() == 1 && droppedMetrics->droppedCount() == 1
            && droppedEvents->droppedCount() == 1;
    }

    bool publication_exposes_no_wire_engine_socket_script_database_or_mutable_surface()
    {
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalStatePublication&>().state()),
            const CanonicalServerState&>);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalStatePublication&>().changes()),
            std::span<const CanonicalStateChangeRecord>>);
        static_assert(!std::is_default_constructible_v<CanonicalStatePublication>);
        return true;
    }

    bool protocol_interest_sinks_checksum_and_online_composition_remain_gated()
    {
        const std::array players{ player(1, 101) };
        const std::array sessions{ session(10, 1, 101) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        IntakeFixture fixture;
        const std::array firstCommand{ proposal(10, 1, 1001, 101, 1, LinearVelocity3(1, 0, 0)) };
        if (!fixture.submit(firstCommand) || !reducer.apply(fixture.pumpFirst().batches().front()))
            return false;
        const std::array reusedId{ proposal(10, 2, 1001, 101, 2, LinearVelocity3(2, 0, 0)) };
        if (!fixture.submit(reusedId) || !reducer.apply(fixture.pumpNext().batches().front()))
            return false;
        const auto publication = reducer.latestPublication();
        return publication->stateVersion().value() == 2 && publication->changes().size() == 1
            && publication->changes().front().commandId().value() == 1001
            && classifyCanonicalPublication(CanonicalStateVersion::initial(), *publication)
            == CanonicalPublicationReadAction::ReplaceFromSnapshot;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "valid_bound_next_command_atomically_replaces_player_and_ack",
            &valid_bound_next_command_atomically_replaces_player_and_ack },
        std::pair{ "unknown_or_old_generation_session_cannot_mutate_or_ack_current_state",
            &unknown_or_old_generation_session_cannot_mutate_or_ack_current_state },
        std::pair{ "unbound_entity_revision_and_epoch_fail_in_closed_validation_order",
            &unbound_entity_revision_and_epoch_fail_in_closed_validation_order },
        std::pair{ "rejected_next_command_advances_ack_while_preserving_player_exactly",
            &rejected_next_command_advances_ack_while_preserving_player_exactly },
        std::pair{ "already_finalized_and_sequence_gap_commands_change_no_state",
            &already_finalized_and_sequence_gap_commands_change_no_state },
        std::pair{ "same_batch_duplicate_id_or_sequence_cannot_commit_twice",
            &same_batch_duplicate_id_or_sequence_cannot_commit_twice },
        std::pair{ "two_same_revision_commands_commit_first_and_reject_second_stale",
            &two_same_revision_commands_commit_first_and_reject_second_stale },
        std::pair{ "sealed_intake_batches_preserve_1025_as_1024_then_1_without_partial_command_state",
            &sealed_intake_batches_preserve_1025_as_1024_then_1_without_partial_command_state },
        std::pair{ "revision_exhaustion_rejects_player_change_and_atomically_finalizes_ack",
            &revision_exhaustion_rejects_player_change_and_atomically_finalizes_ack },
        std::pair{ "tick_regression_rejects_player_change_and_finalizes_ack",
            &tick_regression_rejects_player_change_and_finalizes_ack },
        std::pair{ "accepted_dropped_and_null_observability_produce_identical_state_and_dispositions",
            &accepted_dropped_and_null_observability_produce_identical_state_and_dispositions },
        std::pair{ "zero_negative_vertical_and_extreme_representable_velocity_are_preserved",
            &zero_negative_vertical_and_extreme_representable_velocity_are_preserved },
        std::pair{ "two_bound_players_change_only_their_own_entity_state",
            &two_bound_players_change_only_their_own_entity_state },
        std::pair{ "reducer_exposes_no_mutable_state_wire_engine_socket_script_or_database_surface",
            &reducer_exposes_no_mutable_state_wire_engine_socket_script_or_database_surface },
        std::pair{ "cross_batch_command_id_reuse_remains_an_explicit_slice55_online_gate",
            &cross_batch_command_id_reuse_remains_an_explicit_slice55_online_gate },
        std::pair{ "initial_publication_is_version_zero_complete_and_immutable",
            &initial_publication_is_version_zero_complete_and_immutable },
        std::pair{ "accepted_command_publishes_player_and_session_replacements_at_one_version",
            &accepted_command_publishes_player_and_session_replacements_at_one_version },
        std::pair{ "rejected_next_command_publishes_ack_only_replacement",
            &rejected_next_command_publishes_ack_only_replacement },
        std::pair{ "noncommitting_dispositions_do_not_advance_version_or_publish",
            &noncommitting_dispositions_do_not_advance_version_or_publish },
        std::pair{ "multiple_commits_publish_contiguous_versions_and_exact_final_snapshot",
            &multiple_commits_publish_contiguous_versions_and_exact_final_snapshot },
        std::pair{ "failed_batch_preflight_preserves_state_version_and_publication",
            &failed_batch_preflight_preserves_state_version_and_publication },
        std::pair{ "version_capacity_preflight_fails_before_any_command_executes",
            &version_capacity_preflight_fails_before_any_command_executes },
        std::pair{ "reader_processes_contiguous_batch_or_detects_gap_and_replaces_from_snapshot",
            &reader_processes_contiguous_batch_or_detects_gap_and_replaces_from_snapshot },
        std::pair{ "slow_reader_holding_old_publication_cannot_block_or_mutate_writer",
            &slow_reader_holding_old_publication_cannot_block_or_mutate_writer },
        std::pair{ "latest_slot_retains_one_bounded_batch_independent_of_reader_speed",
            &latest_slot_retains_one_bounded_batch_independent_of_reader_speed },
        std::pair{ "accepted_dropped_and_null_observability_produce_identical_publications",
            &accepted_dropped_and_null_observability_produce_identical_publications },
        std::pair{ "publication_exposes_no_wire_engine_socket_script_database_or_mutable_surface",
            &publication_exposes_no_wire_engine_socket_script_database_or_mutable_surface },
        std::pair{ "protocol_interest_sinks_checksum_and_online_composition_remain_gated",
            &protocol_interest_sinks_checksum_and_online_composition_remain_gated },
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
