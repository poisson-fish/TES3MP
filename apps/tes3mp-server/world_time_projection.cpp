#include "world_time_projection.hpp"

#include <tes3mp/protocol_frame.hpp>

namespace TES3MP::ServerApp
{
    std::optional<ReliableWorldTimeState> projectWorldTimeBaseline(const CanonicalServerState& players,
        const CanonicalWorldState& world, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision)
    {
        const auto* session = players.findActiveSession(target);
        if (!session)
            return std::nullopt;
        return ReliableWorldTimeState{ target, session->sessionGeneration(), tick, canonicalRevision,
            world.time(), true };
    }

    std::optional<ReliableWorldTimeState> projectWorldTimeUpdate(const CanonicalWorldState& world,
        SessionId target, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision)
    {
        return ReliableWorldTimeState{ target, generation, tick, canonicalRevision, world.time(), false };
    }

    bool appendWorldTimeMessage(std::vector<std::vector<std::byte>>& frames,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const ReliableWorldTimeState& state)
    try
    {
        auto encoded = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableWorldTimeState,
            encodeReliableWorldTimeState(state));
        auto* frame = std::get_if<std::vector<std::byte>>(&encoded);
        if (!frame)
            return false;
        frames.push_back(std::move(*frame));
        messages.push_back({ connection, TransportChannel::ReliableOrdered, frames.back() });
        return true;
    }
    catch (...)
    {
        return false;
    }
}
