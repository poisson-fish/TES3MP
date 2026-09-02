#include "fixture_observation_projection.hpp"

#include "tes3mp/protocol_frame.hpp"

#include <algorithm>

namespace TES3MP::ServerApp
{
    namespace
    {
        const CanonicalPlayerEntityState* playerFor(
            const CanonicalServerState& state, const CanonicalSessionProgress& session)
        {
            return state.findPlayer(session.playerId());
        }

        std::vector<const CanonicalPlayerEntityState*> visibleTo(
            const CanonicalServerState& state, const CanonicalSessionProgress& target)
        {
            std::vector<const CanonicalPlayerEntityState*> result;
            const auto* targetPlayer = playerFor(state, target);
            if (!targetPlayer) return result;
            for (const auto& player : state.players())
                if (player.transform().cell() == targetPlayer->transform().cell()) result.push_back(&player);
            return result;
        }
    }

    std::optional<std::vector<FixtureObservationDelivery>> projectFixtureObservations(
        const CanonicalServerState& before, const CanonicalServerState& after, ServerTick tick)
    {
        std::vector<FixtureObservationDelivery> deliveries;
        deliveries.reserve(after.activeSessions().size());
        for (const auto& target : after.activeSessions())
        {
            const auto* oldTarget = before.findActiveSession(target.sessionId());
            if (!oldTarget || oldTarget->sessionGeneration() != target.sessionGeneration()) continue;
            const auto oldVisible = visibleTo(before, *oldTarget);
            const auto newVisible = visibleTo(after, target);
            std::vector<ObservationChange> changes;
            for (const auto* player : oldVisible)
                if (std::ranges::none_of(newVisible, [&](const auto* value) { return value->playerId() == player->playerId(); }))
                    changes.push_back({ player->playerId(), player->entityId(), ObservationChangeKind::Leave });
            for (const auto* player : newVisible)
                if (std::ranges::none_of(oldVisible, [&](const auto* value) { return value->playerId() == player->playerId(); }))
                    changes.push_back({ player->playerId(), player->entityId(), ObservationChangeKind::Enter });
            if (changes.empty()) continue;
            std::ranges::sort(changes, {}, &ObservationChange::playerId);

            std::vector<SpatialEntitySnapshot> entries;
            entries.reserve(newVisible.size());
            for (const auto* player : newVisible)
                entries.emplace_back(tick, player->playerId(), player->entityId(), player->entityRevision(),
                    player->authorityEpoch(), player->transform(), player->linearVelocity());
            auto batch = ReliableObservationBatch::create(
                target.sessionId(), target.sessionGeneration(), tick, changes);
            auto view = SpatialWorldView::create(entries);
            if (!std::holds_alternative<ReliableObservationBatch>(batch)
                || !std::holds_alternative<SpatialWorldView>(view)) return std::nullopt;
            deliveries.push_back({ target.sessionId(),
                std::get<ReliableObservationBatch>(std::move(batch)),
                LatestWinsSnapshot(LatestWinsSnapshotHeader(target.sessionId(), target.sessionGeneration(), tick,
                    target.highestContiguousFinalizedCommand()), std::get<SpatialWorldView>(std::move(view))) });
        }
        return deliveries;
    }

    bool admitFixtureObservation(OutboundQueueSet& queues, TransportConnectionId connection,
        const FixtureObservationDelivery& delivery)
    {
        const auto observation = encodeProtocolFrame(MessageClass::ReliableOperation,
            MessageKind::ReliableObservationBatch, encodeReliableObservationBatch(delivery.observations));
        const auto view = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
            MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(delivery.view));
        if (!std::holds_alternative<std::vector<std::byte>>(observation)
            || !std::holds_alternative<std::vector<std::byte>>(view)) return false;
        const auto& observationFrame = std::get<std::vector<std::byte>>(observation);
        const auto& viewFrame = std::get<std::vector<std::byte>>(view);
        return queues.enqueuePair(connection, TransportChannel::ReliableOrdered, observationFrame,
                   TransportChannel::LatestWins, viewFrame) == TransportResult::Accepted;
    }
}
