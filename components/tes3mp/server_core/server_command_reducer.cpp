#include <tes3mp/movement_policy.hpp>
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
            case CommandDisposition::UnknownCell:
                return CommandReductionObservationOutcome::UnknownCell;
            case CommandDisposition::MotionOutOfRange:
                return CommandReductionObservationOutcome::MotionOutOfRange;
            case CommandDisposition::ObjectInteractionRejected:
                return CommandReductionObservationOutcome::ObjectInteractionRejected;
            case CommandDisposition::InventoryTransactionRejected:
                return CommandReductionObservationOutcome::InventoryTransactionRejected;
            case CommandDisposition::CombatRejected:
                return CommandReductionObservationOutcome::CombatRejected;
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
    namespace
    {
        ServerCollisionQuery& compatibilityCollision() noexcept
        {
            static UnobstructedServerCollisionQuery query;
            return query;
        }
    }
    CanonicalCommandReducer::CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability)
        : CanonicalCommandReducer(std::move(initialState), observability, CanonicalSinkBundle{}, testContentManifest())
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(
        CanonicalServerState initialState, Observability& observability, ContentManifest contentManifest)
        : CanonicalCommandReducer(
              std::move(initialState), observability, CanonicalSinkBundle{}, contentManifest, compatibilityCollision())
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability,
        ContentManifest contentManifest, ServerCollisionQuery& collision)
        : CanonicalCommandReducer(
              std::move(initialState), observability, CanonicalSinkBundle{}, contentManifest, collision)
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(
        CanonicalServerState initialState, Observability& observability, CanonicalSinkBundle sinks)
        : CanonicalCommandReducer(std::move(initialState), observability, sinks, testContentManifest())
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability,
        CanonicalSinkBundle sinks, ContentManifest contentManifest)
        : CanonicalCommandReducer(
              std::move(initialState), observability, sinks, contentManifest, compatibilityCollision())
    {
    }

    CanonicalCommandReducer::CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability,
        CanonicalSinkBundle sinks, ContentManifest contentManifest, ServerCollisionQuery& collision)
        : mState(std::make_shared<CanonicalServerState>(std::move(initialState)))
        , mLatestPublication(std::shared_ptr<const CanonicalStatePublication>(
              new CanonicalStatePublication(mStateVersion, mCheckpointTick, mState, {})))
        , mObservability(observability)
        , mSinks(sinks)
        , mContentManifest(contentManifest)
        , mCollision(&collision)
    {
    }

    std::shared_ptr<const CanonicalStatePublication> CanonicalCommandReducer::latestPublication() const noexcept
    {
        return std::atomic_load_explicit(&mLatestPublication, std::memory_order_acquire);
    }

    std::optional<CanonicalCommandReducer::PreparedJoin> CanonicalCommandReducer::prepareJoin(
        CanonicalPlayerEntityState player, CanonicalSessionProgress session, ServerTick tick)
    {
        if (!mStateVersion.next() || !mCanonicalRevision.next())
            return std::nullopt;
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        const auto existing = std::find_if(
            players.begin(), players.end(), [&](const auto& value) { return value.playerId() == player.playerId(); });
        if (existing == players.end())
            players.push_back(player);
        else if (*existing != player)
        {
            const auto nextRevision = existing->entityRevision().next();
            const auto nextAuthority = existing->authorityEpoch().next();
            if (!nextRevision || !nextAuthority || player.entityId() != existing->entityId()
                || player.appearanceId() != existing->appearanceId()
                || player.entityRevision() != *nextRevision || player.authorityEpoch() != *nextAuthority
                || std::ranges::any_of(sessions, [&](const auto& value) {
                       return value.playerId() == player.playerId();
                   }))
                return std::nullopt;
            *existing = player;
        }
        sessions.push_back(session);
        auto candidate = createCanonicalServerState(players, sessions);
        auto* state = std::get_if<CanonicalServerState>(&candidate);
        if (!state)
            return std::nullopt;
        PreparedJoin prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = *mStateVersion.next();
        prepared.mBaseCanonicalRevision = mCanonicalRevision;
        prepared.mCanonicalRevision = *mCanonicalRevision.next();
        prepared.mCheckpointTick = tick;
        prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
        prepared.mPublication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        prepared.mPublication->mJoinedSessions.push_back({ prepared.mStateVersion, tick, session, player });
        return prepared;
    }

    bool CanonicalCommandReducer::commit(PreparedJoin&& prepared)
    {
        if (prepared.mBaseVersion != mStateVersion || prepared.mBaseCanonicalRevision != mCanonicalRevision
            || !prepared.mState || !prepared.mPublication)
            return false;
        mState = std::move(prepared.mState);
        mStateVersion = prepared.mStateVersion;
        mCanonicalRevision = prepared.mCanonicalRevision;
        mCheckpointTick = prepared.mCheckpointTick;
        prepared.mPublication->mStateVersion = mStateVersion;
        prepared.mPublication->mCheckpointTick = mCheckpointTick;
        prepared.mPublication->mState = mState;
        prepared.mPublication->mChecksum = canonicalStateChecksumV2(mStateVersion, mCheckpointTick, *mState);
        std::shared_ptr<const CanonicalStatePublication> committed = std::move(prepared.mPublication);
        std::atomic_store_explicit(&mLatestPublication, committed, std::memory_order_release);
        (void)deliver(committed);
        return true;
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::prepareLifecycleState(
        std::vector<CanonicalPlayerEntityState> players, std::vector<CanonicalSessionProgress> sessions,
        CanonicalSessionLifecycleKind kind, SessionId session, PlayerId player, SessionGeneration generation,
        ServerTick tick)
    {
        auto candidate = createCanonicalServerState(players, sessions);
        auto* state = std::get_if<CanonicalServerState>(&candidate);
        if (!state || !mStateVersion.next() || !mCanonicalRevision.next())
            return std::nullopt;
        PreparedLifecycle prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = *mStateVersion.next();
        prepared.mBaseCanonicalRevision = mCanonicalRevision;
        prepared.mCanonicalRevision = *mCanonicalRevision.next();
        prepared.mCheckpointTick = tick;
        prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
        prepared.mPublication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        prepared.mPublication->mSessionLifecycle.push_back(
            { prepared.mStateVersion, tick, kind, session, player, generation });
        return prepared;
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::prepareDisconnect(
        SessionId sessionId, ServerTick tick)
    {
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        const auto found = std::find_if(sessions.begin(), sessions.end(),
            [sessionId](const auto& value) { return value.sessionId() == sessionId; });
        if (found == sessions.end())
            return std::nullopt;
        const auto player = found->playerId();
        const auto generation = found->sessionGeneration();
        sessions.erase(found);
        return prepareLifecycleState(std::move(players), std::move(sessions),
            CanonicalSessionLifecycleKind::Disconnected, sessionId, player, generation, tick);
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::prepareDisconnectBatch(
        std::span<const SessionId> sessionIds, ServerTick tick)
    {
        if (sessionIds.empty() || !mStateVersion.next() || !mCanonicalRevision.next())
            return std::nullopt;
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        std::vector<CanonicalSessionProgress> removed;
        removed.reserve(sessionIds.size());
        for (const auto sessionId : sessionIds)
        {
            const auto found = std::find_if(sessions.begin(), sessions.end(),
                [sessionId](const auto& value) { return value.sessionId() == sessionId; });
            if (found == sessions.end())
                return std::nullopt;
            removed.push_back(*found);
            sessions.erase(found);
        }
        auto candidate = createCanonicalServerState(players, sessions);
        auto* state = std::get_if<CanonicalServerState>(&candidate);
        if (!state)
            return std::nullopt;
        PreparedLifecycle prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = *mStateVersion.next();
        prepared.mBaseCanonicalRevision = mCanonicalRevision;
        prepared.mCanonicalRevision = *mCanonicalRevision.next();
        prepared.mCheckpointTick = tick;
        prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
        prepared.mPublication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        for (const auto& session : removed)
            prepared.mPublication->mSessionLifecycle.push_back(
                { prepared.mStateVersion, tick, CanonicalSessionLifecycleKind::Disconnected, session.sessionId(),
                    session.playerId(), session.sessionGeneration() });
        return prepared;
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::prepareResume(
        CanonicalSessionProgress session, ServerTick tick)
    {
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        if (!mState->findPlayer(session.playerId()) || mState->findActiveSession(session.sessionId()))
            return std::nullopt;
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        sessions.push_back(session);
        std::sort(sessions.begin(), sessions.end(),
            [](const auto& a, const auto& b) { return a.sessionId() < b.sessionId(); });
        return prepareLifecycleState(std::move(players), std::move(sessions), CanonicalSessionLifecycleKind::Resumed,
            session.sessionId(), session.playerId(), session.sessionGeneration(), tick);
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::preparePlayerSafePoint(
        PlayerId playerId, Transform transform, ServerTick tick)
    {
        if (!mContentManifest.contains(transform.cell()) || !mStateVersion.next() || !mCanonicalRevision.next())
            return std::nullopt;
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        const auto found = std::find_if(
            players.begin(), players.end(), [playerId](const auto& value) { return value.playerId() == playerId; });
        if (found == players.end()
            || !std::ranges::any_of(mState->activeSessions(),
                [playerId](const auto& session) { return session.playerId() == playerId; }))
            return std::nullopt;
        auto advanced = advanceCanonicalSpatialState(
            *found, tick, transform, LinearVelocity3(0, 0, 0), LocomotionMode::Walk);
        auto* replacement = std::get_if<CanonicalPlayerEntityState>(&advanced);
        if (!replacement)
            return std::nullopt;
        *found = *replacement;
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        auto candidate = createCanonicalServerState(players, sessions);
        auto* state = std::get_if<CanonicalServerState>(&candidate);
        if (!state)
            return std::nullopt;
        PreparedLifecycle prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = *mStateVersion.next();
        prepared.mBaseCanonicalRevision = mCanonicalRevision;
        prepared.mCanonicalRevision = *mCanonicalRevision.next();
        prepared.mCheckpointTick = tick;
        prepared.mState = std::make_shared<CanonicalServerState>(std::move(*state));
        prepared.mPublication = std::shared_ptr<CanonicalStatePublication>(
            new CanonicalStatePublication(mStateVersion, tick, mState, {}));
        prepared.mPublication->mSpatialTicks.push_back({ prepared.mStateVersion, tick, *replacement });
        return prepared;
    }

    std::optional<CanonicalCommandReducer::PreparedLifecycle> CanonicalCommandReducer::prepareExpiration(
        PlayerId playerId, SessionId sessionId, SessionGeneration generation, ServerTick tick)
    {
        std::vector<CanonicalPlayerEntityState> players(mState->players().begin(), mState->players().end());
        const auto found = std::find_if(
            players.begin(), players.end(), [playerId](const auto& value) { return value.playerId() == playerId; });
        if (found == players.end())
            return std::nullopt;
        players.erase(found);
        std::vector<CanonicalSessionProgress> sessions(
            mState->activeSessions().begin(), mState->activeSessions().end());
        if (std::any_of(sessions.begin(), sessions.end(),
                [playerId](const auto& value) { return value.playerId() == playerId; }))
            return std::nullopt;
        return prepareLifecycleState(std::move(players), std::move(sessions), CanonicalSessionLifecycleKind::Expired,
            sessionId, playerId, generation, tick);
    }

    bool CanonicalCommandReducer::commit(PreparedLifecycle&& prepared)
    {
        if (prepared.mBaseVersion != mStateVersion || prepared.mBaseCanonicalRevision != mCanonicalRevision
            || !prepared.mState || !prepared.mPublication)
            return false;
        mState = std::move(prepared.mState);
        mStateVersion = prepared.mStateVersion;
        mCanonicalRevision = prepared.mCanonicalRevision;
        mCheckpointTick = prepared.mCheckpointTick;
        prepared.mPublication->mStateVersion = mStateVersion;
        prepared.mPublication->mCheckpointTick = mCheckpointTick;
        prepared.mPublication->mState = mState;
        prepared.mPublication->mChecksum = canonicalStateChecksumV2(mStateVersion, mCheckpointTick, *mState);
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
        publication->mChecksum = canonicalStateChecksumV2(mStateVersion, mCheckpointTick, *mState);
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

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareCommands(const ServerTickCommandBatch& batch,
        const CanonicalInteractiveObjectWorld* objects, const InteractiveObjectCatalog* objectCatalog,
        const CanonicalInventoryWorld* inventory, const ItemPrototypeCatalog* itemCatalog,
        const CanonicalCombatWorld* combat, const CanonicalActorWorld* actors,
        const MeleeWeaponCatalog* meleeWeapons,
        const OpenMwMeleeSettings* meleeSettings, const MeleeAuthorityPolicy* meleePolicy,
        ServerMeleeContactQuery* meleeContact)
    {
        PreparedBatch prepared;
        prepared.mBaseVersion = mStateVersion;
        prepared.mStateVersion = mStateVersion;
        prepared.mBaseCanonicalRevision = mCanonicalRevision;
        prepared.mCanonicalRevision = mCanonicalRevision;
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
        const bool hasObjectInteraction = std::ranges::any_of(commands, [](const StampedServerCommand& command) {
            return std::holds_alternative<InteractiveObjectCommandProposal>(command.proposal().payload());
        });
        if (hasObjectInteraction && objects != nullptr)
        {
            try
            {
                prepared.mBaseInteractiveObjects = *objects;
                prepared.mInteractiveObjects = *objects;
            }
            catch (...)
            {
                result.mError = CommandBatchReductionError::CandidateStateInvalid;
                return prepared;
            }
        }
        const bool hasInventoryTransaction = std::ranges::any_of(commands, [](const StampedServerCommand& command) {
            return std::holds_alternative<InventoryCommandProposal>(command.proposal().payload());
        });
        const bool hasMeleeAttack = std::ranges::any_of(commands, [](const StampedServerCommand& command) {
            return std::holds_alternative<MeleeAttackCommandProposal>(command.proposal().payload());
        });
        if ((hasInventoryTransaction || hasObjectInteraction || hasMeleeAttack) && inventory != nullptr)
        {
            try
            {
                prepared.mBaseInventory = *inventory;
                prepared.mInventory = *inventory;
            }
            catch (...)
            {
                result.mError = CommandBatchReductionError::CandidateStateInvalid;
                return prepared;
            }
        }
        if ((hasMeleeAttack || hasInventoryTransaction) && combat != nullptr)
        {
            try
            {
                prepared.mBaseCombat = *combat;
                prepared.mCombat = *combat;
            }
            catch (...)
            {
                result.mError = CommandBatchReductionError::CandidateStateInvalid;
                return prepared;
            }
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
                        std::optional<ObjectInteractionOutcome> objectInteractionOutcome;

                        if (session->containsFinalizedCommandId(proposal.commandId()))
                            disposition = CommandDisposition::DuplicateCommandId;
                        else
                        {
                            const EntityPrecondition precondition = proposal.entityPrecondition();
                            if (precondition.entityId() != session->entityId())
                                disposition = CommandDisposition::EntityBindingMismatch;
                            else
                            {
                                const CanonicalPlayerEntityState* player
                                    = prepared.mState->findPlayer(session->playerId());
                                playerIndex = static_cast<std::size_t>(player - prepared.mState->players().data());
                                if (precondition.expectedAuthorityEpoch() != player->authorityEpoch())
                                    disposition = CommandDisposition::AuthorityEpochMismatch;
                                else
                                {
                                    Transform replacementTransform = player->transform();
                                    LinearVelocity3 replacementVelocity = player->linearVelocity();
                                    LocomotionMode replacementLocomotionMode = player->locomotionMode();
                                    bool requiresSpatialAdvance = true;
                                    if (const auto* motion
                                        = std::get_if<PlayerMotionCommandProposal>(&proposal.payload()))
                                    {
                                        if (!isLegacyMotionVelocitySafe(motion->desiredVelocity())
                                            || !mContentManifest.movementProfile().allows(
                                                LocomotionMode::Walk, motion->desiredVelocity()))
                                        {
                                            disposition = CommandDisposition::MotionOutOfRange;
                                            requiresSpatialAdvance = false;
                                        }
                                        else
                                        {
                                            replacementVelocity = motion->desiredVelocity();
                                            replacementLocomotionMode = LocomotionMode::Walk;
                                        }
                                    }
                                    else if (const auto* locomotion
                                        = std::get_if<PlayerLocomotionCommandProposal>(&proposal.payload()))
                                    {
                                        const auto& intent = locomotion->intent();
                                        if (!mContentManifest.movementProfile().allows(
                                                intent.mode(), intent.desiredVelocity()))
                                        {
                                            disposition = CommandDisposition::MotionOutOfRange;
                                            requiresSpatialAdvance = false;
                                        }
                                        else
                                        {
                                            const auto currentOrientation = player->transform().orientation();
                                            replacementTransform
                                                = Transform(player->transform().cell(), player->transform().position(),
                                                    Orientation3(currentOrientation.x(), currentOrientation.y(),
                                                        intent.rootFacing()));
                                            replacementVelocity = intent.desiredVelocity();
                                            replacementLocomotionMode = intent.mode();
                                        }
                                    }
                                    else if (const auto* transition
                                        = std::get_if<CellTransitionCommandProposal>(&proposal.payload()))
                                    {
                                        const auto& requested = transition->requestedCell();
                                        if (!mContentManifest.contains(requested))
                                        {
                                            disposition = CommandDisposition::UnknownCell;
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
                                    else if (const auto* interaction
                                        = std::get_if<InteractiveObjectCommandProposal>(&proposal.payload()))
                                    {
                                        requiresSpatialAdvance = false;
                                        if (!prepared.mInteractiveObjects || objectCatalog == nullptr)
                                        {
                                            ObjectInteractionOutcome outcome;
                                            outcome.code = ObjectInteractionResultCode::InternalError;
                                            outcome.objectId = interaction->objectId();
                                            objectInteractionOutcome = outcome;
                                            disposition = CommandDisposition::ObjectInteractionRejected;
                                        }
                                        else
                                        {
                                            const InteractObjectCommand objectCommand{
                                                .player = session->playerId(),
                                                .objectId = interaction->objectId(),
                                                .cell = interaction->cell(),
                                                .interactionOrigin = interaction->interactionOrigin(),
                                                .expectedRevision = interaction->expectedRevision(),
                                                .kind = interaction->kind(),
                                                .requestedKey = interaction->requestedKey(),
                                            };
                                            ObjectInteractionValidationContext validation;
                                            if (prepared.mInventory)
                                                validation.verifiedPlayerKeys
                                                    = prepared.mInventory->collectVerifiedKeys(session->playerId());
                                            auto interactionResult
                                                = applyObjectInteractionToCandidate(*prepared.mInteractiveObjects,
                                                    *objectCatalog, *prepared.mState, objectCommand, tick, validation);
                                            objectInteractionOutcome = interactionResult.outcome;
                                            disposition
                                                = interactionResult.outcome.code == ObjectInteractionResultCode::Success
                                                    || interactionResult.outcome.code
                                                        == ObjectInteractionResultCode::TrapSprung
                                                ? CommandDisposition::Applied
                                                : CommandDisposition::ObjectInteractionRejected;

                                            bool acceptObjectMutation = interactionResult.worldChanged;
                                            if (interactionResult.outcome.playerTeleport)
                                            {
                                                const auto& destination = *interactionResult.outcome.playerTeleport;
                                                auto teleported
                                                    = advanceCanonicalSpatialState(*player, tick, destination.transform,
                                                        LinearVelocity3(0, 0, 0), replacementLocomotionMode);
                                                if (const auto* value
                                                    = std::get_if<CanonicalPlayerEntityState>(&teleported))
                                                {
                                                    playerReplacement = *value;
                                                    playerStateChanged = true;
                                                }
                                                else
                                                {
                                                    acceptObjectMutation = false;
                                                    playerReplacement.reset();
                                                    playerStateChanged = false;
                                                    if (std::get<SpatialAdvanceError>(teleported).code
                                                        == SpatialAdvanceErrorCode::TickRegression)
                                                    {
                                                        disposition = CommandDisposition::SpatialTickRegression;
                                                        objectInteractionOutcome->code
                                                            = ObjectInteractionResultCode::TickRegression;
                                                    }
                                                    else
                                                    {
                                                        disposition = CommandDisposition::EntityRevisionExhausted;
                                                        objectInteractionOutcome->code
                                                            = ObjectInteractionResultCode::RevisionExhausted;
                                                    }
                                                    objectInteractionOutcome->playerTeleport.reset();
                                                }
                                            }
                                            if (!acceptObjectMutation && interactionResult.previousState
                                                && !restoreInteractiveObjectCandidate(
                                                    *prepared.mInteractiveObjects, *interactionResult.previousState))
                                            {
                                                result.mError = CommandBatchReductionError::CandidateStateInvalid;
                                                prepared.mPublication = std::move(publication);
                                                return prepared;
                                            }
                                        }
                                    }
                                    else if (const auto* inventoryProposal
                                        = std::get_if<InventoryCommandProposal>(&proposal.payload()))
                                    {
                                        requiresSpatialAdvance = false;
                                        const auto& proposalCommand = inventoryProposal->command();
                                        if (!prepared.mInventory || itemCatalog == nullptr)
                                            disposition = CommandDisposition::InventoryTransactionRejected;
                                        else
                                        {
                                            auto transaction = proposalCommand;
                                            transaction.player = session->playerId();
                                            const auto* beforeInventory
                                                = prepared.mInventory->findPlayer(session->playerId());
                                            const auto* combatPlayer
                                                = prepared.mCombat ? prepared.mCombat->findPlayer(session->playerId()) : nullptr;
                                            const auto oldWeight = beforeInventory
                                                ? beforeInventory->totalWeight(*itemCatalog) : 0;
                                            const auto oldWeapon = beforeInventory
                                                ? beforeInventory->equipment[static_cast<std::size_t>(
                                                      EquipmentSlot::CarriedRight)]
                                                : std::optional<ItemStackId>{};
                                            if (prepared.mCombat && (!combatPlayer || !combatPlayer->revision.next()))
                                                disposition = CommandDisposition::InventoryTransactionRejected;
                                            else
                                            {
                                                const auto outcome = applyInventoryTransaction(*prepared.mInventory,
                                                    *prepared.mState, transaction, InventoryValidationContext{}, tick);
                                                disposition = outcome.code == InventoryTransactionResultCode::Success
                                                    ? CommandDisposition::Applied
                                                    : CommandDisposition::InventoryTransactionRejected;
                                                const auto* afterInventory = disposition == CommandDisposition::Applied
                                                    ? prepared.mInventory->findPlayer(session->playerId()) : nullptr;
                                                const auto newWeight = afterInventory
                                                    ? afterInventory->totalWeight(*itemCatalog) : oldWeight;
                                                const auto newWeapon = afterInventory
                                                    ? afterInventory->equipment[static_cast<std::size_t>(
                                                          EquipmentSlot::CarriedRight)]
                                                    : oldWeapon;
                                                if (disposition == CommandDisposition::Applied && prepared.mCombat
                                                    && (oldWeight != newWeight || oldWeapon != newWeapon)
                                                    && !prepared.mCombat->advancePlayerInventoryBinding(
                                                        session->playerId(), newWeight))
                                                {
                                                    result.mError = CommandBatchReductionError::CandidateStateInvalid;
                                                    prepared.mPublication = std::move(publication);
                                                    return prepared;
                                                }
                                            }
                                        }
                                    }
                                    else
                                    {
                                        requiresSpatialAdvance = false;
                                        const auto& melee
                                            = std::get<MeleeAttackCommandProposal>(proposal.payload()).command();
                                        if (!prepared.mCombat || !prepared.mInventory || !itemCatalog
                                            || !actors || !meleeWeapons || !meleeSettings || !meleePolicy
                                            || !meleeContact)
                                            disposition = CommandDisposition::CombatRejected;
                                        else
                                        {
                                            const AuthoritativeMeleeAttack attack{ session->playerId(),
                                                melee.targetActorId, melee.expectedAttackerRevision,
                                                melee.expectedTargetRevision, melee.sourceServerTick,
                                                melee.attackType, melee.attackStrength };
                                            auto combatResult = prepareAuthoritativeMeleeAttack(*prepared.mCombat,
                                                *prepared.mInventory, *itemCatalog, *meleeWeapons,
                                                *prepared.mState, *actors, *meleeSettings, *meleePolicy,
                                                *meleeContact, tick, attack);
                                            if (combatResult.disposition == AuthoritativeMeleeDisposition::Applied
                                                && combatResult.candidate)
                                            {
                                                prepared.mCombat = std::move(*combatResult.candidate);
                                                if (combatResult.candidateInventory)
                                                    prepared.mInventory
                                                        = std::move(*combatResult.candidateInventory);
                                                if (combatResult.event)
                                                    prepared.mCombatEvents.push_back(*combatResult.event);
                                                disposition = CommandDisposition::Applied;
                                            }
                                            else
                                                disposition = CommandDisposition::CombatRejected;
                                        }
                                    }
                                    const auto advanced = requiresSpatialAdvance
                                        ? std::optional<SpatialAdvanceResult>(
                                              advanceCanonicalSpatialState(*player, tick, replacementTransform,
                                                  replacementVelocity, replacementLocomotionMode))
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
                                disposition, sessionReplacement, committedPlayerReplacement,
                                std::move(objectInteractionOutcome));
                            auto nextState = std::make_shared<CanonicalServerState>(std::move(candidateState));
                            publication->mChanges.push_back(std::move(change));
                            prepared.mState = std::move(nextState);
                            prepared.mStateVersion = nextVersion;
                            if (prepared.mCanonicalRevision == prepared.mBaseCanonicalRevision)
                            {
                                const auto nextRevision = prepared.mCanonicalRevision.next();
                                if (!nextRevision)
                                {
                                    result.mError = CommandBatchReductionError::StateVersionCapacityExceeded;
                                    prepared.mPublication = std::move(publication);
                                    return prepared;
                                }
                                prepared.mCanonicalRevision = *nextRevision;
                            }
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

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepare(const ServerTickCommandBatch& batch)
    {
        return prepareCommands(batch, nullptr, nullptr, nullptr, nullptr);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepare(const ServerTickCommandBatch& batch,
        const CanonicalInteractiveObjectWorld& objects, const InteractiveObjectCatalog& catalog)
    {
        return prepareCommands(batch, &objects, &catalog, nullptr, nullptr);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepare(const ServerTickCommandBatch& batch,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& catalog)
    {
        return prepareCommands(batch, nullptr, nullptr, &inventory, &catalog);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepare(const ServerTickCommandBatch& batch,
        const CanonicalInteractiveObjectWorld& objects, const InteractiveObjectCatalog& objectCatalog,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& itemCatalog)
    {
        return prepareCommands(batch, &objects, &objectCatalog, &inventory, &itemCatalog);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTickState(
        PreparedBatch prepared, const ServerTickCommandBatch& batch)
    {
        if (!prepared.result())
            return prepared;
        const ServerTick tick = batch.scheduledTick().value();
        const auto players = prepared.mState->players();
        std::size_t moving = 0;
        for (const auto& player : players)
            if (player.linearVelocity() != LinearVelocity3(0, 0, 0))
                ++moving;
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
                if (velocity == LinearVelocity3(0, 0, 0))
                    continue;
                auto kernel = advanceMovementKernel(mContentManifest.id(), mContentManifest.movementProfile(),
                    current.locomotionMode(), current.entityId(), tick, current.transform(), velocity, *mCollision);
                const auto* step = std::get_if<MovementKernelStep>(&kernel);
                if (!step)
                {
                    prepared.mResult.mError
                        = std::get<MovementKernelError>(kernel) == MovementKernelError::IntegrationOverflow
                        ? CommandBatchReductionError::SpatialIntegrationOverflow
                        : CommandBatchReductionError::CandidateStateInvalid;
                    return prepared;
                }
                auto advanced = advanceCanonicalSpatialState(current, tick, step->root, step->velocity);
                auto* value = std::get_if<CanonicalPlayerEntityState>(&advanced);
                if (!value)
                {
                    prepared.mResult.mError = CommandBatchReductionError::SpatialRevisionExhausted;
                    return prepared;
                }
                replacements[index] = *value;
                prepared.mStateVersion = *prepared.mStateVersion.next();
                if (prepared.mCanonicalRevision == prepared.mBaseCanonicalRevision)
                {
                    const auto nextRevision = prepared.mCanonicalRevision.next();
                    if (!nextRevision)
                    {
                        prepared.mResult.mError = CommandBatchReductionError::StateVersionCapacityExceeded;
                        return prepared;
                    }
                    prepared.mCanonicalRevision = *nextRevision;
                }
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

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(const ServerTickCommandBatch& batch)
    {
        return prepareTickState(prepare(batch), batch);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(const ServerTickCommandBatch& batch,
        const CanonicalInteractiveObjectWorld& objects, const InteractiveObjectCatalog& catalog)
    {
        return prepareTickState(prepare(batch, objects, catalog), batch);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(const ServerTickCommandBatch& batch,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& catalog)
    {
        return prepareTickState(prepare(batch, inventory, catalog), batch);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(const ServerTickCommandBatch& batch,
        const CanonicalInteractiveObjectWorld& objects, const InteractiveObjectCatalog& objectCatalog,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& itemCatalog)
    {
        return prepareTickState(prepare(batch, objects, objectCatalog, inventory, itemCatalog), batch);
    }

    CanonicalCommandReducer::PreparedBatch CanonicalCommandReducer::prepareTick(
        const ServerTickCommandBatch& batch, CanonicalCommandWorlds worlds)
    {
        return prepareTickState(prepareCommands(batch, worlds.interactiveObjects, worlds.interactiveObjectCatalog,
            worlds.inventory, worlds.itemCatalog, worlds.combat, worlds.actors, worlds.meleeWeapons,
            worlds.meleeSettings, worlds.meleePolicy, worlds.meleeContact), batch);
    }

    bool CanonicalCommandReducer::commitPrepared(
        PreparedBatch&& prepared, CanonicalInteractiveObjectWorld* objects, CanonicalInventoryWorld* inventory,
        CanonicalCombatWorld* combat)
    {
        if (prepared.mBaseVersion != mStateVersion || prepared.mBaseCanonicalRevision != mCanonicalRevision
            || !prepared.mState || !prepared.mPublication || (prepared.mInteractiveObjects && objects == nullptr)
            || (prepared.mBaseInteractiveObjects && (!objects || *objects != *prepared.mBaseInteractiveObjects))
            || (prepared.mInventory && inventory == nullptr)
            || (prepared.mBaseInventory && (!inventory || *inventory != *prepared.mBaseInventory))
            || (prepared.mCombat && combat == nullptr)
            || (prepared.mBaseCombat && (!combat || *combat != *prepared.mBaseCombat)))
            return false;
        mState = std::move(prepared.mState);
        if (prepared.mInteractiveObjects)
            *objects = std::move(*prepared.mInteractiveObjects);
        if (prepared.mInventory)
            *inventory = std::move(*prepared.mInventory);
        if (prepared.mCombat)
            *combat = std::move(*prepared.mCombat);
        mStateVersion = prepared.mStateVersion;
        mCanonicalRevision = prepared.mCanonicalRevision;
        mCheckpointTick = prepared.mCheckpointTick;
        for (const auto& record : prepared.mResult.mDispositions)
            observe(record.disposition(), mCheckpointTick);
        if (prepared.mResult.mError != CommandBatchReductionError::None)
            observe(prepared.mResult.mError, mCheckpointTick, prepared.mResult.mDispositions.size());
        prepared.mResult.mSinkDeliveryReport = publish(std::move(prepared.mPublication));
        return true;
    }

    bool CanonicalCommandReducer::commit(PreparedBatch&& prepared)
    {
        return commitPrepared(std::move(prepared), nullptr, nullptr, nullptr);
    }

    bool CanonicalCommandReducer::commit(PreparedBatch&& prepared, CanonicalInteractiveObjectWorld& objects)
    {
        return commitPrepared(std::move(prepared), &objects, nullptr, nullptr);
    }

    bool CanonicalCommandReducer::commit(PreparedBatch&& prepared, CanonicalInventoryWorld& inventory)
    {
        return commitPrepared(std::move(prepared), nullptr, &inventory, nullptr);
    }

    bool CanonicalCommandReducer::commit(
        PreparedBatch&& prepared, CanonicalInteractiveObjectWorld& objects, CanonicalInventoryWorld& inventory)
    {
        return commitPrepared(std::move(prepared), &objects, &inventory, nullptr);
    }

    bool CanonicalCommandReducer::commit(PreparedBatch&& prepared, CanonicalCommandWorlds worlds)
    {
        return commitPrepared(std::move(prepared), worlds.interactiveObjects, worlds.inventory, worlds.combat);
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
