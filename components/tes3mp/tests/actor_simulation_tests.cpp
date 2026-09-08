#include <tes3mp/actor_simulation.hpp>

#include <array>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    Transform root(std::int64_t x, std::uint64_t cell = 7)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(x, 0, 0), Orientation3(zero, zero, zero));
    }

    CanonicalServerState players(bool active, std::uint64_t cell = 7, std::uint64_t entity = 100)
    {
        const std::array values{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(entity),
            id<AppearanceId>(1), root(0, cell), LinearVelocity3(0, 0, 0), EntityRevision::initial(),
            AuthorityEpoch::initial(), ServerTick::initial()) };
        if (!active)
            return std::get<CanonicalServerState>(createCanonicalServerState(values, {}));
        const std::array sessions{ CanonicalSessionProgress(id<SessionId>(1), SessionGeneration::initial(),
            id<PlayerId>(1), id<EntityId>(entity), std::nullopt) };
        return std::get<CanonicalServerState>(createCanonicalServerState(values, sessions));
    }

    ActorCatalog catalog(ActorAiPackageKind kind, std::span<const Position3> waypoints,
        std::uint64_t entity = 200)
    {
        const std::array entries{ ActorCatalogEntry{ id<ActorId>(1), id<EntityId>(entity), id<ActorPrototypeId>(10),
            root(0), *ActorAiPackage::create(kind, waypoints) } };
        return *ActorCatalog::create(testContentManifest(), entries);
    }

    struct RecordingCollision final : ServerCollisionQuery
    {
        std::size_t calls = 0;
        bool block = false;
        std::optional<ServerCollisionResult> resolve(const ServerCollisionRequest& request) noexcept override
        {
            ++calls;
            if (block)
                return ServerCollisionResult{ request.currentRoot.position(), LinearVelocity3(0, 0, 0) };
            return ServerCollisionResult{ request.attemptedPosition, request.attemptedVelocity };
        }
    };

    bool initial_world_has_separate_stable_actor_identity()
    {
        const auto actorCatalog = catalog(ActorAiPackageKind::Idle, {});
        const auto created = createInitialCanonicalActorWorld(actorCatalog);
        const auto* world = std::get_if<CanonicalActorWorld>(&created);
        return world && world->actors().size() == 1 && world->actors()[0].actorId() == id<ActorId>(1)
            && world->actors()[0].entityId() == id<EntityId>(200)
            && world->actors()[0].prototypeId() == id<ActorPrototypeId>(10)
            && actorAndPlayerEntityIdsAreDisjoint(*world, players(true))
            && !actorAndPlayerEntityIdsAreDisjoint(*world, players(true, 7, 200));
    }

    template <class Type>
    concept HasMutableActors = requires(Type& value) {
        { value.actors() } -> std::same_as<std::span<CanonicalActorEntityState>>;
    };

    bool actor_world_is_bounded_ordered_unique_and_immutable()
    {
        static_assert(!HasMutableActors<CanonicalActorWorld>);
        const auto actorCatalog = catalog(ActorAiPackageKind::Idle, {});
        const auto initial = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog)).actors()[0];
        const std::array unordered{ CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(201),
            id<ActorPrototypeId>(11), initial.root(), initial.velocity(), initial.revision(), initial.authorityEpoch(),
            initial.lastChangeTick(), initial.activity(), 0), initial };
        const std::array duplicateEntity{ initial, CanonicalActorEntityState(id<ActorId>(2), initial.entityId(),
            id<ActorPrototypeId>(11), initial.root(), initial.velocity(), initial.revision(), initial.authorityEpoch(),
            initial.lastChangeTick(), initial.activity(), 0) };
        const auto firstResult = createCanonicalActorWorld(unordered);
        const auto secondResult = createCanonicalActorWorld(duplicateEntity);
        const auto first = std::get_if<CanonicalActorWorldError>(&firstResult);
        const auto second = std::get_if<CanonicalActorWorldError>(&secondResult);
        return first && first->code == CanonicalActorWorldErrorCode::ActorIdsNotStrictlyOrdered
            && second && second->code == CanonicalActorWorldErrorCode::DuplicateEntityId;
    }

    bool inactive_exact_cell_freezes_without_collision_or_revision()
    {
        const std::array route{ Position3(10000, 0, 0) };
        const auto actorCatalog = catalog(ActorAiPackageKind::Travel, route);
        const auto before = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog));
        RecordingCollision collision;
        const auto result = advanceActorSimulation(before, actorCatalog, players(false), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* after = std::get_if<CanonicalActorWorld>(&result);
        return after && *after == before && collision.calls == 0;
    }

    bool active_travel_uses_server_movement_and_collision()
    {
        const std::array route{ Position3(10000, 0, 0) };
        const auto actorCatalog = catalog(ActorAiPackageKind::Travel, route);
        const auto before = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog));
        RecordingCollision collision;
        const auto result = advanceActorSimulation(before, actorCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* after = std::get_if<CanonicalActorWorld>(&result);
        if (!after)
            return false;
        const auto& actor = after->actors()[0];
        return collision.calls == 1 && actor.root().position().x() == 4097 && actor.velocity().x() == 4097
            && actor.revision().value() == 2 && actor.activity() == ActorActivity::Travel;
    }

    bool collision_can_stop_actor_without_client_result_authority()
    {
        const std::array route{ Position3(10000, 0, 0) };
        const auto actorCatalog = catalog(ActorAiPackageKind::Travel, route);
        const auto before = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog));
        RecordingCollision collision;
        collision.block = true;
        const auto result = advanceActorSimulation(before, actorCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* after = std::get_if<CanonicalActorWorld>(&result);
        return after && *after == before && collision.calls == 1;
    }

    bool travel_stops_and_wander_cycles_at_waypoints()
    {
        const std::array one{ Position3(0, 0, 0) };
        RecordingCollision collision;
        const auto travelCatalog = catalog(ActorAiPackageKind::Travel, one);
        const auto travelBefore = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(travelCatalog));
        const auto travelResult = advanceActorSimulation(travelBefore, travelCatalog, players(true),
            id<ServerTick>(1), testMovementProfile(), collision);
        const auto* travelAfter = std::get_if<CanonicalActorWorld>(&travelResult);

        const std::array loop{ Position3(0, 0, 0), Position3(10, 0, 0) };
        const auto wanderCatalog = catalog(ActorAiPackageKind::Wander, loop);
        const auto wanderBefore = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(wanderCatalog));
        const auto wanderResult = advanceActorSimulation(wanderBefore, wanderCatalog, players(true),
            id<ServerTick>(1), testMovementProfile(), collision);
        const auto* wanderAfter = std::get_if<CanonicalActorWorld>(&wanderResult);
        return travelAfter && travelAfter->actors()[0].activity() == ActorActivity::Idle
            && travelAfter->actors()[0].revision().value() == 2
            && wanderAfter && wanderAfter->actors()[0].activity() == ActorActivity::Wander
            && wanderAfter->actors()[0].waypointIndex() == 1;
    }

    bool extreme_opposite_sign_waypoint_uses_bounded_step()
    {
        const std::array route{ Position3(std::numeric_limits<std::int64_t>::max(), 0, 0) };
        const std::array entries{ ActorCatalogEntry{ id<ActorId>(1), id<EntityId>(200), id<ActorPrototypeId>(10),
            root(std::numeric_limits<std::int64_t>::min()),
            *ActorAiPackage::create(ActorAiPackageKind::Travel, route) } };
        const auto actorCatalog = *ActorCatalog::create(testContentManifest(), entries);
        const auto before = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog));
        RecordingCollision collision;
        const auto result = advanceActorSimulation(before, actorCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* after = std::get_if<CanonicalActorWorld>(&result);
        return after && after->actors()[0].velocity().x() == 4097
            && after->actors()[0].root().position().x() == std::numeric_limits<std::int64_t>::min() + 4097;
    }

    bool stale_tick_revision_exhaustion_and_catalog_mismatch_fail_atomically()
    {
        const auto actorCatalog = catalog(ActorAiPackageKind::Idle, {});
        const auto base = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(actorCatalog));
        const auto& initial = base.actors()[0];
        const std::array staleValues{ CanonicalActorEntityState(initial.actorId(), initial.entityId(),
            initial.prototypeId(), initial.root(), LinearVelocity3(1, 0, 0), id<EntityRevision>(2),
            initial.authorityEpoch(), id<ServerTick>(5), ActorActivity::Idle, 0) };
        const auto stale = std::get<CanonicalActorWorld>(createCanonicalActorWorld(staleValues));
        const std::array maximumValues{ CanonicalActorEntityState(initial.actorId(), initial.entityId(),
            initial.prototypeId(), initial.root(), LinearVelocity3(1, 0, 0),
            id<EntityRevision>(std::numeric_limits<std::uint64_t>::max()), initial.authorityEpoch(),
            ServerTick::initial(), ActorActivity::Idle, 0) };
        const auto maximum = std::get<CanonicalActorWorld>(createCanonicalActorWorld(maximumValues));
        const std::array wrongCellValues{ CanonicalActorEntityState(initial.actorId(), initial.entityId(),
            initial.prototypeId(), root(0, 8), initial.velocity(), initial.revision(), initial.authorityEpoch(),
            initial.lastChangeTick(), initial.activity(), 0) };
        const auto wrongCell = std::get<CanonicalActorWorld>(createCanonicalActorWorld(wrongCellValues));
        RecordingCollision collision;
        const auto staleResult = advanceActorSimulation(stale, actorCatalog, players(true), id<ServerTick>(4),
            testMovementProfile(), collision);
        const auto maximumResult = advanceActorSimulation(maximum, actorCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto mismatchResult = advanceActorSimulation(base, actorCatalog, players(true, 7, 200),
            id<ServerTick>(1), testMovementProfile(), collision);
        const auto wrongCellResult = advanceActorSimulation(wrongCell, actorCatalog, players(true, 8),
            id<ServerTick>(1), testMovementProfile(), collision);
        return std::get<ActorSimulationError>(staleResult).code == ActorSimulationErrorCode::TickRegression
            && std::get<ActorSimulationError>(maximumResult).code == ActorSimulationErrorCode::RevisionExhausted
            && std::get<ActorSimulationError>(mismatchResult).code == ActorSimulationErrorCode::CatalogMismatch
            && std::get<ActorSimulationError>(wrongCellResult).code == ActorSimulationErrorCode::CatalogMismatch
            && stale.actors()[0] == staleValues[0] && maximum.actors()[0] == maximumValues[0];
    }

    bool actor_simulation_updates_orientation_towards_movement_direction()
    {
        const std::array eastRoute{ Position3(1000, 0, 0) };
        const auto eastCatalog = catalog(ActorAiPackageKind::Travel, eastRoute);
        const auto initialEast = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(eastCatalog));
        RecordingCollision collision;
        const auto advancedEast = advanceActorSimulation(initialEast, eastCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* eastWorld = std::get_if<CanonicalActorWorld>(&advancedEast);
        if (!eastWorld || eastWorld->actors()[0].root().orientation().z() != Turn32::fromValue(0x40000000))
            return false;

        const std::array southRoute{ Position3(0, -1000, 0) };
        const auto southCatalog = catalog(ActorAiPackageKind::Travel, southRoute);
        const auto initialSouth = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(southCatalog));
        const auto advancedSouth = advanceActorSimulation(initialSouth, southCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* southWorld = std::get_if<CanonicalActorWorld>(&advancedSouth);
        if (!southWorld || southWorld->actors()[0].root().orientation().z() != Turn32::fromValue(0x80000000))
            return false;

        const std::array westRoute{ Position3(-1000, 0, 0) };
        const auto westCatalog = catalog(ActorAiPackageKind::Travel, westRoute);
        const auto initialWest = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(westCatalog));
        const auto advancedWest = advanceActorSimulation(initialWest, westCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* westWorld = std::get_if<CanonicalActorWorld>(&advancedWest);
        if (!westWorld || westWorld->actors()[0].root().orientation().z() != Turn32::fromValue(0xc0000000))
            return false;

        const std::array northRoute{ Position3(0, 1000, 0) };
        const auto northCatalog = catalog(ActorAiPackageKind::Travel, northRoute);
        const auto initialNorth = std::get<CanonicalActorWorld>(createInitialCanonicalActorWorld(northCatalog));
        const auto advancedNorth = advanceActorSimulation(initialNorth, northCatalog, players(true), id<ServerTick>(1),
            testMovementProfile(), collision);
        const auto* northWorld = std::get_if<CanonicalActorWorld>(&advancedNorth);
        if (!northWorld || northWorld->actors()[0].root().orientation().z() != Turn32::fromValue(0))
            return false;

        return true;
    }
}

int main()
{
    return initial_world_has_separate_stable_actor_identity()
            && actor_world_is_bounded_ordered_unique_and_immutable()
            && inactive_exact_cell_freezes_without_collision_or_revision()
            && active_travel_uses_server_movement_and_collision()
            && collision_can_stop_actor_without_client_result_authority()
            && travel_stops_and_wander_cycles_at_waypoints()
            && extreme_opposite_sign_waypoint_uses_bounded_step()
            && stale_tick_revision_exhaustion_and_catalog_mismatch_fail_atomically()
            && actor_simulation_updates_orientation_towards_movement_direction()
        ? 0
        : 1;
}
