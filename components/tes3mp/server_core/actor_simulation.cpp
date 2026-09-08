#include <tes3mp/actor_simulation.hpp>
#include <tes3mp/combat_world.hpp>

#include <algorithm>
#include <limits>

namespace TES3MP
{
    namespace
    {
        Turn32 facingToward(std::int64_t vx, std::int64_t vy) noexcept
        {
            // The current movement kernel advances on one horizontal axis at a time.
            // Keep facing updates integer-only so canonical replay is bit-for-bit
            // deterministic across standard-library math implementations.
            if (vx > 0)
                return Turn32::fromValue(0x40000000u);
            if (vx < 0)
                return Turn32::fromValue(0xc0000000u);
            return Turn32::fromValue(vy < 0 ? 0x80000000u : 0u);
        }
        ActorActivity initialActivity(ActorAiPackageKind kind) noexcept
        {
            switch (kind)
            {
                case ActorAiPackageKind::Idle: return ActorActivity::Idle;
                case ActorAiPackageKind::Travel: return ActorActivity::Travel;
                case ActorAiPackageKind::Wander: return ActorActivity::Wander;
            }
            return ActorActivity::Idle;
        }

        std::uint64_t magnitude(std::int64_t value) noexcept
        {
            return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
        }

        std::int64_t stepToward(std::int64_t from, std::int64_t to, std::uint64_t limit) noexcept
        {
            if (from == to)
                return 0;
            std::uint64_t distance = 0;
            if (from >= 0 && to >= 0)
                distance = from < to ? magnitude(to) - magnitude(from) : magnitude(from) - magnitude(to);
            else if (from < 0 && to < 0)
                distance = from < to ? magnitude(from) - magnitude(to) : magnitude(to) - magnitude(from);
            else
            {
                const auto first = magnitude(from);
                const auto second = magnitude(to);
                distance = first >= limit || second >= limit || first >= limit - second ? limit : first + second;
            }
            const auto step = static_cast<std::int64_t>(std::min(distance, limit));
            return from < to ? step : -step;
        }

        LinearVelocity3 velocityToward(Position3 from, Position3 to, std::uint64_t speed) noexcept
        {
            if (from.x() != to.x())
                return LinearVelocity3(stepToward(from.x(), to.x(), speed), 0, 0);
            if (from.y() != to.y())
                return LinearVelocity3(0, stepToward(from.y(), to.y(), speed), 0);
            return LinearVelocity3(0, 0, stepToward(from.z(), to.z(), speed));
        }

        bool activeCell(const CanonicalServerState& players, const CellId& cell) noexcept
        {
            return std::ranges::any_of(players.activeSessions(), [&](const CanonicalSessionProgress& session) {
                const auto* player = players.findPlayer(session.playerId());
                return player && player->transform().cell() == cell;
            });
        }

        bool stateMatchesCatalog(const CanonicalActorEntityState& state, const ActorCatalogEntry& entry) noexcept
        {
            if (state.actorId() != entry.actorId || state.entityId() != entry.entityId
                || state.prototypeId() != entry.prototypeId || state.root().cell() != entry.initialRoot.cell())
                return false;
            const auto waypoints = entry.aiPackage.waypoints();
            switch (entry.aiPackage.kind())
            {
                case ActorAiPackageKind::Idle:
                    return state.activity() == ActorActivity::Idle && state.waypointIndex() == 0;
                case ActorAiPackageKind::Travel:
                    return (state.activity() == ActorActivity::Travel || state.activity() == ActorActivity::Idle)
                        && state.waypointIndex() < waypoints.size();
                case ActorAiPackageKind::Wander:
                    return state.activity() == ActorActivity::Wander && state.waypointIndex() < waypoints.size();
            }
            return false;
        }

        std::variant<CanonicalActorEntityState, ActorSimulationError> replaceActor(
            const CanonicalActorEntityState& current, ServerTick tick, Transform root, LinearVelocity3 velocity,
            ActorActivity activity, std::uint16_t waypointIndex, std::size_t actorIndex) noexcept
        {
            if (root == current.root() && velocity == current.velocity() && activity == current.activity()
                && waypointIndex == current.waypointIndex())
                return current;
            if (tick < current.lastChangeTick())
                return ActorSimulationError{ ActorSimulationErrorCode::TickRegression, actorIndex };
            const auto revision = current.revision().next();
            if (!revision)
                return ActorSimulationError{ ActorSimulationErrorCode::RevisionExhausted, actorIndex };
            return CanonicalActorEntityState(current.actorId(), current.entityId(), current.prototypeId(), root,
                velocity, *revision, current.authorityEpoch(), tick, activity, waypointIndex);
        }
    }

    const CanonicalActorEntityState* CanonicalActorWorld::find(ActorId actorId) const noexcept
    {
        const auto found = std::ranges::lower_bound(mActors, actorId, {}, &CanonicalActorEntityState::actorId);
        return found != mActors.end() && found->actorId() == actorId ? &*found : nullptr;
    }

    CanonicalActorWorldResult createCanonicalActorWorld(std::span<const CanonicalActorEntityState> actors)
    {
        if (actors.size() > MaximumActorCatalogEntries)
            return CanonicalActorWorldError{ CanonicalActorWorldErrorCode::LimitExceeded, actors.size() };
        for (std::size_t index = 1; index < actors.size(); ++index)
        {
            if (actors[index - 1].actorId() >= actors[index].actorId())
            {
                return CanonicalActorWorldError{ CanonicalActorWorldErrorCode::ActorIdsNotStrictlyOrdered, index,
                    actors[index].actorId().value(), actors[index - 1].actorId().value() };
            }
        }
        std::vector<EntityId> entityIds;
        entityIds.reserve(actors.size());
        for (const auto& actor : actors)
            entityIds.push_back(actor.entityId());
        std::ranges::sort(entityIds);
        if (const auto duplicate = std::ranges::adjacent_find(entityIds); duplicate != entityIds.end())
            return CanonicalActorWorldError{ CanonicalActorWorldErrorCode::DuplicateEntityId, 0,
                duplicate->value() };
        return CanonicalActorWorld(std::vector<CanonicalActorEntityState>(actors.begin(), actors.end()));
    }

    CanonicalActorWorldResult createInitialCanonicalActorWorld(const ActorCatalog& catalog)
    try
    {
        std::vector<CanonicalActorEntityState> actors;
        actors.reserve(catalog.entries().size());
        for (const auto& entry : catalog.entries())
            actors.emplace_back(entry.actorId, entry.entityId, entry.prototypeId, entry.initialRoot,
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
                ServerTick::initial(), initialActivity(entry.aiPackage.kind()), 0);
        return createCanonicalActorWorld(actors);
    }
    catch (...)
    {
        return CanonicalActorWorldError{ CanonicalActorWorldErrorCode::AllocationFailure };
    }

    bool actorAndPlayerEntityIdsAreDisjoint(
        const CanonicalActorWorld& actors, const CanonicalServerState& players) noexcept
    {
        return std::ranges::none_of(actors.actors(), [&](const CanonicalActorEntityState& actor) {
            return std::ranges::any_of(players.players(), [&](const CanonicalPlayerEntityState& player) {
                return actor.entityId() == player.entityId();
            });
        });
    }

    namespace
    {
        ActorSimulationResult advanceActors(const CanonicalActorWorld& current, const ActorCatalog& catalog,
            const CanonicalServerState& players, const CanonicalCombatWorld* combat, ServerTick tick,
            MovementProfile movementProfile, ServerCollisionQuery& collision)
    try
    {
        if (current.actors().size() != catalog.entries().size()
            || !actorAndPlayerEntityIdsAreDisjoint(current, players))
            return ActorSimulationError{ ActorSimulationErrorCode::CatalogMismatch };
        std::vector<CanonicalActorEntityState> replacements;
        replacements.reserve(current.actors().size());
        for (std::size_t index = 0; index < current.actors().size(); ++index)
        {
            const auto& actor = current.actors()[index];
            const auto& entry = catalog.entries()[index];
            if (!stateMatchesCatalog(actor, entry))
                return ActorSimulationError{ ActorSimulationErrorCode::CatalogMismatch, index };
            if (tick < actor.lastChangeTick())
                return ActorSimulationError{ ActorSimulationErrorCode::TickRegression, index };
            if (combat)
            {
                const auto* combatActor = combat->findActor(actor.actorId());
                if (!combatActor)
                    return ActorSimulationError{ ActorSimulationErrorCode::CatalogMismatch, index };
                if (combatActor->stats.dead)
                {
                    if (actor.activity() == ActorActivity::Idle
                        && actor.velocity() == LinearVelocity3(0, 0, 0))
                    {
                        replacements.push_back(actor);
                        continue;
                    }
                    auto replacement = replaceActor(actor, tick, actor.root(), LinearVelocity3(0, 0, 0),
                        ActorActivity::Idle, actor.waypointIndex(), index);
                    if (const auto* error = std::get_if<ActorSimulationError>(&replacement))
                        return *error;
                    replacements.push_back(std::get<CanonicalActorEntityState>(std::move(replacement)));
                    continue;
                }
            }
            if (!activeCell(players, actor.root().cell()))
            {
                replacements.push_back(actor);
                continue;
            }

            Transform root = actor.root();
            LinearVelocity3 velocity = actor.velocity();
            ActorActivity activity = actor.activity();
            std::uint16_t waypointIndex = actor.waypointIndex();
            const auto waypoints = entry.aiPackage.waypoints();
            if (activity == ActorActivity::Idle)
                velocity = LinearVelocity3(0, 0, 0);
            else if (root.position() == waypoints[waypointIndex])
            {
                velocity = LinearVelocity3(0, 0, 0);
                if (activity == ActorActivity::Travel && waypointIndex + 1 == waypoints.size())
                    activity = ActorActivity::Idle;
                else
                    waypointIndex = static_cast<std::uint16_t>((waypointIndex + 1) % waypoints.size());
            }
            else
            {
                velocity = velocityToward(root.position(), waypoints[waypointIndex],
                    movementProfile.speed(LocomotionMode::Walk));
                const auto movement = advanceMovementKernel(catalog.contentManifestId(), movementProfile,
                    LocomotionMode::Walk, actor.entityId(), tick, root, velocity, collision);
                if (const auto* error = std::get_if<MovementKernelError>(&movement))
                    return ActorSimulationError{ ActorSimulationErrorCode::MovementRejected, index, *error };
                const auto& step = std::get<MovementKernelStep>(movement);
                root = step.root;
                velocity = step.velocity;
                if (velocity.x() != 0 || velocity.y() != 0)
                {
                    root = Transform(root.cell(), root.position(),
                        Orientation3(root.orientation().x(), root.orientation().y(),
                            facingToward(velocity.x(), velocity.y())));
                }
            }
            auto replacement = replaceActor(actor, tick, root, velocity, activity, waypointIndex, index);
            if (const auto* error = std::get_if<ActorSimulationError>(&replacement))
                return *error;
            replacements.push_back(std::get<CanonicalActorEntityState>(std::move(replacement)));
        }
        auto created = createCanonicalActorWorld(replacements);
        if (const auto* error = std::get_if<CanonicalActorWorldError>(&created))
        {
            (void)error;
            return ActorSimulationError{ ActorSimulationErrorCode::InvalidResult };
        }
        return std::get<CanonicalActorWorld>(std::move(created));
    }
    catch (...)
    {
        return ActorSimulationError{ ActorSimulationErrorCode::InvalidResult };
    }
    }

    ActorSimulationResult advanceActorSimulation(const CanonicalActorWorld& current, const ActorCatalog& catalog,
        const CanonicalServerState& players, ServerTick tick, MovementProfile movementProfile,
        ServerCollisionQuery& collision)
    {
        return advanceActors(current, catalog, players, nullptr, tick, movementProfile, collision);
    }

    ActorSimulationResult advanceActorSimulation(const CanonicalActorWorld& current, const ActorCatalog& catalog,
        const CanonicalServerState& players, const CanonicalCombatWorld& combat, ServerTick tick,
        MovementProfile movementProfile, ServerCollisionQuery& collision)
    {
        return advanceActors(current, catalog, players, &combat, tick, movementProfile, collision);
    }
}
