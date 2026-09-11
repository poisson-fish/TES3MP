#ifndef TES3MP_SERVER_WORLD_TIME_PROJECTION_HPP
#define TES3MP_SERVER_WORLD_TIME_PROJECTION_HPP

#include <tes3mp/canonical_state.hpp>
#include <tes3mp/transport.hpp>
#include <tes3mp/world_state.hpp>
#include <tes3mp/world_time_replication.hpp>

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    std::optional<ReliableWorldTimeState> projectWorldTimeBaseline(const CanonicalServerState& players,
        const CanonicalWorldState& world, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision);
    std::optional<ReliableWorldTimeState> projectWorldTimeUpdate(const CanonicalWorldState& world,
        SessionId target, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision);
    bool appendWorldTimeMessage(std::vector<std::vector<std::byte>>& frames,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const ReliableWorldTimeState& state);
}

#endif
