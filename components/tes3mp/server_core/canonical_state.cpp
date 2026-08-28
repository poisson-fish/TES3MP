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
}

namespace TES3MP
{
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
