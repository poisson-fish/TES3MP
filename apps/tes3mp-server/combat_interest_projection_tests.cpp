#include "combat_interest_projection.hpp"

#include <array>
#include <variant>

namespace
{
    using namespace TES3MP;
    template <class T> T id(std::uint64_t value) { return *T::fromValue(value); }
    Transform root(std::uint64_t cell)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(0, 0, 0),
            Orientation3(zero, zero, zero));
    }

    bool combat_projection_is_private_and_cell_scoped()
    {
        const std::array players{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(10),
            id<AppearanceId>(1), root(7), LinearVelocity3(0, 0, 0), EntityRevision::initial(),
            AuthorityEpoch::initial(), ServerTick::initial()) };
        const std::array sessions{ CanonicalSessionProgress(id<SessionId>(2), SessionGeneration::initial(),
            id<PlayerId>(1), id<EntityId>(10), std::nullopt) };
        const auto canonical = std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
        const std::array actorStates{
            CanonicalActorEntityState(id<ActorId>(3), id<EntityId>(30), id<ActorPrototypeId>(1), root(7),
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
                ServerTick::initial(), ActorActivity::Idle, 0),
            CanonicalActorEntityState(id<ActorId>(4), id<EntityId>(40), id<ActorPrototypeId>(1), root(8),
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
                ServerTick::initial(), ActorActivity::Idle, 0) };
        const auto spatial = std::get<CanonicalActorWorld>(createCanonicalActorWorld(actorStates));
        OpenMwMeleeAttacker attacker;
        attacker.fatigue = 75.f;
        const std::array combatPlayers{ CanonicalPlayerCombatState{ id<PlayerId>(1),
            id<CombatRevision>(5), attacker, std::nullopt, std::nullopt } };
        OpenMwMeleeVictim first;
        first.health = 40.f;
        OpenMwMeleeVictim second;
        second.health = 90.f;
        const std::array combatActors{ CanonicalActorCombatState{ id<ActorId>(3), id<CombatRevision>(6), first },
            CanonicalActorCombatState{ id<ActorId>(4), id<CombatRevision>(7), second } };
        const auto random = Xoshiro256StarStar::fromWorldSeed(1, *RandomStreamKey::fromValues(1, 1)).snapshot();
        const auto combat = std::get<CanonicalCombatWorld>(
            createCanonicalCombatWorld(combatPlayers, combatActors, random));
        const AuthoritativeMeleeEvent visibleEvent{ id<ServerTick>(9), id<PlayerId>(1), id<ActorId>(3),
            id<CombatRevision>(5), id<CombatRevision>(6), OpenMwMeleeResolution{ .damage = 10.f, .hit = true } };
        const AuthoritativeMeleeEvent hiddenEvent{ id<ServerTick>(9), id<PlayerId>(1), id<ActorId>(4),
            id<CombatRevision>(5), id<CombatRevision>(7), OpenMwMeleeResolution{ .damage = 5.f, .hit = true } };
        const std::array events{ visibleEvent, hiddenEvent };
        auto snapshot = TES3MP::ServerApp::projectCombatSnapshot(canonical, spatial, combat, id<SessionId>(2),
            id<ServerTick>(9), id<CanonicalRevision>(4));
        auto batch = TES3MP::ServerApp::projectCombatEvents(canonical, spatial, id<SessionId>(2),
            id<ServerTick>(9), id<CanonicalRevision>(4), events);
        return snapshot && snapshot->selfPlayerId() == id<PlayerId>(1) && snapshot->selfFatigue() == 75.f
            && snapshot->actors().size() == 1 && snapshot->actors()[0].actorId == id<ActorId>(3)
            && batch && batch->events().size() == 1 && batch->events()[0].targetActorId == id<ActorId>(3);
    }
}

int main()
{
    return combat_projection_is_private_and_cell_scoped() ? 0 : 1;
}
