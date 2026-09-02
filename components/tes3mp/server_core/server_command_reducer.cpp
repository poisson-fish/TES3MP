#include <tes3mp/server_command_reducer.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace
{
    using namespace TES3MP;

    CommandReductionObservationOutcome observationOutcome(CommandDisposition disposition) noexcept
    {
        switch (disposition)
        {
            case CommandDisposition::Applied:
                return CommandReductionObservationOutcome::Applied;
            case CommandDisposition::UnknownSession:
                return CommandReductionObservationOutcome::UnknownSession;
            case CommandDisposition::SessionGenerationMismatch:
                return CommandReductionObservationOutcome::SessionGenerationMismatch;
            case CommandDisposition::AlreadyFinalized:
                return CommandReductionObservationOutcome::AlreadyFinalized;
            case CommandDisposition::SequenceGap:
                return CommandReductionObservationOutcome::SequenceGap;
            case CommandDisposition::DuplicateCommandId:
                return CommandReductionObservationOutcome::DuplicateCommandId;
            case CommandDisposition::EntityBindingMismatch:
                return CommandReductionObservationOutcome::EntityBindingMismatch;
            case CommandDisposition::EntityRevisionMismatch:
                return CommandReductionObservationOutcome::EntityRevisionMismatch;
            case CommandDisposition::AuthorityEpochMismatch:
                return CommandReductionObservationOutcome::AuthorityEpochMismatch;
            case CommandDisposition::SpatialTickRegression:
                return CommandReductionObservationOutcome::SpatialTickRegression;
            case CommandDisposition::EntityRevisionExhausted:
                return CommandReductionObservationOutcome::EntityRevisionExhausted;
            case CommandDisposition::UnknownFixtureCell:
                return CommandReductionObservationOutcome::UnknownFixtureCell;
        }
        return CommandReductionObservationOutcome::CandidateStateInvalid;
    }

    CommandReductionObservationOutcome observationOutcome(CommandBatchReductionError error) noexcept
    {
        switch (error)
        {
            case CommandBatchReductionError::CommandLimitExceeded:
                return CommandReductionObservationOutcome::CommandLimitExceeded;
            case CommandBatchReductionError::EligibleTickMismatch:
                return CommandReductionObservationOutcome::EligibleTickMismatch;
            case CommandBatchReductionError::IngressOrdinalNotStrictlyIncreasing:
                return CommandReductionObservationOutcome::IngressOrdinalNotStrictlyIncreasing;
            case CommandBatchReductionError::StateVersionCapacityExceeded:
                return CommandReductionObservationOutcome::StateVersionCapacityExceeded;
            case CommandBatchReductionError::CandidateStateInvalid:
                return CommandReductionObservationOutcome::CandidateStateInvalid;
            case CommandBatchReductionError::None:
                break;
        }
        return CommandReductionObservationOutcome::CandidateStateInvalid;
    }

    MetricDimensionValue metricValue(CommandReductionObservationOutcome outcome) noexcept
    {
        return static_cast<MetricDimensionValue>(
            static_cast<std::uint8_t>(MetricDimensionValue::CommandReductionApplied)
            + static_cast<std::uint8_t>(outcome));
    }

    CanonicalSinkObservationRole observationRole(CanonicalSinkRole role) noexcept
    {
        switch (role)
        {
            case CanonicalSinkRole::Persistence:
                return CanonicalSinkObservationRole::Persistence;
            case CanonicalSinkRole::Replay:
                return CanonicalSinkObservationRole::Replay;
            case CanonicalSinkRole::Script:
                return CanonicalSinkObservationRole::Script;
            case CanonicalSinkRole::Metrics:
                return CanonicalSinkObservationRole::Metrics;
        }
        return CanonicalSinkObservationRole::Persistence;
    }

    CanonicalSinkObservationOutcome observationOutcome(CanonicalSinkDeliveryResult result) noexcept
    {
        switch (result)
        {
            case CanonicalSinkDeliveryResult::Accepted:
                return CanonicalSinkObservationOutcome::Accepted;
            case CanonicalSinkDeliveryResult::Backpressured:
                return CanonicalSinkObservationOutcome::Backpressured;
            case CanonicalSinkDeliveryResult::Failed:
            case CanonicalSinkDeliveryResult::NotConfigured:
                return CanonicalSinkObservationOutcome::Failed;
        }
        return CanonicalSinkObservationOutcome::Failed;
    }

    MetricDimensionValue metricValue(CanonicalSinkRole role) noexcept
    {
        return static_cast<MetricDimensionValue>(
            static_cast<std::uint8_t>(MetricDimensionValue::CanonicalSinkPersistence)
            + static_cast<std::uint8_t>(role));
    }

    MetricDimensionValue metricValue(CanonicalSinkDeliveryResult result) noexcept
    {
        const auto outcome = observationOutcome(result);
        return static_cast<MetricDimensionValue>(static_cast<std::uint8_t>(MetricDimensionValue::CanonicalSinkAccepted)
            + static_cast<std::uint8_t>(outcome));
    }

    bool finalizableSequence(const CanonicalSessionProgress& session, CommandSequence sequence) noexcept
    {
        const auto finalized = session.highestContiguousFinalizedCommand();
        if (!finalized)
            return sequence == CommandSequence::initial();
        const auto next = finalized->next();
        return next && sequence == *next;
    }

    constexpr CellId Phase7InteriorFixture = CellId::interior(CellSpaceId::fromValue(7).value());
    constexpr CellId Phase7ExteriorFixture = CellId::exterior(CellSpaceId::fromValue(8).value(), 0, 0);

    constexpr bool isPhase7Fixture(const CellId& cell) noexcept
    {
        return cell == Phase7InteriorFixture || cell == Phase7ExteriorFixture;
    }

    CommandDisposition nonFinalSequenceDisposition(
        const CanonicalSessionProgress& session, CommandSequence sequence) noexcept
    {
        const auto finalized = session.highestContiguousFinalizedCommand();
        return finalized && sequence <= *finalized ? CommandDisposition::AlreadyFinalized
                                                   : CommandDisposition::SequenceGap;
    }

    std::variant<CanonicalServerState, CanonicalStateError> replacementState(const CanonicalServerState& current,
        std::size_t sessionIndex, FinalizedCommandRecord finalizedCommand, std::optional<std::size_t> playerIndex,
        std::optional<CanonicalPlayerEntityState> playerReplacement)
    {
        std::vector<CanonicalPlayerEntityState> players(current.players().begin(), current.players().end());
        std::vector<CanonicalSessionProgress> sessions(
            current.activeSessions().begin(), current.activeSessions().end());

        const CanonicalSessionProgress& session = sessions[sessionIndex];
        std::vector<FinalizedCommandRecord> history(
            session.finalizedCommandHistory().begin(), session.finalizedCommandHistory().end());
        if (history.size() == MaximumFinalizedCommandHistory)
            history.erase(history.begin());
        history.push_back(finalizedCommand);
        auto replacementSession = createCanonicalSessionProgress(session.sessionId(), session.sessionGeneration(),
            session.playerId(), session.entityId(), finalizedCommand.commandSequence(), history);
        if (const auto* error = std::get_if<CanonicalSessionHistoryError>(&replacementSession))
            return CanonicalStateError{ CanonicalStateErrorCode::FinalizedHistoryNotStrictlyOrdered, sessionIndex,
                error->value, error->relatedValue };
        sessions[sessionIndex] = std::get<CanonicalSessionProgress>(std::move(replacementSession));
        if (playerIndex && playerReplacement)
            players[*playerIndex] = *playerReplacement;

        return createCanonicalServerState(players, sessions);
    }
}

namespace TES3MP
{
    namespace
    {
        std::optional<std::int64_t> checkedAdd(std::int64_t left, std::int64_t right) noexcept
        {
            if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
                return std::nullopt;
            return left + right;
        }
    }
    CanonicalCommandReducer::CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability)
        : CanonicalCommandReducer(std::move(initialState), observability, CanonicalSinkBundle{})
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(
        CanonicalServerState initialState, Observability& observability, CanonicalSinkBundle sinks)
        : mState(std::make_shared<CanonicalServerState>(std::move(initialState)))
        , mLatestPublication(std::shared_ptr<const CanonicalStatePublication>(
              new CanonicalStatePublication(mStateVersion, mCheckpointTick, mState, {})))
        , mObservability(observability)
        , mSinks(sinks)
    {
    }

    std::shared_ptr<const CanonicalStatePublication> CanonicalCommandReducer::latestPublication() const noexcept
    {
        return std::atomic_load_explicit(&mLatestPublication, std::memory_order_acquire);
    }

    std::optional<CanonicalCommandReducer::PreparedJoin> CanonicalCommandReducer::prepareJoin(
        CanonicalPlayerEntityState player, CanonicalSessionProgress session, ServerTick tick)
    {
        if (!mStateVersion.next()) return std::nullopt;
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        std::vector<CanonicalSessionProgress> sessions(mState->activeSessions().begin(), mState->activeSessions().end());
        players.push_back(player);
        sessions.push_back(session);
        auto candidate = createCanonicalServerState(players, sessions);
        auto* state = std::get_if<CanonicalServerState>(&candidate);
        if (!state) return std::nullopt;
        PreparedJoin prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = *mStateVersion.next();
        prepared.mCheckpointTick = tick;
        prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
        prepared.mPublication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        prepared.mPublication->mJoinedSessions.push_back(
            { prepared.mStateVersion, tick, session, player });
        return prepared;
    }

    bool CanonicalCommandReducer::commit(PreparedJoin&& prepared)
    {
        if (prepared.mBaseVersion != mStateVersion || !prepared.mState || !prepared.mPublication) return false;
        mState = std::move(prepared.mState);
        mStateVersion = prepared.mStateVersion;
        mCheckpointTick = prepared.mCheckpointTick;
        prepared.mPublication->mStateVersion = mStateVersion;
        prepared.mPublication->mCheckpointTick = mCheckpointTick;
        prepared.mPublication->mState = mState;
        prepared.mPublication->mChecksum = canonicalStateChecksumV1(mStateVersion, mCheckpointTick, *mState);
        std::shared_ptr<const CanonicalStatePublication> committed = std::move(prepared.mPublication);
        std::atomic_store_explicit(&mLatestPublication, committed, std::memory_order_release);
        (void)deliver(committed);
        return true;
    }

    CanonicalSinkDeliveryReport CanonicalCommandReducer::publish(
        std::shared_ptr<CanonicalStatePublication> publication) noexcept
    {
        if (publication->mChanges.empty() && publication->mSpatialTicks.empty())
            return {};
        publication->mStateVersion = mStateVersion;
        publication->mCheckpointTick = mCheckpointTick;
        publication->mState = mState;
        publication->mChecksum = canonicalStateChecksumV1(mStateVersion, mCheckpointTick, *mState);
        std::shared_ptr<const CanonicalStatePublication> committed = std::move(publication);
        std::atomic_store_explicit(&mLatestPublication, committed, std::memory_order_release);
        return deliver(committed);
    }

    CanonicalSinkDeliveryReport CanonicalCommandReducer::deliver(
        const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept
    {
        CanonicalSinkDeliveryReport report;
        report.markPublicationOffered();
        const auto attempt = [this, &publication, &report](CanonicalSinkRole role, auto* sink) noexcept {
            if (sink == nullptr)
                return;
            CanonicalSinkDeliveryResult result = sink->tryConsume(publication);
            if (result == CanonicalSinkDeliveryResult::NotConfigured)
                result = CanonicalSinkDeliveryResult::Failed;
            report.setResult(role, result);
            observe(role, result, publication->checkpointTick());
        };
        attempt(CanonicalSinkRole::Persistence, mSinks.persistence());
        attempt(CanonicalSinkRole::Replay, mSinks.replay());
        attempt(CanonicalSinkRole::Script, mSinks.script());
        attempt(CanonicalSinkRole::Metrics, mSinks.metrics());
        return report;
    }

    void CanonicalCommandReducer::observe(CommandDisposition disposition, ServerTick tick) noexcept
    {
        const auto outcome = observationOutcome(disposition);
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::CommandReductionOutcome, metricValue(outcome) },
        };
        if (const auto metric
            = MetricObservation::create(MetricKey::CommandReductionOutcomes, CounterAddition{ 1 }, dimensions))
            (void)mObservability.metrics().tryRecord(*metric);

        const EventSeverity severity = disposition == CommandDisposition::Applied ? EventSeverity::Info
            : disposition == CommandDisposition::AlreadyFinalized || disposition == CommandDisposition::SequenceGap
            ? EventSeverity::Debug
            : EventSeverity::Warning;
        if (const auto event = StructuredEvent::create(severity, tick, CommandReductionEvent{ outcome, 1 }))
            (void)mObservability.events().tryRecord(*event);
    }

    void CanonicalCommandReducer::observe(
        CommandBatchReductionError error, ServerTick tick, std::uint64_t processedCommands) noexcept
    {
        const auto outcome = observationOutcome(error);
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::CommandReductionOutcome, metricValue(outcome) },
        };
        if (const auto metric
            = MetricObservation::create(MetricKey::CommandReductionOutcomes, CounterAddition{ 1 }, dimensions))
            (void)mObservability.metrics().tryRecord(*metric);
        if (const auto event
            = StructuredEvent::create(EventSeverity::Error, tick, CommandReductionEvent{ outcome, processedCommands }))
            (void)mObservability.events().tryRecord(*event);
    }

    void CanonicalCommandReducer::observe(
        CanonicalSinkRole role, CanonicalSinkDeliveryResult result, ServerTick tick) noexcept
    {
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::CanonicalSinkRole, metricValue(role) },
            MetricDimension{ MetricDimensionKey::CanonicalSinkDeliveryOutcome, metricValue(result) },
        };
        if (const auto metric
            = MetricObservation::create(MetricKey::CanonicalSinkDeliveries, CounterAddition{ 1 }, dimensions))
            (void)mObservability.metrics().tryRecord(*metric);

        const EventSeverity severity = result == CanonicalSinkDeliveryResult::Accepted ? EventSeverity::Debug
            : result == CanonicalSinkDeliveryResult::Backpressured                     ? EventSeverity::Warning
                                                                                       : EventSeverity::Error;
        if (const auto event = StructuredEvent::create(
                severity, tick, CanonicalSinkDeliveryEvent{ observationRole(role), observationOutcome(result) }))
            (void)mObservability.events().tryRecord(*event);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepare(const ServerTickCommandBatch& batch)
    {
        PreparedBatch prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = mStateVersion;
        prepared.mCheckpointTick = mCheckpointTick;
        prepared.mState = mState;
        auto& result = prepared.mResult;
        const auto commands = batch.commands();
        const ServerTick tick = batch.scheduledTick().value();
        auto publication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(prepared.mStateVersion, tick, prepared.mState, {}));
        prepared.mPublication = publication;
        if (commands.size() > MaximumServerCommandsPerTick)
        {
            result.mError = CommandBatchReductionError::CommandLimitExceeded;
            return prepared;
        }
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            if (commands[index].stamp().eligibleServerTick() != tick)
            {
                result.mError = CommandBatchReductionError::EligibleTickMismatch;
                return prepared;
            }
            if (index != 0 && commands[index - 1].stamp().ingressOrdinal() >= commands[index].stamp().ingressOrdinal())
            {
                result.mError = CommandBatchReductionError::IngressOrdinalNotStrictlyIncreasing;
                return prepared;
            }
        }
        if (!canReserveCanonicalStateVersions(prepared.mStateVersion, commands.size()))
        {
            result.mError = CommandBatchReductionError::StateVersionCapacityExceeded;
            return prepared;
        }

        result.mDispositions.reserve(commands.size());
        publication->mChanges.reserve(commands.size());

        try
        {
            for (const StampedServerCommand& command : commands)
            {
                const ServerCommandProposal& proposal = command.proposal();
                CommandDisposition disposition = CommandDisposition::UnknownSession;
                bool acknowledgementAdvanced = false;
                bool playerStateChanged = false;

                const CanonicalSessionProgress* session = prepared.mState->findActiveSession(proposal.sessionId());
                if (session != nullptr)
                {
                    if (session->sessionGeneration() != proposal.sessionGeneration())
                        disposition = CommandDisposition::SessionGenerationMismatch;
                    else if (!finalizableSequence(*session, proposal.commandSequence()))
                        disposition = nonFinalSequenceDisposition(*session, proposal.commandSequence());
                    else
                    {
                        const std::size_t sessionIndex
                            = static_cast<std::size_t>(session - prepared.mState->activeSessions().data());
                        std::optional<std::size_t> playerIndex;
                        std::optional<CanonicalPlayerEntityState> playerReplacement;

                        if (session->containsFinalizedCommandId(proposal.commandId()))
                            disposition = CommandDisposition::DuplicateCommandId;
                        else
                        {
                            const EntityPrecondition precondition = proposal.entityPrecondition();
                            if (precondition.entityId() != session->entityId())
                                disposition = CommandDisposition::EntityBindingMismatch;
                            else
                            {
                                const CanonicalPlayerEntityState* player = prepared.mState->findPlayer(session->playerId());
                                playerIndex = static_cast<std::size_t>(player - prepared.mState->players().data());
                                if (precondition.expectedRevision() != player->entityRevision())
                                    disposition = CommandDisposition::EntityRevisionMismatch;
                                else if (precondition.expectedAuthorityEpoch() != player->authorityEpoch())
                                    disposition = CommandDisposition::AuthorityEpochMismatch;
                                else
                                {
                                    Transform replacementTransform = player->transform();
                                    LinearVelocity3 replacementVelocity = player->linearVelocity();
                                    bool requiresSpatialAdvance = true;
                                    if (const auto* motion
                                        = std::get_if<PlayerMotionCommandProposal>(&proposal.payload()))
                                        replacementVelocity = motion->desiredVelocity();
                                    else
                                    {
                                        const auto& requested = std::get<FixtureCellTransitionCommandProposal>(
                                            proposal.payload()).requestedCell();
                                        if (!isPhase7Fixture(requested))
                                        {
                                            disposition = CommandDisposition::UnknownFixtureCell;
                                            requiresSpatialAdvance = false;
                                        }
                                        else if (requested == player->transform().cell())
                                        {
                                            disposition = CommandDisposition::Applied;
                                            requiresSpatialAdvance = false;
                                        }
                                        else
                                        {
                                            replacementTransform = Transform(requested, player->transform().position(),
                                                player->transform().orientation());
                                        }
                                    }
                                    const auto advanced = requiresSpatialAdvance
                                        ? std::optional<SpatialAdvanceResult>(advanceCanonicalSpatialState(
                                              *player, tick, replacementTransform, replacementVelocity))
                                        : std::nullopt;
                                    if (!advanced)
                                    {
                                        // The command is finalized below without a spatial replacement.
                                    }
                                    else if (const auto* value = std::get_if<CanonicalPlayerEntityState>(&*advanced))
                                    {
                                        disposition = CommandDisposition::Applied;
                                        playerReplacement = *value;
                                        playerStateChanged = true;
                                    }
                                    else if (std::get<SpatialAdvanceError>(*advanced).code
                                        == SpatialAdvanceErrorCode::TickRegression)
                                        disposition = CommandDisposition::SpatialTickRegression;
                                    else
                                        disposition = CommandDisposition::EntityRevisionExhausted;
                                }
                            }
                        }

                        auto candidate = replacementState(*prepared.mState, sessionIndex,
                            FinalizedCommandRecord(proposal.commandSequence(), proposal.commandId(), disposition),
                            playerIndex, playerReplacement);
                        if (std::holds_alternative<CanonicalServerState>(candidate))
                        {
                            const CanonicalStateVersion nextVersion = *prepared.mStateVersion.next();
                            CanonicalServerState& candidateState = std::get<CanonicalServerState>(candidate);
                            const CanonicalSessionProgress sessionReplacement
                                = candidateState.activeSessions()[sessionIndex];
                            std::optional<CanonicalPlayerEntityState> committedPlayerReplacement;
                            if (playerReplacement)
                                committedPlayerReplacement = candidateState.players()[*playerIndex];

                            CanonicalStateChangeRecord change(nextVersion, command.stamp(), proposal.sessionId(),
                                proposal.sessionGeneration(), proposal.commandSequence(), proposal.commandId(),
                                disposition, sessionReplacement, committedPlayerReplacement);
                            auto nextState = std::make_shared<CanonicalServerState>(std::move(candidateState));
                            publication->mChanges.push_back(std::move(change));
                            prepared.mState = std::move(nextState);
                            prepared.mStateVersion = nextVersion;
                            prepared.mCheckpointTick = tick;
                            acknowledgementAdvanced = true;
                        }
                        else
                        {
                            result.mError = CommandBatchReductionError::CandidateStateInvalid;
                            prepared.mPublication = std::move(publication);
                            return prepared;
                        }
                    }
                }

                result.mDispositions.emplace_back(command.stamp(), proposal.sessionId(), proposal.sessionGeneration(),
                    proposal.commandSequence(), proposal.commandId(), disposition, acknowledgementAdvanced,
                    playerStateChanged);
            }
        }
        catch (...)
        {
            throw;
        }
        prepared.mPublication = std::move(publication);
        return prepared;
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(const ServerTickCommandBatch& batch)
    {
        auto prepared = prepare(batch);
        if (!prepared.result()) return prepared;
        const ServerTick tick = batch.scheduledTick().value();
        const auto players = prepared.mState->players();
        std::size_t moving = 0;
        for (const auto& player : players)
            if (player.linearVelocity() != LinearVelocity3(0, 0, 0)) ++moving;
        if (!canReserveCanonicalStateVersions(prepared.mStateVersion, moving))
        {
            prepared.mResult.mError = CommandBatchReductionError::StateVersionCapacityExceeded;
            return prepared;
        }
        std::vector<CanonicalPlayerEntityState> replacements(players.begin(), players.end());
        try
        {
            for (std::size_t index = 0; index < replacements.size(); ++index)
            {
                const auto current = replacements[index];
                const auto velocity = current.linearVelocity();
                if (velocity == LinearVelocity3(0, 0, 0)) continue;
                const auto position = current.transform().position();
                const auto x = checkedAdd(position.x(), velocity.x());
                const auto y = checkedAdd(position.y(), velocity.y());
                const auto z = checkedAdd(position.z(), velocity.z());
                if (!x || !y || !z)
                {
                    prepared.mResult.mError = CommandBatchReductionError::SpatialIntegrationOverflow;
                    return prepared;
                }
                const Transform transform(current.transform().cell(), Position3(*x, *y, *z),
                    current.transform().orientation());
                auto advanced = advanceCanonicalSpatialState(current, tick, transform, velocity);
                auto* value = std::get_if<CanonicalPlayerEntityState>(&advanced);
                if (!value)
                {
                    prepared.mResult.mError = CommandBatchReductionError::SpatialRevisionExhausted;
                    return prepared;
                }
                replacements[index] = *value;
                prepared.mStateVersion = *prepared.mStateVersion.next();
                prepared.mPublication->mSpatialTicks.push_back({ prepared.mStateVersion, tick, *value });
            }
            auto candidate = createCanonicalServerState(replacements, prepared.mState->activeSessions());
            auto* state = std::get_if<CanonicalServerState>(&candidate);
            if (!state)
            {
                prepared.mResult.mError = CommandBatchReductionError::CandidateStateInvalid;
                return prepared;
            }
            prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
            prepared.mCheckpointTick = tick;
        }
        catch (...)
        {
            prepared.mResult.mError = CommandBatchReductionError::CandidateStateInvalid;
        }
        return prepared;
    }

    bool CanonicalCommandReducer::commit(PreparedBatch&& prepared)
    {
        if (prepared.mBaseVersion != mStateVersion || !prepared.mState || !prepared.mPublication)
            return false;
        mState = std::move(prepared.mState);
        mStateVersion = prepared.mStateVersion;
        mCheckpointTick = prepared.mCheckpointTick;
        for (const auto& record : prepared.mResult.mDispositions)
            observe(record.disposition(), mCheckpointTick);
        if (prepared.mResult.mError != CommandBatchReductionError::None)
            observe(prepared.mResult.mError, mCheckpointTick, prepared.mResult.mDispositions.size());
        prepared.mResult.mSinkDeliveryReport = publish(std::move(prepared.mPublication));
        return true;
    }

    CommandBatchReductionResult CanonicalCommandReducer::apply(const ServerTickCommandBatch& batch)
    {
        auto prepared = prepare(batch);
        if (commit(std::move(prepared)))
            return std::move(prepared.mResult);
        prepared.mResult.mError = CommandBatchReductionError::CandidateStateInvalid;
        return std::move(prepared.mResult);
    }
}
