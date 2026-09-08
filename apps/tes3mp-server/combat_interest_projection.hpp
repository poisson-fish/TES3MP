#ifndef TES3MP_SERVER_COMBAT_INTEREST_PROJECTION_HPP
#define TES3MP_SERVER_COMBAT_INTEREST_PROJECTION_HPP

#include <tes3mp/combat_replication.hpp>
#include <tes3mp/combat_world.hpp>

#include <optional>
#include <span>

namespace TES3MP::ServerApp
{
    std::optional<LatestWinsCombatSnapshot> projectCombatSnapshot(const CanonicalServerState& players,
        const CanonicalActorWorld& spatialActors, const CanonicalCombatWorld& combat, SessionId target,
        ServerTick tick, CanonicalRevision canonicalRevision);
    std::optional<ReliableCombatEventBatch> projectCombatEvents(const CanonicalServerState& players,
        const CanonicalActorWorld& spatialActors, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision, std::span<const AuthoritativeMeleeEvent> events);
}

#endif
