#include "interest_projection.hpp"

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

    bool sharesInterest(const CanonicalServerState& state, SessionId target, SessionId source) noexcept
    {
        const auto* targetSession = state.findActiveSession(target);
        const auto* sourceSession = state.findActiveSession(source);
        const auto* targetPlayer = targetSession ? playerFor(state, *targetSession) : nullptr;
        const auto* sourcePlayer = sourceSession ? playerFor(state, *sourceSession) : nullptr;
        return targetPlayer && sourcePlayer && targetPlayer->transform().cell() == sourcePlayer->transform().cell();
    }

    std::optional<InterestBaselineDelivery> projectInterestBaseline(const CanonicalServerState& state,
        SessionId targetId, ServerTick tick, CanonicalRevision revision, CanonicalStateVersion stateVersion)
    {
        try
        {
            const auto* target = state.findActiveSession(targetId);
            if (!target) return std::nullopt;
            const auto visible = visibleTo(state, *target);
            std::vector<InterestMember> members;
            std::vector<SpatialEntitySnapshot> entries;
            members.reserve(visible.size());
            entries.reserve(visible.size());
            for (const auto* player : visible)
            {
                members.push_back({ player->playerId(), player->entityId() });
                entries.emplace_back(tick, player->playerId(), player->entityId(), player->appearanceId(),
                    player->entityRevision(), player->authorityEpoch(), player->transform(), player->linearVelocity(),
                    player->locomotionMode());
            }
            std::ranges::sort(members);
            auto baseline = ReliableInterestBaseline::create(target->sessionId(), target->sessionGeneration(),
                revision, stateVersion, tick, members);
            auto view = SpatialWorldView::create(entries);
            if (!std::holds_alternative<ReliableInterestBaseline>(baseline)
                || !std::holds_alternative<SpatialWorldView>(view)) return std::nullopt;
            return InterestBaselineDelivery{ target->sessionId(),
                std::get<ReliableInterestBaseline>(std::move(baseline)),
                LatestWinsSnapshot(LatestWinsSnapshotHeader(target->sessionId(), target->sessionGeneration(),
                    target->playerId(), target->entityId(), revision,
                    target->highestContiguousFinalizedCommand()),
                    std::get<SpatialWorldView>(std::move(view))) };
        }
        catch (...) { return std::nullopt; }
    }

    bool admitInterestBaseline(OutboundQueueSet& queues, TransportConnectionId connection,
        const InterestBaselineDelivery& delivery)
    {
        auto baseline = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableInterestBaseline,
            encodeReliableInterestBaseline(delivery.baseline));
        auto view = encodeProtocolFrame(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
            encodeLatestWinsSnapshot(delivery.view));
        if (!std::holds_alternative<std::vector<std::byte>>(baseline)
            || !std::holds_alternative<std::vector<std::byte>>(view)) return false;
        return queues.enqueuePair(connection, TransportChannel::ReliableOrdered,
                   std::get<std::vector<std::byte>>(baseline), TransportChannel::LatestWins,
                   std::get<std::vector<std::byte>>(view)) == TransportResult::Accepted;
    }

    std::optional<std::vector<InterestDelivery>> projectInterestChanges(
        const CanonicalServerState& before, const CanonicalServerState& after, ServerTick tick,
        CanonicalRevision revision)
    {
        std::vector<InterestDelivery> deliveries;
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
                entries.emplace_back(tick, player->playerId(), player->entityId(), player->appearanceId(),
                    player->entityRevision(), player->authorityEpoch(), player->transform(), player->linearVelocity(),
                    player->locomotionMode());
            auto batch = ReliableObservationBatch::create(
                target.sessionId(), target.sessionGeneration(), revision, changes);
            auto view = SpatialWorldView::create(entries);
            if (!std::holds_alternative<ReliableObservationBatch>(batch)
                || !std::holds_alternative<SpatialWorldView>(view)) return std::nullopt;
            deliveries.push_back({ target.sessionId(),
                std::get<ReliableObservationBatch>(std::move(batch)),
                LatestWinsSnapshot(LatestWinsSnapshotHeader(target.sessionId(), target.sessionGeneration(),
                    target.playerId(), target.entityId(), revision, target.highestContiguousFinalizedCommand()),
                    std::get<SpatialWorldView>(std::move(view))) });
        }
        return deliveries;
    }

    bool admitInterestChange(OutboundQueueSet& queues, TransportConnectionId connection,
        const InterestDelivery& delivery)
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

    bool admitInterestChangesAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& deliveries)
    {
        try
        {
            if (deliveries.empty()) return true;
            std::vector<std::vector<std::byte>> reliable;
            std::vector<std::vector<std::byte>> latest;
            reliable.reserve(deliveries.size()); latest.reserve(deliveries.size());
            for (const auto& [connection, delivery] : deliveries)
            {
                (void)connection;
                auto first = encodeProtocolFrame(MessageClass::ReliableOperation,
                    MessageKind::ReliableObservationBatch, encodeReliableObservationBatch(delivery.observations));
                auto second = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(delivery.view));
                if (!std::holds_alternative<std::vector<std::byte>>(first)
                    || !std::holds_alternative<std::vector<std::byte>>(second)) return false;
                reliable.push_back(std::get<std::vector<std::byte>>(std::move(first)));
                latest.push_back(std::get<std::vector<std::byte>>(std::move(second)));
            }
            std::vector<OutboundQueueSet::AtomicPair> pairs;
            pairs.reserve(deliveries.size());
            for (std::size_t i = 0; i < deliveries.size(); ++i)
                pairs.push_back({ deliveries[i].first, TransportChannel::ReliableOrdered, reliable[i],
                    TransportChannel::LatestWins, latest[i] });
            return queues.enqueuePairsAtomically(pairs) == TransportResult::Accepted;
        }
        catch (...) { return false; }
    }

    std::optional<std::vector<std::pair<SessionId, LatestWinsSnapshot>>> projectInterestViews(
        const CanonicalServerState& state, ServerTick tick, CanonicalRevision revision)
    {
        try
        {
            std::vector<std::pair<SessionId, LatestWinsSnapshot>> result;
            result.reserve(state.activeSessions().size());
            for (const auto& target : state.activeSessions())
            {
                const auto visible = visibleTo(state, target);
                std::vector<SpatialEntitySnapshot> entries;
                entries.reserve(visible.size());
                for (const auto* player : visible)
                    entries.emplace_back(tick, player->playerId(), player->entityId(), player->appearanceId(),
                        player->entityRevision(), player->authorityEpoch(), player->transform(),
                        player->linearVelocity(), player->locomotionMode());
                auto view = SpatialWorldView::create(entries);
                if (!std::holds_alternative<SpatialWorldView>(view)) return std::nullopt;
                result.emplace_back(target.sessionId(), LatestWinsSnapshot(
                    LatestWinsSnapshotHeader(target.sessionId(), target.sessionGeneration(), target.playerId(),
                        target.entityId(), revision, target.highestContiguousFinalizedCommand()),
                    std::get<SpatialWorldView>(std::move(view))));
            }
            return result;
        }
        catch (...) { return std::nullopt; }
    }

    bool admitInterestViewsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& deliveries)
    {
        try
        {
            if (deliveries.empty()) return true;
            std::vector<std::vector<std::byte>> frames;
            frames.reserve(deliveries.size());
            for (const auto& [connection, snapshot] : deliveries)
            {
                (void)connection;
                auto frame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(snapshot));
                if (!std::holds_alternative<std::vector<std::byte>>(frame)) return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
            }
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            messages.reserve(deliveries.size());
            for (std::size_t index = 0; index < deliveries.size(); ++index)
                messages.push_back({ deliveries[index].first, TransportChannel::LatestWins, frames[index] });
            return queues.enqueueMessagesAtomically(messages) == TransportResult::Accepted;
        }
        catch (...) { return false; }
    }

    bool admitInterestTickAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& observations,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& views)
    {
        try
        {
            std::vector<std::vector<std::byte>> frames;
            frames.reserve(observations.size() + views.size());
            for (const auto& [connection, delivery] : observations)
            {
                (void)connection;
                auto frame = encodeProtocolFrame(MessageClass::ReliableOperation,
                    MessageKind::ReliableObservationBatch, encodeReliableObservationBatch(delivery.observations));
                if (!std::holds_alternative<std::vector<std::byte>>(frame)) return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
            }
            for (const auto& [connection, view] : views)
            {
                (void)connection;
                auto frame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(view));
                if (!std::holds_alternative<std::vector<std::byte>>(frame)) return false;
                frames.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
            }
            std::vector<OutboundQueueSet::AtomicMessage> messages;
            messages.reserve(frames.size());
            std::size_t index = 0;
            for (const auto& delivery : observations)
                messages.push_back({ delivery.first, TransportChannel::ReliableOrdered, frames[index++] });
            for (const auto& delivery : views)
                messages.push_back({ delivery.first, TransportChannel::LatestWins, frames[index++] });
            return messages.empty() || queues.enqueueMessagesAtomically(messages) == TransportResult::Accepted;
        }
        catch (...) { return false; }
    }
}
