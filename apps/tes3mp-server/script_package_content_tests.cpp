#include "canonical_persistence_file.hpp"
#include "script_module.hpp"
#include "script_package_content.hpp"

#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/server_command_reducer.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <variant>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::ServerApp;

    template <class Value>
    Value id(std::uint64_t value)
    {
        return Value::fromValue(value).value();
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path() / ("tes3mp-script-packages-" + std::to_string(suffix));
            std::filesystem::create_directories(mPath);
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

    ContentManifest manifest()
    {
        const auto manifestId
            = ContentManifestId::fromHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
        const std::array spaces{ CellSpaceDeclaration{ id<CellSpaceId>(1), CellSpaceKind::Interior } };
        const std::array cells{ CellId::interior(id<CellSpaceId>(1)) };
        const auto movement = MovementProfile::create(1, 2, 3, 4);
        return ContentManifest::create(*manifestId, spaces, cells, id<AppearanceId>(2), *movement).value();
    }

    GlobalVariableCatalog globals()
    {
        const std::array entries{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 7 } } };
        return GlobalVariableCatalog::create(entries).value();
    }

    QuestJournalCatalog quests()
    {
        const std::array questEntries{ QuestCatalogEntry{
            id<QuestId>(1), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10) } } };
        const std::array journalEntries{ JournalCatalogEntry{
            id<JournalEntryId>(1), id<QuestId>(1), id<QuestStage>(10) } };
        return QuestJournalCatalog::create(manifest().id(), questEntries, journalEntries).value();
    }

    FactionDialogueCatalog factions()
    {
        const std::array factionEntries{
            FactionCatalogEntry{ id<FactionId>(1), { id<FactionRank>(0), id<FactionRank>(1) } } };
        const std::array choices{ DialogueChoiceCatalogEntry{
            id<DialogueChoiceId>(1), id<FactionId>(1), id<FactionRank>(0), 5 } };
        return FactionDialogueCatalog::create(manifest().id(), factionEntries, choices).value();
    }

    WeatherCatalog weather()
    {
        const std::array weatherIds{ id<WeatherId>(1), id<WeatherId>(2) };
        const std::array regions{ WeatherRegionCatalogEntry{ id<WeatherRegionId>(50), id<WeatherId>(1), 10, 4,
            { id<WeatherId>(1), id<WeatherId>(2) } } };
        return WeatherCatalog::create(manifest().id(), weatherIds, regions).value();
    }

    RandomStateV1 weatherRandom()
    {
        return Xoshiro256StarStar::fromWorldSeed(
            1234, RandomStreamKey::fromValues(0x5745415448455231ULL, 0).value())
            .snapshot();
    }

    std::string packageText(std::uint32_t firstVersion = 1, std::string_view firstInitial = "7")
    {
        return "TES3MP_SCRIPT_PACKAGES_V2\n"
               "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
               "package 22 1 20 6 1 module22.t3sm "
               "962f9cb324dd2d98c5b43991dbaf737f2b740904eafe426a5183616ec547f322 joined 32\n"
               "package 11 "
            + std::to_string(firstVersion)
            + " 10 6 1 module11.t3sm "
              "ff389bbabd319cd7e79b6b3ea4136f43fb70afbc04a30755bee9aeb8ebf1f24b joined 32\n"
              "variable 22 4 string_hex 68656c6c6f\n"
              "variable 22 5 integer 0\n"
              "variable 11 2 boolean true\n"
              "variable 11 1 integer "
            + std::string(firstInitial) + "\n"
              "variable 22 3 float 1.5\n";
    }

    std::string moduleText(std::uint64_t variable, std::uint32_t api = ServerScriptApiVersion)
    {
        return "TES3MP_SCRIPT_MODULE_V1\nabi 1\napi " + std::to_string(api)
            + "\nentry joined\ncallback 0 session_joined\nincrement_integer " + std::to_string(variable) + " 1\nend\n";
    }

    std::string questModuleText()
    {
        return "TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
               "callback 0 session_joined\n"
               "global_equals 1 long 7\n"
               "quest_stage_equals 1 0\n"
               "journal_entry_absent 1\n"
               "set_quest_stage 1 10\n"
               "add_journal_entry 1 1\n"
               "end\n";
    }

    std::string dialogueModuleText()
    {
        return "TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
               "callback 0 dialogue_choice_committed\n"
               "dialogue_choice_is 1\n"
               "faction_rank_at_least 1 0\n"
               "reputation_at_least 1 5\n"
               "set_quest_stage 1 10\n"
               "set_faction_rank 1 1\n"
               "set_reputation 1 9\n"
               "end\n";
    }

    std::string weatherModuleText()
    {
        return "TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
               "callback 0 session_joined\n"
               "weather_is 50 1\n"
               "weather_target_is 50 1\n"
               "set_weather 50 2\n"
               "end\n";
    }

    std::string singlePackageText(std::string_view hash, std::uint32_t budget = 5)
    {
        return "TES3MP_SCRIPT_PACKAGES_V2\n"
               "manifest 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
               "package 1 1 0 6 1 module.t3sm "
            + std::string(hash) + " quest_start " + std::to_string(budget) + "\n";
    }

    void write(const std::filesystem::path& path, std::string_view text);

    void writeModules(const std::filesystem::path& directory)
    {
        write(directory / "module11.t3sm", moduleText(1));
        write(directory / "module22.t3sm", moduleText(5));
    }

    std::optional<ServerScriptPumpResult> joinAndPump(DeterministicServerScriptRuntime& runtime)
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        auto empty = std::get<CanonicalServerState>(createCanonicalServerState({}, {}));
        CanonicalCommandReducer reducer(
            std::move(empty), observability, CanonicalSinkBundle(nullptr, nullptr, &runtime, nullptr), manifest());
        const auto zero = Turn32::fromValue(0);
        const auto player = CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(2), id<AppearanceId>(2),
            Transform(CellId::interior(id<CellSpaceId>(1)), Position3(0, 0, 0), Orientation3(zero, zero, zero)),
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial());
        const auto session = CanonicalSessionProgress(
            id<SessionId>(3), SessionGeneration::initial(), id<PlayerId>(1), id<EntityId>(2), std::nullopt);
        auto prepared = reducer.prepareJoin(player, session, id<ServerTick>(1));
        if (!prepared || !reducer.commit(std::move(*prepared)))
            return std::nullopt;
        return runtime.pump(id<ServerTick>(2));
    }

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool deterministic_load_order_and_typed_catalog()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        writeModules(directory.path());
        write(path, packageText());
        const auto expectedManifest = manifest();
        auto loaded = loadScriptPackageContent(path, expectedManifest);
        const auto* content = std::get_if<ScriptPackageContent>(&loaded);
        if (!content || content->packages.size() != 2 || content->modules.size() != 2
            || content->stateCatalog.entries().size() != 5)
            return false;
        DeterministicServerScriptRuntime runtime;
        const auto state = CanonicalScriptState::initial(content->stateCatalog);
        if (!state || !runtime.configurePackages(content->packages, content->stateCatalog))
            return false;
        auto modules = loadExecutableScriptModules(path, *content, globals(), quests(), runtime);
        if (!std::get_if<ExecutableScriptModules>(&modules) || !runtime.bindPersistentState(*state))
            return false;
        const auto commands = joinAndPump(runtime);
        if (!commands || !*commands || commands->commands().size() != 2)
            return false;
        const auto* first = std::get_if<ServerScriptCompareAndSetPersistentCommand>(&commands->commands()[0].payload());
        const auto* second
            = std::get_if<ServerScriptCompareAndSetPersistentCommand>(&commands->commands()[1].payload());
        const auto& entries = content->stateCatalog.entries();
        return runtime.packages().size() == 2 && content->packages[0].packageId() == 11
            && content->packages[1].packageId() == 22 && entries[0].packageId == 11
            && entries[0].id == id<ScriptVariableId>(1) && std::get<std::int64_t>(entries[0].initialValue) == 7
            && entries[1].id == id<ScriptVariableId>(2) && std::get<bool>(entries[1].initialValue)
            && entries[2].packageId == 22 && entries[2].id == id<ScriptVariableId>(3)
            && std::get<double>(entries[2].initialValue) == 1.5
            && std::get<std::string>(entries[3].initialValue) == "hello" && first && second
            && first->id() == id<ScriptVariableId>(1) && second->id() == id<ScriptVariableId>(5)
            && commands->commands()[0].order().packageId() == 11 && commands->commands()[1].order().packageId() == 22;
    }

    CanonicalPersistenceIdentity identity(const ScriptPackageContent& content)
    {
        std::array<std::byte, 32> configuration{};
        configuration[0] = std::byte{ 1 };
        return CanonicalPersistenceIdentity::create(manifest().id(),
            ServerConfigurationId::fromBytes(configuration).value(), content.packages, content.stateCatalog, {})
            .value();
    }

    bool restart_restores_state_and_upgrade_or_catalog_mismatch_rejects()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path, packageText());
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        if (!content)
            return false;
        auto state = CanonicalScriptState::initial(content->stateCatalog).value();
        auto changed = compareAndSetCanonicalScriptVariable(state, content->stateCatalog, 11, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), std::int64_t{ 19 }, id<ServerTick>(1));
        auto* restoredState = std::get_if<CanonicalScriptState>(&changed);
        if (!restoredState)
            return false;
        const auto tick
            = CanonicalDurableTick::create(id<CanonicalStateVersion>(1), id<CanonicalRevision>(1), id<ServerTick>(1),
                {}, {}, CanonicalChecksum(0), nullptr, nullptr, nullptr, nullptr, nullptr, restoredState);
        const auto prefix = tick ? CanonicalDurablePrefix::create(identity(*content), { *tick }) : std::nullopt;
        if (!prefix)
            return false;
        const auto bytes = encodeCanonicalDurablePrefixV2(*prefix);
        const auto persistencePath = directory.path() / "world-v2.t3p";
        write(persistencePath, bytes);
        auto reopened = CanonicalPersistenceFile::open(persistencePath, identity(*content));
        auto* same = std::get_if<std::unique_ptr<CanonicalPersistenceFile>>(&reopened);
        if (!same || !*same || !(*same)->restoredScriptState())
            return false;
        const auto* value = (*same)->restoredScriptState()->find(11, id<ScriptVariableId>(1));
        if (!value || std::get<std::int64_t>(value->value) != 19 || value->revision.value() != 2)
            return false;

        write(path, packageText(2));
        auto upgraded = loadScriptPackageContent(path, manifest());
        const auto* upgradedContent = std::get_if<ScriptPackageContent>(&upgraded);
        if (!upgradedContent)
            return false;
        auto upgradeResult = CanonicalPersistenceFile::open(persistencePath, identity(*upgradedContent));
        write(path, packageText(1, "8"));
        auto changedCatalog = loadScriptPackageContent(path, manifest());
        const auto* changedContent = std::get_if<ScriptPackageContent>(&changedCatalog);
        if (!changedContent)
            return false;
        auto catalogResult = CanonicalPersistenceFile::open(persistencePath, identity(*changedContent));
        return std::get_if<CanonicalPersistenceFileError>(&upgradeResult)
            && std::get<CanonicalPersistenceFileError>(upgradeResult) == CanonicalPersistenceFileError::IdentityMismatch
            && std::get_if<CanonicalPersistenceFileError>(&catalogResult)
            && std::get<CanonicalPersistenceFileError>(catalogResult)
            == CanonicalPersistenceFileError::IdentityMismatch;
    }

    bool malformed_bounds_and_manifest_mismatch_fail_closed()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V2\nmanifest "
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
            "package 1 1 0 6 1 module.t3sm "
            "a51a8b25ada18922a00da8db3e1c8724fa27a063dfc0528e80ffaf1a1aa7300e joined 32\n");
        auto mismatched = loadScriptPackageContent(path, manifest());
        if (!std::get_if<ScriptPackageContentError>(&mismatched)
            || std::get<ScriptPackageContentError>(mismatched) != ScriptPackageContentError::ManifestMismatch)
            return false;
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V2\nmanifest "
            "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
            "package 1 1 0 5 1 module.t3sm "
            "a51a8b25ada18922a00da8db3e1c8724fa27a063dfc0528e80ffaf1a1aa7300e joined 32\n");
        auto wrongApi = loadScriptPackageContent(path, manifest());
        if (std::get<ScriptPackageContentError>(wrongApi) != ScriptPackageContentError::Malformed)
            return false;
        write(path,
            "TES3MP_SCRIPT_PACKAGES_V2\nmanifest "
            "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n"
            "package 1 1 0 6 1 module.t3sm "
            "a51a8b25ada18922a00da8db3e1c8724fa27a063dfc0528e80ffaf1a1aa7300e joined 32\n"
            "variable 2 1 integer 0\n");
        auto unknownPackage = loadScriptPackageContent(path, manifest());
        if (std::get<ScriptPackageContentError>(unknownPackage) != ScriptPackageContentError::InvalidCatalog)
            return false;
        std::string tooMany
            = "TES3MP_SCRIPT_PACKAGES_V2\nmanifest "
              "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\n";
        for (std::uint64_t value = 1; value <= MaximumServerScriptPackages + 1; ++value)
            tooMany += "package " + std::to_string(value) + " 1 " + std::to_string(value)
                + " 6 1 module.t3sm "
                  "a51a8b25ada18922a00da8db3e1c8724fa27a063dfc0528e80ffaf1a1aa7300e joined 32\n";
        write(path, tooMany);
        auto oversized = loadScriptPackageContent(path, manifest());
        return std::get<ScriptPackageContentError>(oversized) == ScriptPackageContentError::TooLarge;
    }

    bool missing_corrupt_and_api_mismatched_modules_fail_closed()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(path, packageText());
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        DeterministicServerScriptRuntime missingRuntime;
        if (!content || !missingRuntime.configurePackages(content->packages, content->stateCatalog))
            return false;
        auto missing = loadExecutableScriptModules(path, *content, globals(), quests(), missingRuntime);
        if (std::get<ExecutableScriptModuleError>(missing) != ExecutableScriptModuleError::Unavailable)
            return false;

        writeModules(directory.path());
        write(directory.path() / "module11.t3sm", moduleText(1) + "corrupt\n");
        DeterministicServerScriptRuntime corruptRuntime;
        if (!corruptRuntime.configurePackages(content->packages, content->stateCatalog))
            return false;
        auto corrupt = loadExecutableScriptModules(path, *content, globals(), quests(), corruptRuntime);
        if (std::get<ExecutableScriptModuleError>(corrupt) != ExecutableScriptModuleError::HashMismatch)
            return false;

        write(directory.path() / "module11.t3sm", std::string(MaximumScriptModuleBytes + 1, 'x'));
        DeterministicServerScriptRuntime oversizedRuntime;
        if (!oversizedRuntime.configurePackages(content->packages, content->stateCatalog))
            return false;
        auto oversized = loadExecutableScriptModules(path, *content, globals(), quests(), oversizedRuntime);
        if (std::get<ExecutableScriptModuleError>(oversized) != ExecutableScriptModuleError::TooLarge)
            return false;

        write(directory.path() / "module11.t3sm", moduleText(1, 5));
        auto apiText = packageText();
        apiText.replace(apiText.find("ff389bbabd319cd7e79b6b3ea4136f43fb70afbc04a30755bee9aeb8ebf1f24b"), 64,
            "8f8e49a1b09145a3ca84c70bc21838b85761db293e3afe476db513bad37627ca");
        write(path, apiText);
        auto apiLoaded = loadScriptPackageContent(path, manifest());
        auto* apiContent = std::get_if<ScriptPackageContent>(&apiLoaded);
        DeterministicServerScriptRuntime apiRuntime;
        if (!apiContent || !apiRuntime.configurePackages(apiContent->packages, apiContent->stateCatalog))
            return false;
        auto mismatch = loadExecutableScriptModules(path, *apiContent, globals(), quests(), apiRuntime);
        return std::get<ExecutableScriptModuleError>(mismatch) == ExecutableScriptModuleError::ApiMismatch;
    }

    bool restored_state_precedes_callback_and_budget_failure_is_atomic()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        writeModules(directory.path());
        write(path, packageText());
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        if (!content)
            return false;
        auto state = CanonicalScriptState::initial(content->stateCatalog).value();
        auto changed = compareAndSetCanonicalScriptVariable(state, content->stateCatalog, 11, id<ScriptVariableId>(1),
            ScriptStateRevision::initial(), std::int64_t{ 19 }, id<ServerTick>(1));
        auto* restored = std::get_if<CanonicalScriptState>(&changed);
        DeterministicServerScriptRuntime runtime;
        if (!restored || !runtime.configurePackages(content->packages, content->stateCatalog))
            return false;
        auto modules = loadExecutableScriptModules(path, *content, globals(), quests(), runtime);
        if (!std::get_if<ExecutableScriptModules>(&modules) || !runtime.bindPersistentState(*restored))
            return false;
        auto pumped = joinAndPump(runtime);
        const auto* command = pumped && *pumped && !pumped->commands().empty()
            ? std::get_if<ServerScriptCompareAndSetPersistentCommand>(&pumped->commands()[0].payload())
            : nullptr;
        if (!command || std::get<std::int64_t>(command->value()) != 20 || command->expectedRevision().value() != 2)
            return false;

        const auto budgetModule
            = "TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry joined\ncallback 0 session_joined\nconsume 2\nend\n";
        write(directory.path() / "module11.t3sm", budgetModule);
        auto budgetText = packageText();
        budgetText.replace(budgetText.find("ff389bbabd319cd7e79b6b3ea4136f43fb70afbc04a30755bee9aeb8ebf1f24b"), 64,
            "4a68b02aed3a80baef2d038f1622532a383e8592058a4765cf1f2b01daf14c91");
        const auto budgetPosition = budgetText.find(" joined 32", budgetText.find("module11.t3sm"));
        budgetText.replace(budgetPosition, 10, " joined 1");
        write(path, budgetText);
        auto budgetLoaded = loadScriptPackageContent(path, manifest());
        auto* budgetContent = std::get_if<ScriptPackageContent>(&budgetLoaded);
        DeterministicServerScriptRuntime budgetRuntime;
        auto budgetState = budgetContent ? CanonicalScriptState::initial(budgetContent->stateCatalog) : std::nullopt;
        if (!budgetContent || !budgetState
            || !budgetRuntime.configurePackages(budgetContent->packages, budgetContent->stateCatalog))
            return false;
        auto budgetModules = loadExecutableScriptModules(path, *budgetContent, globals(), quests(), budgetRuntime);
        if (!std::get_if<ExecutableScriptModules>(&budgetModules) || !budgetRuntime.bindPersistentState(*budgetState))
            return false;
        const auto budgetPump = joinAndPump(budgetRuntime);
        return (!budgetPump || !*budgetPump) && !budgetRuntime.healthy();
    }

    bool quest_predicates_emit_ordered_revision_checked_commands_and_restart_is_idempotent()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(directory.path() / "module.t3sm", questModuleText());
        write(path, singlePackageText("5ff928087ead46d78e4e32499cf5f8837ee47e5d5818520e53da6f523a7cfea5"));
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        const auto globalCatalog = globals();
        const auto questCatalog = quests();
        CanonicalWorldTimeState time;
        auto world = CanonicalWorldState::initial(time, globalCatalog, questCatalog).value();
        auto state = content ? CanonicalScriptState::initial(content->stateCatalog) : std::nullopt;
        DeterministicServerScriptRuntime runtime;
        if (!content || !state || !runtime.configurePackages(content->packages, content->stateCatalog)
            || !runtime.bindPersistentState(*state) || !runtime.bindWorldState(world))
            return false;
        auto modules = loadExecutableScriptModules(path, *content, globalCatalog, questCatalog, runtime);
        if (!std::get_if<ExecutableScriptModules>(&modules))
            return false;
        const auto generated = joinAndPump(runtime);
        if (!generated || !*generated || generated->commands().size() != 2)
            return false;
        const auto* set = std::get_if<ServerScriptSetQuestStageCommand>(&generated->commands()[0].payload());
        const auto* add = std::get_if<ServerScriptAddJournalEntryCommand>(&generated->commands()[1].payload());
        if (!set || !add || set->player() != id<PlayerId>(1) || set->quest() != id<QuestId>(1)
            || set->stage() != id<QuestStage>(10) || set->expectedRevision() != QuestRevision::initial()
            || add->player() != id<PlayerId>(1) || add->quest() != id<QuestId>(1)
            || add->entry() != id<JournalEntryId>(1) || add->expectedRevision() != JournalRevision::initial()
            || !(generated->commands()[0].order() < generated->commands()[1].order()))
            return false;

        auto advanced = setCanonicalQuestStage(
            world, id<PlayerId>(1), id<QuestId>(1), QuestRevision::initial(), id<QuestStage>(10), id<ServerTick>(1));
        auto* questAdvanced = std::get_if<CanonicalWorldState>(&advanced);
        if (!questAdvanced)
            return false;
        auto journalAdvanced = addCanonicalJournalEntry(*questAdvanced, id<PlayerId>(1), id<QuestId>(1),
            JournalRevision::initial(), id<JournalEntryId>(1), id<ServerTick>(1));
        auto* restoredWorld = std::get_if<CanonicalWorldState>(&journalAdvanced);
        if (!restoredWorld)
            return false;
        DeterministicServerScriptRuntime restarted;
        if (!restarted.configurePackages(content->packages, content->stateCatalog)
            || !restarted.bindPersistentState(*state) || !restarted.bindWorldState(*restoredWorld))
            return false;
        auto restartedModules = loadExecutableScriptModules(path, *content, globalCatalog, questCatalog, restarted);
        const auto afterRestart
            = std::get_if<ExecutableScriptModules>(&restartedModules) ? joinAndPump(restarted) : std::nullopt;
        return afterRestart && *afterRestart && afterRestart->commands().empty() && restarted.healthy();
    }

    bool malformed_world_references_and_quest_budget_exhaustion_fail_closed()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        const auto globalCatalog = globals();
        const auto questCatalog = quests();
        const auto rejects = [&](std::string_view module, std::string_view hash) {
            write(directory.path() / "module.t3sm", module);
            write(path, singlePackageText(hash));
            auto loaded = loadScriptPackageContent(path, manifest());
            auto* content = std::get_if<ScriptPackageContent>(&loaded);
            DeterministicServerScriptRuntime runtime;
            if (!content || !runtime.configurePackages(content->packages, content->stateCatalog))
                return false;
            const auto result = loadExecutableScriptModules(path, *content, globalCatalog, questCatalog, runtime);
            return std::get_if<ExecutableScriptModuleError>(&result)
                && std::get<ExecutableScriptModuleError>(result) == ExecutableScriptModuleError::InvalidWorldCatalog
                && runtime.callbackCount() == 0;
        };
        if (!rejects("TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
                     "callback 0 session_joined\nquest_stage_equals 999 0\nend\n",
                "651ba9c97c469999b9afa2779b05e107cabe0d6b684d50a4f4bce2ac92020bb8")
            || !rejects("TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
                        "callback 0 session_joined\nglobal_equals 999 long 7\nend\n",
                "37115a8d2b32b4350c11326d6b0a7352ce1d64b207cae13a483e8584eb873b9d")
            || !rejects("TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
                        "callback 0 session_joined\nglobal_equals 1 short 7\nend\n",
                "0c39087b600213e5d20b9b32735df3127aac2913d0d3fe214b1ac82550610269")
            || !rejects("TES3MP_SCRIPT_MODULE_V1\nabi 1\napi 6\nentry quest_start\n"
                        "callback 0 session_joined\nadd_journal_entry 2 1\nend\n",
                "9567a165b6f00e9542d7b928c49b54092e2318be22ba12ed46da10fb6d95034b"))
            return false;

        write(directory.path() / "module.t3sm", questModuleText());
        write(path, singlePackageText("5ff928087ead46d78e4e32499cf5f8837ee47e5d5818520e53da6f523a7cfea5", 4));
        auto loaded = loadScriptPackageContent(path, manifest());
        auto* content = std::get_if<ScriptPackageContent>(&loaded);
        auto state = content ? CanonicalScriptState::initial(content->stateCatalog) : std::nullopt;
        auto world = CanonicalWorldState::initial(CanonicalWorldTimeState{}, globalCatalog, questCatalog).value();
        DeterministicServerScriptRuntime runtime;
        if (!content || !state || !runtime.configurePackages(content->packages, content->stateCatalog)
            || !runtime.bindPersistentState(*state) || !runtime.bindWorldState(world))
            return false;
        auto modules = loadExecutableScriptModules(path, *content, globalCatalog, questCatalog, runtime);
        if (!std::get_if<ExecutableScriptModules>(&modules))
            return false;
        const auto exhausted = joinAndPump(runtime);
        return exhausted && !*exhausted && exhausted->commands().empty() && runtime.pendingCommandCount() == 0
            && !runtime.healthy();
    }

    bool dialogue_faction_predicates_commands_and_budget_are_bounded()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(directory.path() / "module.t3sm", dialogueModuleText());
        const auto run = [&](std::uint32_t budget, bool expectCommands) {
            write(path, singlePackageText(
                "06de8b8767740b0d702a9c100034c315edf479b64ac71839f99eb92b23036b00", budget));
            auto loaded = loadScriptPackageContent(path, manifest());
            auto* content = std::get_if<ScriptPackageContent>(&loaded);
            auto state = content ? CanonicalScriptState::initial(content->stateCatalog) : std::nullopt;
            auto world = CanonicalWorldState::initial(
                CanonicalWorldTimeState{}, globals(), quests(), factions())
                             .value();
            world = std::get<CanonicalWorldState>(setCanonicalFactionRank(world, id<PlayerId>(1), id<FactionId>(1),
                FactionMembershipRevision::initial(), id<FactionRank>(0), id<ServerTick>(1)));
            world = std::get<CanonicalWorldState>(setCanonicalFactionReputation(world, id<PlayerId>(1),
                id<FactionId>(1), FactionReputationRevision::initial(), 5, id<ServerTick>(1)));
            DeterministicServerScriptRuntime runtime;
            if (!content || !state || !runtime.configurePackages(content->packages, content->stateCatalog)
                || !runtime.bindPersistentState(*state) || !runtime.bindWorldState(world))
                return false;
            auto modules
                = loadExecutableScriptModules(path, *content, globals(), quests(), factions(), runtime);
            if (!std::get_if<ExecutableScriptModules>(&modules) || !runtime.pump(ServerTick::initial()))
                return false;
            const auto zero = Turn32::fromValue(0);
            const std::array players{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(2),
                id<AppearanceId>(2),
                Transform(CellId::interior(id<CellSpaceId>(1)), Position3(0, 0, 0),
                    Orientation3(zero, zero, zero)),
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
                ServerTick::initial()) };
            const std::array sessions{ CanonicalSessionProgress(id<SessionId>(3), SessionGeneration::initial(),
                id<PlayerId>(1), id<EntityId>(2), std::nullopt) };
            auto initial = std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
            NullMetricSink metrics;
            NullStructuredEventSink events;
            Observability observability(metrics, events);
            CanonicalCommandReducer reducer(std::move(initial), observability,
                CanonicalSinkBundle(nullptr, nullptr, &runtime, nullptr), manifest());
            auto dialogue
                = reducer.prepareDialogueChoice(id<PlayerId>(1), id<DialogueChoiceId>(1), world, id<ServerTick>(1));
            if (!dialogue || !reducer.commit(std::move(*dialogue)))
                return false;
            const auto pumped = runtime.pump(id<ServerTick>(2));
            if (!expectCommands)
                return !pumped && pumped.commands().empty() && !runtime.healthy();
            if (!pumped || pumped.commands().size() != 3)
                return false;
            const auto* quest = std::get_if<ServerScriptSetQuestStageCommand>(&pumped.commands()[0].payload());
            const auto* rank = std::get_if<ServerScriptSetFactionRankCommand>(&pumped.commands()[1].payload());
            const auto* reputation = std::get_if<ServerScriptSetReputationCommand>(&pumped.commands()[2].payload());
            return quest && rank && reputation && quest->stage() == id<QuestStage>(10)
                && rank->rank() == id<FactionRank>(1) && rank->expectedRevision().value() == 2
                && reputation->reputation() == 9 && reputation->expectedRevision().value() == 2
                && pumped.commands()[0].order() < pumped.commands()[1].order()
                && pumped.commands()[1].order() < pumped.commands()[2].order();
        };
        return run(6, true) && run(5, false);
    }

    bool weather_predicates_emit_revision_checked_commands_and_budget_failure_is_atomic()
    {
        TemporaryDirectory directory;
        const auto path = directory.path() / "scripts.txt";
        write(directory.path() / "module.t3sm", weatherModuleText());
        const auto weatherCatalog = weather();
        const auto run = [&](std::uint32_t budget, bool expectCommand) {
            write(path, singlePackageText(
                "bfad0ec3a74e02351ca3548b2bb4d01ece4a3cb493a0cc6c0d3eb4f80312c08b", budget));
            auto loaded = loadScriptPackageContent(path, manifest());
            auto* content = std::get_if<ScriptPackageContent>(&loaded);
            auto state = content ? CanonicalScriptState::initial(content->stateCatalog) : std::nullopt;
            auto world = CanonicalWorldState::initial(
                CanonicalWorldTimeState{}, globals(), quests(), factions(), weatherCatalog, weatherRandom());
            DeterministicServerScriptRuntime runtime;
            if (!content || !state || !world || !runtime.configurePackages(content->packages, content->stateCatalog)
                || !runtime.bindPersistentState(*state) || !runtime.bindWorldState(*world))
                return false;
            auto modules = loadExecutableScriptModules(
                path, *content, globals(), quests(), factions(), weatherCatalog, runtime);
            if (!std::get_if<ExecutableScriptModules>(&modules))
                return false;
            const auto pumped = joinAndPump(runtime);
            if (!expectCommand)
                return pumped && !*pumped && pumped->commands().empty() && runtime.pendingCommandCount() == 0
                    && !runtime.healthy();
            if (!pumped || !*pumped || pumped->commands().size() != 1 || !runtime.healthy())
                return false;
            const auto* command = std::get_if<ServerScriptSetWeatherCommand>(&pumped->commands()[0].payload());
            return command && command->region() == id<WeatherRegionId>(50)
                && command->target() == id<WeatherId>(2)
                && command->expectedRevision() == WeatherRevision::initial();
        };
        return run(3, true) && run(2, false);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "deterministic_load_order_and_typed_catalog", &deterministic_load_order_and_typed_catalog },
        std::pair{ "restart_restores_state_and_upgrade_or_catalog_mismatch_rejects",
            &restart_restores_state_and_upgrade_or_catalog_mismatch_rejects },
        std::pair{
            "malformed_bounds_and_manifest_mismatch_fail_closed", &malformed_bounds_and_manifest_mismatch_fail_closed },
        std::pair{ "missing_corrupt_and_api_mismatched_modules_fail_closed",
            &missing_corrupt_and_api_mismatched_modules_fail_closed },
        std::pair{ "restored_state_precedes_callback_and_budget_failure_is_atomic",
            &restored_state_precedes_callback_and_budget_failure_is_atomic },
        std::pair{ "quest_predicates_emit_ordered_revision_checked_commands_and_restart_is_idempotent",
            &quest_predicates_emit_ordered_revision_checked_commands_and_restart_is_idempotent },
        std::pair{ "malformed_world_references_and_quest_budget_exhaustion_fail_closed",
            &malformed_world_references_and_quest_budget_exhaustion_fail_closed },
        std::pair{ "dialogue_faction_predicates_commands_and_budget_are_bounded",
            &dialogue_faction_predicates_commands_and_budget_are_bounded },
        std::pair{ "weather_predicates_emit_revision_checked_commands_and_budget_failure_is_atomic",
            &weather_predicates_emit_revision_checked_commands_and_budget_failure_is_atomic },
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
