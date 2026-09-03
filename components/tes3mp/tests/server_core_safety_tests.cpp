#include <tes3mp/canonical_resync.hpp>
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

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

    CanonicalPlayerEntityState player(std::uint64_t playerValue = 1, std::uint64_t entityValue = 101,
        std::uint64_t revision = 1, std::uint64_t epoch = 1, std::uint64_t tick = 0,
        LinearVelocity3 velocity = LinearVelocity3(0, 0, 0))
    {
        return CanonicalPlayerEntityState(playerId(playerValue), entityId(entityValue),
            transform(playerValue, static_cast<std::int64_t>(playerValue * 100)), velocity,
            EntityRevision::fromValue(revision).value(), AuthorityEpoch::fromValue(epoch).value(),
            ServerTick::fromValue(tick).value());
    }

    CanonicalSessionProgress session(std::uint64_t sessionValue = 10, std::uint64_t playerValue = 1,
        std::uint64_t entityValue = 101, std::uint64_t generation = 1,
        std::optional<std::uint64_t> acknowledgement = std::nullopt, std::vector<FinalizedCommandRecord> history = {})
    {
        const auto ack = acknowledgement
            ? std::optional<CommandSequence>(CommandSequence::fromValue(*acknowledgement).value())
            : std::nullopt;
        return std::get<CanonicalSessionProgress>(
            createCanonicalSessionProgress(sessionId(sessionValue), SessionGeneration::fromValue(generation).value(),
                playerId(playerValue), entityId(entityValue), ack, history));
    }

    CanonicalServerState state(
        std::span<const CanonicalPlayerEntityState> players, std::span<const CanonicalSessionProgress> sessions)
    {
        return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
    }

    ServerCommandProposal proposal(std::uint64_t sessionValue, std::uint64_t sequence, std::uint64_t command,
        std::uint64_t revision, LinearVelocity3 velocity = LinearVelocity3(1, 0, 0), std::uint64_t epoch = 1,
        std::uint64_t generation = 1, std::uint64_t entityValue = 101)
    {
        return ServerCommandProposal(sessionId(sessionValue), SessionGeneration::fromValue(generation).value(),
            CommandSequence::fromValue(sequence).value(), CommandId::fromValue(command).value(), CanonicalRevision::initial(),
            EntityPrecondition(entityId(entityValue), EntityRevision::fromValue(revision).value(),
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

        CommandBatchReductionResult reduce(
            CanonicalCommandReducer& reducer, std::span<const ServerCommandProposal> proposals)
        {
            for (const auto& value : proposals)
            {
                if (intake.submit(value) != CommandSubmissionResult::Accepted)
                    return {};
            }
            clock.advance(33'333'334);
            const auto pumped = intake.pump();
            if (!pumped || pumped.batches().size() != 1)
                return {};
            return reducer.apply(pumped.batches().front());
        }

    private:
        ManualClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability;
        ServerCommandIntakeCoordinator intake;
    };

    CommandBatchReductionResult reduce(
        CanonicalCommandReducer& reducer, std::span<const ServerCommandProposal> proposals)
    {
        IntakeFixture fixture;
        return fixture.reduce(reducer, proposals);
    }

    bool cross_batch_reused_command_id_is_finalized_once_without_player_change()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array first{ proposal(10, 1, 500, 1, LinearVelocity3(3, 0, 0)) };
        const std::array duplicate{ proposal(10, 2, 500, 2, LinearVelocity3(9, 0, 0)) };
        const auto accepted = reduce(reducer, first);
        const auto rejected = reduce(reducer, duplicate);
        const auto& current = reducer.state();
        return accepted && rejected
            && rejected.dispositions().front().disposition() == CommandDisposition::DuplicateCommandId
            && rejected.dispositions().front().acknowledgementAdvanced()
            && !rejected.dispositions().front().playerStateChanged()
            && current.players().front().linearVelocity() == LinearVelocity3(3, 0, 0)
            && current.players().front().entityRevision().value() == 2
            && current.activeSessions().front().highestContiguousFinalizedCommand()->value() == 2;
    }

    bool idempotency_windows_are_isolated_by_active_session_generation()
    {
        const std::array players{ player(1, 101), player(2, 202) };
        const std::array sessions{ session(10, 1, 101), session(20, 2, 202, 2) };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 700, 1),
            proposal(20, 1, 700, 1, LinearVelocity3(2, 0, 0), 1, 2, 202) };
        const auto result = reduce(reducer, commands);
        return result && result.dispositions()[0].disposition() == CommandDisposition::Applied
            && result.dispositions()[1].disposition() == CommandDisposition::Applied
            && reducer.state().activeSessions()[0].finalizedCommandHistory().size() == 1
            && reducer.state().activeSessions()[1].finalizedCommandHistory().size() == 1;
    }

    bool finalized_acceptance_and_rejection_both_enter_ordered_history()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 1, 1), proposal(10, 2, 2, 2, LinearVelocity3(1, 0, 0), 1, 1, 999) };
        const auto result = reduce(reducer, commands);
        const auto history = reducer.state().activeSessions().front().finalizedCommandHistory();
        return result && history.size() == 2 && history[0].commandSequence().value() == 1
            && history[0].disposition() == CommandDisposition::Applied && history[1].commandSequence().value() == 2
            && history[1].disposition() == CommandDisposition::EntityBindingMismatch;
    }

    bool fillHistory(CanonicalCommandReducer& reducer, std::uint64_t count)
    {
        for (std::uint64_t sequence = 1; sequence <= count; ++sequence)
        {
            const std::array command{ proposal(10, sequence, sequence, sequence) };
            if (!reduce(reducer, command))
                return false;
        }
        return true;
    }

    bool window_1025_evicts_only_oldest_and_retains_exactly_1024()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        if (!fillHistory(reducer, 1025))
            return false;
        const auto history = reducer.state().activeSessions().front().finalizedCommandHistory();
        std::vector<FinalizedCommandRecord> oversized;
        oversized.reserve(MaximumFinalizedCommandHistory + 1);
        for (std::uint64_t sequence = 1; sequence <= MaximumFinalizedCommandHistory + 1; ++sequence)
            oversized.emplace_back(CommandSequence::fromValue(sequence).value(), CommandId::fromValue(sequence).value(),
                CommandDisposition::Applied);
        const auto rejected = createCanonicalSessionProgress(sessionId(10), SessionGeneration::initial(), playerId(1),
            entityId(101), CommandSequence::fromValue(MaximumFinalizedCommandHistory + 1).value(), oversized);
        const auto* error = std::get_if<CanonicalSessionHistoryError>(&rejected);
        return history.size() == MaximumFinalizedCommandHistory && history.front().commandSequence().value() == 2
            && history.front().commandId().value() == 2 && history.back().commandSequence().value() == 1025
            && error != nullptr && error->code == CanonicalSessionHistoryErrorCode::LimitExceeded;
    }

    bool evicted_id_reuse_documents_the_bounded_retry_horizon()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        if (!fillHistory(reducer, 1025))
            return false;
        const std::array reused{ proposal(10, 1026, 1, 1026, LinearVelocity3(8, 0, 0)) };
        const auto result = reduce(reducer, reused);
        return result && result.dispositions().front().disposition() == CommandDisposition::Applied
            && reducer.state().activeSessions().front().finalizedCommandHistory().front().commandSequence().value() == 3
            && reducer.state().activeSessions().front().finalizedCommandHistory().back().commandId().value() == 1;
    }

    bool same_batch_and_cross_batch_duplicates_use_one_membership_rule()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array sameBatch{ proposal(10, 1, 90, 1), proposal(10, 2, 90, 2) };
        const auto first = reduce(reducer, sameBatch);
        const std::array crossBatch{ proposal(10, 3, 90, 2) };
        const auto second = reduce(reducer, crossBatch);
        return first && second && first.dispositions()[1].disposition() == CommandDisposition::DuplicateCommandId
            && second.dispositions()[0].disposition() == CommandDisposition::DuplicateCommandId
            && reducer.state().activeSessions().front().finalizedCommandHistory().size() == 3;
    }

    bool stale_and_future_authority_epochs_finalize_without_player_mutation()
    {
        const std::array players{ player(1, 101, 1, 5) };
        const std::array sessions{ session() };
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer(state(players, sessions), observability);
        const std::array commands{ proposal(10, 1, 1, 1, LinearVelocity3(1, 0, 0), 4),
            proposal(10, 2, 2, 1, LinearVelocity3(1, 0, 0), 6) };
        const auto result = reduce(reducer, commands);
        return result && result.dispositions()[0].disposition() == CommandDisposition::AuthorityEpochMismatch
            && result.dispositions()[1].disposition() == CommandDisposition::AuthorityEpochMismatch
            && reducer.state().players().front() == players.front()
            && reducer.state().activeSessions().front().finalizedCommandHistory().size() == 2;
    }

    template <class Value>
    void appendLittleEndian(std::vector<std::uint8_t>& bytes, Value value)
    {
        using Unsigned = std::make_unsigned_t<Value>;
        const Unsigned bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index)
            bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8)));
    }

    bool canonical_v1_bytes_are_explicit_stable_and_cover_complete_phase5_state()
    {
        const std::vector history{ FinalizedCommandRecord(
            CommandSequence::initial(), CommandId::fromValue(1001).value(), CommandDisposition::Applied) };
        const std::array players{ player(1, 101, 1, 1, 0, LinearVelocity3(1, 2, 3)) };
        const std::array sessions{ session(10, 1, 101, 1, 1, history) };
        const auto canonical = state(players, sessions);
        const auto bytes = canonicalStateBytesV1(
            CanonicalStateVersion::fromValue(7).value(), ServerTick::fromValue(9).value(), canonical);

        std::vector<std::uint8_t> expected{ 'T', '3', 'C', 'S' };
        appendLittleEndian(expected, std::uint16_t{ 1 });
        appendLittleEndian(expected, std::uint16_t{ 1 });
        appendLittleEndian(expected, std::uint32_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 7 });
        appendLittleEndian(expected, std::uint64_t{ 9 });
        appendLittleEndian(expected, std::uint32_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 101 });
        expected.push_back(0);
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::int64_t{ 100 });
        appendLittleEndian(expected, std::int64_t{ 101 });
        appendLittleEndian(expected, std::int64_t{ 102 });
        appendLittleEndian(expected, std::uint32_t{ 0 });
        appendLittleEndian(expected, std::uint32_t{ 0 });
        appendLittleEndian(expected, std::uint32_t{ 0 });
        appendLittleEndian(expected, std::int64_t{ 1 });
        appendLittleEndian(expected, std::int64_t{ 2 });
        appendLittleEndian(expected, std::int64_t{ 3 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 0 });
        appendLittleEndian(expected, std::uint32_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 10 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 101 });
        expected.push_back(1);
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint32_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 1 });
        appendLittleEndian(expected, std::uint64_t{ 1001 });
        expected.push_back(static_cast<std::uint8_t>(CommandDisposition::Applied));
        appendLittleEndian(expected, std::uint32_t{ 0 });
        return bytes == expected;
    }

    bool crc64_ecma_check_vector_and_canonical_checksum_are_stable()
    {
        constexpr std::array<std::uint8_t, 9> Check{ '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        const std::array players{ player() };
        const std::array sessions{ session() };
        const auto canonical = state(players, sessions);
        const auto bytes = canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), canonical);
        return crc64Ecma182(Check).value() == 0x6C40DF5F0B497347ULL
            && canonicalStateChecksumV1(CanonicalStateVersion::initial(), ServerTick::initial(), canonical)
            == crc64Ecma182(bytes);
    }

    CanonicalServerState fieldState(std::uint64_t playerValue, std::uint64_t revision, std::uint64_t epoch,
        std::uint64_t acknowledgement, std::uint64_t commandIdValue)
    {
        const std::vector history{ FinalizedCommandRecord(CommandSequence::fromValue(acknowledgement).value(),
            CommandId::fromValue(commandIdValue).value(), CommandDisposition::Applied) };
        const std::array players{ player(playerValue, playerValue + 100, revision, epoch) };
        const std::array sessions{ session(10, playerValue, playerValue + 100, 1, acknowledgement, history) };
        return state(players, sessions);
    }

    bool identity_revision_epoch_ack_history_version_or_tick_change_changes_bytes()
    {
        const auto base = fieldState(1, 1, 1, 1, 10);
        const auto identity = fieldState(2, 1, 1, 1, 10);
        const auto revision = fieldState(1, 2, 1, 1, 10);
        const auto epoch = fieldState(1, 1, 2, 1, 10);
        const auto ackHistory = fieldState(1, 1, 1, 2, 11);
        const auto bytes = canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), base);
        return bytes != canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), identity)
            && bytes != canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), revision)
            && bytes != canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), epoch)
            && bytes != canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::initial(), ackHistory)
            && bytes != canonicalStateBytesV1(CanonicalStateVersion::fromValue(1).value(), ServerTick::initial(), base)
            && bytes != canonicalStateBytesV1(CanonicalStateVersion::initial(), ServerTick::fromValue(1).value(), base);
    }

    bool observability_allocation_and_publication_handle_do_not_change_checksum()
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        auto recordingMetrics = RecordingMetricSink::create(4);
        auto recordingEvents = RecordingStructuredEventSink::create(4);
        NullMetricSink nullMetrics;
        NullStructuredEventSink nullEvents;
        Observability recording(*recordingMetrics, *recordingEvents);
        Observability quiet(nullMetrics, nullEvents);
        CanonicalCommandReducer first(state(players, sessions), recording);
        CanonicalCommandReducer second(state(players, sessions), quiet);
        const std::array command{ proposal(10, 1, 44, 1) };
        if (!reduce(first, command) || !reduce(second, command))
            return false;
        const auto firstPublication = first.latestPublication();
        const auto secondPublication = second.latestPublication();
        return firstPublication != secondPublication && firstPublication->checksum() == secondPublication->checksum()
            && canonicalStateBytesV1(
                   firstPublication->stateVersion(), firstPublication->checkpointTick(), firstPublication->state())
            == canonicalStateBytesV1(
                secondPublication->stateVersion(), secondPublication->checkpointTick(), secondPublication->state());
    }

    CanonicalCommandReducer makeReducer(Observability& observability)
    {
        const std::array players{ player() };
        const std::array sessions{ session() };
        return CanonicalCommandReducer(state(players, sessions), observability);
    }

    bool current_session_resync_returns_latest_immutable_publication_without_mutation()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer = makeReducer(observability);
        const std::array command{ proposal(10, 1, 1, 1) };
        if (!reduce(reducer, command))
            return false;
        const auto latest = reducer.latestPublication();
        const auto beforeState = reducer.state();
        const auto beforeVersion = reducer.stateVersion();
        const CanonicalResyncRequest request(sessionId(10), SessionGeneration::initial(),
            CanonicalResyncReason::ChecksumMismatch, CanonicalStateVersion::initial());
        const auto result = resolveCanonicalResync(request, latest);
        return result.disposition() == CanonicalResyncDisposition::SnapshotRequired && result.publication() == latest
            && reducer.state() == beforeState && reducer.stateVersion() == beforeVersion;
    }

    bool unknown_or_old_generation_resync_returns_no_publication()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        CanonicalCommandReducer reducer = makeReducer(observability);
        const auto latest = reducer.latestPublication();
        const CanonicalResyncRequest unknown(sessionId(99), SessionGeneration::initial(),
            CanonicalResyncReason::LocalFeedGap, CanonicalStateVersion::initial());
        const CanonicalResyncRequest old(sessionId(10), SessionGeneration::fromValue(2).value(),
            CanonicalResyncReason::EntityRevisionMismatch, CanonicalStateVersion::initial());
        const auto unknownResult = resolveCanonicalResync(unknown, latest);
        const auto oldResult = resolveCanonicalResync(old, latest);
        return unknownResult.disposition() == CanonicalResyncDisposition::UnknownSession && !unknownResult.publication()
            && oldResult.disposition() == CanonicalResyncDisposition::SessionGenerationMismatch
            && !oldResult.publication();
    }

    bool resync_request_cannot_upload_client_state_or_depend_on_wire_interest_or_transport()
    {
        static_assert(std::is_constructible_v<CanonicalResyncRequest, SessionId, SessionGeneration,
            CanonicalResyncReason, CanonicalStateVersion>);
        static_assert(!std::is_constructible_v<CanonicalResyncRequest, SessionId, SessionGeneration,
            CanonicalResyncReason, CanonicalStateVersion, CanonicalChecksum>);
        static_assert(!std::is_constructible_v<CanonicalResyncRequest, SessionId, SessionGeneration,
            CanonicalResyncReason, CanonicalStateVersion, CanonicalServerState>);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalResyncResult&>().publication()),
            const std::shared_ptr<const CanonicalStatePublication>&>);
        return true;
    }

    bool slice55_closes_only_the_core_idempotency_blocker_without_runtime_composition()
    {
        static_assert(!std::is_copy_constructible_v<CanonicalCommandReducer>);
        static_assert(std::is_same_v<decltype(std::declval<const CanonicalCommandReducer&>().latestPublication()),
            std::shared_ptr<const CanonicalStatePublication>>);
        const std::array players{ player() };
        const std::array sessions{ session() };
        const auto canonical = state(players, sessions);
        return canonical.activeSessions().front().finalizedCommandHistory().empty()
            && CanonicalStateEncodingVersion == 1 && CanonicalChecksumAlgorithmVersion == 1
            && CanonicalRulesVersion == 1;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "cross_batch_reused_command_id_is_finalized_once_without_player_change",
            &cross_batch_reused_command_id_is_finalized_once_without_player_change },
        std::pair{ "idempotency_windows_are_isolated_by_active_session_generation",
            &idempotency_windows_are_isolated_by_active_session_generation },
        std::pair{ "finalized_acceptance_and_rejection_both_enter_ordered_history",
            &finalized_acceptance_and_rejection_both_enter_ordered_history },
        std::pair{ "window_1025_evicts_only_oldest_and_retains_exactly_1024",
            &window_1025_evicts_only_oldest_and_retains_exactly_1024 },
        std::pair{ "evicted_id_reuse_documents_the_bounded_retry_horizon",
            &evicted_id_reuse_documents_the_bounded_retry_horizon },
        std::pair{ "same_batch_and_cross_batch_duplicates_use_one_membership_rule",
            &same_batch_and_cross_batch_duplicates_use_one_membership_rule },
        std::pair{ "stale_and_future_authority_epochs_finalize_without_player_mutation",
            &stale_and_future_authority_epochs_finalize_without_player_mutation },
        std::pair{ "canonical_v1_bytes_are_explicit_stable_and_cover_complete_phase5_state",
            &canonical_v1_bytes_are_explicit_stable_and_cover_complete_phase5_state },
        std::pair{ "crc64_ecma_check_vector_and_canonical_checksum_are_stable",
            &crc64_ecma_check_vector_and_canonical_checksum_are_stable },
        std::pair{ "identity_revision_epoch_ack_history_version_or_tick_change_changes_bytes",
            &identity_revision_epoch_ack_history_version_or_tick_change_changes_bytes },
        std::pair{ "observability_allocation_and_publication_handle_do_not_change_checksum",
            &observability_allocation_and_publication_handle_do_not_change_checksum },
        std::pair{ "current_session_resync_returns_latest_immutable_publication_without_mutation",
            &current_session_resync_returns_latest_immutable_publication_without_mutation },
        std::pair{ "unknown_or_old_generation_resync_returns_no_publication",
            &unknown_or_old_generation_resync_returns_no_publication },
        std::pair{ "resync_request_cannot_upload_client_state_or_depend_on_wire_interest_or_transport",
            &resync_request_cannot_upload_client_state_or_depend_on_wire_interest_or_transport },
        std::pair{ "slice55_closes_only_the_core_idempotency_blocker_without_runtime_composition",
            &slice55_closes_only_the_core_idempotency_blocker_without_runtime_composition },
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
