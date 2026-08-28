#include <tes3mp/canonical_state.hpp>

#include <algorithm>
#include <utility>

namespace
{
    TES3MP::CanonicalStateError stateError(TES3MP::CanonicalStateErrorCode code, std::size_t index = 0,
        std::uint64_t value = 0, std::uint64_t relatedValue = 0) noexcept
    {
        return { code, index, value, relatedValue };
    }

    std::optional<TES3MP::CanonicalSessionHistoryError> validateHistory(
        std::optional<TES3MP::CommandSequence> acknowledgement,
        std::span<const TES3MP::FinalizedCommandRecord> history) noexcept
    {
        using namespace TES3MP;
        if (history.size() > MaximumFinalizedCommandHistory)
            return CanonicalSessionHistoryError{ CanonicalSessionHistoryErrorCode::LimitExceeded, history.size(),
                history.size() };
        if (!history.empty() && !acknowledgement)
            return CanonicalSessionHistoryError{ CanonicalSessionHistoryErrorCode::WithoutAcknowledgement };
        for (std::size_t index = 0; index < history.size(); ++index)
        {
            const CommandDisposition disposition = history[index].disposition();
            if (disposition == CommandDisposition::UnknownSession
                || disposition == CommandDisposition::SessionGenerationMismatch
                || disposition == CommandDisposition::AlreadyFinalized
                || disposition == CommandDisposition::SequenceGap)
                return CanonicalSessionHistoryError{ CanonicalSessionHistoryErrorCode::ContainsNonFinalDisposition,
                    index, static_cast<std::uint64_t>(disposition) };
            if (index != 0)
            {
                const auto expected = history[index - 1].commandSequence().next();
                if (!expected || history[index].commandSequence() != *expected)
                    return CanonicalSessionHistoryError{ CanonicalSessionHistoryErrorCode::NotContiguous, index,
                        history[index].commandSequence().value(), history[index - 1].commandSequence().value() };
            }
        }
        if (!history.empty() && history.back().commandSequence() != *acknowledgement)
            return CanonicalSessionHistoryError{ CanonicalSessionHistoryErrorCode::DoesNotEndAtAcknowledgement,
                history.size() - 1, history.back().commandSequence().value(), acknowledgement->value() };
        return std::nullopt;
    }
}

namespace TES3MP
{
    CanonicalSessionProgress::CanonicalSessionProgress(SessionId sessionId, SessionGeneration sessionGeneration,
        PlayerId playerId, EntityId entityId, std::optional<CommandSequence> highestContiguousFinalizedCommand)
        : CanonicalSessionProgress(
              sessionId, sessionGeneration, playerId, entityId, highestContiguousFinalizedCommand, {})
    {
    }

    CanonicalSessionProgress::CanonicalSessionProgress(SessionId sessionId, SessionGeneration sessionGeneration,
        PlayerId playerId, EntityId entityId, std::optional<CommandSequence> highestContiguousFinalizedCommand,
        std::vector<FinalizedCommandRecord> finalizedCommandHistory)
        : mSessionId(sessionId)
        , mSessionGeneration(sessionGeneration)
        , mPlayerId(playerId)
        , mEntityId(entityId)
        , mHighestContiguousFinalizedCommand(highestContiguousFinalizedCommand)
        , mFinalizedCommandHistory(
              std::make_shared<const std::vector<FinalizedCommandRecord>>(std::move(finalizedCommandHistory)))
    {
    }

    bool CanonicalSessionProgress::containsFinalizedCommandId(CommandId commandId) const noexcept
    {
        return std::any_of(mFinalizedCommandHistory->begin(), mFinalizedCommandHistory->end(),
            [commandId](FinalizedCommandRecord record) { return record.commandId() == commandId; });
    }

    bool operator==(const CanonicalSessionProgress& left, const CanonicalSessionProgress& right) noexcept
    {
        return left.mSessionId == right.mSessionId && left.mSessionGeneration == right.mSessionGeneration
            && left.mPlayerId == right.mPlayerId && left.mEntityId == right.mEntityId
            && left.mHighestContiguousFinalizedCommand == right.mHighestContiguousFinalizedCommand
            && *left.mFinalizedCommandHistory == *right.mFinalizedCommandHistory;
    }

    CanonicalSessionProgressCreationResult createCanonicalSessionProgress(SessionId sessionId,
        SessionGeneration sessionGeneration, PlayerId playerId, EntityId entityId,
        std::optional<CommandSequence> highestContiguousFinalizedCommand,
        std::span<const FinalizedCommandRecord> finalizedCommandHistory)
    {
        if (const auto error = validateHistory(highestContiguousFinalizedCommand, finalizedCommandHistory))
            return *error;
        return CanonicalSessionProgress(sessionId, sessionGeneration, playerId, entityId,
            highestContiguousFinalizedCommand,
            std::vector<FinalizedCommandRecord>(finalizedCommandHistory.begin(), finalizedCommandHistory.end()));
    }

    SpatialAdvanceResult advanceCanonicalSpatialState(const CanonicalPlayerEntityState& current, ServerTick commitTick,
        Transform replacementTransform, LinearVelocity3 replacementVelocity) noexcept
    {
        if (commitTick < current.lastSpatialChangeTick())
        {
            return SpatialAdvanceError{ SpatialAdvanceErrorCode::TickRegression, current.lastSpatialChangeTick(),
                commitTick, current.entityRevision() };
        }

        const auto nextRevision = current.entityRevision().next();
        if (!nextRevision)
        {
            return SpatialAdvanceError{ SpatialAdvanceErrorCode::RevisionExhausted, current.lastSpatialChangeTick(),
                commitTick, current.entityRevision() };
        }

        return CanonicalPlayerEntityState(current.playerId(), current.entityId(), replacementTransform,
            replacementVelocity, *nextRevision, current.authorityEpoch(), commitTick);
    }

    CanonicalServerState::CanonicalServerState(
        std::vector<CanonicalPlayerEntityState> players, std::vector<CanonicalSessionProgress> activeSessions) noexcept
        : mPlayers(std::move(players))
        , mActiveSessions(std::move(activeSessions))
    {
    }

    const CanonicalPlayerEntityState* CanonicalServerState::findPlayer(PlayerId playerId) const noexcept
    {
        const auto found = std::lower_bound(mPlayers.begin(), mPlayers.end(), playerId,
            [](const CanonicalPlayerEntityState& player, PlayerId candidate) { return player.playerId() < candidate; });
        return found != mPlayers.end() && found->playerId() == playerId ? &*found : nullptr;
    }

    const CanonicalSessionProgress* CanonicalServerState::findActiveSession(SessionId sessionId) const noexcept
    {
        const auto found = std::lower_bound(mActiveSessions.begin(), mActiveSessions.end(), sessionId,
            [](const CanonicalSessionProgress& session, SessionId candidate) {
                return session.sessionId() < candidate;
            });
        return found != mActiveSessions.end() && found->sessionId() == sessionId ? &*found : nullptr;
    }

    CanonicalStateCreationResult createCanonicalServerState(
        std::span<const CanonicalPlayerEntityState> players, std::span<const CanonicalSessionProgress> activeSessions)
    {
        if (players.size() > MaximumCanonicalPlayerEntities)
            return stateError(CanonicalStateErrorCode::PlayerLimitExceeded, players.size());
        if (activeSessions.size() > MaximumCanonicalActiveSessions)
            return stateError(CanonicalStateErrorCode::ActiveSessionLimitExceeded, activeSessions.size());

        for (std::size_t index = 1; index < players.size(); ++index)
        {
            if (players[index - 1].playerId() >= players[index].playerId())
            {
                return stateError(CanonicalStateErrorCode::PlayerIdsNotStrictlyOrdered, index,
                    players[index].playerId().value(), players[index - 1].playerId().value());
            }
        }
        for (std::size_t index = 1; index < activeSessions.size(); ++index)
        {
            if (activeSessions[index - 1].sessionId() >= activeSessions[index].sessionId())
            {
                return stateError(CanonicalStateErrorCode::SessionIdsNotStrictlyOrdered, index,
                    activeSessions[index].sessionId().value(), activeSessions[index - 1].sessionId().value());
            }
        }

        for (std::size_t index = 0; index < players.size(); ++index)
        {
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (players[previous].entityId() == players[index].entityId())
                {
                    return stateError(
                        CanonicalStateErrorCode::DuplicateEntityId, index, players[index].entityId().value(), previous);
                }
            }
        }

        for (std::size_t index = 0; index < activeSessions.size(); ++index)
        {
            const CanonicalSessionProgress& session = activeSessions[index];
            const auto history = session.finalizedCommandHistory();
            if (const auto error = validateHistory(session.highestContiguousFinalizedCommand(), history))
            {
                switch (error->code)
                {
                    case CanonicalSessionHistoryErrorCode::LimitExceeded:
                        return stateError(CanonicalStateErrorCode::FinalizedHistoryLimitExceeded, index, error->value);
                    case CanonicalSessionHistoryErrorCode::WithoutAcknowledgement:
                        return stateError(CanonicalStateErrorCode::FinalizedHistoryWithoutAcknowledgement, index);
                    case CanonicalSessionHistoryErrorCode::NotContiguous:
                        return stateError(CanonicalStateErrorCode::FinalizedHistoryNotStrictlyOrdered, index,
                            error->value, error->relatedValue);
                    case CanonicalSessionHistoryErrorCode::DoesNotEndAtAcknowledgement:
                        return stateError(CanonicalStateErrorCode::FinalizedHistoryDoesNotEndAtAcknowledgement, index,
                            error->value, error->relatedValue);
                    case CanonicalSessionHistoryErrorCode::ContainsNonFinalDisposition:
                        return stateError(CanonicalStateErrorCode::FinalizedHistoryContainsNonFinalDisposition, index,
                            error->index, error->value);
                }
            }

            const auto player = std::lower_bound(players.begin(), players.end(), session.playerId(),
                [](const CanonicalPlayerEntityState& candidate, PlayerId playerId) {
                    return candidate.playerId() < playerId;
                });
            if (player == players.end() || player->playerId() != session.playerId())
            {
                return stateError(CanonicalStateErrorCode::SessionPlayerMissing, index, session.playerId().value(),
                    session.entityId().value());
            }
            if (player->entityId() != session.entityId())
            {
                return stateError(CanonicalStateErrorCode::SessionEntityMismatch, index, session.entityId().value(),
                    player->entityId().value());
            }

            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (activeSessions[previous].playerId() == session.playerId()
                    || activeSessions[previous].entityId() == session.entityId())
                {
                    return stateError(CanonicalStateErrorCode::DuplicateActiveBinding, index,
                        session.playerId().value(), session.entityId().value());
                }
            }
        }

        return CanonicalServerState(std::vector<CanonicalPlayerEntityState>(players.begin(), players.end()),
            std::vector<CanonicalSessionProgress>(activeSessions.begin(), activeSessions.end()));
    }
}
