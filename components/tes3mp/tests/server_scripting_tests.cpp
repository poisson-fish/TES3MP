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

    static_assert(ServerScriptApiVersion == 6);
    static_assert(MaximumServerScriptCallbacks == 64);
    static_assert(MaximumServerScriptCommandsPerCallback == 16);
    static_assert(MaximumServerScriptCommandsPerTick == 256);
    static_assert(!std::is_constructible_v<ServerScriptCallbackInput, std::uint64_t, std::uint32_t, ServerScriptEvent>);
    static_assert(
        std::is_same_v<decltype(std::declval<const ServerScriptCallbackInput&>().event()), const ServerScriptEvent&>);
    static_assert(std::is_same_v<decltype(std::declval<const ServerScriptCallbackInput&>().readModel()),
        const ServerScriptReadModel*>);
    static_assert(std::is_same_v<decltype(std::declval<const ServerScriptReadModel&>().globals()),
        std::span<const CanonicalGlobalVariableState>>);

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

    class DialogueConsequenceCallback final : public ServerScriptCallback
    {
    public:
        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept override
        {
            const auto player = input.event().playerId();
            const auto* read = input.readModel();
            const auto faction = player && read ? read->findFaction(*player, id<FactionId>(30)) : std::nullopt;
            sawEligibleSnapshot = input.event().kind() == ServerScriptEventKind::DialogueChoiceCommitted
                && input.event().dialogueChoiceId() == id<DialogueChoiceId>(40) && faction && faction->rank
                && *faction->rank == id<FactionRank>(1) && faction->reputation == 10;
            if (!sawEligibleSnapshot || !player)
                return ServerScriptCallbackResult::Failed;
            return output.enqueue(ServerScriptSetQuestStageCommand(
                       *player, id<QuestId>(10), QuestRevision::initial(), id<QuestStage>(20)))
                        == ServerScriptEmitResult::Accepted
                    && output.enqueue(ServerScriptSetFactionRankCommand(
                           *player, id<FactionId>(30), faction->membershipRevision, id<FactionRank>(2)))
                        == ServerScriptEmitResult::Accepted
                    && output.enqueue(ServerScriptSetReputationCommand(
                           *player, id<FactionId>(30), faction->reputationRevision, 20))
                        == ServerScriptEmitResult::Accepted
                ? ServerScriptCallbackResult::Accepted
                : ServerScriptCallbackResult::Failed;
        }

        bool sawEligibleSnapshot = false;
    };

    class WeatherConsequenceCallback final : public ServerScriptCallback
    {
    public:
        explicit WeatherConsequenceCallback(bool includeGlobal) noexcept
            : mIncludeGlobal(includeGlobal)
        {
        }

        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter& output) noexcept override
        {
            const auto* read = input.readModel();
            const auto* weather = read ? read->findWeather(id<WeatherRegionId>(50)) : nullptr;
            sawImmutableWeather = input.event().kind() == ServerScriptEventKind::WeatherChanged
                && input.event().weather() && input.event().weather()->targetWeather == id<WeatherId>(2) && weather
                && weather->targetWeather == id<WeatherId>(2) && weather->revision.value() == 2;
            if (!sawImmutableWeather
                || output.enqueue(ServerScriptSetWeatherCommand(
                       id<WeatherRegionId>(50), weather->revision, id<WeatherId>(1)))
                    != ServerScriptEmitResult::Accepted)
                return ServerScriptCallbackResult::Failed;
            if (mIncludeGlobal
                && output.enqueue(ServerScriptSetGlobalCommand(
                       id<GlobalVariableId>(1), GlobalVariableRevision::initial(), std::int32_t{ 7 }))
                    != ServerScriptEmitResult::Accepted)
                return ServerScriptCallbackResult::Failed;
            return ServerScriptCallbackResult::Accepted;
        }

        bool sawImmutableWeather = false;

    private:
        bool mIncludeGlobal;
    };

    class ReadModelCallback final : public ServerScriptCallback
    {
    public:
        ServerScriptCallbackResult onEvent(
            const ServerScriptCallbackInput& input, ServerScriptCommandEmitter&) noexcept override
        {
            const auto* read = input.readModel();
            const auto* global = read ? read->findGlobal(id<GlobalVariableId>(1)) : nullptr;
            const auto quest = read ? read->findQuestStage(id<PlayerId>(1), id<QuestId>(10)) : std::nullopt;
            const auto* journal = read ? read->findJournalEntry(id<PlayerId>(1), id<JournalEntryId>(100)) : nullptr;
            sawSnapshot = global && std::get<std::int32_t>(global->value) == 9 && global->revision.value() == 2 && quest
                && quest->stage == id<QuestStage>(20) && quest->revision.value() == 2 && journal
                && journal->revision.value() == 2 && read->journalRevision(id<PlayerId>(1)).value() == 2;
            return sawSnapshot ? ServerScriptCallbackResult::Accepted : ServerScriptCallbackResult::Failed;
        }

        bool sawSnapshot = false;
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
        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& publication,
            CanonicalRevision, std::span<const DurableCommandOrder> commands, const CanonicalInventoryWorld*, const CanonicalCombatWorld*,
            const CanonicalInteractiveObjectWorld*, const CanonicalActorWorld*, const CanonicalWorldState* world,
            const CanonicalScriptState* scriptState, std::span<const std::byte>) noexcept override
        {
            sawExpected = (!expected || (scriptState && *scriptState == *expected))
                && (!expectedWorld || (world && *world == *expectedWorld));
            installedBeforeAcknowledgement = (expected && current && *current == *expected)
                || (expectedWorld && currentWorld && *currentWorld == *expectedWorld);
            if (publication && !publication->dialogueChoices().empty())
                sawDialogueOrder = commands.size() == 1
                    && commands.front().source == DurableCommandSource::DialogueChoice
                    && commands.front().fields[0] == publication->checkpointTick().value()
                    && commands.front().fields[1] == publication->dialogueChoices().front().player.value()
                    && commands.front().fields[2] == publication->dialogueChoices().front().choice.value();
            return result;
        }

        CanonicalDurabilityResult result = CanonicalDurabilityResult::Committed;
        const CanonicalScriptState* current = nullptr;
        const CanonicalScriptState* expected = nullptr;
        const CanonicalWorldState* currentWorld = nullptr;
        const CanonicalWorldState* expectedWorld = nullptr;
        bool sawExpected = false;
        bool installedBeforeAcknowledgement = false;
        bool sawDialogueOrder = false;
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
        DeterministicServerScriptRuntime configured;
        DeterministicServerScriptRuntime duplicatePackages;
        SafePointCallback callback(CellId::interior(id<CellSpaceId>(7)), 100);
        const auto valid = ServerScriptPackage::create(1, 1, 1);
        const auto conflicting = ServerScriptPackage::create(1, 2, 1);
        const auto unknown = ServerScriptPackage::create(2, 1, 2);
        const auto duplicate = ServerScriptPackage::create(1, 2, 2);
        const auto emptyCatalog = ServerScriptStateCatalog::create({});
        const std::array configuredPackages{ *valid };
        const std::array invalidPackages{ *valid, *duplicate };
        return !ServerScriptPackage::create(0, 1, 1) && !ServerScriptPackage::create(1, 0, 1)
            && !ServerScriptPackage::create(1, 1, 1, ServerScriptApiVersion + 1) && valid && conflicting && unknown
            && duplicate && emptyCatalog && !duplicatePackages.configurePackages(invalidPackages, *emptyCatalog)
            && duplicatePackages.configurePackages(configuredPackages, *emptyCatalog)
            && configured.configurePackages(configuredPackages, *emptyCatalog) && configured.packages().size() == 1
            && configured.packages()[0] == *valid
            && configured.registerCallback(*conflicting, 1, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::PackageConflict
            && configured.registerCallback(*unknown, 1, ServerScriptEventKind::CommandFinalized, callback)
            == ServerScriptRegistrationResult::UnknownPackage
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

    bool configured_runtime_requires_state_before_any_callback()
    {
        Fixture fixture;
        DeterministicServerScriptRuntime scripts;
        std::vector<std::uint64_t> trace;
        SafePointCallback callback(CellId::interior(id<CellSpaceId>(7)), 100, 1, &trace, 1);
        const auto package = ServerScriptPackage::create(1, 1, 1).value();
        const std::array packages{ package };
        const auto catalog = ServerScriptStateCatalog::create({}).value();
        if (!scripts.configurePackages(packages, catalog)
            || scripts.registerCallback(package, 1, ServerScriptEventKind::CommandFinalized, callback)
                != ServerScriptRegistrationResult::Accepted
            || fixture.intake.submit(fixture.clientCommand()) != CommandSubmissionResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        const auto first = fixture.pumpFirst();
        if (!first || first.batches().size() != 1)
            return false;
        auto prepared = reducer.prepareTick(first.batches()[0], CanonicalCommandWorlds{}, {});
        return prepared.result() && reducer.commit(std::move(prepared)) && trace.empty() && !scripts.healthy();
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

    bool immutable_world_read_model_exposes_restored_values_and_revisions()
    {
        Fixture fixture;
        const std::array globalEntries{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto globalCatalog = GlobalVariableCatalog::create(globalEntries).value();
        const std::array questEntries{ QuestCatalogEntry{
            id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(20) } } };
        const std::array journalEntries{ JournalCatalogEntry{
            id<JournalEntryId>(100), id<QuestId>(10), id<QuestStage>(20) } };
        const auto questCatalog
            = QuestJournalCatalog::create(testContentManifestId(), questEntries, journalEntries).value();
        auto world = CanonicalWorldState::initial(CanonicalWorldTimeState{}, globalCatalog, questCatalog).value();
        world = std::get<CanonicalWorldState>(setCanonicalGlobal(world, globalCatalog, id<GlobalVariableId>(1),
            GlobalVariableRevision::initial(), std::int32_t{ 9 }, id<ServerTick>(1)));
        world = std::get<CanonicalWorldState>(setCanonicalQuestStage(
            world, id<PlayerId>(1), id<QuestId>(10), QuestRevision::initial(), id<QuestStage>(20), id<ServerTick>(1)));
        world = std::get<CanonicalWorldState>(addCanonicalJournalEntry(world, id<PlayerId>(1), id<QuestId>(10),
            JournalRevision::initial(), id<JournalEntryId>(100), id<ServerTick>(1)));
        DeterministicServerScriptRuntime scripts;
        ReadModelCallback callback;
        const auto package = ServerScriptPackage::create(1, 1, 1);
        if (!package || !scripts.bindWorldState(world)
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
        return prepared.result() && reducer.commit(std::move(prepared)) && callback.sawSnapshot && scripts.healthy();
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
        ScriptDurabilityProbe durability;
        durability.currentWorld = &world;
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, nullptr, &world, nullptr))
            return false;
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
        if (!prepared.candidateWorld())
            return false;
        const auto expected = *prepared.candidateWorld();
        durability.expectedWorld = &expected;
        durability.result = CanonicalDurabilityResult::Rejected;
        if (reducer.commit(std::move(prepared), worlds) || !durability.sawExpected
            || durability.installedBeforeAcknowledgement || world != before)
            return false;
        durability.result = CanonicalDurabilityResult::Committed;
        durability.sawExpected = false;
        auto accepted = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        if (!accepted.result() || !reducer.commit(std::move(accepted), worlds) || !durability.sawExpected
            || durability.installedBeforeAcknowledgement || world == before)
            return false;
        const auto* state = world.findQuestJournal(id<PlayerId>(1));
        return state && state->quests.size() == 1 && state->quests[0].stage == id<QuestStage>(20)
            && state->quests[0].revision.value() == 2 && state->quests[0].lastChangeTick == id<ServerTick>(2)
            && state->journal.size() == 1 && state->journal[0].id == id<JournalEntryId>(100)
            && state->journalRevision.value() == 2 && state->lastJournalChangeTick == id<ServerTick>(2);
    }

    bool stale_quest_and_journal_revisions_fail_without_mutation()
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
        const auto globalCatalog = GlobalVariableCatalog::create({}).value();
        const std::array quests{ QuestCatalogEntry{
            id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10), id<QuestStage>(20) } } };
        const std::array journal{ JournalCatalogEntry{ id<JournalEntryId>(100), id<QuestId>(10), id<QuestStage>(20) } };
        const auto questCatalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        auto world = CanonicalWorldState::initial(CanonicalWorldTimeState{}, globalCatalog, questCatalog).value();
        world = std::get<CanonicalWorldState>(setCanonicalQuestStage(
            world, id<PlayerId>(1), id<QuestId>(10), QuestRevision::initial(), id<QuestStage>(10), id<ServerTick>(1)));
        world = std::get<CanonicalWorldState>(addCanonicalJournalEntry(world, id<PlayerId>(1), id<QuestId>(10),
            JournalRevision::initial(), id<JournalEntryId>(100), id<ServerTick>(1)));
        const auto before = world;
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
        auto stale = reducer.prepareTick(second.batches()[0], worlds, generated.commands());
        return stale.result() && stale.result().scriptDispositions().size() == 2
            && stale.result().scriptDispositions()[0].disposition()
            == ServerScriptCommandDisposition::QuestRevisionMismatch
            && stale.result().scriptDispositions()[1].disposition()
            == ServerScriptCommandDisposition::JournalRevisionMismatch
            && !stale.candidateWorld() && reducer.commit(std::move(stale), worlds) && world == before;
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
        const std::array packages{ package };
        if (!scripts.configurePackages(packages, catalog) || !scripts.bindPersistentState(state)
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

    bool committed_dialogue_choice_triggers_ordered_atomic_cross_domain_consequences()
    {
        Fixture fixture;
        const std::array globals{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto globalCatalog = GlobalVariableCatalog::create(globals).value();
        const std::array quests{ QuestCatalogEntry{
            id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(20) } } };
        const std::array<JournalCatalogEntry, 0> journal{};
        const auto questCatalog = QuestJournalCatalog::create(testContentManifestId(), quests, journal).value();
        const std::array factions{ FactionCatalogEntry{
            id<FactionId>(30), { id<FactionRank>(0), id<FactionRank>(1), id<FactionRank>(2) } } };
        const std::array choices{ DialogueChoiceCatalogEntry{
            id<DialogueChoiceId>(40), id<FactionId>(30), id<FactionRank>(1), 10 } };
        const auto factionCatalog
            = FactionDialogueCatalog::create(testContentManifestId(), factions, choices).value();
        auto world = CanonicalWorldState::initial(
            CanonicalWorldTimeState{}, globalCatalog, questCatalog, factionCatalog)
                         .value();
        world = std::get<CanonicalWorldState>(setCanonicalFactionRank(world, id<PlayerId>(1), id<FactionId>(30),
            FactionMembershipRevision::initial(), id<FactionRank>(1), id<ServerTick>(1)));
        world = std::get<CanonicalWorldState>(setCanonicalFactionReputation(world, id<PlayerId>(1), id<FactionId>(30),
            FactionReputationRevision::initial(), 10, id<ServerTick>(1)));

        DeterministicServerScriptRuntime scripts;
        DialogueConsequenceCallback callback;
        const auto package = ServerScriptPackage::create(1, 1, 1).value();
        if (!scripts.bindWorldState(world)
            || scripts.registerCallback(package, 1, ServerScriptEventKind::DialogueChoiceCommitted, callback)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        CanonicalCommandWorlds worlds;
        worlds.world = &world;
        worlds.globalCatalog = &globalCatalog;
        ScriptDurabilityProbe durability;
        durability.currentWorld = &world;
        const auto initialTick = fixture.pumpFirst();
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, nullptr, &world, nullptr)
            || !initialTick || initialTick.batches().size() != 1 || !scripts.pump(id<ServerTick>(1))
            || reducer.prepareDialogueChoice(id<PlayerId>(1), id<DialogueChoiceId>(99), world, id<ServerTick>(1)))
            return false;
        const auto ineligibleWorld
            = CanonicalWorldState::initial(CanonicalWorldTimeState{}, globalCatalog, questCatalog, factionCatalog)
                  .value();
        if (reducer.prepareDialogueChoice(
                id<PlayerId>(1), id<DialogueChoiceId>(40), ineligibleWorld, id<ServerTick>(1)))
            return false;
        auto dialogue
            = reducer.prepareDialogueChoice(id<PlayerId>(1), id<DialogueChoiceId>(40), world, id<ServerTick>(1));
        if (!dialogue || !reducer.commit(std::move(*dialogue)) || !callback.sawEligibleSnapshot
            || !durability.sawDialogueOrder
            || reducer.latestPublication()->dialogueChoices().size() != 1)
            return false;
        const auto tick = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!tick || tick.batches().size() != 1 || !generated || generated.commands().size() != 3
            || generated.commands()[0].order().commandOrdinal() != 1
            || generated.commands()[1].order().commandOrdinal() != 2
            || generated.commands()[2].order().commandOrdinal() != 3)
            return false;
        auto prepared = reducer.prepareTick(tick.batches()[0], worlds, generated.commands());
        if (!prepared.result() || !prepared.candidateWorld())
            return false;
        const auto unchanged = world;
        durability.result = CanonicalDurabilityResult::Rejected;
        durability.expectedWorld = &*prepared.candidateWorld();
        return !reducer.commit(std::move(prepared), worlds) && world == unchanged
            && !durability.installedBeforeAcknowledgement;
    }

    bool weather_event_reads_ordering_stale_revisions_and_atomic_consequences_are_bounded()
    {
        Fixture fixture;
        const std::array globals{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 0 } } };
        const auto globalCatalog = GlobalVariableCatalog::create(globals).value();
        const auto questCatalog = QuestJournalCatalog::create(testContentManifestId(), {}, {}).value();
        const auto factionCatalog = FactionDialogueCatalog::create(testContentManifestId(), {}, {}).value();
        const std::array weatherIds{ id<WeatherId>(1), id<WeatherId>(2) };
        const std::array regions{ WeatherRegionCatalogEntry{ id<WeatherRegionId>(50), id<WeatherId>(1), 10, 4,
            { id<WeatherId>(1), id<WeatherId>(2) } } };
        const auto weatherCatalog = WeatherCatalog::create(testContentManifestId(), weatherIds, regions).value();
        const auto random = Xoshiro256StarStar::fromWorldSeed(
            55, *RandomStreamKey::fromValues(0x5745415448455231ULL, 0));
        auto world = CanonicalWorldState::initial(CanonicalWorldTimeState{}, globalCatalog, questCatalog,
            factionCatalog, weatherCatalog, random.snapshot())
                         .value();

        DeterministicServerScriptRuntime scripts;
        WeatherConsequenceCallback first(true);
        WeatherConsequenceCallback second(false);
        const auto package = ServerScriptPackage::create(1, 1, 1).value();
        if (!scripts.bindWorldState(world)
            || scripts.registerCallback(package, 1, ServerScriptEventKind::WeatherChanged, first)
                != ServerScriptRegistrationResult::Accepted
            || scripts.registerCallback(package, 2, ServerScriptEventKind::WeatherChanged, second)
                != ServerScriptRegistrationResult::Accepted)
            return false;
        CanonicalCommandReducer reducer(fixture.initialState(), fixture.observability,
            CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), testContentManifest());
        CanonicalCommandWorlds worlds;
        worlds.world = &world;
        worlds.globalCatalog = &globalCatalog;
        ScriptDurabilityProbe durability;
        durability.currentWorld = &world;
        if (!reducer.configureDurability(durability, nullptr, nullptr, nullptr, nullptr, &world, nullptr))
            return false;

        const auto firstTick = fixture.pumpFirst();
        if (!firstTick || !scripts.pump(id<ServerTick>(1)))
            return false;
        auto changed = setCanonicalWeather(world, id<WeatherRegionId>(50), WeatherRevision::initial(),
            id<WeatherId>(2), id<ServerTick>(1));
        auto* changedWorld = std::get_if<CanonicalWorldState>(&changed);
        if (!changedWorld)
            return false;
        auto prepared = reducer.prepareTick(firstTick.batches()[0], worlds, {});
        const bool staged = reducer.stageSimulationCandidates(prepared, nullptr, std::nullopt, nullptr, std::nullopt,
            nullptr, std::nullopt, &world, *changedWorld);
        const bool committed = staged && reducer.commit(std::move(prepared), worlds);
        if (!staged || !committed || !first.sawImmutableWeather
            || !second.sawImmutableWeather || reducer.latestPublication()->weatherChanges().size() != 1)
            return false;

        const auto secondTick = fixture.pumpNext();
        const auto generated = scripts.pump(id<ServerTick>(2));
        if (!secondTick || !generated || generated.commands().size() != 3
            || generated.commands()[0].order().callbackOrder() != 1
            || generated.commands()[0].order().commandOrdinal() != 1
            || generated.commands()[1].order().commandOrdinal() != 2
            || generated.commands()[2].order().callbackOrder() != 2)
            return false;
        auto consequences = reducer.prepareTick(secondTick.batches()[0], worlds, generated.commands());
        if (!consequences.result() || consequences.result().scriptDispositions().size() != 3
            || consequences.result().scriptDispositions()[0].disposition() != ServerScriptCommandDisposition::Applied
            || consequences.result().scriptDispositions()[1].disposition() != ServerScriptCommandDisposition::Applied
            || consequences.result().scriptDispositions()[2].disposition()
                != ServerScriptCommandDisposition::WeatherRevisionMismatch
            || !consequences.candidateWorld())
            return false;
        const auto* stagedWeather = consequences.candidateWorld()->findWeather(id<WeatherRegionId>(50));
        const auto* stagedGlobal = consequences.candidateWorld()->find(id<GlobalVariableId>(1));
        if (!stagedWeather || stagedWeather->targetWeather != id<WeatherId>(1) || !stagedGlobal
            || stagedGlobal->value != GlobalVariableValue(std::int32_t{ 7 }))
            return false;
        const auto unchanged = world;
        durability.result = CanonicalDurabilityResult::Rejected;
        durability.expectedWorld = &*consequences.candidateWorld();
        const bool rejected = !reducer.commit(std::move(consequences), worlds);
        const bool unchangedWorld = world == unchanged;
        return rejected && unchangedWorld && durability.sawExpected && !durability.installedBeforeAcknowledgement;
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
        std::pair{ "configured_runtime_requires_state_before_any_callback",
            &configured_runtime_requires_state_before_any_callback },
        std::pair{ "typed_time_and_global_commands_commit_as_one_script_batch",
            &typed_time_and_global_commands_commit_as_one_script_batch },
        std::pair{ "immutable_world_read_model_exposes_restored_values_and_revisions",
            &immutable_world_read_model_exposes_restored_values_and_revisions },
        std::pair{ "quest_stage_and_journal_entry_commands_commit_as_one_durable_candidate",
            &quest_stage_and_journal_entry_commands_commit_as_one_durable_candidate },
        std::pair{ "stale_quest_and_journal_revisions_fail_without_mutation",
            &stale_quest_and_journal_revisions_fail_without_mutation },
        std::pair{ "persistent_state_is_restored_before_callbacks_and_cas_is_atomic",
            &persistent_state_is_restored_before_callbacks_and_cas_is_atomic },
        std::pair{ "stale_persistent_state_revision_fails_without_mutation",
            &stale_persistent_state_revision_fails_without_mutation },
        std::pair{
            "persistent_variable_bounds_and_types_fail_closed", &persistent_variable_bounds_and_types_fail_closed },
        std::pair{ "committed_dialogue_choice_triggers_ordered_atomic_cross_domain_consequences",
            &committed_dialogue_choice_triggers_ordered_atomic_cross_domain_consequences },
        std::pair{ "weather_event_reads_ordering_stale_revisions_and_atomic_consequences_are_bounded",
            &weather_event_reads_ordering_stale_revisions_and_atomic_consequences_are_bounded },
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
