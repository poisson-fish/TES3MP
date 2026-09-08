#ifndef TES3MP_SERVER_ACTOR_INTEREST_PROJECTION_HPP
#define TES3MP_SERVER_ACTOR_INTEREST_PROJECTION_HPP

#include "interactive_object_interest_projection.hpp"
#include "interest_projection.hpp"
#include "inventory_interest_projection.hpp"
#include "combat_interest_projection.hpp"
#include <tes3mp/actor_replication.hpp>
#include <tes3mp/actor_simulation.hpp>
#include <tes3mp/canonical_state.hpp>
#include <tes3mp/transport.hpp>

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct ActorInterestBaselineDelivery
    {
        SessionId targetSession;
        ReliableActorInterestBaseline baseline;
        LatestWinsActorSnapshot view;
    };

    std::optional<ActorInterestBaselineDelivery> projectActorInterestBaseline(const CanonicalServerState& players,
        const CanonicalActorWorld& actors, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision);
    std::optional<LatestWinsActorSnapshot> projectActorInterestView(const CanonicalServerState& players,
        const CanonicalActorWorld& actors, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision);
    bool admitActorInterestBaseline(
        OutboundQueueSet& queues, TransportConnectionId connection, const ActorInterestBaselineDelivery& delivery);
    bool admitActorInterestViewsAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, LatestWinsActorSnapshot>>& deliveries);
    bool admitCombinedInterestTickAtomically(OutboundQueueSet& queues,
        const std::vector<std::pair<TransportConnectionId, InterestDelivery>>& playerObservations,
        const std::vector<std::pair<TransportConnectionId, LatestWinsSnapshot>>& playerViews,
        const std::vector<std::pair<TransportConnectionId, ActorInterestBaselineDelivery>>& actorBaselines,
        const std::vector<std::pair<TransportConnectionId, LatestWinsActorSnapshot>>& actorViews,
        const std::vector<std::pair<TransportConnectionId, InteractiveObjectInterestBaselineDelivery>>& objectBaselines
        = {},
        const std::vector<std::pair<TransportConnectionId, InventoryInterestDelivery>>& inventoryBaselines = {},
        const std::vector<std::pair<TransportConnectionId, LatestWinsCombatSnapshot>>& combatViews = {},
        const std::vector<std::pair<TransportConnectionId, ReliableCombatEventBatch>>& combatEvents = {});
}

#endif
