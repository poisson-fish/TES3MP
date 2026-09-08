#include "actor_interest_projection.hpp"

#include <tes3mp/protocol_frame.hpp>

namespace TES3MP::ServerApp
{
    namespace
    {
        const CanonicalPlayerEntityState* targetPlayer(
            const CanonicalServerState& players, SessionId target) noexcept
        {
            const auto* session = players.findActiveSession(target);
            return session ? players.findPlayer(session->playerId()) : nullptr;
        }

        std::optional<std::vector<ActorSpatialSnapshot>> visibleEntries(const CanonicalServerState& players,
            const CanonicalActorWorld& actors, SessionId target, ServerTick tick)
        {
            const auto* player = targetPlayer(players, target);
            if (!player) return std::nullopt;
            std::vector<ActorSpatialSnapshot> entries;
            for (const auto& actor : actors.actors())
                if (actor.root().cell() == player->transform().cell())
                    entries.emplace_back(tick, actor.actorId(), actor.entityId(), actor.prototypeId(),
                        actor.revision(), actor.authorityEpoch(), actor.root(), actor.velocity(), actor.activity());
            return entries;
        }
    }

    std::optional<ActorInterestBaselineDelivery> projectActorInterestBaseline(
        const CanonicalServerState& players, const CanonicalActorWorld& actors, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        auto entries = visibleEntries(players, actors, target, tick);
        if (!session || !entries) return std::nullopt;
        std::vector<ActorInterestMember> members;
        members.reserve(entries->size());
        for (const auto& entry : *entries)
            members.push_back({ entry.actorId(), entry.entityId(), entry.prototypeId() });
        auto baseline = ReliableActorInterestBaseline::create(
            target, session->sessionGeneration(), tick, canonicalRevision, members);
        auto view = ActorWorldView::create(*entries);
        if (!std::holds_alternative<ReliableActorInterestBaseline>(baseline)
            || !std::holds_alternative<ActorWorldView>(view)) return std::nullopt;
        return ActorInterestBaselineDelivery{ target,
            std::get<ReliableActorInterestBaseline>(std::move(baseline)),
            LatestWinsActorSnapshot(target, session->sessionGeneration(), tick, canonicalRevision,
                std::get<ActorWorldView>(std::move(view))) };
    }
    catch (...) { return std::nullopt; }

    std::optional<LatestWinsActorSnapshot> projectActorInterestView(
        const CanonicalServerState& players, const CanonicalActorWorld& actors, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        auto entries = visibleEntries(players, actors, target, tick);
        if (!session || !entries) return std::nullopt;
        auto view = ActorWorldView::create(*entries);
        if (!std::holds_alternative<ActorWorldView>(view)) return std::nullopt;
        return LatestWinsActorSnapshot(target, session->sessionGeneration(), tick, canonicalRevision,
            std::get<ActorWorldView>(std::move(view)));
    }
    catch (...) { return std::nullopt; }

    bool admitActorInterestBaseline(OutboundQueueSet& queues, TransportConnectionId connection,
        const ActorInterestBaselineDelivery& delivery)
    {
        auto baseline = encodeProtocolFrame(MessageClass::ReliableOperation,
            MessageKind::ReliableActorInterestBaseline, encodeReliableActorInterestBaseline(delivery.baseline));
        auto view = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
            MessageKind::LatestWinsActorSnapshot, encodeLatestWinsActorSnapshot(delivery.view));
        if (!std::holds_alternative<std::vector<std::byte>>(baseline)
            || !std::holds_alternative<std::vector<std::byte>>(view)) return false;
        return queues.enqueuePair(connection, TransportChannel::ReliableOrdered,
                   std::get<std::vector<std::byte>>(baseline), TransportChannel::LatestWins,
                   std::get<std::vector<std::byte>>(view)) == TransportResult::Accepted;
    }

    bool admitActorInterestViewsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, LatestWinsActorSnapshot>>& deliveries)
    try
    {
        if (deliveries.empty()) return true;
        std::vector<std::vector<std::byte>> frames;
        frames.reserve(deliveries.size());
        for (const auto& [connection, view] : deliveries)
        {
            (void)connection;
            auto frame = encodeProtocolFrame(MessageClass::LatestWinsSnapshot,
                MessageKind::LatestWinsActorSnapshot, encodeLatestWinsActorSnapshot(view));
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

    bool admitCombinedInterestTickAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& playerObservations,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& playerViews,
        const std::vector<std::pair<TransportConnectionId, ActorInterestBaselineDelivery>>& actorBaselines,
        const std::vector<std::pair<TransportConnectionId, LatestWinsActorSnapshot>>& actorViews,
        const std::vector<std::pair<TransportConnectionId, InteractiveObjectInterestBaselineDelivery>>& objectBaselines)
    try
    {
        std::vector<std::vector<std::byte>> frames;
        std::vector<OutboundQueueSet::AtomicMessage> messages;
        frames.reserve(playerObservations.size() + playerViews.size() + actorBaselines.size() * 2
            + actorViews.size() + objectBaselines.size());
        messages.reserve(frames.capacity());
        const auto add = [&](TransportConnectionId connection, TransportChannel channel,
                             MessageClass messageClass, MessageKind kind, std::vector<std::byte> payload) {
            auto frame = encodeProtocolFrame(messageClass, kind, payload);
            if (!std::holds_alternative<std::vector<std::byte>>(frame)) return false;
            frames.push_back(std::get<std::vector<std::byte>>(std::move(frame)));
            messages.push_back({ connection, channel, frames.back() });
            return true;
        };
        for (const auto& [connection, delivery] : playerObservations)
            if (!add(connection, TransportChannel::ReliableOrdered, MessageClass::ReliableOperation,
                    MessageKind::ReliableObservationBatch, encodeReliableObservationBatch(delivery.observations)))
                return false;
        for (const auto& [connection, view] : playerViews)
            if (!add(connection, TransportChannel::LatestWins, MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(view))) return false;
        for (const auto& [connection, delivery] : actorBaselines)
        {
            if (!add(connection, TransportChannel::ReliableOrdered, MessageClass::ReliableOperation,
                    MessageKind::ReliableActorInterestBaseline,
                    encodeReliableActorInterestBaseline(delivery.baseline))
                || !add(connection, TransportChannel::LatestWins, MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsActorSnapshot, encodeLatestWinsActorSnapshot(delivery.view)))
                return false;
        }
        for (const auto& [connection, view] : actorViews)
            if (!add(connection, TransportChannel::LatestWins, MessageClass::LatestWinsSnapshot,
                    MessageKind::LatestWinsActorSnapshot, encodeLatestWinsActorSnapshot(view))) return false;
        for (const auto& [connection, delivery] : objectBaselines)
            if (!add(connection, TransportChannel::ReliableOrdered, MessageClass::ReliableOperation,
                    MessageKind::ReliableInteractiveObjectInterestBaseline,
                    encodeReliableInteractiveObjectInterestBaseline(delivery.baseline)))
                return false;
        return messages.empty() || queues.enqueueMessagesAtomically(messages) == TransportResult::Accepted;
    }
    catch (...) { return false; }
}
