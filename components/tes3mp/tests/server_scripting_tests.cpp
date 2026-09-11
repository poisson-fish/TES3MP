#include <tes3mp/server_command_reducer.hpp>
#include <tes3mp/server_scripting.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/recording_observability.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    constexpr std::uint64_t FirstTickDeadline = 33'333'334;
    constexpr std::uint64_t NextTickIncrement = 33'333'333;

    static_assert(ServerScriptApiVersion == 3);
    static_assert(MaximumServerScriptCallbacks == 64);
    static_assert(MaximumServerScriptCommandsPerCallback == 16);
    static_assert(MaximumServerScriptCommandsPerTick == 256);
    static_assert(!std::is_constructible_v<ServerScriptCallbackInput, std::uint64_t, std::uint32_t, ServerScriptEvent>);
    static_assert(
        std::is_same_v<decltype(std::declval<const ServerScriptCallbackInput&>().event()), const ServerScriptEvent&>);

    template <class Value>
    Value id(std::uint64_t value)
    {
        return Value::fromValue(value).value();
    }

    Transform root(CellId cell, std::int64_t position)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(cell, Position3(position, position + 1, position + 2), Orientation3(zero, zero, zero));
    }

    struct Fixture
    {
        Fixture()
            : clock(MonotonicInstant::fromNanoseconds(0))
            , observability(metrics, events)
            , intake(clock, observability, clock.now(), id<ServerTick>(1), IngressOrdinal::initial())
        {
        }

        CanonicalServerState initialState() const
        {
            const auto cell = CellId::interior(id<CellSpaceId>(7));
            const std::array players{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(101),
                id<AppearanceId>(1), root(cell, 10), LinearVelocity3(0, 0, 0), EntityRevision::initial(),
                AuthorityEpoch::initial(), ServerTick::initial()) };
            const std::array sessions{ CanonicalSessionProgress(
                id<SessionId>(10), SessionGeneration::initial(), id<PlayerId>(1), id<EntityId>(101), std::nullopt) };
            return std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
        }

        ServerCommandProposal clientCommand() const
        {
            return ServerCommandProposal(id<SessionId>(10), SessionGeneration::initial(), CommandSequence::initial(),
                id<CommandId>(1001), CanonicalRevision::initial(),
                EntityPrecondition(id<EntityId>(101), EntityRevision::initial(), AuthorityEpoch::initial()),
                PlayerMotionCommandProposal(LinearVelocity3(0, 0, 0)));
        }

        ServerCommandPumpResult pumpFirst()
        {
            clock.advance(FirstTickDeadline);
            return intake.pump();
        }

        ServerCommandPumpResult pumpNext()
        {
            clock.advance(NextTickIncrement);
            return intake.pump();
        }

        ManualClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability;
        ServerCommandIntakeCoordinator intake;
    };

    class SafePointCallback final : public ServerScriptCallback
    {
    public:
        SafePointCallback(CellId destinationCell, std::int64_t position, std::uint32_t emissions = 1,
            std::vector<std::uint64_t>* trace = nullptr, std::uint64_t marker = 0, bool staleRevision = false) noexcept
            : mDestinationCell(destinationCell)
            , mPosition(position)
            , mEmissions(emissions)
            , mTrace(trace)
            , mMarker(marker)
            , mStaleRevision(staleRevision)
        {
        }

        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept override
        {
            if (mTrace)
                mTrace->push_back(mMarker);
            const auto& player = input.event().playerState();
            if (!player)
                return ServerScriptCallbackResult::Failed;
            for (std::uint32_t index = 0; index < mEmissions; ++index)
            {
                const EntityPrecondition precondition(player->entityId(),
                    mStaleRevision ? EntityRevision::initial() : player->entityRevision(), player->authorityEpoch());
                if (output.enqueue(ServerScriptPlayerSafePointCommand(
                        player->playerId(), precondition, root(mDestinationCell, mPosition + index)))
                    != ServerScriptEmitResult::Accepted)
                    break;
            }
            return ServerScriptCallbackResult::Accepted;
        }

    private:
        CellId mDestinationCell;
        std::int64_t mPosition;
        std::uint32_t mEmissions;
        std::vector<std::uint64_t>* mTrace;
        std::uint64_t mMarker;
        bool mStaleRevision;
    };

    class WorldMutationCallback final : public ServerScriptCallback
    {
    public:
        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput&, ServerScriptCommandEmitter& output) noexcept override
        {
            CanonicalWorldTimeState time;
            time.day = 2;
            time.month = 3;
            time.year = 428;
            time.millisecondsSinceMidnight = 12 * WorldMillisecondsPerHour;
            time.timeScaleUnits = 15 * WorldTimeScaleUnitsPerOne;
            if (output.enqueue(ServerScriptSetGlobalCommand(
                    id<GlobalVariableId>(1), GlobalVariableRevision::initial(), std::int32_t{ 9 }))
                    != ServerScriptEmitResult::Accepted
                || output.enqueue(ServerScriptSetWorldTimeCommand(WorldTimeRevision::initial(), time))
                    != ServerScriptEmitResult::Accepted)
                return ServerScriptCallbackResult::Failed;
            return ServerScriptCallbackResult::Accepted;
        }
    };

    class QuestJournalCallback final : public ServerScriptCallback
    {
    public:
        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput&, ServerScriptCommandEmitter& output) noexcept override
        {
            return output.enqueue(ServerScriptSetQuestStageCommand(
                       id<PlayerId>(1), id<QuestId>(10), QuestRevision::initial(), id<QuestStage>(20)))
                        == ServerScriptEmitResult::Accepted
                    && output.enqueue(ServerScriptAddJournalEntryCommand(
                           id<PlayerId>(1), id<QuestId>(10), JournalRevision::initial(), id<JournalEntryId>(100)))
                        == ServerScriptEmitResult::Accepted
                ? ServerScriptCallbackResult::Accepted
                : ServerScriptCallbackResult::Failed;
        }
    };

    class PersistentStateCallback final : public ServerScriptCallback
    {
    public:
        explicit PersistentStateCallback(bool stale = false) noexcept
            : mStale(stale)
        {
        }

        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept override
        {
            if (input.persistentState().size() != 1)
                return ServerScriptCallbackResult::Failed;
            const auto& state = input.persistentState().front();
            sawRestoredState = state.packageId == 1 && state.id == id<ScriptVariableId>(1)
                && std::get<std::int64_t>(state.value) == 8 && state.revision.value() == 2
                && state.lastChangeTick == id<ServerTick>(1);
            const auto expected = mStale ? ScriptStateRevision::initial() : state.revision;
            return output.enqueue(ServerScriptCompareAndSetPersistentCommand(state.id, expected, std::int64_t{ 9 }))
                    == ServerScriptEmitResult::Accepted
                ? ServerScriptCallbackResult::Accepted
                : ServerScriptCallbackResult::Failed;
        }

        bool sawRestoredState = false;

    private:
        bool mStale;
    };

    class ScriptDurabilityProbe final : public CanonicalDurabilityPort
    {
    public:
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>&, CanonicalRevision,
            std::span<const DurableCommandOrder>, const CanonicalInventoryWorld*, const CanonicalCombatWorld*,
            const CanonicalInteractiveObjectWorld*, const CanonicalActorWorld*, const CanonicalWorldState*,
            const CanonicalScriptState* scriptState) noexcept override
        {
            sawExpected = !expected || (scriptState && *scriptState == *expected);
            installedBeforeAcknowledgement = expected && current && *current == *expected;
            return result;
        }

        CanonicalDurabilityResult result = CanonicalDurabilityResult::Committed;
        const CanonicalScriptState* current = nullptr;
        const CanonicalScriptState* expected = nullptr;
        bool sawExpected = false;
        bool installedBeforeAcknowledgement = false;
    };

    struct ScenarioResult
    {
        Transform root;
        CanonicalChecksum checksum;
        std::vector<ServerScriptCommandOrder> commandOrder;
        std::vector<ServerScriptCommandDisposition> dispositions;

        friend bool operator==(const ScenarioResult&, const ScenarioResult&) noexcept = default;
    };

    std::optional<ScenarioResult> runScenario()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        const auto package = ServerScriptPackage::create(0x5155455354ULL, 1, 7);
        SafePointCallback callback(CellId::exterior(id<CellSpaceId>(8), 0, 0), 900);
        if (!package
            || scripts.registerCallback(*package, 3, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return std::nullopt;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return std::nullopt;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1)
            return std::nullopt;
        const auto noScripts = scripts.pump(first.batches()[0].scheduledTick().value());
        if (!noScripts || !noScripts.commands().empty())
            return std::nullopt;
        auto prepared = reducer.prepareTick(first.batches()[0], CanonicalCommandWorlds{}, noScripts.commands());
        if (!prepared.result() || !reducer.commit(std::move(prepared)))
            return std::nullopt;
        if (scripts.pendingCommandCount() != 1)
            return std::nullopt;

        const auto second = fixture.pumpNext();
        if (!second || second.batches().size() != 1)
            return std::nullopt;
        const auto generated = scripts.pump(second.batches()[0].scheduledTick().value());
        if (!generated || generated.commands().size() != 1
            || generated.commands()[0].order().eligibleTick() != second.batches()[0].scheduledTick().value()
            || generated.commands()[0].order().packageVersion() != 1
            || generated.commands()[0].order().apiVersion() != ServerScriptApiVersion)
            return std::nullopt;
        std::vector<ServerScriptCommandOrder> order;
        order.push_back(generated.commands()[0].order());
        auto scripted = reducer.prepareTick(second.batches()[0], CanonicalCommandWorlds{}, generated.commands());
        if (!scripted.result() || scripted.result().scriptDispositions().size() != 1)
            return std::nullopt;
        std::vector<ServerScriptCommandDisposition> dispositions;
        dispositions.push_back(scripted.result().scriptDispositions()[0].disposition());
        if (!reducer.commit(std::move(scripted)))
            return std::nullopt;
        return ScenarioResult{ reducer.state().players()[0].transform(), reducer.latestPublication()->checksum(),
            std::move(order), std::move(dispositions) };
    }

    bool script_commands_apply_only_on_the_next_tick_safe_point()
    {
        const auto result = runScenario();
        return result && result->root.cell() == CellId::exterior(id<CellSpaceId>(8), 0, 0)
            && result->root.position() == Position3(900, 901, 902)
            && result->dispositions == std::vector{ ServerScriptCommandDisposition::Applied }
        && result->commandOrder[0].eligibleTick() == id<ServerTick>(2);
    }

    bool identical_inputs_replay_the_same_command_order_and_checksum()
    {
        const auto first = runScenario();
        const auto replay = runScenario();
        return first && replay && *first == *replay;
    }

    bool callback_load_order_wins_over_registration_order()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        std::vector<std::uint64_t> trace;
        SafePointCallback later(CellId::interior(id<CellSpaceId>(7)), 300, 1, &trace, 2);
        SafePointCallback earlier(CellId::interior(id<CellSpaceId>(7)), 200, 1, &trace, 1);
        const auto packageLater = ServerScriptPackage::create(2, 1, 20);
        const auto packageEarlier = ServerScriptPackage::create(1, 1, 10);
        if (!packageLater || !packageEarlier
            || scripts.registerCallback(*packageLater, 1, ServerScriptEventKind::CommandFinalized, later)
                != ServerScriptRegistrationResult::Accepted
            || scripts.registerCallback(*packageEarlier, 1, ServerScriptEventKind::CommandFinalized, earlier)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto prepared = reducer.prepareTick(first.batches()[0], CanonicalCommandWorlds{}, {});
        if (!prepared.result() || !reducer.commit(std::move(prepared)) || trace != std::vector<std::uint64_t>{ 1, 2 })
            return false;
        const auto generated = scripts.pump(id<ServerTick>(2));
        return generated && generated.commands().size() == 2 && generated.commands()[0].order().packageId() == 1
            && generated.commands()[1].order().packageId() == 2;
    }

    bool callback_limit_failure_discards_the_entire_publication_output()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        SafePointCallback callback(CellId::interior(id<CellSpaceId>(7)), 400,
            static_cast<std::uint32_t>(MaximumServerScriptCommandsPerCallback + 1));
        const auto package = ServerScriptPackage::create(1, 1, 1);
        if (!package
            || scripts.registerCallback(*package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto prepared = reducer.prepareTick(first.batches()[0], CanonicalCommandWorlds{}, {});
        if (!prepared.result() || !reducer.commit(std::move(prepared)) || scripts.pendingCommandCount() != 0)
            return false;
        const auto failed = scripts.pump(id<ServerTick>(2));
        return !failed && failed.error() == ServerScriptPumpError::RuntimeTerminated;
    }

    bool rejectedCommand(CellId destination, bool staleRevision, ServerScriptCommandDisposition expected)
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        SafePointCallback callback(destination, 500, 1, nullptr, 0, staleRevision);
        const auto package = ServerScriptPackage::create(1, 1, 1);
        if (!package
            || scripts.registerCallback(*package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto client = reducer.prepareTick(first.batches()[0], CanonicalCommandWorlds{}, {});
        if (!client.result() || !reducer.commit(std::move(client)))
            return false;
        const auto before = reducer.state();
        const auto second = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!second || second.batches().size() != 1 || !generated || generated.commands().size() != 1)
            return false;
        auto scripted = reducer.prepareTick(second.batches()[0], CanonicalCommandWorlds{}, generated.commands());
        return scripted.result() && scripted.result().scriptDispositions().size() == 1
            && scripted.result().scriptDispositions()[0].disposition() == expected
            && scripted.candidateState() == before && reducer.commit(std::move(scripted)) && reducer.state() == before;
    }

    bool script_command_preconditions_and_manifest_fail_closed()
    {
        return rejectedCommand(
                   CellId::interior(id<CellSpaceId>(99)), false, ServerScriptCommandDisposition::UnknownCell)
            && rejectedCommand(
                CellId::interior(id<CellSpaceId>(7)), true, ServerScriptCommandDisposition::EntityRevisionMismatch);
    }

    bool package_and_registration_versions_fail_closed()
    {
        DeterministicServerScriptRuntime scripts;
        DeterministicServerScriptRuntime started;
        SafePointCallback callback(CellId::interior(id<CellSpaceId>(7)), 100);
        const auto valid = ServerScriptPackage::create(1, 1, 1);
        const auto conflicting = ServerScriptPackage::create(1, 2, 1);
        return !ServerScriptPackage::create(0, 1, 1) && !ServerScriptPackage::create(1, 0, 1)
            && !ServerScriptPackage::create(1, 1, 1, ServerScriptApiVersion + 1) && valid && conflicting
            && scripts.registerCallback(*valid, 1, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::Accepted
            && scripts.registerCallback(*conflicting, 2, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::PackageConflict
            && scripts.registerCallback(*valid, 1, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::DuplicateRegistration
            && scripts.pump(ServerTick::initial())
            && scripts.registerCallback(*valid, 2, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::Accepted
            && started.tryConsume(nullptr) == CanonicalSinkDeliveryResult::Failed
            && started.registerCallback(*valid, 1, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::RuntimeStarted;
    }

    bool typed_time_and_global_commands_commit_as_one_script_batch()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        WorldMutationCallback callback;
        const auto package = ServerScriptPackage::create(1, 1, 1);
        if (!package
            || scripts.registerCallback(*package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        const std::array declarations{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto catalog = GlobalVariableCatalog::create(declarations).value();
        CanonicalWorldTimeState time;
        auto world = CanonicalWorldState::initial(time, catalog).value();
        CanonicalCommandWorlds worlds;
        worlds.world = &world;
        worlds.globalCatalog = &catalog;
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto client = reducer.prepareTick(first.batches()[0], worlds, {});
        if (!client.result() || !reducer.commit(std::move(client), worlds))
            return false;
        const auto second = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!second || second.batches().size() != 1 || !generated || generated.commands().size() != 2)
            return false;
        auto scripted = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        if (!scripted.result() || scripted.result().scriptDispositions().size() != 2
            || std::ranges::any_of(scripted.result().scriptDispositions(), [](const auto& disposition) {
                   return disposition.disposition() != ServerScriptCommandDisposition::Applied;
               }))
            return false;
        const auto before = world;
        if (!reducer.commit(std::move(scripted), worlds) || world == before)
            return false;
        const auto* global = world.find(id<GlobalVariableId>(1));
        return global && std::get<std::int32_t>(global->value) == 9 && global->revision.value() == 2
            && global->lastChangeTick == id<ServerTick>(2) && world.time().day == 2 && world.time().month == 3
            && world.time().year == 428 && world.time().hour() == 12.0 && world.time().timeScale() == 15.0
            && world.time().revision.value() == 2 && world.time().lastChangeTick == id<ServerTick>(2);
    }

    bool quest_stage_and_journal_entry_commands_commit_as_one_durable_candidate()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        QuestJournalCallback callback;
        const auto package = ServerScriptPackage::create(1, 1, 1);
        if (!package
            || scripts.registerCallback(*package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        const std::array globals{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto globalCatalog = GlobalVariableCatalog::create(globals).value();
        const std::array quests{ QuestCatalogEntry{
            id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10), id<QuestStage>(20) } } };
        const std::array journal{ JournalCatalogEntry{ id<JournalEntryId>(100), id<QuestId>(10), id<QuestStage>(20) } };
        const auto questCatalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        CanonicalWorldTimeState time;
        auto world = CanonicalWorldState::initial(time, globalCatalog, questCatalog).value();
        CanonicalCommandWorlds worlds;
        worlds.world = &world;
        worlds.globalCatalog = &globalCatalog;
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto client = reducer.prepareTick(first.batches()[0], worlds, {});
        if (!client.result() || !reducer.commit(std::move(client), worlds))
            return false;
        const auto second = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!second || second.batches().size() != 1 || !generated || generated.commands().size() != 2)
            return false;
        auto prepared = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        if (!prepared.result() || prepared.result().scriptDispositions().size() != 2
            || std::ranges::any_of(prepared.result().scriptDispositions(),
                [](const auto& value) { return value.disposition() != ServerScriptCommandDisposition::Applied; }))
            return false;
        const auto before = world;
        if (!reducer.commit(std::move(prepared), worlds) || world == before)
            return false;
        const auto* state = world.findQuestJournal(id<PlayerId>(1));
        return state && state->quests.size() == 1 && state->quests[0].stage == id<QuestStage>(20)
            && state->quests[0].revision.value() == 2 && state->quests[0].lastChangeTick == id<ServerTick>(2)
            && state->journal.size() == 1 && state->journal[0].id == id<JournalEntryId>(100)
            && state->journalRevision.value() == 2 && state->lastJournalChangeTick == id<ServerTick>(2);
    }

    bool persistent_state_is_restored_before_callbacks_and_cas_is_atomic()
    {
        Fixture fixture;
        const auto package = ServerScriptPackage::create(1, 1, 1).value();
        const std::array declarations{ ServerScriptVariableCatalogEntry{
            1, id<ScriptVariableId>(1), std::int64_t{ 5 } } };
        const auto catalog = ServerScriptStateCatalog::create(declarations).value();
        auto initial = CanonicalScriptState::initial(catalog).value();
        auto restoredResult = compareAndSetCanonicalScriptVariable(initial, catalog, 1, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), std::int64_t{ 8 }, id<ServerTick>(1));
        auto* restored = std::get_if<CanonicalScriptState>(&restoredResult);
        if (!restored)
            return false;
        auto state = std::move(*restored);
        DeterministicServerScriptRuntime scripts;
        PersistentStateCallback callback;
        if (!scripts.bindPersistentState(state)
            || scripts.registerCallback(package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        ScriptDurabilityProbe durability;
        durability.current = &state;
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, nullptr, nullptr, &state))
            return false;
        CanonicalCommandWorlds worlds;
        worlds.scriptState = &state;
        worlds.scriptStateCatalog = &catalog;
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1 || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto client = reducer.prepareTick(first.batches()[0], worlds, {});
        if (!client.result() || !reducer.commit(std::move(client), worlds) || !callback.sawRestoredState)
            return false;
        const auto second = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!second || second.batches().size() != 1 || !generated || generated.commands().size() != 1)
            return false;
        auto prepared = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        if (!prepared.result() || prepared.result().scriptDispositions().size() != 1
            || prepared.result().scriptDispositions()[0].disposition() != ServerScriptCommandDisposition::Applied
            || !prepared.candidateScriptState())
            return false;
        const auto expected = *prepared.candidateScriptState();
        durability.expected = &expected;
        durability.result = CanonicalDurabilityResult::Rejected;
        if (reducer.commit(std::move(prepared), worlds) || !durability.sawExpected
            || durability.installedBeforeAcknowledgement || state == expected)
            return false;
        durability.result = CanonicalDurabilityResult::Committed;
        durability.sawExpected = false;
        auto accepted = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        if (!accepted.result() || !reducer.commit(std::move(accepted), worlds) || !durability.sawExpected
            || durability.installedBeforeAcknowledgement)
            return false;
        const auto* value = state.find(1, id<ScriptVariableId>(1));
        return value && std::get<std::int64_t>(value->value) == 9 && value->revision.value() == 3
            && value->lastChangeTick == id<ServerTick>(2);
    }

    bool stale_persistent_state_revision_fails_without_mutation()
    {
        Fixture fixture;
        const auto package = ServerScriptPackage::create(1, 1, 1).value();
        const std::array declarations{ ServerScriptVariableCatalogEntry{
            1, id<ScriptVariableId>(1), std::int64_t{ 5 } } };
        const auto catalog = ServerScriptStateCatalog::create(declarations).value();
        auto state = CanonicalScriptState::initial(catalog).value();
        auto advanced = compareAndSetCanonicalScriptVariable(state, catalog, 1, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), std::int64_t{ 8 }, id<ServerTick>(1));
        state = std::move(std::get<CanonicalScriptState>(advanced));
        const auto before = state;
        DeterministicServerScriptRuntime scripts;
        PersistentStateCallback callback(true);
        if (!scripts.bindPersistentState(state)
            || scripts.registerCallback(package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        CanonicalCommandWorlds worlds;
        worlds.scriptState = &state;
        worlds.scriptStateCatalog = &catalog;
        if (fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        const auto first = fixture.pumpFirst();
        if (!first || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto client = reducer.prepareTick(first.batches()[0], worlds, {});
        if (!client.result() || !reducer.commit(std::move(client), worlds))
            return false;
        const auto second = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!second || !generated)
            return false;
        auto prepared = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        return prepared.result() && prepared.result().scriptDispositions().size() == 1
            && prepared.result().scriptDispositions()[0].disposition()
            == ServerScriptCommandDisposition::PersistentVariableRevisionMismatch
            && reducer.commit(std::move(prepared), worlds) && state == before;
    }

    bool persistent_variable_bounds_and_types_fail_closed()
    {
        const std::array duplicate{ ServerScriptVariableCatalogEntry{ 1, id<ScriptVariableId>(1), std::int64_t{ 0 } },
            ServerScriptVariableCatalogEntry{ 1, id<ScriptVariableId>(1), std::int64_t{ 1 } } };
        const std::array oversized{ ServerScriptVariableCatalogEntry{
            1, id<ScriptVariableId>(1), std::string(MaximumScriptStringBytes + 1, 'x') } };
        const std::array invalidFloat{ ServerScriptVariableCatalogEntry{
            1, id<ScriptVariableId>(1), std::numeric_limits<double>::quiet_NaN() } };
        std::vector<ServerScriptVariableCatalogEntry> tooMany;
        for (std::uint64_t value = 1; value <= MaximumScriptVariablesPerPackage + 1; ++value)
            tooMany.push_back({ 1, id<ScriptVariableId>(value), std::int64_t{ 0 } });
        const std::array valid{ ServerScriptVariableCatalogEntry{ 1, id<ScriptVariableId>(1), std::int64_t{ 0 } } };
        const auto catalog = ServerScriptStateCatalog::create(valid);
        const auto state = catalog ? CanonicalScriptState::initial(*catalog) : std::nullopt;
        if (ServerScriptStateCatalog::create(duplicate) || ServerScriptStateCatalog::create(oversized)
            || ServerScriptStateCatalog::create(invalidFloat) || ServerScriptStateCatalog::create(tooMany) || !catalog
            || !state)
            return false;
        const auto wrongType = compareAndSetCanonicalScriptVariable(
            *state, *catalog, 1, id<ScriptVariableId>(1), ScriptStateRevision::initial(), true, id<ServerTick>(1));
        const std::array floatDeclaration{ ServerScriptVariableCatalogEntry{ 1, id<ScriptVariableId>(1), 0.0 } };
        const auto floatCatalog = ServerScriptStateCatalog::create(floatDeclaration).value();
        const auto floatState = CanonicalScriptState::initial(floatCatalog).value();
        const auto invalidValue
            = compareAndSetCanonicalScriptVariable(floatState, floatCatalog, 1, id<ScriptVariableId>(1),
                ScriptStateRevision::initial(), std::numeric_limits<double>::infinity(), id<ServerTick>(1));
        return std::get<CanonicalScriptStateMutationError>(wrongType) == CanonicalScriptStateMutationError::TypeMismatch
            && std::get<CanonicalScriptStateMutationError>(invalidValue)
            == CanonicalScriptStateMutationError::InvalidValue
            && *state == CanonicalScriptState::initial(*catalog).value();
    }
}

int main()
{
    const std::array tests{
        std::pair{ "script_commands_apply_only_on_the_next_tick_safe_point",
            &script_commands_apply_only_on_the_next_tick_safe_point },
        std::pair{ "identical_inputs_replay_the_same_command_order_and_checksum",
            &identical_inputs_replay_the_same_command_order_and_checksum },
        std::pair{
            "callback_load_order_wins_over_registration_order", &callback_load_order_wins_over_registration_order },
        std::pair{ "callback_limit_failure_discards_the_entire_publication_output",
            &callback_limit_failure_discards_the_entire_publication_output },
        std::pair{ "script_command_preconditions_and_manifest_fail_closed",
            &script_command_preconditions_and_manifest_fail_closed },
        std::pair{ "package_and_registration_versions_fail_closed", &package_and_registration_versions_fail_closed },
        std::pair{ "typed_time_and_global_commands_commit_as_one_script_batch",
            &typed_time_and_global_commands_commit_as_one_script_batch },
        std::pair{ "quest_stage_and_journal_entry_commands_commit_as_one_durable_candidate",
            &quest_stage_and_journal_entry_commands_commit_as_one_durable_candidate },
        std::pair{ "persistent_state_is_restored_before_callbacks_and_cas_is_atomic",
            &persistent_state_is_restored_before_callbacks_and_cas_is_atomic },
        std::pair{ "stale_persistent_state_revision_fails_without_mutation",
            &stale_persistent_state_revision_fails_without_mutation },
        std::pair{
            "persistent_variable_bounds_and_types_fail_closed", &persistent_variable_bounds_and_types_fail_closed },
    };
    bool passed = true;
    for (const auto& [name, test] : tests)
    {
        const bool result = test();
        std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
        passed = passed && result;
    }
    return passed ? 0 : 1;
}
