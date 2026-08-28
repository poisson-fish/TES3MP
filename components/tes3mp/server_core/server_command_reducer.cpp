#include <tes3mp/server_command_reducer.hpp>

#include <algorithm>
#include <array>
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

    CanonicalSinkDeliveryReport CanonicalCommandReducer::publish(
        std::shared_ptr<CanonicalStatePublication> publication) noexcept
    {
        if (publication->mChanges.empty())
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

    CommandBatchReductionResult CanonicalCommandReducer::apply(const ServerTickCommandBatch& batch)
    {
        CommandBatchReductionResult result;
        const auto commands = batch.commands();
        const ServerTick tick = batch.scheduledTick().value();
        if (commands.size() > MaximumServerCommandsPerTick)
        {
            result.mError = CommandBatchReductionError::CommandLimitExceeded;
            observe(result.mError, tick, 0);
            return result;
        }
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            if (commands[index].stamp().eligibleServerTick() != tick)
            {
                result.mError = CommandBatchReductionError::EligibleTickMismatch;
                observe(result.mError, tick, 0);
                return result;
            }
            if (index != 0 && commands[index - 1].stamp().ingressOrdinal() >= commands[index].stamp().ingressOrdinal())
            {
                result.mError = CommandBatchReductionError::IngressOrdinalNotStrictlyIncreasing;
                observe(result.mError, tick, 0);
                return result;
            }
        }
        if (!canReserveCanonicalStateVersions(mStateVersion, commands.size()))
        {
            result.mError = CommandBatchReductionError::StateVersionCapacityExceeded;
            observe(result.mError, tick, 0);
            return result;
        }

        result.mDispositions.reserve(commands.size());
        auto publication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        publication->mChanges.reserve(commands.size());

        try
        {
            for (const StampedServerCommand& command : commands)
            {
                const ServerCommandProposal& proposal = command.proposal();
                CommandDisposition disposition = CommandDisposition::UnknownSession;
                bool acknowledgementAdvanced = false;
                bool playerStateChanged = false;

                const CanonicalSessionProgress* session = mState->findActiveSession(proposal.sessionId());
                if (session != nullptr)
                {
                    if (session->sessionGeneration() != proposal.sessionGeneration())
                        disposition = CommandDisposition::SessionGenerationMismatch;
                    else if (!finalizableSequence(*session, proposal.commandSequence()))
                        disposition = nonFinalSequenceDisposition(*session, proposal.commandSequence());
                    else
                    {
                        const std::size_t sessionIndex
                            = static_cast<std::size_t>(session - mState->activeSessions().data());
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
                                const CanonicalPlayerEntityState* player = mState->findPlayer(session->playerId());
                                playerIndex = static_cast<std::size_t>(player - mState->players().data());
                                if (precondition.expectedRevision() != player->entityRevision())
                                    disposition = CommandDisposition::EntityRevisionMismatch;
                                else if (precondition.expectedAuthorityEpoch() != player->authorityEpoch())
                                    disposition = CommandDisposition::AuthorityEpochMismatch;
                                else
                                {
                                    const auto advanced = advanceCanonicalSpatialState(
                                        *player, tick, player->transform(), proposal.motion().desiredVelocity());
                                    if (const auto* value = std::get_if<CanonicalPlayerEntityState>(&advanced))
                                    {
                                        disposition = CommandDisposition::Applied;
                                        playerReplacement = *value;
                                        playerStateChanged = true;
                                    }
                                    else if (std::get<SpatialAdvanceError>(advanced).code
                                        == SpatialAdvanceErrorCode::TickRegression)
                                        disposition = CommandDisposition::SpatialTickRegression;
                                    else
                                        disposition = CommandDisposition::EntityRevisionExhausted;
                                }
                            }
                        }

                        auto candidate = replacementState(*mState, sessionIndex,
                            FinalizedCommandRecord(proposal.commandSequence(), proposal.commandId(), disposition),
                            playerIndex, playerReplacement);
                        if (std::holds_alternative<CanonicalServerState>(candidate))
                        {
                            const CanonicalStateVersion nextVersion = *mStateVersion.next();
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
                            mState = std::move(nextState);
                            mStateVersion = nextVersion;
                            mCheckpointTick = tick;
                            acknowledgementAdvanced = true;
                        }
                        else
                        {
                            result.mError = CommandBatchReductionError::CandidateStateInvalid;
                            result.mSinkDeliveryReport = publish(std::move(publication));
                            observe(result.mError, tick, result.mDispositions.size());
                            return result;
                        }
                    }
                }

                result.mDispositions.emplace_back(command.stamp(), proposal.sessionId(), proposal.sessionGeneration(),
                    proposal.commandSequence(), proposal.commandId(), disposition, acknowledgementAdvanced,
                    playerStateChanged);
                observe(disposition, tick);
            }
        }
        catch (...)
        {
            (void)publish(std::move(publication));
            throw;
        }
        result.mSinkDeliveryReport = publish(std::move(publication));
        return result;
    }
}
