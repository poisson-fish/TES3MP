#include "canonical_persistence_file.hpp"

#include <tes3mp/observability.hpp>
#include <tes3mp/server_command_reducer.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::ServerApp;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path() / ("tes3mp-persistence-" + std::to_string(suffix));
            std::filesystem::create_directory(mPath);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(mPath, ignored);
        }
        const std::filesystem::path& path() const noexcept { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    CanonicalPersistenceIdentity identity(std::uint64_t configuration = 1)
    {
        std::array<std::byte, 32> configurationBytes{};
        configurationBytes[0] = static_cast<std::byte>(configuration);
        const std::array seeds{ PersistenceSeed{ 1, { 4, 3, 2, 1 } } };
        return CanonicalPersistenceIdentity::create(
            testContentManifestId(), ServerConfigurationId::fromBytes(configurationBytes).value(), {}, seeds)
            .value();
    }

    CanonicalPlayerEntityState player()
    {
        const auto zero = Turn32::fromValue(0);
        return CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(2), id<AppearanceId>(3),
            Transform(CellId::interior(id<CellSpaceId>(7)), Position3(10, 20, 30), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), id<EntityRevision>(1), id<AuthorityEpoch>(1), ServerTick::initial());
    }

    struct DurableDomains
    {
        ItemPrototypeCatalog catalog;
        CanonicalInventoryWorld inventory;
        CanonicalCombatWorld combat;
        InteractiveObjectCatalog objectCatalog;
        CanonicalInteractiveObjectWorld objects;
        ActorCatalog actorCatalog;
        CanonicalActorWorld actors;
        GlobalVariableCatalog globalCatalog;
        CanonicalWorldState world;
    };

    DurableDomains domains(std::uint32_t count, float health, std::uint64_t randomSeed, bool actorDead = false)
    {
        const auto manifest = testContentManifest();
        const std::array declarations{ ItemPrototypeDeclaration{
            id<ItemPrototypeId>(1), ItemCategory::Miscellaneous, 1, 1, 0, 0, 0, true, id<KeyPrototypeId>(500) } };
        auto catalog = ItemPrototypeCatalog::create(manifest, declarations).value();
        CanonicalPlayerInventoryState inventoryPlayer{ .player = id<PlayerId>(1),
            .revision = id<InventoryRevision>(count),
            .lastChangeTick = id<ServerTick>(count),
            .stacks = { { id<ItemStackId>(1), id<ItemPrototypeId>(1), count, 0, 0, std::nullopt } } };
        auto inventory = CanonicalInventoryWorld::create(
            manifest, catalog, std::array{ inventoryPlayer }, {}, {}, id<ItemStackId>(2))
                             .value();

        OpenMwMeleeAttacker attacker;
        attacker.agility = 50.f;
        attacker.luck = 50.f;
        attacker.strength = 50.f;
        attacker.fatigueTerm = 1.f;
        attacker.fatigue = 100.f;
        OpenMwMeleeVictim victim;
        victim.health = health;
        victim.fatigue = 100.f;
        victim.dead = health <= 0.f;
        const std::array combatPlayers{ CanonicalPlayerCombatState{ .playerId = id<PlayerId>(1),
            .revision = id<CombatRevision>(count),
            .stats = attacker,
            .maximumEncumbranceWeightUnits = 100,
            .victim = victim,
            .respawnVictim = victim,
            .deathTick = victim.dead ? std::optional(id<ServerTick>(count)) : std::nullopt,
            .maximumHealth = 100.f,
            .maximumFatigue = 100.f } };
        OpenMwMeleeVictim actorVictim;
        actorVictim.health = actorDead ? 0.f : 20.f;
        actorVictim.fatigue = 50.f;
        actorVictim.dead = actorDead;
        OpenMwMeleeVictim actorRespawn = actorVictim;
        actorRespawn.health = 20.f;
        actorRespawn.dead = false;
        const std::array combatActors{ CanonicalActorCombatState{ .actorId = id<ActorId>(1),
            .revision = id<CombatRevision>(count),
            .stats = actorVictim,
            .respawnStats = actorRespawn,
            .attackReachQuanta = 128,
            .aggressionTarget = id<PlayerId>(1),
            .lastAttackTick = id<ServerTick>(count),
            .deathTick = actorDead ? std::optional(id<ServerTick>(count)) : std::nullopt,
            .maximumHealth = 20.f,
            .maximumFatigue = 50.f } };
        const auto key = RandomStreamKey::fromValues(5, 0).value();
        auto combat = std::get<CanonicalCombatWorld>(createCanonicalCombatWorld(combatPlayers, combatActors,
            Xoshiro256StarStar::fromWorldSeed(randomSeed, key).snapshot(), id<ServerTick>(count)));

        const auto zero = Turn32::fromValue(0);
        const auto cell = CellId::interior(id<CellSpaceId>(7));
        const auto doorRoot = Transform(cell, Position3(10, 20, 30), Orientation3(zero, zero, zero));
        const std::array objectEntries{ InteractiveObjectCatalogEntry{ id<InteractiveObjectId>(1),
            InteractiveObjectKind::StandardDoor, cell, doorRoot, std::nullopt,
            ObjectLockDeclaration{ true, 25, id<KeyPrototypeId>(500) },
            ObjectTrapDeclaration{ true, id<TrapPrototypeId>(600) } } };
        auto objectCatalog = *InteractiveObjectCatalog::create(manifest, objectEntries);
        auto objects
            = std::get<CanonicalInteractiveObjectWorld>(createInitialCanonicalInteractiveObjectWorld(objectCatalog));
        if (count > 1)
        {
            const std::array changed{ CanonicalInteractiveObjectState(id<InteractiveObjectId>(1), cell,
                DoorState::Closed, LockState::Unlocked, 25, id<KeyPrototypeId>(500), TrapState::Disarmed,
                id<TrapPrototypeId>(600), id<ObjectRevision>(count), id<ServerTick>(count)) };
            objects = std::get<CanonicalInteractiveObjectWorld>(createCanonicalInteractiveObjectWorld(changed));
        }

        const std::array waypoints{ Position3(1000, 0, 0) };
        const std::array actorEntries{ ActorCatalogEntry{ id<ActorId>(1), id<EntityId>(200), id<ActorPrototypeId>(10),
            Transform(cell, Position3(0, 0, 0), Orientation3(zero, zero, zero)),
            *ActorAiPackage::create(ActorAiPackageKind::Travel, waypoints) } };
        auto actorCatalog = *ActorCatalog::create(manifest, actorEntries);
        const std::array actorStates{ CanonicalActorEntityState(id<ActorId>(1), id<EntityId>(200),
            id<ActorPrototypeId>(10),
            Transform(cell, Position3(static_cast<std::int64_t>(count), 0, 0), Orientation3(zero, zero, zero)),
            actorDead ? LinearVelocity3(0, 0, 0) : LinearVelocity3(1, 0, 0), id<EntityRevision>(count),
            AuthorityEpoch::initial(), id<ServerTick>(count), actorDead ? ActorActivity::Idle : ActorActivity::Travel,
            0) };
        auto actors = std::get<CanonicalActorWorld>(createCanonicalActorWorld(actorStates));
        const std::array globalDeclarations{
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int16_t{ 0 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(2), std::int32_t{ 0 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(3), 0.f },
        };
        auto globalCatalog = *GlobalVariableCatalog::create(globalDeclarations);
        CanonicalWorldTimeState time;
        time.day = static_cast<std::uint8_t>(count);
        time.month = 6;
        time.year = 427;
        time.millisecondsSinceMidnight = count * 1000;
        time.timeScaleUnits = count * WorldTimeScaleUnitsPerOne;
        time.revision = id<WorldTimeRevision>(count);
        time.lastChangeTick = id<ServerTick>(count);
        time.lastAdvanceTick = id<ServerTick>(count);
        const std::array globals{ CanonicalGlobalVariableState{ id<GlobalVariableId>(1),
                                      static_cast<std::int16_t>(count), id<GlobalVariableRevision>(count),
                                      id<ServerTick>(count) },
            CanonicalGlobalVariableState{ id<GlobalVariableId>(2), static_cast<std::int32_t>(count * 10),
                id<GlobalVariableRevision>(count), id<ServerTick>(count) },
            CanonicalGlobalVariableState{ id<GlobalVariableId>(3), static_cast<float>(count) + .5f,
                id<GlobalVariableRevision>(count), id<ServerTick>(count) } };
        auto world = *CanonicalWorldState::create(time, globals);
        return { std::move(catalog), std::move(inventory), std::move(combat), std::move(objectCatalog),
            std::move(objects), std::move(actorCatalog), std::move(actors), std::move(globalCatalog),
            std::move(world) };
    }

    bool commitJoin(CanonicalPersistenceFile& file, CanonicalInventoryWorld* inventory = nullptr,
        CanonicalCombatWorld* combat = nullptr, CanonicalInteractiveObjectWorld* objects = nullptr,
        CanonicalActorWorld* actors = nullptr, CanonicalWorldState* world = nullptr, std::uint64_t tick = 1)
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
        CanonicalCommandReducer reducer(std::move(empty), observability, testContentManifest());
        if (!reducer.configureDurability(file, inventory, combat, objects, actors, world))
            return false;
        CanonicalSessionProgress session(
            id<SessionId>(1), id<SessionGeneration>(1), id<PlayerId>(1), id<EntityId>(2), std::nullopt);
        auto prepared = reducer.prepareJoin(player(), session, id<ServerTick>(tick));
        return prepared && reducer.commit(std::move(*prepared));
    }

    bool advance(
        CanonicalPersistenceFile& file, DurableDomains& domains, std::uint64_t firstTick, std::uint64_t lastTick)
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        UnobstructedServerCollisionQuery collision;
        auto restored = file.restoredState();
        if (!restored || !file.restoredStateVersion() || !file.restoredCanonicalRevision()
            || !file.restoredCheckpointTick())
            return false;
        CanonicalCommandReducer reducer(std::move(*restored), *file.restoredStateVersion(),
            *file.restoredCanonicalRevision(), *file.restoredCheckpointTick(), observability, CanonicalSinkBundle{},
            testContentManifest(), collision);
        if (!reducer.configureDurability(
                file, &domains.inventory, &domains.combat, &domains.objects, &domains.actors, &domains.world))
            return false;
        CanonicalSessionProgress session(
            id<SessionId>(1), id<SessionGeneration>(2), id<PlayerId>(1), id<EntityId>(2), std::nullopt);
        auto resumed = reducer.prepareResume(session, id<ServerTick>(firstTick));
        if (!resumed || !reducer.commit(std::move(*resumed)))
            return false;
        const auto zero = Turn32::fromValue(0);
        for (std::uint64_t tick = firstTick + 1; tick <= lastTick; ++tick)
        {
            auto prepared = reducer.preparePlayerSafePoint(id<PlayerId>(1),
                Transform(CellId::interior(id<CellSpaceId>(7)), Position3(static_cast<std::int64_t>(tick), 20, 30),
                    Orientation3(zero, zero, zero)),
                id<ServerTick>(tick));
            if (!prepared)
            {
                std::cerr << "safe point prepare failed " << tick << '\n';
                return false;
            }
            if (!reducer.commit(std::move(*prepared)))
            {
                std::cerr << "safe point commit failed " << tick << '\n';
                return false;
            }
        }
        return true;
    }

    bool file_round_trip_restores_one_verified_session_independent_prefix()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file) || !std::filesystem::exists(path))
            return false;
        file->reset();
        auto reopened = CanonicalPersistenceFile::open(path, identity());
        auto* restoredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        if (!restoredFile)
            return false;
        const auto restored = (*restoredFile)->restoredState();
        return restored && restored->players().size() == 1 && restored->activeSessions().empty()
            && restored->players().front() == player()
            && (*restoredFile)->restoredStateVersion() == id<CanonicalStateVersion>(1)
            && (*restoredFile)->restoredCanonicalRevision() == id<CanonicalRevision>(1)
            && (*restoredFile)->restoredCheckpointTick() == id<ServerTick>(1)
            && (*restoredFile)->prefix().transactions().size() == 1;
    }

    bool file_round_trip_rebuilds_inventory_combat_and_rng()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto expected = domains(2, 75.f, 20);
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file
            || !commitJoin(**file, &expected.inventory, &expected.combat, &expected.objects, &expected.actors,
                &expected.world, 2))
            return false;
        file->reset();
        auto reopened = CanonicalPersistenceFile::open(path, identity());
        auto* restoredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        if (!restoredFile || !(*restoredFile)->restoredInventory() || !(*restoredFile)->restoredCombat()
            || !(*restoredFile)->restoredObjects() || !(*restoredFile)->restoredActors()
            || !(*restoredFile)->restoredWorld())
            return false;
        const auto& inventory = *(*restoredFile)->restoredInventory();
        auto rebuiltInventory = CanonicalInventoryWorld::create(testContentManifest(), expected.catalog,
            inventory.players, inventory.containers, inventory.worldItems, inventory.nextItemStackId);
        const auto& combat = *(*restoredFile)->restoredCombat();
        const auto random = RandomStateV1::fromWords(
            combat.randomWords[0], combat.randomWords[1], combat.randomWords[2], combat.randomWords[3]);
        auto rebuiltCombat = random
            ? createCanonicalCombatWorld(combat.players, combat.actors, *random, combat.lastSimulationTick)
            : std::variant<CanonicalCombatWorld, CanonicalCombatWorldError>(
                  CanonicalCombatWorldError{ CanonicalCombatWorldErrorCode::InvalidStat });
        const auto* rebuiltCombatWorld = std::get_if<CanonicalCombatWorld>(&rebuiltCombat);
        const auto rebuiltObjects = restoreCanonicalInteractiveObjectWorld(
            expected.objectCatalog, (*restoredFile)->restoredObjects()->objects);
        const auto rebuiltActors
            = restoreCanonicalActorWorld(expected.actorCatalog, (*restoredFile)->restoredActors()->actors);
        const auto* latest = (*restoredFile)->prefix().latest();
        return rebuiltInventory && *rebuiltInventory == expected.inventory && rebuiltCombatWorld
            && *rebuiltCombatWorld == expected.combat && std::get_if<CanonicalInteractiveObjectWorld>(&rebuiltObjects)
            && *std::get_if<CanonicalInteractiveObjectWorld>(&rebuiltObjects) == expected.objects
            && std::get_if<CanonicalActorWorld>(&rebuiltActors)
            && *std::get_if<CanonicalActorWorld>(&rebuiltActors) == expected.actors && latest
            && *(*restoredFile)->restoredWorld() == expected.world
            && latest->canonicalChecksum()
            == canonicalDurableStateChecksumV1(latest->stateVersion(), latest->checkpointTick(), latest->players(),
                latest->inventory(), latest->combat(), latest->objects(), latest->actors(), latest->world());
    }

    std::vector<char> read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    void write(const std::filesystem::path& path, std::span<const char> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    bool corruption_truncation_and_identity_mismatch_reject_without_restore()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file))
            return false;
        file->reset();
        const auto valid = read(path);
        auto corrupted = valid;
        corrupted[corrupted.size() / 2] ^= 0x20;
        write(path, corrupted);
        auto corruptResult = CanonicalPersistenceFile::open(path, identity());
        if (!std::holds_alternative<CanonicalPersistenceFileError>(corruptResult))
            return false;
        write(path, std::span(valid).first(valid.size() - 3));
        auto truncatedResult = CanonicalPersistenceFile::open(path, identity());
        if (!std::holds_alternative<CanonicalPersistenceFileError>(truncatedResult))
            return false;
        write(path, valid);
        auto mismatchResult = CanonicalPersistenceFile::open(path, identity(2));
        return std::get<CanonicalPersistenceFileError>(mismatchResult)
            == CanonicalPersistenceFileError::IdentityMismatch;
    }

    bool stale_uncommitted_temporary_file_never_replaces_the_committed_prefix()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file || !commitJoin(**file))
            return false;
        file->reset();
        auto temporary = path;
        temporary += ".tmp";
        const std::array junk{ 'u', 'n', 'c', 'o', 'm', 'm', 'i', 't', 't', 'e', 'd' };
        write(temporary, junk);
        auto reopened = CanonicalPersistenceFile::open(path, identity());
        auto* restored = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        return restored && (*restored)->prefix().transactions().size() == 1 && !std::filesystem::exists(temporary);
    }

    bool matchesDomains(const CanonicalPersistenceFile& file, std::uint32_t count, float health,
        std::array<std::uint64_t, 4> randomWords, CanonicalChecksum checksum, bool actorDead = false)
    {
        const auto* inventory = file.restoredInventory();
        const auto* combat = file.restoredCombat();
        const auto* objects = file.restoredObjects();
        const auto* actors = file.restoredActors();
        const auto* world = file.restoredWorld();
        const auto* latest = file.prefix().latest();
        return inventory && combat && objects && actors && world && latest && inventory->players.size() == 1
            && inventory->players.front().stacks.size() == 1 && inventory->players.front().stacks.front().count == count
            && combat->players.size() == 1 && combat->players.front().victim.health == health
            && objects->objects.size() == 1 && objects->objects.front().doorState() == DoorState::Closed
            && objects->objects.front().lockState() == (count == 1 ? LockState::Locked : LockState::Unlocked)
            && actors->actors.size() == 1
            && actors->actors.front().root().position().x() == static_cast<std::int64_t>(count)
            && combat->actors.size() == 1 && combat->actors.front().stats.dead == actorDead
            && combat->actors.front().respawnStats.health == 20.f && !combat->actors.front().respawnStats.dead
            && combat->actors.front().lastAttackTick == id<ServerTick>(count)
            && combat->actors.front().deathTick.has_value() == actorDead && combat->randomWords == randomWords
            && world->time().day == count && world->time().timeScaleUnits == count * WorldTimeScaleUnitsPerOne
            && std::get<std::int16_t>(world->globals()[0].value) == static_cast<std::int16_t>(count)
            && std::get<std::int32_t>(world->globals()[1].value) == static_cast<std::int32_t>(count * 10)
            && std::get<float>(world->globals()[2].value) == static_cast<float>(count) + .5f
            && latest->canonicalChecksum() == checksum
            && canonicalDurableStateChecksumV1(latest->stateVersion(), latest->checkpointTick(), latest->players(),
                   latest->inventory(), latest->combat(), latest->objects(), latest->actors(), latest->world())
            == checksum;
    }

    bool bounded_compaction_keeps_one_checkpoint_and_a_bounded_tail()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto state = domains(1, 100.f, 10);
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file
            || !commitJoin(
                **file, &state.inventory, &state.combat, &state.objects, &state.actors, &state.world))
        {
            std::cerr << "compaction setup failed\n";
            return false;
        }
        if (!advance(**file, state, 2, 41))
        {
            std::cerr << "compaction advance failed at durable tick "
                      << ((*file)->restoredCheckpointTick() ? (*file)->restoredCheckpointTick()->value() : 9999)
                      << '\n';
            return false;
        }
        const bool result = (*file)->prefix().transactions().size() <= MaximumPersistenceTransactions
            && (*file)->prefix().journal().size() <= MaximumPersistenceJournalTransactions
            && (*file)->prefix().checkpoint() && (*file)->prefix().checkpoint()->checkpointTick() == id<ServerTick>(33)
            && (*file)->prefix().latest()->checkpointTick() == id<ServerTick>(41);
        if (!result)
            std::cerr << "compaction shape " << (*file)->prefix().transactions().size() << ' '
                      << (*file)->prefix().journal().size() << ' '
                      << (*file)->prefix().checkpoint()->checkpointTick().value() << ' '
                      << (*file)->prefix().latest()->checkpointTick().value() << '\n';
        return result;
    }

    bool crash_cuts_preserve_time_global_key_door_and_actor_transactions()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "world.t3p";
        auto previousDomains = domains(1, 100.f, 10);
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file
            || !commitJoin(**file, &previousDomains.inventory, &previousDomains.combat, &previousDomains.objects,
                &previousDomains.actors, &previousDomains.world))
            return false;
        const auto previousBytes = read(path);
        const auto previousChecksum = (*file)->prefix().latest()->canonicalChecksum();
        const auto previousRandom = previousDomains.combat.randomState().words();

        auto nextDomains = domains(2, 75.f, 20);
        if (!advance(**file, nextDomains, 2, 2))
            return false;
        const auto nextBytes = read(path);
        const auto nextChecksum = (*file)->prefix().latest()->canonicalChecksum();
        const auto nextRandom = nextDomains.combat.randomState().words();
        file->reset();

        auto temporary = path;
        temporary += ".tmp";
        for (std::size_t cut = 0; cut <= nextBytes.size(); ++cut)
        {
            write(path, previousBytes);
            write(temporary, std::span(nextBytes).first(cut));
            auto recovered = CanonicalPersistenceFile::open(path, identity());
            auto* recoveredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&recovered);
            if (!recoveredFile || !matchesDomains(**recoveredFile, 1, 100.f, previousRandom, previousChecksum))
                return false;
        }

        write(path, nextBytes);
        write(temporary, previousBytes);
        auto recovered = CanonicalPersistenceFile::open(path, identity());
        auto* recoveredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&recovered);
        return recoveredFile && matchesDomains(**recoveredFile, 2, 75.f, nextRandom, nextChecksum);
    }

    bool crash_cuts_preserve_actor_death_and_respawn_state()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "actor-death.t3p";
        auto before = domains(1, 100.f, 10);
        auto opened = CanonicalPersistenceFile::open(path, identity());
        auto* file = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&opened);
        if (!file
            || !commitJoin(**file, &before.inventory, &before.combat, &before.objects, &before.actors, &before.world))
            return false;
        const auto previousBytes = read(path);
        const auto previousChecksum = (*file)->prefix().latest()->canonicalChecksum();
        const auto previousRandom = before.combat.randomState().words();

        auto after = domains(2, 75.f, 20, true);
        if (!advance(**file, after, 2, 2))
            return false;
        const auto nextBytes = read(path);
        const auto nextChecksum = (*file)->prefix().latest()->canonicalChecksum();
        const auto nextRandom = after.combat.randomState().words();
        file->reset();

        auto temporary = path;
        temporary += ".tmp";
        for (std::size_t cut = 0; cut <= nextBytes.size(); ++cut)
        {
            write(path, previousBytes);
            write(temporary, std::span(nextBytes).first(cut));
            auto recovered = CanonicalPersistenceFile::open(path, identity());
            auto* recoveredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&recovered);
            if (!recoveredFile || !matchesDomains(**recoveredFile, 1, 100.f, previousRandom, previousChecksum, false))
                return false;
        }

        write(path, nextBytes);
        auto recovered = CanonicalPersistenceFile::open(path, identity());
        auto* recoveredFile = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&recovered);
        return recoveredFile && matchesDomains(**recoveredFile, 2, 75.f, nextRandom, nextChecksum, true);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "file_round_trip_restores_one_verified_session_independent_prefix",
            &file_round_trip_restores_one_verified_session_independent_prefix },
        std::pair{
            "file_round_trip_rebuilds_inventory_combat_and_rng", &file_round_trip_rebuilds_inventory_combat_and_rng },
        std::pair{ "corruption_truncation_and_identity_mismatch_reject_without_restore",
            &corruption_truncation_and_identity_mismatch_reject_without_restore },
        std::pair{ "stale_uncommitted_temporary_file_never_replaces_the_committed_prefix",
            &stale_uncommitted_temporary_file_never_replaces_the_committed_prefix },
        std::pair{ "bounded_compaction_keeps_one_checkpoint_and_a_bounded_tail",
            &bounded_compaction_keeps_one_checkpoint_and_a_bounded_tail },
        std::pair{ "crash_cuts_preserve_time_global_key_door_and_actor_transactions",
            &crash_cuts_preserve_time_global_key_door_and_actor_transactions },
        std::pair{
            "crash_cuts_preserve_actor_death_and_respawn_state", &crash_cuts_preserve_actor_death_and_respawn_state },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
