#include <tes3mp/canonical_checksum.hpp>
#include <tes3mp/deterministic_random.hpp>
#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    constexpr std::size_t ClientCount = 8;
    constexpr std::size_t BatchesPerSimulation = 48;
    constexpr std::size_t MaximumGeneratedCommandsPerBatch = 6;
    constexpr std::size_t DispositionCount = 11;
    constexpr std::uint64_t FirstTickDeadline = 33'333'334;

    PlayerId playerId(std::size_t index)
    {
        return PlayerId::fromValue(index + 1).value();
    }

    EntityId entityId(std::size_t index)
    {
        return EntityId::fromValue(101 + index).value();
    }

    SessionId sessionId(std::size_t index)
    {
        return SessionId::fromValue(201 + index).value();
    }

    Transform transform(std::size_t index)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(CellSpaceId::fromValue(index + 1).value()),
            Position3(static_cast<std::int64_t>(index * 100), static_cast<std::int64_t>(index * 100 + 1),
                static_cast<std::int64_t>(index * 100 + 2)),
            Orientation3(zero, zero, zero));
    }

    CanonicalServerState initialState()
    {
        std::array<CanonicalPlayerEntityState, ClientCount> players{
            CanonicalPlayerEntityState(playerId(0), entityId(0), transform(0), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(1), entityId(1), transform(1), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(2), entityId(2), transform(2), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(3), entityId(3), transform(3), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(4), entityId(4), transform(4), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(5), entityId(5), transform(5), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(6), entityId(6), transform(6), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
            CanonicalPlayerEntityState(playerId(7), entityId(7), transform(7), LinearVelocity3(0, 0, 0),
                EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial()),
        };
        std::array<CanonicalSessionProgress, ClientCount> sessions{
            CanonicalSessionProgress(
                sessionId(0), SessionGeneration::initial(), playerId(0), entityId(0), std::nullopt),
            CanonicalSessionProgress(
                sessionId(1), SessionGeneration::initial(), playerId(1), entityId(1), std::nullopt),
            CanonicalSessionProgress(
                sessionId(2), SessionGeneration::initial(), playerId(2), entityId(2), std::nullopt),
            CanonicalSessionProgress(
                sessionId(3), SessionGeneration::initial(), playerId(3), entityId(3), std::nullopt),
            CanonicalSessionProgress(
                sessionId(4), SessionGeneration::initial(), playerId(4), entityId(4), std::nullopt),
            CanonicalSessionProgress(
                sessionId(5), SessionGeneration::initial(), playerId(5), entityId(5), std::nullopt),
            CanonicalSessionProgress(
                sessionId(6), SessionGeneration::initial(), playerId(6), entityId(6), std::nullopt),
            CanonicalSessionProgress(
                sessionId(7), SessionGeneration::initial(), playerId(7), entityId(7), std::nullopt),
        };
        return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
    }

    std::uint64_t drawBelow(Xoshiro256StarStar& random, std::uint64_t upperBound)
    {
        return random.uniformBelow(upperBound).value();
    }

    CommandSequence nextSequence(const CanonicalSessionProgress& session)
    {
        return session.highestContiguousFinalizedCommand() ? session.highestContiguousFinalizedCommand()->next().value()
                                                           : CommandSequence::initial();
    }

    void appendU64(std::vector<std::uint8_t>& trace, std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            trace.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    struct SimulationResult
    {
        bool passed = true;
        std::uint64_t seed = 0;
        std::size_t failedBatch = 0;
        std::vector<std::uint8_t> trace;
        std::vector<std::uint8_t> finalStateBytes;
        std::array<std::uint64_t, DispositionCount> dispositions{};
        CanonicalChecksum finalChecksum;
        CanonicalStateVersion finalVersion = CanonicalStateVersion::initial();
    };

    ServerCommandProposal generateProposal(
        Xoshiro256StarStar& random, const CanonicalServerState& state, std::size_t batchIndex, std::size_t commandIndex)
    {
        const std::size_t client = static_cast<std::size_t>(drawBelow(random, ClientCount));
        const CanonicalSessionProgress& session = state.activeSessions()[client];
        const CanonicalPlayerEntityState& player = state.players()[client];
        const std::uint64_t kind = (drawBelow(random, 10) + batchIndex + commandIndex) % 10;
        CommandSequence sequence = nextSequence(session);
        CommandId commandId
            = CommandId::fromValue(batchIndex * MaximumGeneratedCommandsPerBatch + commandIndex + 1).value();
        SessionId proposalSession = session.sessionId();
        SessionGeneration generation = session.sessionGeneration();
        EntityId proposalEntity = player.entityId();
        EntityRevision revision = player.entityRevision();
        AuthorityEpoch epoch = player.authorityEpoch();

        switch (kind)
        {
            case 2:
                revision = player.entityRevision().next().value();
                break;
            case 3:
                epoch = player.authorityEpoch().next().value();
                break;
            case 4:
                if (!session.finalizedCommandHistory().empty())
                    commandId = session.finalizedCommandHistory().back().commandId();
                else
                    revision = player.entityRevision().next().value();
                break;
            case 5:
                sequence = sequence.next().value();
                break;
            case 6:
                sequence = session.highestContiguousFinalizedCommand().value_or(sequence.next().value());
                break;
            case 7:
                generation = session.sessionGeneration().next().value();
                break;
            case 8:
                proposalSession = SessionId::fromValue(10'000 + client).value();
                break;
            case 9:
                proposalEntity = entityId((client + 1) % ClientCount);
                break;
            default:
                break;
        }

        const auto velocityComponent = [&random] { return static_cast<std::int64_t>(drawBelow(random, 2001)) - 1000; };
        return ServerCommandProposal(proposalSession, generation, sequence, commandId,
            ServerTick::fromValue(drawBelow(random, 64)).value(), EntityPrecondition(proposalEntity, revision, epoch),
            PlayerMotionCommandProposal(
                LinearVelocity3(velocityComponent(), velocityComponent(), velocityComponent())));
    }

    bool verifyBatch(const CanonicalServerState& before, CanonicalStateVersion versionBefore,
        const std::shared_ptr<const CanonicalStatePublication>& publicationBefore, const ServerTickCommandBatch& batch,
        const CommandBatchReductionResult& result, const CanonicalCommandReducer& reducer)
    {
        const auto failure = [](const char* contract) {
            std::cerr << " invariant=" << contract;
            return false;
        };
        if (!result || result.dispositions().size() != batch.commands().size())
            return failure("batch_result_shape");

        std::vector<CanonicalPlayerEntityState> expectedPlayers(before.players().begin(), before.players().end());
        std::array<std::size_t, ClientCount> sessionAdvances{};
        std::size_t committed = 0;
        for (std::size_t index = 0; index < result.dispositions().size(); ++index)
        {
            const CommandDispositionRecord& disposition = result.dispositions()[index];
            const StampedServerCommand& command = batch.commands()[index];
            if (disposition.stamp() != command.stamp() || disposition.commandId() != command.proposal().commandId()
                || disposition.commandSequence() != command.proposal().commandSequence())
                return failure("disposition_identity");
            if (!disposition.acknowledgementAdvanced())
            {
                if (disposition.playerStateChanged())
                    return failure("noncommit_player_change");
                continue;
            }

            const CanonicalSessionProgress* session = before.findActiveSession(disposition.sessionId());
            if (session == nullptr)
                return failure("committed_session_exists");
            const std::size_t sessionIndex = static_cast<std::size_t>(session - before.activeSessions().data());
            ++sessionAdvances[sessionIndex];
            if (disposition.playerStateChanged())
            {
                if (disposition.disposition() != CommandDisposition::Applied)
                    return failure("player_change_is_applied");
                const std::size_t playerIndex
                    = static_cast<std::size_t>(before.findPlayer(session->playerId()) - before.players().data());
                auto advanced
                    = advanceCanonicalSpatialState(expectedPlayers[playerIndex], batch.scheduledTick().value(),
                        expectedPlayers[playerIndex].transform(), command.proposal().motion().desiredVelocity());
                if (!std::holds_alternative<CanonicalPlayerEntityState>(advanced))
                    return failure("expected_spatial_advance");
                expectedPlayers[playerIndex] = std::get<CanonicalPlayerEntityState>(advanced);
            }
            ++committed;
        }

        const auto publication = reducer.latestPublication();
        if (committed == 0)
            return publication == publicationBefore && reducer.stateVersion() == versionBefore;
        if (publication == publicationBefore || publication->changes().size() != committed
            || reducer.stateVersion().value() != versionBefore.value() + committed
            || publication->stateVersion() != reducer.stateVersion() || publication->state() != reducer.state()
            || publication->checksum()
                != canonicalStateChecksumV1(
                    publication->stateVersion(), publication->checkpointTick(), reducer.state()))
            return failure("publication_complete");

        std::size_t changeIndex = 0;
        for (const CommandDispositionRecord& disposition : result.dispositions())
        {
            if (!disposition.acknowledgementAdvanced())
                continue;
            const CanonicalStateChangeRecord& change = publication->changes()[changeIndex];
            if (change.stateVersion().value() != versionBefore.value() + changeIndex + 1
                || change.stamp() != disposition.stamp() || change.commandId() != disposition.commandId()
                || change.commandSequence() != disposition.commandSequence()
                || change.disposition() != disposition.disposition()
                || change.playerReplacement().has_value() != disposition.playerStateChanged())
                return failure("change_matches_disposition");
            ++changeIndex;
        }

        if (!std::equal(expectedPlayers.begin(), expectedPlayers.end(), reducer.state().players().begin()))
            return failure("player_partition_exact");
        for (std::size_t index = 0; index < ClientCount; ++index)
        {
            const auto beforeAcknowledgement = before.activeSessions()[index].highestContiguousFinalizedCommand();
            const auto afterAcknowledgement
                = reducer.state().activeSessions()[index].highestContiguousFinalizedCommand();
            const std::uint64_t beforeValue = beforeAcknowledgement ? beforeAcknowledgement->value() : 0;
            const std::uint64_t expectedAcknowledgement = beforeValue + sessionAdvances[index];
            if ((expectedAcknowledgement == 0 && afterAcknowledgement)
                || (expectedAcknowledgement != 0
                    && (!afterAcknowledgement || afterAcknowledgement->value() != expectedAcknowledgement)))
                return failure("acknowledgement_progress");
            const std::size_t expectedHistory = std::min(MaximumFinalizedCommandHistory,
                before.activeSessions()[index].finalizedCommandHistory().size() + sessionAdvances[index]);
            if (reducer.state().activeSessions()[index].finalizedCommandHistory().size() != expectedHistory)
                return failure("history_bound");
        }

        const auto rebuilt = createCanonicalServerState(reducer.state().players(), reducer.state().activeSessions());
        if (!std::holds_alternative<CanonicalServerState>(rebuilt)
            || std::get<CanonicalServerState>(rebuilt) != reducer.state())
            return failure("state_reconstructs");
        return true;
    }

    SimulationResult runSimulation(std::uint64_t seed)
    {
        SimulationResult simulation;
        simulation.seed = seed;
        simulation.trace.insert(simulation.trace.end(), { 'T', '3', 'P', 1 });
        appendU64(simulation.trace, seed);

        auto random = Xoshiro256StarStar::fromWorldSeed(
            seed, RandomStreamKey::fromValues(0x5035524f50455254ULL, ClientCount).value());
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), ServerTick::fromValue(1).value(), IngressOrdinal::initial());
        CanonicalCommandReducer reducer(initialState(), observability);

        for (std::size_t batchIndex = 0; batchIndex < BatchesPerSimulation; ++batchIndex)
        {
            simulation.failedBatch = batchIndex;
            const CanonicalServerState before = reducer.state();
            const CanonicalStateVersion versionBefore = reducer.stateVersion();
            const auto publicationBefore = reducer.latestPublication();
            const std::size_t commandCount
                = static_cast<std::size_t>(drawBelow(random, MaximumGeneratedCommandsPerBatch) + 1);
            std::vector<ServerCommandProposal> proposals;
            proposals.reserve(commandCount);
            for (std::size_t commandIndex = 0; commandIndex < commandCount; ++commandIndex)
            {
                proposals.push_back(generateProposal(random, before, batchIndex, commandIndex));
                const ServerCommandProposal& proposal = proposals.back();
                appendU64(simulation.trace, proposal.sessionId().value());
                appendU64(simulation.trace, proposal.sessionGeneration().value());
                appendU64(simulation.trace, proposal.commandSequence().value());
                appendU64(simulation.trace, proposal.commandId().value());
                appendU64(simulation.trace, proposal.observedServerTick().value());
                appendU64(simulation.trace, proposal.entityPrecondition().entityId().value());
                appendU64(simulation.trace, proposal.entityPrecondition().expectedRevision().value());
                appendU64(simulation.trace, proposal.entityPrecondition().expectedAuthorityEpoch().value());
                appendU64(simulation.trace, static_cast<std::uint64_t>(proposal.motion().desiredVelocity().x()));
                appendU64(simulation.trace, static_cast<std::uint64_t>(proposal.motion().desiredVelocity().y()));
                appendU64(simulation.trace, static_cast<std::uint64_t>(proposal.motion().desiredVelocity().z()));
                if (intake.submit(proposal) != CommandSubmissionResult::Accepted)
                {
                    simulation.passed = false;
                    return simulation;
                }
            }

            if (!clock.advance(FirstTickDeadline))
            {
                simulation.passed = false;
                return simulation;
            }
            const auto pumped = intake.pump();
            if (!pumped || pumped.batches().size() != 1)
            {
                simulation.passed = false;
                return simulation;
            }
            const ServerTickCommandBatch& batch = pumped.batches().front();
            const auto result = reducer.apply(batch);
            if (!verifyBatch(before, versionBefore, publicationBefore, batch, result, reducer))
            {
                simulation.passed = false;
                return simulation;
            }
            for (const CommandDispositionRecord& disposition : result.dispositions())
            {
                const std::size_t index = static_cast<std::size_t>(disposition.disposition());
                ++simulation.dispositions[index];
                simulation.trace.push_back(static_cast<std::uint8_t>(disposition.disposition()));
                simulation.trace.push_back(disposition.acknowledgementAdvanced() ? 1 : 0);
                simulation.trace.push_back(disposition.playerStateChanged() ? 1 : 0);
                appendU64(simulation.trace, disposition.stamp().ingressOrdinal().value());
            }
            appendU64(simulation.trace, reducer.stateVersion().value());
            appendU64(simulation.trace, reducer.latestPublication()->checksum().value());
        }

        simulation.failedBatch = BatchesPerSimulation;
        simulation.finalVersion = reducer.stateVersion();
        simulation.finalChecksum = reducer.latestPublication()->checksum();
        simulation.finalStateBytes = canonicalStateBytesV1(
            reducer.stateVersion(), reducer.latestPublication()->checkpointTick(), reducer.state());
        return simulation;
    }

    bool randomized_multi_client_streams_preserve_canonical_invariants()
    {
        for (std::uint64_t seed = 1; seed <= 16; ++seed)
        {
            const auto result = runSimulation(seed * 0x9e3779b97f4a7c15ULL);
            if (!result.passed)
            {
                std::cerr << "seed=" << result.seed << " failed_batch=" << result.failedBatch << '\n';
                return false;
            }
        }
        return true;
    }

    bool same_seed_reproduces_exact_commands_dispositions_bytes_and_checksum()
    {
        constexpr std::uint64_t Seed = 0x0123456789abcdefULL;
        const auto first = runSimulation(Seed);
        const auto replay = runSimulation(Seed);
        return first.passed && replay.passed && first.trace == replay.trace
            && first.finalStateBytes == replay.finalStateBytes && first.dispositions == replay.dispositions
            && first.finalVersion == replay.finalVersion && first.finalChecksum == replay.finalChecksum;
    }

    bool different_seed_changes_the_replayable_command_trace()
    {
        const auto first = runSimulation(0x1111111111111111ULL);
        const auto second = runSimulation(0x2222222222222222ULL);
        return first.passed && second.passed && first.trace != second.trace;
    }

    bool randomized_matrix_covers_multi_client_finalizing_and_rejection_paths()
    {
        std::array<std::uint64_t, DispositionCount> coverage{};
        for (std::uint64_t seed = 101; seed <= 108; ++seed)
        {
            const auto result = runSimulation(seed);
            if (!result.passed)
                return false;
            for (std::size_t index = 0; index < coverage.size(); ++index)
                coverage[index] += result.dispositions[index];
        }
        const std::array required{
            CommandDisposition::Applied,
            CommandDisposition::UnknownSession,
            CommandDisposition::SessionGenerationMismatch,
            CommandDisposition::AlreadyFinalized,
            CommandDisposition::SequenceGap,
            CommandDisposition::DuplicateCommandId,
            CommandDisposition::EntityBindingMismatch,
            CommandDisposition::EntityRevisionMismatch,
            CommandDisposition::AuthorityEpochMismatch,
        };
        return std::all_of(required.begin(), required.end(), [&coverage](CommandDisposition disposition) {
            return coverage[static_cast<std::size_t>(disposition)] != 0;
        });
    }
}

int main()
{
    const std::array tests{
        std::pair{ "randomized_multi_client_streams_preserve_canonical_invariants",
            &randomized_multi_client_streams_preserve_canonical_invariants },
        std::pair{ "same_seed_reproduces_exact_commands_dispositions_bytes_and_checksum",
            &same_seed_reproduces_exact_commands_dispositions_bytes_and_checksum },
        std::pair{ "different_seed_changes_the_replayable_command_trace",
            &different_seed_changes_the_replayable_command_trace },
        std::pair{ "randomized_matrix_covers_multi_client_finalizing_and_rejection_paths",
            &randomized_matrix_covers_multi_client_finalizing_and_rejection_paths },
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
