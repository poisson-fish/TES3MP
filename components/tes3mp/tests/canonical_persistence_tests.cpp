#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/server_command_reducer.hpp>

#include <array>
#include <iostream>
#include <memory>
#include <variant>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    struct TestClock final : MonotonicClock
    {
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(nanoseconds); }
        std::uint64_t nanoseconds = 0;
    };

    CanonicalPlayerEntityState player(std::int64_t position, std::uint64_t revision = 1)
    {
        const auto zero = Turn32::fromValue(0);
        return CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(10), id<AppearanceId>(20),
            Transform(CellId::interior(id<CellSpaceId>(30)), Position3(position, 2, 3), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), id<EntityRevision>(revision), id<AuthorityEpoch>(1), id<ServerTick>(revision),
            LocomotionMode::Walk);
    }

    CanonicalPersistenceIdentity identity(std::uint64_t configuration = 1, std::uint32_t packageVersion = 1)
    {
        std::array<std::byte, 32> configurationBytes{};
        configurationBytes[0] = static_cast<std::byte>(configuration);
        const auto package = ServerScriptPackage::create(11, packageVersion, 0).value();
        const std::array scripts{ package };
        const std::array seeds{ PersistenceSeed{ 7, { 1, 2, 3, 4 } } };
        return CanonicalPersistenceIdentity::create(
            testContentManifestId(), ServerConfigurationId::fromBytes(configurationBytes).value(), scripts, seeds)
            .value();
    }

    DurableCommandOrder client(std::uint64_t tick, std::uint64_t ingress)
    {
        return { DurableCommandSource::Client, { tick, ingress, 1, 1, ingress, 100 + ingress, 0, 0, 0 }, 0 };
    }

    DurableCommandOrder script(std::uint64_t tick, std::uint64_t ordinal)
    {
        return { DurableCommandSource::Script, { tick, 1, 0, 0, 11, 1, ServerScriptApiVersion, 0, ordinal }, 0 };
    }

    CanonicalDurablePrefix prefix()
    {
        const std::array firstPlayers{ player(10) };
        const std::array firstCommands{ client(1, 1), script(1, 0) };
        auto first = CanonicalDurableTick::create(
            id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1), firstPlayers, firstCommands)
                         .value();
        const std::array secondPlayers{ player(20, 2) };
        const std::array secondCommands{ client(2, 2) };
        auto second = CanonicalDurableTick::create(id<CanonicalStateVersion>(2), id<CanonicalRevision>(2),
            id<ServerTick>(2), secondPlayers, secondCommands, first.transactionChecksum())
                          .value();
        return CanonicalDurablePrefix::create(identity(), { std::move(first), std::move(second) }).value();
    }

    CanonicalDurableInventoryState inventory(std::uint32_t count, std::uint64_t tick)
    {
        return { { CanonicalPlayerInventoryState{ .player = id<PlayerId>(1),
                     .revision = id<InventoryRevision>(count),
                     .lastChangeTick = id<ServerTick>(tick),
                     .stacks = { { id<ItemStackId>(1), id<ItemPrototypeId>(1), count, 0, 0, std::nullopt } } } },
            {}, {}, id<ItemStackId>(2) };
    }

    CanonicalDurableCombatState combat(float health, std::uint64_t seed, std::uint64_t tick)
    {
        OpenMwMeleeAttacker attacker;
        attacker.agility = 50.f;
        attacker.luck = 50.f;
        attacker.strength = 50.f;
        attacker.fatigueTerm = 1.f;
        attacker.fatigue = 100.f;
        OpenMwMeleeVictim victim;
        victim.health = health;
        victim.fatigue = 100.f;
        CanonicalPlayerCombatState playerCombat{ .playerId = id<PlayerId>(1),
            .revision = id<CombatRevision>(tick),
            .stats = attacker,
            .maximumEncumbranceWeightUnits = 100,
            .victim = victim,
            .respawnVictim = victim,
            .maximumHealth = 100.f,
            .maximumFatigue = 100.f };
        OpenMwMeleeVictim actorVictim;
        actorVictim.health = 20.f;
        actorVictim.fatigue = 50.f;
        CanonicalActorCombatState actorCombat{ .actorId = id<ActorId>(2),
            .revision = id<CombatRevision>(tick),
            .stats = actorVictim,
            .respawnStats = actorVictim,
            .aggressionTarget = id<PlayerId>(1),
            .lastAttackTick = id<ServerTick>(tick),
            .maximumHealth = 20.f,
            .maximumFatigue = 50.f };
        const auto key = RandomStreamKey::fromValues(5, 0).value();
        const auto random = Xoshiro256StarStar::fromWorldSeed(seed, key).snapshot();
        return { { std::move(playerCombat) }, { std::move(actorCombat) }, random.words(), id<ServerTick>(tick) };
    }

    CanonicalDurableInteractiveObjectState objects(DoorState door, LockState lock, std::uint64_t tick)
    {
        return { { CanonicalInteractiveObjectState(id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(30)),
            door, lock, 25, id<KeyPrototypeId>(9), TrapState::Disarmed, id<TrapPrototypeId>(10),
            id<ObjectRevision>(tick), id<ServerTick>(tick)) } };
    }

    CanonicalDurableActorState actors(std::int64_t position, std::uint64_t tick)
    {
        const auto zero = Turn32::fromValue(0);
        return { { CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(11), id<ActorPrototypeId>(12),
            Transform(CellId::interior(id<CellSpaceId>(30)), Position3(position, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(1, 0, 0), id<EntityRevision>(tick), AuthorityEpoch::initial(), id<ServerTick>(tick),
            ActorActivity::Travel, 0) } };
    }

    CanonicalDurablePrefix domainPrefix()
    {
        const std::array firstPlayers{ player(10) };
        const std::array firstCommands{ client(1, 1) };
        auto first = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1),
            id<ServerTick>(1), firstPlayers, firstCommands, CanonicalChecksum(0), inventory(1, 1), combat(100.f, 10, 1),
            objects(DoorState::Closed, LockState::Locked, 1), actors(10, 1))
                         .value();
        const std::array secondPlayers{ player(20, 2) };
        const std::array secondCommands{ client(2, 2) };
        auto second = CanonicalDurableTick::create(id<CanonicalStateVersion>(2), id<CanonicalRevision>(2),
            id<ServerTick>(2), secondPlayers, secondCommands, first.transactionChecksum(), inventory(2, 2),
            combat(75.f, 20, 2), objects(DoorState::Open, LockState::Unlocked, 2), actors(20, 2))
                          .value();
        return CanonicalDurablePrefix::create(identity(), { std::move(first), std::move(second) }).value();
    }

    bool round_trip_preserves_identity_roots_versions_seeds_and_order()
    {
        const auto original = prefix();
        const auto bytes = encodeCanonicalDurablePrefixV2(original);
        const auto decoded = decodeCanonicalDurablePrefix(bytes, identity());
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        return restored && *restored == original && restored->latest()->players().front() == player(20, 2)
            && restored->latest()->stateVersion().value() == 2 && restored->latest()->canonicalRevision().value() == 2
            && restored->latest()->checkpointTick().value() == 2
            && restored->transactions().front().commands().size() == 2
            && restored->transactions().front().commands()[1].source == DurableCommandSource::Script
            && restored->identity().seeds().front().words[3] == 4;
    }

    bool malformed_inputs_and_identity_mismatches_reject_atomically()
    {
        auto bytes = encodeCanonicalDurablePrefixV2(prefix());
        auto truncated = bytes;
        truncated.pop_back();
        if (!std::holds_alternative<CanonicalPersistenceDecodeError>(
                decodeCanonicalDurablePrefix(truncated, identity())))
            return false;
        auto corrupted = bytes;
        corrupted[corrupted.size() / 2] ^= std::byte{ 0x40 };
        if (std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefix(corrupted, identity()))
            != CanonicalPersistenceDecodeError::Corrupted)
            return false;
        return std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefix(bytes, identity(2)))
            == CanonicalPersistenceDecodeError::IdentityMismatch
            && std::get<CanonicalPersistenceDecodeError>(decodeCanonicalDurablePrefix(bytes, identity(1, 2)))
            == CanonicalPersistenceDecodeError::IdentityMismatch;
    }

    std::variant<CanonicalReplayState, CanonicalChecksum> replayStep(
        const CanonicalReplayState&, std::span<const DurableCommandOrder> commands, ServerTick)
    {
        if (commands.empty() || commands.front().source != DurableCommandSource::Client)
            return CanonicalChecksum(0);
        const auto ingress = commands.front().fields[1];
        const std::array players{ ingress == 1 ? player(10) : player(20, 2) };
        auto state = createCanonicalServerState(players, {});
        if (auto* value = std::get_if<CanonicalServerState>(&state))
            return CanonicalReplayState{ std::move(*value), std::nullopt, std::nullopt };
        return CanonicalChecksum(0);
    }

    bool replayed_command_stream_reaches_each_recorded_checksum()
    {
        return replayCanonicalDurablePrefix(prefix(), &replayStep);
    }

    std::variant<CanonicalReplayState, CanonicalChecksum> replayDomains(
        const CanonicalReplayState&, std::span<const DurableCommandOrder> commands, ServerTick tick)
    {
        if (commands.size() != 1 || commands.front().fields[1] != 2 || tick != id<ServerTick>(2))
            return CanonicalChecksum(0);
        const std::array players{ player(20, 2) };
        auto state = createCanonicalServerState(players, {});
        if (auto* value = std::get_if<CanonicalServerState>(&state))
            return CanonicalReplayState{ std::move(*value), inventory(2, 2), combat(75.f, 20, 2),
                objects(DoorState::Open, LockState::Unlocked, 2), actors(20, 2) };
        return CanonicalChecksum(0);
    }

    bool replay_restores_inventory_combat_rng_and_the_full_canonical_checksum()
    {
        const auto original = domainPrefix();
        const auto decoded = decodeCanonicalDurablePrefix(encodeCanonicalDurablePrefixV2(original), identity());
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        if (!restored || !restored->latest()->inventory() || !restored->latest()->combat()
            || !restored->latest()->objects() || !restored->latest()->actors())
            return false;
        const auto& latest = *restored->latest();
        return latest.inventory()->players.front().stacks.front().count == 2
            && latest.combat()->players.front().victim.health == 75.f
            && latest.combat()->randomWords == combat(75.f, 20, 2).randomWords
            && latest.objects()->objects.front().doorState() == DoorState::Open
            && latest.objects()->objects.front().lockState() == LockState::Unlocked
            && latest.actors()->actors.front().root().position() == Position3(20, 0, 0)
            && latest.combat()->actors.front().aggressionTarget == id<PlayerId>(1)
            && latest.canonicalChecksum()
            == canonicalDurableStateChecksumV1(latest.stateVersion(), latest.checkpointTick(), latest.players(),
                latest.inventory(), latest.combat(), latest.objects(), latest.actors())
            && replayCanonicalDurablePrefix(*restored, &replayDomains);
    }

    class ProbeDurability final : public CanonicalDurabilityPort
    {
    public:
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision, std::span<const DurableCommandOrder>, const CanonicalInventoryWorld*,
            const CanonicalCombatWorld*, const CanonicalInteractiveObjectWorld*,
            const CanonicalActorWorld* actors) noexcept override
        {
            called = true;
            sawCandidate = candidate && candidate->state().players().size() == 1;
            installedBeforeAcknowledgement = reducer && !reducer->state().players().empty();
            if (expectedActors)
            {
                sawStagedActors = actors && *actors == *expectedActors;
                actorsInstalledBeforeAcknowledgement = currentActors && *currentActors == *expectedActors;
            }
            return result;
        }
        CanonicalCommandReducer* reducer = nullptr;
        CanonicalDurabilityResult result = CanonicalDurabilityResult::Rejected;
        bool called = false;
        bool sawCandidate = false;
        bool installedBeforeAcknowledgement = false;
        const CanonicalActorWorld* currentActors = nullptr;
        const CanonicalActorWorld* expectedActors = nullptr;
        bool sawStagedActors = false;
        bool actorsInstalledBeforeAcknowledgement = false;
    };

    bool durability_acknowledgement_precedes_installation_and_publication()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
        CanonicalCommandReducer reducer(std::move(empty), observability, testContentManifest());
        ProbeDurability durability;
        durability.reducer = &reducer;
        if (!reducer.configureDurability(durability))
            return false;
        CanonicalSessionProgress session(
            id<SessionId>(1), id<SessionGeneration>(1), id<PlayerId>(1), id<EntityId>(10), std::nullopt);
        auto rejected = reducer.prepareJoin(player(10), session, id<ServerTick>(1));
        const auto before = reducer.latestPublication();
        if (!rejected || reducer.commit(std::move(*rejected)) || !durability.called || !durability.sawCandidate
            || durability.installedBeforeAcknowledgement || reducer.latestPublication() != before
            || !reducer.state().players().empty())
            return false;
        durability.result = CanonicalDurabilityResult::Committed;
        auto accepted = reducer.prepareJoin(player(10), session, id<ServerTick>(1));
        return accepted && reducer.commit(std::move(*accepted)) && reducer.state().players().size() == 1
            && reducer.latestPublication() != before;
    }

    bool actor_simulation_is_durable_before_installation()
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        const auto durablePlayer = player(10);
        const std::array players{ durablePlayer };
        const std::array sessions{ CanonicalSessionProgress(id<SessionId>(1), SessionGeneration::initial(),
            durablePlayer.playerId(), durablePlayer.entityId(), std::nullopt) };
        auto state = std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
        CanonicalCommandReducer reducer(std::move(state), observability, testContentManifest());

        const auto zero = Turn32::fromValue(0);
        const auto cell = CellId::interior(id<CellSpaceId>(30));
        const std::array beforeStates{ CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(11),
            id<ActorPrototypeId>(12), Transform(cell, Position3(0, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial(),
            ActorActivity::Travel, 0) };
        const std::array afterStates{ CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(11),
            id<ActorPrototypeId>(12), Transform(cell, Position3(5, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(5, 0, 0), id<EntityRevision>(2), AuthorityEpoch::initial(), id<ServerTick>(1),
            ActorActivity::Travel, 0) };
        auto liveActors = std::get<CanonicalActorWorld>(createCanonicalActorWorld(beforeStates));
        const auto expectedActors = std::get<CanonicalActorWorld>(createCanonicalActorWorld(afterStates));
        ProbeDurability durability;
        durability.reducer = &reducer;
        durability.currentActors = &liveActors;
        durability.expectedActors = &expectedActors;
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, &liveActors))
            return false;

        TestClock clock;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), id<ServerTick>(1), IngressOrdinal::initial());
        clock.nanoseconds = 33'333'334;
        const auto pumped = intake.pump();
        if (!pumped || pumped.batches().size() != 1)
            return false;
        auto prepared = reducer.prepareTick(pumped.batches().front());
        if (!reducer.stageSimulationCandidates(
                prepared, nullptr, std::nullopt, nullptr, std::nullopt, &liveActors, expectedActors))
            return false;
        CanonicalCommandWorlds worlds;
        worlds.actors = &liveActors;
        if (reducer.commit(std::move(prepared), worlds) || !durability.called || !durability.sawStagedActors
            || durability.actorsInstalledBeforeAcknowledgement || liveActors == expectedActors)
            return false;

        durability.result = CanonicalDurabilityResult::Committed;
        durability.called = false;
        auto accepted = reducer.prepareTick(pumped.batches().front());
        if (!reducer.stageSimulationCandidates(
                accepted, nullptr, std::nullopt, nullptr, std::nullopt, &liveActors, expectedActors))
            return false;
        const bool committed = reducer.commit(std::move(accepted), worlds);
        return committed && durability.called && durability.sawStagedActors
            && !durability.actorsInstalledBeforeAcknowledgement && liveActors == expectedActors;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "round_trip_preserves_identity_roots_versions_seeds_and_order",
            &round_trip_preserves_identity_roots_versions_seeds_and_order },
        std::pair{ "malformed_inputs_and_identity_mismatches_reject_atomically",
            &malformed_inputs_and_identity_mismatches_reject_atomically },
        std::pair{ "replayed_command_stream_reaches_each_recorded_checksum",
            &replayed_command_stream_reaches_each_recorded_checksum },
        std::pair{ "replay_restores_inventory_combat_rng_and_the_full_canonical_checksum",
            &replay_restores_inventory_combat_rng_and_the_full_canonical_checksum },
        std::pair{ "durability_acknowledgement_precedes_installation_and_publication",
            &durability_acknowledgement_precedes_installation_and_publication },
        std::pair{
            "actor_simulation_is_durable_before_installation", &actor_simulation_is_durable_before_installation },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
