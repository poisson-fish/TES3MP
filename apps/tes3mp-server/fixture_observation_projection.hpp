#ifndef TES3MP_SERVER_FIXTURE_OBSERVATION_PROJECTION_HPP
#define TES3MP_SERVER_FIXTURE_OBSERVATION_PROJECTION_HPP

#include "tes3mp/canonical_state.hpp"
#include "tes3mp/protocol_exchange.hpp"
#include "tes3mp/transport.hpp"

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct FixtureObservationDelivery
    {
        SessionId targetSession;
        ReliableObservationBatch observations;
        LatestWinsSnapshot view;
    };

    std::optional<std::vector<FixtureObservationDelivery>> projectFixtureObservations(
        const CanonicalServerState& before, const CanonicalServerState& after, ServerTick tick);

    bool admitFixtureObservation(OutboundQueueSet& queues, TransportConnectionId connection,
        const FixtureObservationDelivery& delivery);
    bool admitFixtureObservationsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, FixtureObservationDelivery>>& deliveries);

    std::optional<std::vector<std::pair<SessionId, LatestWinsSnapshot>>> projectFixtureViews(
        const CanonicalServerState& state, ServerTick tick);
    bool admitFixtureViewsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& deliveries);
    bool admitFixtureTickAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, FixtureObservationDelivery>>& observations,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& views);
}

#endif
