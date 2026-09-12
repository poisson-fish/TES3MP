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

    ServerScriptStateCatalog scriptCatalog(std::int64_t initialValue = 0)
    {
        const std::array entries{ ServerScriptVariableCatalogEntry{ 11, id<ScriptVariableId>(1), initialValue } };
        return ServerScriptStateCatalog::create(entries).value();
    }

    CanonicalPersistenceIdentity identityWithScriptState(std::int64_t initialValue = 0)
    {
        std::array<std::byte, 32> configurationBytes{};
        configurationBytes[0] = std::byte{ 1 };
        const std::array scripts{ ServerScriptPackage::create(11, 1, 0).value() };
        const std::array seeds{ PersistenceSeed{ 7, { 1, 2, 3, 4 } } };
        const auto catalog = scriptCatalog(initialValue);
        return CanonicalPersistenceIdentity::create(testContentManifestId(),
            ServerConfigurationId::fromBytes(configurationBytes).value(), scripts, catalog, seeds)
            .value();
    }

    CanonicalScriptState scriptState(std::int64_t value, std::uint64_t tick)
    {
        auto state = CanonicalScriptState::initial(scriptCatalog()).value();
        if (tick == 0)
            return state;
        auto changed = compareAndSetCanonicalScriptVariable(state, scriptCatalog(), 11, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), value, id<ServerTick>(tick));
        return std::get<CanonicalScriptState>(std::move(changed));
    }

    DurableCommandOrder client(std::uint64_t tick, std::uint64_t ingress)
    {
        return { DurableCommandSource::Client, { tick, ingress, 1, 1, ingress, 100 + ingress, 0, 0, 0 }, 0 };
    }

    DurableCommandOrder script(std::uint64_t tick, std::uint64_t ordinal)
    {
        return { DurableCommandSource::Script, { tick, 1, 0, 0, 11, 1, ServerScriptApiVersion, 0, ordinal }, 0 };
    }

    DurableCommandOrder dialogue(std::uint64_t tick)
    {
        return { DurableCommandSource::DialogueChoice, { tick, 1, 40, 0, 0, 0, 0, 0, 0 }, 0 };
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
        return {
            { CanonicalPlayerInventoryState{ .player = id<PlayerId>(1),
                .revision = id<InventoryRevision>(count),
                .lastChangeTick = id<ServerTick>(tick),
                .stacks = { { id<ItemStackId>(1), id<ItemPrototypeId>(1), count, 26 - count, 0, std::nullopt } } } },
            {}, {}, id<ItemStackId>(2)
        };
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
            .maximumFatigue = 100.f,
            .magicka = 60.f,
            .maximumMagicka = 80.f,
            .magicSkills = { 31.f, 32.f, 33.f, 34.f, 35.f, 36.f },
            .enchantSkill = 37.f,
            .knownSpells = { id<SpellRecordId>(7), id<SpellRecordId>(9) },
            .lastMagicUseTick = id<ServerTick>(tick),
            .securitySkill = 47.f };
        const auto security = static_cast<std::size_t>(CombatProgressionSkill::Security);
        playerCombat.skillRules[security].useGain = 3.f;
        playerCombat.skillProgression[security].progress = tick > 1 ? 0.02f : 0.f;
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
            .maximumFatigue = 50.f,
            .magicka = 15.f,
            .maximumMagicka = 25.f };
        const auto key = RandomStreamKey::fromValues(5, 0).value();
        const auto random = Xoshiro256StarStar::fromWorldSeed(seed, key).snapshot();
        return { { std::move(playerCombat) }, { std::move(actorCombat) }, random.words(), id<ServerTick>(tick) };
    }

    CanonicalDurableInteractiveObjectState objects(DoorState door, LockState lock, std::uint64_t tick)
    {
        return { { CanonicalInteractiveObjectState(id<InteractiveObjectId>(1), CellId::interior(id<CellSpaceId>(30)),
            door, lock, 25, id<KeyPrototypeId>(9), tick > 1 ? TrapState::Disarmed : TrapState::Armed,
            id<TrapPrototypeId>(10), id<ObjectRevision>(tick), id<ServerTick>(tick)) } };
    }

    CanonicalDurableActorState actors(std::int64_t position, std::uint64_t tick)
    {
        const auto zero = Turn32::fromValue(0);
        return { { CanonicalActorEntityState(id<ActorId>(2), id<EntityId>(11), id<ActorPrototypeId>(12),
            Transform(CellId::interior(id<CellSpaceId>(30)), Position3(position, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(1, 0, 0), id<EntityRevision>(tick), AuthorityEpoch::initial(), id<ServerTick>(tick),
            ActorActivity::Travel, 0) } };
    }

    CanonicalWorldState world(std::int32_t value, std::uint64_t tick)
    {
        CanonicalWorldTimeState time;
        time.day = static_cast<std::uint8_t>(value);
        time.month = 6;
        time.year = 427;
        time.millisecondsSinceMidnight = static_cast<std::uint32_t>(value) * 1000;
        time.timeScaleUnits = static_cast<std::uint32_t>(value) * WorldTimeScaleUnitsPerOne;
        time.revision = id<WorldTimeRevision>(tick);
        time.lastChangeTick = id<ServerTick>(tick);
        time.lastAdvanceTick = id<ServerTick>(tick);
        const std::array globals{ CanonicalGlobalVariableState{
            id<GlobalVariableId>(1), value, id<GlobalVariableRevision>(tick), id<ServerTick>(tick) } };
        const std::array quests{ QuestCatalogEntry{
            id<QuestId>(1), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10) } } };
        const std::array journal{ JournalCatalogEntry{ id<JournalEntryId>(1), id<QuestId>(1), id<QuestStage>(10) } };
        const auto catalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        const std::array factions{ FactionCatalogEntry{
            id<FactionId>(1), { id<FactionRank>(0), id<FactionRank>(1) } } };
        const std::array choices{ DialogueChoiceCatalogEntry{
            id<DialogueChoiceId>(1), id<FactionId>(1), id<FactionRank>(1), 5 } };
        const auto factionCatalog = FactionDialogueCatalog::create(testContentManifestId(), factions, choices).value();
        const std::array weatherIds{ id<WeatherId>(1), id<WeatherId>(2) };
        const std::array weatherRegions{ WeatherRegionCatalogEntry{
            id<WeatherRegionId>(1), id<WeatherId>(1), 3, 3, { id<WeatherId>(1), id<WeatherId>(2) } } };
        const auto weatherCatalog = WeatherCatalog::create(testContentManifestId(), weatherIds, weatherRegions).value();
        const auto random
            = Xoshiro256StarStar::fromWorldSeed(44, *RandomStreamKey::fromValues(0x5745415448455231ULL, 0));
        CanonicalWeatherState weather{ {}, random.snapshot(), id<ServerTick>(tick) };
        weather.regions.push_back(
            { id<WeatherRegionId>(1), id<WeatherId>(1), tick > 1 ? id<WeatherId>(2) : id<WeatherId>(1),
                id<ServerTick>(tick), id<ServerTick>(tick > 1 ? tick + 3 : tick),
                id<ServerTick>(tick > 1 ? tick + 6 : tick + 3), id<WeatherRevision>(tick), id<ServerTick>(tick) });
        std::vector<CanonicalPlayerQuestJournalState> players;
        std::vector<CanonicalPlayerFactionState> playerFactions;
        if (tick > 1)
        {
            players.push_back(
                { id<PlayerId>(1), { { id<QuestId>(1), id<QuestStage>(10), id<QuestRevision>(2), id<ServerTick>(2) } },
                    { { id<JournalEntryId>(1), id<JournalRevision>(2), id<ServerTick>(2) } }, id<JournalRevision>(2),
                    id<ServerTick>(2) });
            playerFactions.push_back({ id<PlayerId>(1),
                { { id<FactionId>(1), id<FactionRank>(1), id<FactionMembershipRevision>(2), id<ServerTick>(2), 5,
                    id<FactionReputationRevision>(2), id<ServerTick>(2) } } });
        }
        return CanonicalWorldState::create(
            time, globals, catalog, factionCatalog, weatherCatalog, players, playerFactions, weather)
            .value();
    }

    CanonicalDurablePrefix domainPrefix()
    {
        const std::array firstPlayers{ player(10) };
        const std::array firstCommands{ client(1, 1) };
        auto first = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1),
            id<ServerTick>(1), firstPlayers, firstCommands, CanonicalChecksum(0), inventory(1, 1), combat(100.f, 10, 1),
            objects(DoorState::Closed, LockState::Locked, 1), actors(10, 1), world(1, 1))
                         .value();
        const std::array secondPlayers{ player(20, 2) };
        const std::array secondCommands{ client(2, 2) };
        auto second = CanonicalDurableTick::create(id<CanonicalStateVersion>(2), id<CanonicalRevision>(2),
            id<ServerTick>(2), secondPlayers, secondCommands, first.transactionChecksum(), inventory(2, 2),
            combat(75.f, 20, 2), objects(DoorState::Open, LockState::Unlocked, 2), actors(20, 2), world(2, 2))
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

    bool dialogue_choice_identity_has_a_bounded_durable_order()
    {
        const std::array players{ player(10) };
        const std::array commands{ dialogue(1) };
        const auto tick = CanonicalDurableTick::create(
            id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1), players, commands);
        const auto prefix = tick ? CanonicalDurablePrefix::create(identity(), { *tick }) : std::nullopt;
        const auto decoded = prefix ? decodeCanonicalDurablePrefix(encodeCanonicalDurablePrefixV2(*prefix), identity())
                                    : CanonicalPersistenceDecodeResult(CanonicalPersistenceDecodeError::Malformed);
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        auto invalid = dialogue(1);
        invalid.fields[3] = 1;
        return restored && restored->latest()->commands().size() == 1
            && restored->latest()->commands().front() == commands.front()
            && !CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1),
                players, std::array{ invalid });
    }

    std::variant<CanonicalReplayState, CanonicalChecksum> replayScriptState(
        const CanonicalReplayState& prior, std::span<const DurableCommandOrder> commands, ServerTick tick)
    {
        if (commands.size() != 1 || commands.front().source != DurableCommandSource::Script
            || tick != id<ServerTick>(2))
            return CanonicalChecksum(0);
        auto next = prior;
        next.scriptState = scriptState(7, 2);
        return next;
    }

    bool script_state_round_trip_replay_checksum_and_catalog_identity_are_exact()
    {
        const std::array players{ player(10) };
        auto first = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1),
            id<ServerTick>(1), players, std::span<const DurableCommandOrder>{}, CanonicalChecksum(0), std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt, scriptState(0, 0))
                         .value();
        const std::array commands{ script(2, 1) };
        auto second = CanonicalDurableTick::create(id<CanonicalStateVersion>(2), id<CanonicalRevision>(2),
            id<ServerTick>(2), players, commands, first.transactionChecksum(), std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, scriptState(7, 2))
                          .value();
        const auto original
            = CanonicalDurablePrefix::create(identityWithScriptState(), { std::move(first), std::move(second) })
                  .value();
        const auto bytes = encodeCanonicalDurablePrefixV2(original);
        const auto decoded = decodeCanonicalDurablePrefix(bytes, identityWithScriptState());
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        if (!restored || !restored->latest()->scriptState())
            return false;
        const auto* variable = restored->latest()->scriptState()->find(11, id<ScriptVariableId>(1));
        return variable && std::get<std::int64_t>(variable->value) == 7 && variable->revision.value() == 2
            && variable->lastChangeTick == id<ServerTick>(2)
            && restored->latest()->canonicalChecksum()
            == canonicalDurableStateChecksumV1(restored->latest()->stateVersion(), restored->latest()->checkpointTick(),
                restored->latest()->players(), restored->latest()->inventory(), restored->latest()->combat(),
                restored->latest()->objects(), restored->latest()->actors(), restored->latest()->world(),
                restored->latest()->scriptState())
            && replayCanonicalDurablePrefix(*restored, &replayScriptState)
            && std::get<CanonicalPersistenceDecodeError>(
                   decodeCanonicalDurablePrefix(bytes, identityWithScriptState(1)))
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
                objects(DoorState::Open, LockState::Unlocked, 2), actors(20, 2), world(2, 2) };
        return CanonicalChecksum(0);
    }

    bool replay_restores_inventory_combat_rng_and_the_full_canonical_checksum()
    {
        const auto original = domainPrefix();
        const auto decoded = decodeCanonicalDurablePrefix(encodeCanonicalDurablePrefixV2(original), identity());
        const auto* restored = std::get_if<CanonicalDurablePrefix>(&decoded);
        if (!restored || !restored->latest()->inventory() || !restored->latest()->combat()
            || !restored->latest()->objects() || !restored->latest()->actors() || !restored->latest()->world())
            return false;
        const auto& latest = *restored->latest();
        auto restartedAtCompletion = advanceCanonicalWeather(*latest.world(), id<ServerTick>(5));
        auto uninterruptedAtCompletion = advanceCanonicalWeather(world(2, 2), id<ServerTick>(5));
        auto* restartedWorld = std::get_if<CanonicalWorldState>(&restartedAtCompletion);
        auto* uninterruptedWorld = std::get_if<CanonicalWorldState>(&uninterruptedAtCompletion);
        if (!restartedWorld || !uninterruptedWorld || *restartedWorld != *uninterruptedWorld)
            return false;
        auto restartedAtSelection = advanceCanonicalWeather(*restartedWorld, id<ServerTick>(8));
        auto uninterruptedAtSelection = advanceCanonicalWeather(*uninterruptedWorld, id<ServerTick>(8));
        if (!std::get_if<CanonicalWorldState>(&restartedAtSelection)
            || restartedAtSelection != uninterruptedAtSelection)
            return false;
        return latest.inventory()->players.front().stacks.front().count == 2
            && latest.inventory()->players.front().stacks.front().condition == 24
            && latest.combat()->players.front().victim.health == 75.f
            && latest.combat()->players.front().securitySkill == 47.f
            && latest.combat()->players.front().magicka == 60.f
            && latest.combat()->players.front().maximumMagicka == 80.f
            && latest.combat()->players.front().magicSkills[2] == 33.f
            && latest.combat()->players.front().enchantSkill == 37.f
            && latest.combat()->players.front().knownSpells
            == std::vector<SpellRecordId>{ id<SpellRecordId>(7), id<SpellRecordId>(9) }
        && latest.combat()->players.front().lastMagicUseTick == id<ServerTick>(2)
            && latest.combat()
                   ->players.front()
                   .skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Security)]
                   .progress
            == 0.02f
            && latest.combat()->randomWords == combat(75.f, 20, 2).randomWords
            && latest.objects()->objects.front().doorState() == DoorState::Open
            && latest.objects()->objects.front().lockState() == LockState::Unlocked
            && latest.objects()->objects.front().trapState() == TrapState::Disarmed
            && latest.actors()->actors.front().root().position() == Position3(20, 0, 0)
            && latest.world()->time().day == 2 && std::get<std::int32_t>(latest.world()->globals().front().value) == 2
            && latest.world()->questJournalCatalog()
            && latest.world()->questJournalCatalog()->manifest() == testContentManifestId()
            && latest.world()->questJournal().size() == 1
            && latest.world()->questJournal().front().quests.front().stage == id<QuestStage>(10)
            && latest.world()->questJournal().front().journal.front().id == id<JournalEntryId>(1)
            && latest.world()->factionDialogueCatalog()
            && latest.world()->factionDialogueCatalog()->dialogueChoices().size() == 1
            && latest.world()->factionStates().size() == 1
            && latest.world()->factionStates().front().factions.front().reputation == 5
            && latest.world()->weatherCatalog() && latest.world()->weather()
            && latest.world()->weatherCatalog()->regions().size() == 1
            && latest.world()->weather()->regions.front().currentWeather == id<WeatherId>(1)
            && latest.world()->weather()->regions.front().targetWeather == id<WeatherId>(2)
            && latest.world()->weather()->regions.front().transitionEndTick == id<ServerTick>(5)
            && latest.combat()->actors.front().aggressionTarget == id<PlayerId>(1)
            && latest.combat()->actors.front().magicka == 15.f && latest.combat()->actors.front().maximumMagicka == 25.f
            && latest.canonicalChecksum()
            == canonicalDurableStateChecksumV1(latest.stateVersion(), latest.checkpointTick(), latest.players(),
                latest.inventory(), latest.combat(), latest.objects(), latest.actors(), latest.world())
            && replayCanonicalDurablePrefix(*restored, &replayDomains);
    }

    class ProbeDurability final : public CanonicalDurabilityPort
    {
    public:
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision, std::span<const DurableCommandOrder>, const CanonicalInventoryWorld*,
            const CanonicalCombatWorld*, const CanonicalInteractiveObjectWorld*, const CanonicalActorWorld* actors,
            const CanonicalWorldState* world, const CanonicalScriptState*) noexcept override
        {
            called = true;
            sawCandidate = candidate && candidate->state().players().size() == 1;
            installedBeforeAcknowledgement = reducer && !reducer->state().players().empty();
            if (expectedActors)
            {
                sawStagedActors = actors && *actors == *expectedActors;
                actorsInstalledBeforeAcknowledgement = currentActors && *currentActors == *expectedActors;
            }
            if (expectedWorld)
            {
                sawStagedWorld = world && *world == *expectedWorld;
                worldInstalledBeforeAcknowledgement = currentWorld && *currentWorld == *expectedWorld;
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
        const CanonicalWorldState* currentWorld = nullptr;
        const CanonicalWorldState* expectedWorld = nullptr;
        bool sawStagedWorld = false;
        bool worldInstalledBeforeAcknowledgement = false;
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

    bool time_and_global_mutations_are_durable_before_installation()
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

        const std::array declarations{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto catalog = GlobalVariableCatalog::create(declarations).value();
        const std::array quests{ QuestCatalogEntry{
            id<QuestId>(1), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10) } } };
        const std::array journal{ JournalCatalogEntry{ id<JournalEntryId>(1), id<QuestId>(1), id<QuestStage>(10) } };
        const auto questCatalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        CanonicalWorldTimeState initialTime;
        initialTime.day = 30;
        initialTime.month = 11;
        initialTime.year = 427;
        initialTime.millisecondsSinceMidnight = WorldMillisecondsPerDay - 480;
        auto liveWorld = CanonicalWorldState::initial(initialTime, catalog, questCatalog).value();
        auto advanced = advanceCanonicalWorldTime(liveWorld, id<ServerTick>(1), 16);
        auto* advancedWorld = std::get_if<CanonicalWorldState>(&advanced);
        if (!advancedWorld)
            return false;
        auto changed = setCanonicalGlobal(*advancedWorld, catalog, id<GlobalVariableId>(1),
            GlobalVariableRevision::initial(), std::int32_t{ 7 }, id<ServerTick>(1));
        auto* changedWorld = std::get_if<CanonicalWorldState>(&changed);
        if (!changedWorld)
            return false;
        auto questChanged = setCanonicalQuestStage(*changedWorld, id<PlayerId>(1), id<QuestId>(1),
            QuestRevision::initial(), id<QuestStage>(10), id<ServerTick>(1));
        auto* questWorld = std::get_if<CanonicalWorldState>(&questChanged);
        if (!questWorld)
            return false;
        auto journalChanged = addCanonicalJournalEntry(*questWorld, id<PlayerId>(1), id<QuestId>(1),
            JournalRevision::initial(), id<JournalEntryId>(1), id<ServerTick>(1));
        auto* expected = std::get_if<CanonicalWorldState>(&journalChanged);
        if (!expected)
            return false;
        const auto expectedWorld = *expected;

        ProbeDurability durability;
        durability.reducer = &reducer;
        durability.currentWorld = &liveWorld;
        durability.expectedWorld = &expectedWorld;
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, nullptr, &liveWorld))
            return false;

        TestClock clock;
        ServerCommandIntakeCoordinator intake(
            clock, observability, clock.now(), id<ServerTick>(1), IngressOrdinal::initial());
        clock.nanoseconds = 33'333'334;
        const auto pumped = intake.pump();
        if (!pumped || pumped.batches().size() != 1)
            return false;
        auto prepared = reducer.prepareTick(pumped.batches().front());
        if (!reducer.stageSimulationCandidates(prepared, nullptr, std::nullopt, nullptr, std::nullopt, nullptr,
                std::nullopt, &liveWorld, expectedWorld))
            return false;
        CanonicalCommandWorlds worlds;
        worlds.world = &liveWorld;
        worlds.globalCatalog = &catalog;
        if (reducer.commit(std::move(prepared), worlds) || !durability.called || !durability.sawStagedWorld
            || durability.worldInstalledBeforeAcknowledgement || liveWorld == expectedWorld)
            return false;

        durability.result = CanonicalDurabilityResult::Committed;
        durability.called = false;
        auto accepted = reducer.prepareTick(pumped.batches().front());
        if (!reducer.stageSimulationCandidates(accepted, nullptr, std::nullopt, nullptr, std::nullopt, nullptr,
                std::nullopt, &liveWorld, expectedWorld))
            return false;
        const bool committed = reducer.commit(std::move(accepted), worlds);
        return committed && durability.called && durability.sawStagedWorld
            && !durability.worldInstalledBeforeAcknowledgement && liveWorld == expectedWorld
            && liveWorld.time().year == 428 && liveWorld.time().month == 0 && liveWorld.time().day == 1
            && std::get<std::int32_t>(liveWorld.globals().front().value) == 7 && liveWorld.questJournal().size() == 1
            && liveWorld.questJournal().front().quests.front().stage == id<QuestStage>(10)
            && liveWorld.questJournal().front().journal.front().id == id<JournalEntryId>(1);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "round_trip_preserves_identity_roots_versions_seeds_and_order",
            &round_trip_preserves_identity_roots_versions_seeds_and_order },
        std::pair{ "malformed_inputs_and_identity_mismatches_reject_atomically",
            &malformed_inputs_and_identity_mismatches_reject_atomically },
        std::pair{ "dialogue_choice_identity_has_a_bounded_durable_order",
            &dialogue_choice_identity_has_a_bounded_durable_order },
        std::pair{ "script_state_round_trip_replay_checksum_and_catalog_identity_are_exact",
            &script_state_round_trip_replay_checksum_and_catalog_identity_are_exact },
        std::pair{ "replayed_command_stream_reaches_each_recorded_checksum",
            &replayed_command_stream_reaches_each_recorded_checksum },
        std::pair{ "replay_restores_inventory_combat_rng_and_the_full_canonical_checksum",
            &replay_restores_inventory_combat_rng_and_the_full_canonical_checksum },
        std::pair{ "durability_acknowledgement_precedes_installation_and_publication",
            &durability_acknowledgement_precedes_installation_and_publication },
        std::pair{
            "actor_simulation_is_durable_before_installation", &actor_simulation_is_durable_before_installation },
        std::pair{ "time_and_global_mutations_are_durable_before_installation",
            &time_and_global_mutations_are_durable_before_installation },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
