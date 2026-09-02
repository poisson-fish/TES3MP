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
}

#endif
