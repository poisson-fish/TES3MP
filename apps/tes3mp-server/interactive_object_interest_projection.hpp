#ifndef TES3MP_SERVER_INTERACTIVE_OBJECT_INTEREST_PROJECTION_HPP
#define TES3MP_SERVER_INTERACTIVE_OBJECT_INTEREST_PROJECTION_HPP

#include <tes3mp/canonical_state.hpp>
#include <tes3mp/interactive_object_replication.hpp>
#include <tes3mp/interactive_object_world.hpp>
#include <tes3mp/transport.hpp>

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct InteractiveObjectInterestBaselineDelivery
    {
        SessionId targetSession;
        ReliableInteractiveObjectInterestBaseline baseline;
    };

    std::optional<InteractiveObjectInterestBaselineDelivery> projectInteractiveObjectInterestBaseline(
        const CanonicalServerState& players, const CanonicalInteractiveObjectWorld& objects,
        SessionId target, ServerTick tick, CanonicalRevision canonicalRevision);

    std::optional<ReliableInteractiveObjectInterestBaseline> projectCellInteractiveObjectBaseline(
        const CellId& cell, const CanonicalInteractiveObjectWorld& objects,
        SessionId target, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision);

    bool admitInteractiveObjectInterestBaseline(OutboundQueueSet& queues, TransportConnectionId connection,
        const InteractiveObjectInterestBaselineDelivery& delivery);
}

#endif
