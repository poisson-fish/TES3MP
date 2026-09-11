#include "actor_content.hpp"
#include "canonical_persistence_file.hpp"
#include "character_content.hpp"
#include "combat_content.hpp"
#include "connection_session_coordinator.hpp"
#include "content_collision.hpp"
#include "interactive_object_content.hpp"
#include "inventory_content.hpp"
#include "melee_contact_history.hpp"
#include "phase7_proof_profile.hpp"
#include "phase7_queue_telemetry.hpp"
#include "player_identity_file.hpp"
#include "script_module.hpp"
#include "script_package_content.hpp"
#include "server_application.hpp"
#include "server_config.hpp"
#include "world_content.hpp"

#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/observability.hpp>
#include <tes3mp/server_authentication.hpp>
#include <tes3mp/transport_gns.hpp>

#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace
{
    volatile std::sig_atomic_t stopRequested = 0;
    void requestStop(int)
    {
        stopRequested = 1;
    }

    class SteadyMonotonicClock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
            return TES3MP::MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(nanoseconds));
        }
    };

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: tes3mp_server <config-file>\n";
        return 2;
    }
    std::ifstream configStream(argv[1], std::ios::binary);
    if (!configStream)
    {
        std::cerr << "configuration unavailable\n";
        return 2;
    }
    std::string text;
    text.reserve(TES3MP::ServerApp::MaximumConfigBytes + 1);
    char byte = 0;
    while (configStream.get(byte) && text.size() <= TES3MP::ServerApp::MaximumConfigBytes)
        text.push_back(byte);
    auto parsed = TES3MP::ServerApp::parseServerConfig(text);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&parsed))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto config = std::get<TES3MP::ServerApp::ServerConfig>(std::move(parsed));
    const auto configDirectory = std::filesystem::absolute(std::filesystem::path(argv[1])).parent_path();
    const auto resolve = [&](std::filesystem::path& path) {
        if (!path.empty() && path.is_relative())
            path = configDirectory / path;
    };
    resolve(config.joinPasswordFile);
    resolve(config.collisionContentFile);
    resolve(config.actorContentFile);
    resolve(config.interactiveObjectContentFile);
    resolve(config.inventoryContentFile);
    resolve(config.combatContentFile);
    resolve(config.playerIdentityFile);
    resolve(config.characterContentFile);
    resolve(config.worldContentFile);
    resolve(config.scriptPackageContentFile);
    auto loadedWorld = TES3MP::ServerApp::loadWorldContent(config.worldContentFile, config.contentManifest);
    auto* worldValue = std::get_if<TES3MP::ServerApp::WorldContent>(&loadedWorld);
    if (!worldValue)
    {
        std::cerr << "world content initialization failed\n";
        return 2;
    }
    auto worldContent = std::move(*worldValue);
    std::vector<TES3MP::ServerScriptPackage> scriptPackages;
    auto scriptStateCatalog = TES3MP::ServerScriptStateCatalog::create({});
    std::optional<TES3MP::ServerApp::ScriptPackageContent> scriptContent;
    if (!config.scriptPackageContentFile.empty())
    {
        auto loaded
            = TES3MP::ServerApp::loadScriptPackageContent(config.scriptPackageContentFile, config.contentManifest);
        auto* content = std::get_if<TES3MP::ServerApp::ScriptPackageContent>(&loaded);
        if (!content)
        {
            std::cerr << "script package content initialization failed\n";
            return 2;
        }
        scriptContent = std::move(*content);
        scriptPackages = scriptContent->packages;
        scriptStateCatalog = scriptContent->stateCatalog;
    }
    TES3MP::DeterministicServerScriptRuntime scripts;
    if (!scriptStateCatalog || !scripts.configurePackages(scriptPackages, *scriptStateCatalog))
    {
        std::cerr << "script package runtime configuration failed\n";
        return 3;
    }
    TES3MP::ServerApp::ExecutableScriptModules executableScriptModules;
    if (scriptContent)
    {
        auto loadedModules = TES3MP::ServerApp::loadExecutableScriptModules(
            config.scriptPackageContentFile, *scriptContent, worldContent.globals, worldContent.questJournal,
            worldContent.factionDialogue, worldContent.weather, scripts);
        auto* modules = std::get_if<TES3MP::ServerApp::ExecutableScriptModules>(&loadedModules);
        if (!modules)
        {
            std::cerr << "executable script module initialization failed\n";
            return 2;
        }
        executableScriptModules = std::move(*modules);
    }
    auto collisionResult
        = TES3MP::ServerApp::ContentCollisionProvider::load(config.collisionContentFile, config.contentManifest);
    auto* collisionValue = std::get_if<std::unique_ptr<TES3MP::ServerApp::ContentCollisionProvider>>(&collisionResult);
    auto collision = collisionValue ? std::move(*collisionValue) : nullptr;
    if (!collision
        || std::any_of(config.spawnPositions.begin(), config.spawnPositions.end(),
            [&](const auto& position) { return !collision->canOccupy(config.spawnCell, position); }))
    {
        std::cerr << "collision content initialization failed\n";
        return 2;
    }
    std::optional<TES3MP::CharacterContentCatalog> characterContent;
    if (!config.characterContentFile.empty())
    {
        auto loaded = TES3MP::ServerApp::loadCharacterContent(config.characterContentFile, config.contentManifest);
        auto* catalog = std::get_if<TES3MP::CharacterContentCatalog>(&loaded);
        if (!catalog)
        {
            std::cerr << "character content initialization failed: "
                      << TES3MP::ServerApp::describeCharacterContentError(
                             std::get<TES3MP::ServerApp::CharacterContentError>(loaded))
                      << '\n';
            return 2;
        }
        if (!collision->canOccupy(catalog->creationSpawn().cell(), catalog->creationSpawn().position())
            || !collision->canOccupy(catalog->completionSpawn().cell(), catalog->completionSpawn().position()))
        {
            std::cerr << "character safe-point spawn collision validation failed\n";
            return 2;
        }
        characterContent.emplace(std::move(*catalog));
    }
    auto actorContent = TES3MP::ServerApp::loadActorContent(config.actorContentFile, config.contentManifest);
    auto* actorCatalogValue = std::get_if<TES3MP::ActorCatalog>(&actorContent);
    if (!actorCatalogValue)
    {
        std::cerr << "actor content initialization failed\n";
        return 2;
    }
    auto actorCatalog = std::move(*actorCatalogValue);
    for (const auto& actor : actorCatalog.entries())
    {
        if (!collision->canOccupy(actor.initialRoot.cell(), actor.initialRoot.position()))
        {
            std::cerr << "actor content collision validation failed\n";
            return 2;
        }
        for (const auto& waypoint : actor.aiPackage.waypoints())
            if (!collision->canOccupy(actor.initialRoot.cell(), waypoint))
            {
                std::cerr << "actor waypoint collision validation failed\n";
                return 2;
            }
    }
    auto initialActorWorld = TES3MP::createInitialCanonicalActorWorld(actorCatalog);
    auto* actorWorldValue = std::get_if<TES3MP::CanonicalActorWorld>(&initialActorWorld);
    if (!actorWorldValue)
    {
        std::cerr << "actor world initialization failed\n";
        return 3;
    }
    auto actorWorld = std::move(*actorWorldValue);
    std::optional<TES3MP::InteractiveObjectCatalog> interactiveObjectCatalog;
    std::optional<TES3MP::CanonicalInteractiveObjectWorld> interactiveObjectWorld;
    if (!config.interactiveObjectContentFile.empty())
    {
        auto objectContent = TES3MP::ServerApp::loadInteractiveObjectContent(
            config.interactiveObjectContentFile, config.contentManifest);
        auto* catalog = std::get_if<TES3MP::InteractiveObjectCatalog>(&objectContent);
        if (!catalog)
        {
            std::cerr << "interactive object content initialization failed\n";
            return 2;
        }
        interactiveObjectCatalog.emplace(std::move(*catalog));
        auto initialObjects = TES3MP::createInitialCanonicalInteractiveObjectWorld(*interactiveObjectCatalog);
        auto* world = std::get_if<TES3MP::CanonicalInteractiveObjectWorld>(&initialObjects);
        if (!world)
        {
            std::cerr << "interactive object world initialization failed\n";
            return 3;
        }
        interactiveObjectWorld.emplace(std::move(*world));
    }
    std::optional<TES3MP::ItemPrototypeCatalog> itemCatalog;
    std::optional<TES3MP::CanonicalInventoryWorld> inventoryWorld;
    if (!config.inventoryContentFile.empty())
    {
        auto loaded = TES3MP::ServerApp::loadInventoryContent(config.inventoryContentFile, config.contentManifest);
        auto* content = std::get_if<TES3MP::ServerApp::InventoryContent>(&loaded);
        if (!content)
        {
            std::cerr << "inventory content initialization failed\n";
            return 2;
        }
        for (const auto& container : content->world.containers())
            if (!collision->canOccupy(container.cell, container.position))
            {
                std::cerr << "inventory container collision validation failed\n";
                return 2;
            }
        for (const auto& item : content->world.worldItems())
            if (!collision->canOccupy(item.cell, item.position))
            {
                std::cerr << "ground item collision validation failed\n";
                return 2;
            }
        itemCatalog.emplace(std::move(content->catalog));
        inventoryWorld.emplace(std::move(content->world));
    }
    std::optional<TES3MP::ServerApp::CombatContent> combatContent;
    if (!config.combatContentFile.empty())
    {
        if (!itemCatalog || !inventoryWorld)
        {
            std::cerr << "combat content requires inventory content\n";
            return 2;
        }
        auto loaded = TES3MP::ServerApp::loadCombatContent(
            config.combatContentFile, config.contentManifest, actorCatalog, *itemCatalog);
        auto* content = std::get_if<TES3MP::ServerApp::CombatContent>(&loaded);
        if (!content)
        {
            std::cerr << "combat content initialization failed: "
                      << TES3MP::ServerApp::describeCombatContentError(
                             std::get<TES3MP::ServerApp::CombatContentError>(loaded))
                      << '\n';
            return 2;
        }
        combatContent.emplace(std::move(*content));
    }
    TES3MP::MeleeAuthorityPolicy meleePolicy{};
    meleePolicy.difficulty = config.combatDifficulty;
    std::optional<TES3MP::ServerApp::MeleeContactHistory> meleeContactHistory;
    if (combatContent)
    {
        auto created = TES3MP::ServerApp::MeleeContactHistory::create(meleePolicy, *collision);
        if (!created)
        {
            std::cerr << "melee contact history initialization failed\n";
            return 3;
        }
        meleeContactHistory.emplace(std::move(*created));
    }
    auto password = TES3MP::ServerApp::loadJoinPassword(config.joinPasswordFile);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&password))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto limits = TES3MP::TransportLimits::create(1, 8, 8, 128);
    if (!limits)
    {
        std::cerr << "invalid compiled transport limits\n";
        return 3;
    }
    TES3MP::ServerApp::Phase7QueueTelemetry queueTelemetry;
    auto factory = TES3MP::makeGameNetworkingSocketsTransport(*limits, queueTelemetry);
    if (!factory)
    {
        std::cerr << "transport initialization failed\n";
        return 3;
    }
    if (!TES3MP::ServerApp::phase7ProofDisconnectGraceAccepted(config.disconnectGraceMilliseconds))
    {
        std::cerr << "disconnect grace is outside Phase 7 proof bounds\n";
        return 2;
    }

    SteadyMonotonicClock clock;
    auto crypto = TES3MP::makeProductionCredentialCrypto();
    TES3MP::CredentialDigest configurationDigest;
    const auto configurationBytes
        = std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size());
    const auto configurationId = crypto && crypto->sha256(configurationBytes, configurationDigest)
        ? TES3MP::ServerConfigurationId::fromBytes(configurationDigest.bytes)
        : std::nullopt;
    if (!configurationId)
    {
        std::cerr << "configuration identity is unavailable\n";
        return 2;
    }
    auto ratePolicy = TES3MP::AuthenticationRateLimitPolicy::create(TES3MP::ServerApp::Phase7SourceAuthenticationBurst,
        TES3MP::ServerApp::Phase7GlobalAuthenticationBurst, TES3MP::ServerApp::Phase7AuthenticationRefillMilliseconds,
        TES3MP::ServerApp::Phase7AuthenticationRefillMilliseconds);
    auto limiter = ratePolicy ? TES3MP::AuthenticationRateLimiter::create(*ratePolicy, clock.now()) : nullptr;
    auto joinProvider = crypto ? TES3MP::JoinPasswordAuthenticationProvider::create(
                                     *crypto, std::get<TES3MP::AuthenticationMaterial>(std::move(password)))
                               : nullptr;
    auto resumeStore = crypto ? TES3MP::ResumeTokenStore::create(*crypto, config.disconnectGraceMilliseconds) : nullptr;
    auto identityFileResult = TES3MP::ServerApp::PlayerIdentityFile::open(config.playerIdentityFile);
    if (const auto* identityError = std::get_if<TES3MP::ServerApp::PlayerIdentityFileError>(&identityFileResult))
    {
        if (*identityError == TES3MP::ServerApp::PlayerIdentityFileError::UnsupportedVersion)
            std::cerr << "player identity file version is unsupported; V5 is required\n";
        else
            std::cerr << "player identity file could not be loaded\n";
        return 2;
    }
    auto* identityFileValue = std::get_if<std::unique_ptr<TES3MP::ServerApp::PlayerIdentityFile>>(&identityFileResult);
    auto identityFile = identityFileValue ? std::move(*identityFileValue) : nullptr;
    if (identityFile)
        for (const auto& record : identityFile->records())
            if (record.savedPlayer
                && (!config.contentManifest.contains(record.savedPlayer->transform().cell())
                    || !collision->canOccupy(
                        record.savedPlayer->transform().cell(), record.savedPlayer->transform().position())))
            {
                std::cerr << "persisted player state validation failed\n";
                return 2;
            }
    std::vector<TES3MP::PersistenceSeed> persistenceSeeds;
    if (combatContent)
        persistenceSeeds.push_back({ 1, combatContent->world.randomState().words() });
    persistenceSeeds.push_back({ 2, worldContent.weatherRandomState.words() });
    auto scriptState = scriptStateCatalog ? TES3MP::CanonicalScriptState::initial(*scriptStateCatalog) : std::nullopt;
    auto persistenceIdentity = scriptStateCatalog
        ? TES3MP::CanonicalPersistenceIdentity::create(
              config.contentManifest.id(), *configurationId, scriptPackages, *scriptStateCatalog, persistenceSeeds)
        : std::nullopt;
    auto persistencePath = config.playerIdentityFile;
    persistencePath += ".world-v2";
    auto persistenceFileResult = persistenceIdentity
        ? TES3MP::ServerApp::CanonicalPersistenceFile::open(persistencePath, *persistenceIdentity)
        : std::variant<std::unique_ptr<TES3MP::ServerApp::CanonicalPersistenceFile>,
              TES3MP::ServerApp::CanonicalPersistenceFileError>(
              TES3MP::ServerApp::CanonicalPersistenceFileError::Malformed);
    if (const auto* persistenceError
        = std::get_if<TES3MP::ServerApp::CanonicalPersistenceFileError>(&persistenceFileResult))
    {
        if (*persistenceError == TES3MP::ServerApp::CanonicalPersistenceFileError::IdentityMismatch)
            std::cerr << "canonical persistence content, configuration, script, or seed identity mismatch\n";
        else
            std::cerr << "canonical persistence file could not be verified\n";
        return 2;
    }
    auto persistenceFile
        = std::move(std::get<std::unique_ptr<TES3MP::ServerApp::CanonicalPersistenceFile>>(persistenceFileResult));
    auto restoredState = persistenceFile->restoredState();
    if (restoredState)
    {
        for (const auto& player : restoredState->players())
        {
            if (!config.contentManifest.contains(player.transform().cell())
                || !collision->canOccupy(player.transform().cell(), player.transform().position()))
            {
                std::cerr << "persisted canonical player validation failed\n";
                return 2;
            }
        }
    }
    if (const auto* restoredInventory = persistenceFile->restoredInventory())
    {
        if (!inventoryWorld || !itemCatalog)
        {
            std::cerr << "persisted inventory has no configured domain\n";
            return 2;
        }
        auto world
            = TES3MP::CanonicalInventoryWorld::create(config.contentManifest, *itemCatalog, restoredInventory->players,
                restoredInventory->containers, restoredInventory->worldItems, restoredInventory->nextItemStackId);
        if (!world)
        {
            std::cerr << "persisted inventory validation failed\n";
            return 2;
        }
        inventoryWorld = std::move(*world);
    }
    if (const auto* restoredObjects = persistenceFile->restoredObjects())
    {
        if (!interactiveObjectCatalog || !interactiveObjectWorld)
        {
            std::cerr << "persisted interactive objects have no configured domain\n";
            return 2;
        }
        auto world
            = TES3MP::restoreCanonicalInteractiveObjectWorld(*interactiveObjectCatalog, restoredObjects->objects);
        auto* restored = std::get_if<TES3MP::CanonicalInteractiveObjectWorld>(&world);
        if (!restored)
        {
            std::cerr << "persisted interactive object catalog validation failed\n";
            return 2;
        }
        interactiveObjectWorld = std::move(*restored);
    }
    else if (persistenceFile->prefix().latest() && interactiveObjectWorld)
    {
        std::cerr << "persisted interactive object domain is missing\n";
        return 2;
    }
    if (const auto* restoredActors = persistenceFile->restoredActors())
    {
        auto world = TES3MP::restoreCanonicalActorWorld(actorCatalog, restoredActors->actors);
        auto* restored = std::get_if<TES3MP::CanonicalActorWorld>(&world);
        if (!restored || std::ranges::any_of(restored->actors(), [&](const auto& actor) {
                return !collision->canOccupy(actor.root().cell(), actor.root().position());
            }))
        {
            std::cerr << "persisted actor catalog validation failed\n";
            return 2;
        }
        actorWorld = std::move(*restored);
    }
    else if (persistenceFile->prefix().latest())
    {
        std::cerr << "persisted actor domain is missing\n";
        return 2;
    }
    if (const auto* restoredScriptState = persistenceFile->restoredScriptState())
    {
        auto restored = scriptStateCatalog
            ? TES3MP::CanonicalScriptState::restore(*scriptStateCatalog, restoredScriptState->variables())
            : std::nullopt;
        if (!restored)
        {
            std::cerr << "persisted script state catalog validation failed\n";
            return 2;
        }
        scriptState = std::move(*restored);
    }
    else if (persistenceFile->prefix().latest())
    {
        std::cerr << "persisted script state domain is missing\n";
        return 2;
    }
    if (const auto* restoredCombat = persistenceFile->restoredCombat())
    {
        const auto random = TES3MP::RandomStateV1::fromWords(restoredCombat->randomWords[0],
            restoredCombat->randomWords[1], restoredCombat->randomWords[2], restoredCombat->randomWords[3]);
        auto world = random
            ? TES3MP::createCanonicalCombatWorld(
                  restoredCombat->players, restoredCombat->actors, *random, restoredCombat->lastSimulationTick)
            : std::variant<TES3MP::CanonicalCombatWorld, TES3MP::CanonicalCombatWorldError>(
                  TES3MP::CanonicalCombatWorldError{ TES3MP::CanonicalCombatWorldErrorCode::InvalidStat });
        auto* restored = std::get_if<TES3MP::CanonicalCombatWorld>(&world);
        if (!combatContent || !restored || restored->actors().size() != combatContent->world.actors().size()
            || !std::equal(restored->actors().begin(), restored->actors().end(), combatContent->world.actors().begin(),
                [](const auto& left, const auto& right) { return left.actorId == right.actorId; }))
        {
            std::cerr << "persisted combat validation failed\n";
            return 2;
        }
        combatContent->world = std::move(*restored);
    }
    if (const auto* restoredWorld = persistenceFile->restoredWorld())
    {
        if (!restoredWorld->questJournalCatalog() || !restoredWorld->factionDialogueCatalog()
            || !restoredWorld->weatherCatalog() || !restoredWorld->weather()
            || *restoredWorld->questJournalCatalog() != worldContent.questJournal
            || *restoredWorld->factionDialogueCatalog() != worldContent.factionDialogue
            || *restoredWorld->weatherCatalog() != worldContent.weather)
        {
            std::cerr << "persisted quest/journal catalog validation failed\n";
            return 2;
        }
        auto restored = TES3MP::restoreCanonicalWorldState(worldContent.globals, worldContent.questJournal,
            worldContent.factionDialogue, worldContent.weather, restoredWorld->time(), restoredWorld->globals(),
            *restoredWorld->questJournalCatalog(), *restoredWorld->factionDialogueCatalog(),
            *restoredWorld->weatherCatalog(), restoredWorld->questJournal(), restoredWorld->factionStates(),
            *restoredWorld->weather());
        auto* world = std::get_if<TES3MP::CanonicalWorldState>(&restored);
        if (!world)
        {
            std::cerr << "persisted world catalog validation failed\n";
            return 2;
        }
        worldContent.world = std::move(*world);
    }
    else if (persistenceFile->prefix().latest())
    {
        std::cerr << "persisted time/global/quest/journal/faction/weather domain is missing\n";
        return 2;
    }
    std::vector<TES3MP::PersistedPlayerIdentity> identityRecords(
        identityFile->records().begin(), identityFile->records().end());
    if (restoredState)
        for (const auto& player : restoredState->players())
        {
            auto record = std::find_if(identityRecords.begin(), identityRecords.end(), [&](const auto& value) {
                return value.claim.player == player.playerId() && value.claim.entity == player.entityId()
                    && value.claim.appearance == player.appearanceId()
                    && value.characterProfile.lifecycle() == TES3MP::CharacterLifecycle::EstablishedCharacter;
            });
            if (record == identityRecords.end())
            {
                std::cerr << "persisted canonical player identity mismatch\n";
                return 2;
            }
            record->savedPlayer = player;
        }
    std::vector<TES3MP::EntityId> actorEntityIds;
    actorEntityIds.reserve(actorCatalog.entries().size());
    for (const auto& actor : actorCatalog.entries())
        actorEntityIds.push_back(actor.entityId);
    auto playerIdentityResult = crypto && identityFile
        ? TES3MP::PlayerIdentityRegistry::create(*crypto, *identityFile, identityRecords, actorEntityIds)
        : std::variant<std::unique_ptr<TES3MP::PlayerIdentityRegistry>, TES3MP::PlayerIdentityError>(
              TES3MP::PlayerIdentityError::InvalidInitialState);
    auto* playerIdentityValue = std::get_if<std::unique_ptr<TES3MP::PlayerIdentityRegistry>>(&playerIdentityResult);
    auto playerIdentities = playerIdentityValue ? std::move(*playerIdentityValue) : nullptr;
    if (!playerIdentities || !persistenceFile->bindPlayerIdentities(*playerIdentities))
    {
        std::cerr << "canonical persistence identity composition failed\n";
        return 3;
    }
    auto queues = TES3MP::OutboundQueueSet::create(
        TES3MP::OutboundQueuePolicy{}, TES3MP::ServerApp::Phase7ConnectionCapacity, queueTelemetry);
    const auto timeoutNanoseconds = config.disconnectGraceMilliseconds * 1'000'000;
    auto timeouts = TES3MP::SessionTimeoutPolicy::create(timeoutNanoseconds, timeoutNanoseconds, timeoutNanoseconds);
    auto versions = std::get<TES3MP::ProtocolVersionRange>(
        TES3MP::ProtocolVersionRange::create(TES3MP::ServerApp::Phase7ProtocolMajor,
            TES3MP::ServerApp::Phase7ProtocolMinimumMinor, TES3MP::ServerApp::Phase7ProtocolMaximumMinor));
    std::vector<TES3MP::CapabilityId> optionalCapabilities{ TES3MP::vrPoseCapability(),
        TES3MP::actorReplicationCapability(), TES3MP::dialogueChoiceCapability() };
    if (characterContent)
        optionalCapabilities.push_back(TES3MP::characterCreationCapability());
    if (interactiveObjectWorld)
        optionalCapabilities.push_back(TES3MP::interactiveObjectReplicationCapability());
    if (inventoryWorld)
        optionalCapabilities.push_back(TES3MP::inventoryReplicationCapability());
    if (combatContent && meleeContactHistory)
        optionalCapabilities.push_back(TES3MP::combatReplicationCapability());
    std::sort(optionalCapabilities.begin(), optionalCapabilities.end());
    optionalCapabilities.erase(
        std::unique(optionalCapabilities.begin(), optionalCapabilities.end()), optionalCapabilities.end());
    auto offer
        = TES3MP::CapabilityOffer::create(std::move(versions), optionalCapabilities, {}, config.contentManifest.id());
    std::vector<TES3MP::Transform> spawns;
    if (characterContent)
        spawns.push_back(characterContent->creationSpawn());
    else
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        spawns.reserve(config.spawnPositions.size());
        for (const auto& position : config.spawnPositions)
            spawns.emplace_back(config.spawnCell, position, TES3MP::Orientation3(zero, zero, zero));
    }
    TES3MP::NullMetricSink metrics;
    TES3MP::NullStructuredEventSink events;
    TES3MP::Observability observability(metrics, events);
    if (!scriptState || !scripts.bindPersistentState(*scriptState) || !scripts.bindWorldState(worldContent.world))
    {
        std::cerr << "script state composition failed\n";
        return 3;
    }
    auto initialState = restoredState
        ? std::move(*restoredState)
        : std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState({}, {}));
    const auto restoredVersion
        = persistenceFile->restoredStateVersion().value_or(TES3MP::CanonicalStateVersion::initial());
    const auto restoredRevision
        = persistenceFile->restoredCanonicalRevision().value_or(TES3MP::CanonicalRevision::initial());
    const auto restoredTick = persistenceFile->restoredCheckpointTick().value_or(TES3MP::ServerTick::initial());
    TES3MP::CanonicalCommandReducer reducer(std::move(initialState), restoredVersion, restoredRevision, restoredTick,
        observability, TES3MP::CanonicalSinkBundle(nullptr, nullptr, &scripts, nullptr), config.contentManifest,
        *collision);
    if (!reducer.configureDurability(*persistenceFile, inventoryWorld ? &*inventoryWorld : nullptr,
            combatContent ? &combatContent->world : nullptr,
            interactiveObjectWorld ? &*interactiveObjectWorld : nullptr, &actorWorld, &worldContent.world,
            &*scriptState))
    {
        std::cerr << "canonical persistence composition failed\n";
        return 3;
    }
    const auto nextTick = persistenceFile->restoredCheckpointTick()
        ? restoredTick.next()
        : std::optional<TES3MP::ServerTick>(TES3MP::ServerTick::initial());
    if (!nextTick)
    {
        std::cerr << "persisted checkpoint tick is exhausted\n";
        return 2;
    }
    TES3MP::ServerCommandIntakeCoordinator intake(
        clock, observability, clock.now(), *nextTick, TES3MP::IngressOrdinal::initial());
    auto joins = playerIdentities ? TES3MP::AuthenticatedJoinCoordinator::create(spawns, config.contentManifest,
                                        *TES3MP::SessionId::fromValue(1), *playerIdentities, reducer)
                                  : std::nullopt;
    auto lifecycle
        = TES3MP::ServerLifecycleCoordinator::create(config.disconnectGraceMilliseconds * 1'000'000, reducer);
    if (!crypto || !limiter || !joinProvider || !resumeStore || !identityFile || !playerIdentities || !queues
        || !timeouts || !joins || !lifecycle || !std::holds_alternative<TES3MP::CapabilityOffer>(offer))
    {
        std::cerr << "server composition failed\n";
        return 3;
    }
    TES3MP::SharedServerAuthenticationService authentication(
        *limiter, *joinProvider, *resumeStore, clock, playerIdentities.get());
    TES3MP::ServerApp::ConnectionSessionCoordinator sessions(clock, observability, *timeouts,
        std::get<TES3MP::CapabilityOffer>(std::move(offer)), authentication, *queues,
        TES3MP::ServerApp::Phase7ConnectionCapacity, &actorWorld,
        interactiveObjectWorld ? &*interactiveObjectWorld : nullptr, inventoryWorld ? &*inventoryWorld : nullptr,
        combatContent ? &combatContent->world : nullptr, combatContent ? &combatContent->playerTemplate : nullptr,
        itemCatalog ? &*itemCatalog : nullptr, characterContent ? &*characterContent : nullptr);
    TES3MP::ServerApp::ServerApplication application(*factory.runtime, config,
        { sessions, *joins, *crypto, *queues, clock, intake, reducer, *lifecycle, &actorCatalog, &actorWorld,
            collision.get(), interactiveObjectCatalog ? &*interactiveObjectCatalog : nullptr,
            interactiveObjectWorld ? &*interactiveObjectWorld : nullptr, itemCatalog ? &*itemCatalog : nullptr,
            inventoryWorld ? &*inventoryWorld : nullptr, combatContent ? &combatContent->world : nullptr,
            combatContent ? &combatContent->weapons : nullptr, combatContent ? &combatContent->playerTemplate : nullptr,
            combatContent ? &combatContent->settings : nullptr, combatContent ? &meleePolicy : nullptr,
            meleeContactHistory ? &*meleeContactHistory : nullptr,
            meleeContactHistory ? &*meleeContactHistory : nullptr, combatContent ? &combatContent->magic : nullptr,
            &scripts, &worldContent.globals, &worldContent.world, &*scriptStateCatalog, &*scriptState });
    if (!application.start())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::cout << "server started\n";
    while (stopRequested == 0)
    {
        if (!application.pump(intake.nextTick()))
        {
            std::cerr << application.failure() << '\n';
            return 3;
        }
        if (const auto evidence = queueTelemetry.takeDrainEvidence())
            std::cout << "{\"event\":\"phase7_queue_drain\",\"reliable_high_water_messages\":"
                      << evidence->reliableHighWaterMessages
                      << ",\"reliable_high_water_bytes\":" << evidence->reliableHighWaterBytes
                      << ",\"latest_high_water_messages\":" << evidence->latestHighWaterMessages
                      << ",\"latest_high_water_bytes\":" << evidence->latestHighWaterBytes
                      << ",\"final_reliable_messages\":0,\"final_reliable_bytes\":0,"
                         "\"final_latest_messages\":0,\"final_latest_bytes\":0}"
                      << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(config.tickIntervalMilliseconds));
    }
    if (!application.stop())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::cout << "server stopped\n";
    return 0;
}
