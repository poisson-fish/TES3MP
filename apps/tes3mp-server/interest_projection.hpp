#ifndef TES3MP_SERVER_INTEREST_PROJECTION_HPP
#define TES3MP_SERVER_INTEREST_PROJECTION_HPP

#include "tes3mp/canonical_state.hpp"
#include "tes3mp/protocol_exchange.hpp"
#include "tes3mp/transport.hpp"

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct InterestDelivery
    {
        SessionId targetSession;
        ReliableObservationBatch observations;
        LatestWinsSnapshot view;
    };

    struct InterestBaselineDelivery
    {
        SessionId targetSession;
        ReliableInterestBaseline baseline;
        LatestWinsSnapshot view;
    };

    bool sharesInterest(const CanonicalServerState& state, SessionId target, SessionId source) noexcept;
    std::optional<InterestBaselineDelivery> projectInterestBaseline(const CanonicalServerState& state,
        SessionId target, ServerTick tick, CanonicalRevision revision, CanonicalStateVersion stateVersion);
    bool admitInterestBaseline(OutboundQueueSet& queues, TransportConnectionId connection,
        const InterestBaselineDelivery& delivery);

    std::optional<std::vector<InterestDelivery>> projectInterestChanges(
        const CanonicalServerState& before, const CanonicalServerState& after, ServerTick tick,
        CanonicalRevision revision);

    bool admitInterestChange(OutboundQueueSet& queues, TransportConnectionId connection,
        const InterestDelivery& delivery);
    bool admitInterestChangesAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& deliveries);

    std::optional<std::vector<std::pair<SessionId, LatestWinsSnapshot>>> projectInterestViews(
        const CanonicalServerState& state, ServerTick tick, CanonicalRevision revision);
    bool admitInterestViewsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& deliveries);
    bool admitInterestTickAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& observations,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& views);
}

#endif
