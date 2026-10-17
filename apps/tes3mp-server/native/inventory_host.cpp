#include <apps/openmw/mwworld/intervention.hpp>
#include "inventory_host.hpp"
#include "character_construction.hpp"
#include "placement_scene.hpp"
#include "inventory_service.hpp"
#include "loadout.hpp"
#include "environment.hpp"
#include "actor_campaign.hpp"
#include "actor_processing_regions.hpp"
#include "world_script_admission.hpp"
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwclass/classes.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <set>

namespace TES3MP::Native
{
    namespace
    {
        std::string readDescriptor(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            const auto size = input.tellg();
            if (size <= 0 || size > 128 * 1024) throw std::invalid_argument("Native inventory descriptor unavailable or oversized");
            std::string text(static_cast<size_t>(size), '\0');
            input.seekg(0);
            if (!input.read(text.data(), size)) throw std::invalid_argument("Native inventory descriptor read failed");
            return text;
        }
        struct Navigation
        {
            std::string record, settings;
            std::string meleeGroup, meleeAttack;
            float meleeSpeed = 1;
            std::array<float, 3> destination;
            float speed = 120;
            bool doors = false;
            bool avoidance = false;
            size_t cells = 9, steps = 2;
        };
        struct Startup
        {
            struct StartingRecord { ESM::RefId record; uint32_t count; std::optional<uint8_t> slot; };
            std::string text;
            LoadoutOptions options;
            InventoryServiceBinding binding;
            ESM::RefId cell;
            std::string plugin;
            uint32_t index;
            CellId wireCell;
            bool worldActors = false;
            bool worldItems = false;
            bool stockPlacement = false;
            std::string doorPlugin;
            uint32_t doorIndex = 0;
            ESM::RefId secondCell;
            std::optional<CellId> secondWireCell;
            std::vector<std::pair<ESM::RefId, CellId>> additionalCells;
            std::optional<Navigation> navigation;
            std::vector<StartingRecord> startingItems;
            ESM::RefId sharedScript;
            std::vector<ESM::RefId> sharedGlobals;
            struct PersonalRecord
            {
                uint64_t story;
                ESM::RefId script;
                std::vector<ESM::RefId> globals;
                std::vector<std::pair<ESM::RefId, bool>> bank;
                std::vector<ESM::RefId> dialogues;
            };
            std::vector<PersonalRecord> personalScripts;
            bool personalScheduling = false;
            bool personalBanks = false;
        };
        Startup startup(const std::filesystem::path& path, const ContentManifest& manifest,
            const PlayerIdentityRegistry& players)
        {
            auto text = readDescriptor(path);
            std::istringstream in(text);
            auto key = [&](const char* expected) {
                std::string word;
                if (!(in >> word) || word != expected) throw std::invalid_argument("Native inventory descriptor field/order invalid");
            };
            auto record = [&]() {
                std::string name;
                if (!(in >> std::quoted(name)) || name.empty() || name.size() > 256
                    || name.find_first_of("\r\n\t") != std::string::npos)
                    throw std::invalid_argument("Native inventory record ID invalid");
                return ESM::RefId::stringRefId(name);
            };
            std::string version; in >> version;
            unsigned descriptorVersion = 0;
            for (unsigned candidate = 3; candidate <= 96; ++candidate)
                if (version == "native-inventory-" + std::to_string(candidate)) descriptorVersion = candidate;
            if (version == "native-inventory-56c") descriptorVersion = 56;
            if (!descriptorVersion) throw std::invalid_argument("Native inventory descriptor version incompatible");
            const bool meleeCampaign = descriptorVersion >= 21;
            const bool neighborhood = meleeCampaign || version == "native-inventory-20";
            const bool traveler = neighborhood || version == "native-inventory-19";
            const bool movingActor = traveler || version == "native-inventory-16" || version == "native-inventory-17" || version == "native-inventory-18";
            const bool streaming = movingActor || version == "native-inventory-14" || version == "native-inventory-15";
            const bool exteriorCells = streaming || version == "native-inventory-13";
            const bool twoCells = exteriorCells || version == "native-inventory-10" || version == "native-inventory-11" || version == "native-inventory-12";
            const bool door = version == "native-inventory-9" || twoCells;
            const bool baseInventory = version == "native-inventory-5" || version == "native-inventory-6" || version == "native-inventory-7" || version == "native-inventory-8" || door;
            const bool wholeInterior = version != "native-inventory-3";
            key("manifest");
            std::string identity; in >> identity;
            if (ContentManifestId::fromHex(identity) != manifest.id())
                throw std::invalid_argument("Native inventory descriptor is not bound to the authenticated manifest");
            key("config"); std::string config; in >> std::quoted(config);
            if (config.empty() || config.size() > 1024) throw std::invalid_argument("Native loadout configuration path invalid");
            auto configPath = std::filesystem::u8path(config);
            if (configPath.is_relative()) configPath = path.parent_path() / configPath;
            const auto configArgument = configPath.string();
            const char* arguments[]{"tes3mp_server", "--config", configArgument.c_str()};
            auto options = readLoadoutOptions(3, arguments);
            key("players"); uint64_t a = 0, b = 0; in >> a >> b;
            const auto first = PlayerId::fromValue(a), second = PlayerId::fromValue(b);
            if (!first || !second || first == second)
                throw std::invalid_argument("Native inventory requires two distinct stable player IDs");
            std::string registration;
            for (auto player : {*first, *second})
            {
                const auto* profile = players.characterProfile(player);
                if (!profile || (descriptorVersion < 78 && profile->lifecycle() != CharacterLifecycle::EstablishedCharacter))
                    throw std::invalid_argument("Native actor binding requires an already registered established character");
                const auto registered = std::ranges::find_if(players.records(),
                    [player](const auto& value) { return value.claim.player == player; });
                if (registered == players.records().end() || registered->claim.contentManifest != manifest.id())
                    throw std::invalid_argument("Native player binding belongs to another content manifest");
                registration += "\nregistered-binding " + std::to_string(player.value()) + " "
                    + std::to_string(registered->claim.entity.value()) + " " + std::to_string(registered->claim.appearance.value());
            }
            key("actors"); const auto actorA = record(); int countA = 0, countB = 0;
            if (!baseInventory) in >> countA;
            const auto actorB = record();
            ESM::RefId shirt;
            std::optional<ItemPrototypeId> itemId;
            if (!baseInventory)
            {
                in >> countB;
                if (countA <= 0 || countB <= 0 || countA > MaximumTransferCount || countB > MaximumTransferCount)
                    throw std::invalid_argument("Native starting shirt counts out of range");
                key("shirt"); shirt = record();
                itemId = ItemPrototypeId::fromValue(MWWorld::inventoryRecordId(shirt));
            }
            key("loot"); int lootLevel = 0; uint64_t lootSeed = 0; in >> lootLevel >> lootSeed;
            const auto readCell = [&]() {
                std::string kind; in >> kind;
                if (kind == "interior")
                {
                    std::string name; in >> std::quoted(name);
                    return interiorCell(name);
                }
                if (exteriorCells && kind == "exterior")
                {
                    int64_t x = 0, y = 0; in >> x >> y;
                    if (!in || x < -32768 || x > 32767 || y < -32768 || y > 32767)
                        throw std::invalid_argument("Native exterior coordinates outside supported bounds");
                    return ESM::RefId::esm3ExteriorCell(int32_t(x), int32_t(y));
                }
                throw std::invalid_argument("Native cell selection invalid for descriptor version");
            };
            const auto matches = [](ESM::RefId engine, CellId wire) {
                if (const auto* ext = engine.getIf<ESM::ESM3ExteriorCellRefId>())
                    return wire.asExterior() && wire.asExterior()->gridX() == ext->getX()
                        && wire.asExterior()->gridY() == ext->getY();
                return wire.kind() == CellId::Kind::Interior;
            };
            ESM::RefId cell;
            std::string plugin; uint64_t index = 0;
            if (wholeInterior) cell = readCell();
            else
            {
                std::string name;
                key("container"); in >> std::quoted(name) >> std::quoted(plugin) >> index;
                cell = interiorCell(name);
            }
            std::string doorPlugin;
            uint64_t doorIndex = 0;
            if (streaming)
            {
                key("doors"); std::string mode; in >> mode;
                if (mode != "auto") throw std::invalid_argument("Native streaming doors require automatic discovery");
            }
            else if (door)
            {
                key("door"); in >> std::quoted(doorPlugin) >> doorIndex;
                if (!in || doorPlugin.empty() || doorPlugin.size() > 256
                    || doorPlugin.find_first_of("\r\n\t") != std::string::npos
                    || doorPlugin.find('\0') != std::string::npos || doorIndex > UINT32_MAX)
                    throw std::invalid_argument("Native door selection invalid");
            }
            key("cell"); std::string cellText; in >> cellText;
            const auto cells = parseContentCells(cellText);
            ESM::RefId secondCell;
            std::string secondCellText;
            std::optional<CellId> secondWireCell;
            std::vector<std::pair<ESM::RefId, CellId>> additionalCells;
            std::optional<Navigation> navigation;
            uint64_t respawnDelayTicks = 27'000;
            size_t areaCount = 2;
            if (streaming)
            {
                key("areas"); in >> areaCount;
                if (!in || areaCount < 1 || areaCount > MaxEquipmentCells)
                    throw std::invalid_argument("Native area count outside bounds");
            }
            if (twoCells && (!streaming || areaCount > 1))
            {
                secondCell = readCell();
                key("cell"); in >> secondCellText;
                const auto second = parseContentCells(secondCellText);
                if (!in || !second || second->size() != 1 || !manifest.contains(second->front())
                    || !matches(secondCell, second->front())
                    || (cells && cells->size() == 1 && cells->front() == second->front()))
                    throw std::invalid_argument("Second native cell mapping invalid");
                secondWireCell = second->front();
                for (size_t i = 2; i < areaCount; ++i)
                {
                    const auto name = readCell();
                    key("cell"); std::string areaText; in >> areaText;
                    const auto area = parseContentCells(areaText);
                    if (!in || !area || area->size() != 1 || !manifest.contains(area->front())
                        || !matches(name, area->front()))
                        throw std::invalid_argument("Native area mapping invalid");
                    additionalCells.emplace_back(name, area->front());
                    secondCellText += "\n" + areaText;
                }
            }
            if (movingActor)
            {
                Navigation nav;
                nav.doors = traveler || version == "native-inventory-17" || version == "native-inventory-18";
                nav.avoidance = traveler || version == "native-inventory-18";
                if (descriptorVersion >= 77)
                {
                    key("actors"); key("auto"); in >> std::quoted(nav.settings);
                    nav.destination = {};
                    nav.meleeGroup = "handtohand"; nav.meleeAttack = "chop";
                }
                else
                {
                    key("npc"); in >> std::quoted(nav.record) >> std::quoted(nav.settings);
                    key("destination"); in >> nav.destination[0] >> nav.destination[1] >> nav.destination[2] >> nav.speed;
                    if (neighborhood) { key("processing"); in >> nav.cells >> nav.steps; }
                    if (meleeCampaign)
                    {
                        key("melee"); in >> std::quoted(nav.meleeGroup) >> std::quoted(nav.meleeAttack) >> nav.meleeSpeed;
                        if (!in || nav.meleeGroup.empty() || nav.meleeGroup.size() > 64
                            || !std::isfinite(nav.meleeSpeed) || nav.meleeSpeed <= 0 || nav.meleeSpeed > 100)
                            throw std::invalid_argument("Native melee descriptor invalid");
                    }
                }
                if (descriptorVersion >= 25)
                {
                    key("respawn"); in >> respawnDelayTicks;
                    if (!in || !respawnDelayTicks || respawnDelayTicks > 30ull * 60 * 60 * 24)
                        throw std::invalid_argument("Native NPC respawn deadline invalid");
                }
                if (!in || (descriptorVersion < 77 && nav.record.empty()) || nav.record.size()>256 || nav.settings.empty() || nav.settings.size()>1024
                    || !std::isfinite(nav.speed) || nav.speed<=0 || nav.speed>4096
                    || (neighborhood ? ((descriptorVersion < 77 && areaCount > 9) || nav.cells > 9 || nav.steps > 2)
                        : (cell.is<ESM::ESM3ExteriorCellRefId>() || areaCount != 1)))
                    throw std::invalid_argument("Native navigating NPC descriptor invalid");
                for (float value : nav.destination) if (!std::isfinite(value) || std::abs(value)>1e7f)
                    throw std::invalid_argument("Native navigation destination invalid");
                auto settings = std::filesystem::u8path(nav.settings);
                if (settings.is_relative()) settings = path.parent_path()/settings;
                nav.settings = settings.string();
                navigation = std::move(nav);
            }
            bool levitationEnabled = true;
            bool haveLevitation = false, haveStartingItems = false;
            std::vector<Startup::StartingRecord> startingItems;
            ESM::RefId sharedScript;
            std::vector<ESM::RefId> sharedGlobals;
            std::vector<Startup::PersonalRecord> personalScripts;
            while (in >> std::ws, in && !in.eof())
            {
                std::string field; in >> field;
                if (field == "levitation" && descriptorVersion >= 56 && !haveLevitation)
                {
                    int enabled = -1; in >> enabled;
                    if (!in || (enabled != 0 && enabled != 1))
                        throw std::invalid_argument("Native levitation rule invalid");
                    levitationEnabled = enabled != 0; haveLevitation = true;
                }
                else if (field == "starting_items" && descriptorVersion >= 78 && !haveStartingItems)
                {
                    size_t count = 0; in >> count;
                    if (!in || count > MaximumStartingItems)
                        throw std::invalid_argument("Native starting item count outside bound");
                    haveStartingItems = true;
                    std::set<ESM::RefId> records;
                    std::set<int> slots;
                    for (size_t i = 0; i < count; ++i)
                    {
                        auto name = record();
                        uint64_t quantity = 0; int slot = -2; in >> quantity >> slot;
                        if (!in || !quantity || quantity > MaximumTransferCount || slot < -1 || slot >= 19
                            || !records.insert(name).second || (slot >= 0 && !slots.insert(slot).second))
                            throw std::invalid_argument("Native starting item quantity, record or slot invalid");
                        startingItems.push_back({name, uint32_t(quantity), slot < 0 ? std::nullopt : std::optional{uint8_t(slot)}});
                    }
                }
                else if (field == "shared_script" && descriptorVersion >= 80 && sharedScript.empty())
                {
                    sharedScript = record(); key("globals"); size_t count = 0; in >> count;
                    if (!in || count > MWScript::WorldScript::MaximumGlobals)
                        throw std::invalid_argument("Shared script global ownership exceeds bound");
                    std::set<ESM::RefId> owned;
                    for (size_t i = 0; i < count; ++i)
                    {
                        auto global = record();
                        if (!owned.insert(global).second)
                            throw std::invalid_argument("Shared script global ownership repeated");
                        sharedGlobals.push_back(global);
                    }
                }
                else if (field == "personal_scripts" && descriptorVersion >= 81 && personalScripts.empty())
                {
                    size_t count = 0; in >> count;
                    if (!in || !count || count > PersonalScripts::MaximumInstances / 2)
                        throw std::invalid_argument("Personal story budget exceeded");
                    std::set<uint64_t> stories;
                    size_t instances = 0;
                    for (size_t i = 0; i < count; ++i)
                    {
                        key("story"); uint64_t story = 0; in >> story;
                        if (!in || !story || !stories.insert(story).second)
                            throw std::invalid_argument("Personal story identity invalid or repeated");
                        Startup::PersonalRecord entry{story, {}, {}};
                        if (descriptorVersion >= 83)
                        {
                            key("scripts"); size_t scripts = 0; in >> scripts;
                            if (!in || !scripts || scripts > PersonalScripts::MaximumScriptsPerBank
                                || (instances += scripts * 2) > PersonalScripts::MaximumInstances)
                                throw std::invalid_argument("Personal script bank budget exceeded");
                            std::set<ESM::RefId> names;
                            for (size_t j = 0; j < scripts; ++j)
                            {
                                key("script"); auto script = record(); key("running"); int running = -1; in >> running;
                                if (!in || (running != 0 && running != 1) || !names.insert(script).second)
                                    throw std::invalid_argument("Personal bank script or running flag invalid");
                                entry.bank.emplace_back(script, running != 0);
                            }
                            entry.script = entry.bank.front().first;
                        }
                        else { key("script"); entry.script = record(); }
                        key("globals"); size_t globals = 0; in >> globals;
                        if (!in || globals > MWScript::WorldScript::MaximumGlobals)
                            throw std::invalid_argument("Personal global budget exceeded");
                        std::set<ESM::RefId> owned;
                        for (size_t j = 0; j < globals; ++j)
                        {
                            auto global = record();
                            if (!owned.insert(global).second) throw std::invalid_argument("Repeated personal global ownership");
                            entry.globals.push_back(global);
                        }
                        if (descriptorVersion >= 84)
                        {
                            key("dialogues"); size_t count = 0; in >> count;
                            if (!in || !count || count > MWScript::DialogueState::MaximumRecords)
                                throw std::invalid_argument("Personal dialogue record budget exceeded");
                            std::set<ESM::RefId> owned;
                            for (size_t j = 0; j < count; ++j)
                            {
                                auto id = record();
                                if (!owned.insert(id).second) throw std::invalid_argument("Repeated personal dialogue ownership");
                                entry.dialogues.push_back(id);
                            }
                        }
                        personalScripts.push_back(std::move(entry));
                    }
                }
                else throw std::invalid_argument("Native optional descriptor field invalid or repeated");
            }
            if (!in || !(in >> std::ws).eof() || (!baseInventory && !itemId)
                || (descriptorVersion >= 80 && sharedScript.empty())
                || (descriptorVersion >= 81 && personalScripts.empty())
                || lootLevel < 1 || lootLevel > 1000 || lootSeed > UINT32_MAX
                || (!wholeInterior && plugin.empty()) || plugin.size() > 256 || index > UINT32_MAX
                || !cells || cells->size() != 1 || !manifest.contains(cells->front())
                || !matches(cell, cells->front()))
                throw std::invalid_argument("Native inventory placed selection or cell mapping invalid");
            InventoryServiceBinding binding{{*first, *second}, itemId,
                {{{actorA, shirt, countA, false, baseInventory}, {actorB, shirt, countB, false, baseInventory}}}, {}, {}};
            binding.mStreamExteriors = streaming;
            binding.mRetainTraveler = traveler;
            binding.mTravelerNeighborhood = neighborhood && cells->front().asExterior() != nullptr;
            binding.mCharacterCreation = descriptorVersion >= 78;
            binding.mPlayerPhysics = descriptorVersion >= 79;
            if (binding.mCharacterCreation)
                for (auto& actor : binding.mActors) actor.mEmptyInventory = true;
            binding.mCellActors = descriptorVersion >= 77;
            if (binding.mCellActors) binding.mRetainTraveler = binding.mTravelerNeighborhood = false;
            binding.mMeleeContact = version == "native-inventory-22" || version == "native-inventory-23" || version == "native-inventory-24" || version == "native-inventory-25" || version == "native-inventory-26" || version == "native-inventory-27";
            binding.mCombatState = version == "native-inventory-23" || version == "native-inventory-24" || version == "native-inventory-25" || version == "native-inventory-26" || version == "native-inventory-27";
            binding.mCombatResolution = version == "native-inventory-24" || version == "native-inventory-25" || version == "native-inventory-26" || version == "native-inventory-27";
            binding.mNpcLifecycle = version == "native-inventory-25" || version == "native-inventory-26" || version == "native-inventory-27";
            binding.mMagicUse = version == "native-inventory-26" || version == "native-inventory-27";
            binding.mMagicProjectile = version == "native-inventory-27";
            if (descriptorVersion >= 28)
            {
                binding.mMeleeContact = binding.mCombatState = binding.mCombatResolution = true;
                binding.mNpcLifecycle = binding.mMagicUse = binding.mMagicProjectile = binding.mMagicItemUse = true;
                binding.mMagicTimed = descriptorVersion >= 29;
                binding.mMagicArea = descriptorVersion >= 30;
                binding.mMagicPlayerTarget = descriptorVersion >= 31;
                binding.mMagicProjectileCollection = descriptorVersion >= 32;
                binding.mKnockoutRules = descriptorVersion >= 33;
                binding.mKnockoutAnimation = descriptorVersion >= 50;
                binding.mExpandedEffects = descriptorVersion >= 51;
                binding.mActorPresentation = descriptorVersion >= 52;
                binding.mPlayerCastLifecycle = descriptorVersion >= 53;
                binding.mPersistentConditions = descriptorVersion >= 54;
                binding.mSpecialConditions = descriptorVersion >= 55;
                binding.mMovementEffects = descriptorVersion >= 56;
                binding.mLevitationEnabled = levitationEnabled;
                binding.mScriptedMovementRules = descriptorVersion >= 65;
                binding.mPlayerTravel = descriptorVersion >= 67;
                binding.mScriptedTravelRules = descriptorVersion >= 68;
                binding.mObjectTravelFamily = descriptorVersion >= 69;
                binding.mEquipmentFamily = descriptorVersion >= 70;
                binding.mSummons = descriptorVersion >= 71;
                binding.mPlayerLifecycle = descriptorVersion >= 72;
                binding.mCommittedDeath = descriptorVersion >= 73;
                binding.mPowerSources = descriptorVersion >= 74;
                binding.mPassiveSelf = descriptorVersion >= 75;
                binding.mGenericTargets = descriptorVersion >= 76;
                binding.mAiDecisions = descriptorVersion >= 57;
                binding.mPlayerAi = descriptorVersion >= 58;
                binding.mSocialLifecycle = descriptorVersion >= 59;
                binding.mNeighborAi = descriptorVersion >= 60;
                binding.mPlacementCombat = descriptorVersion >= 61;
                binding.mNeighborCombat = descriptorVersion >= 62;
                binding.mAuthoritativeAim = descriptorVersion >= 64;
                binding.mContainerMagic = version == "native-inventory-56c" || binding.mObjectTravelFamily;
                binding.mNeighborLimit = descriptorVersion >= 77 ? 36 : descriptorVersion >= 66 ? 4
                    : descriptorVersion >= 63 ? 3 : descriptorVersion >= 62 ? 2 : 1;
                binding.mMeleeDefenseRules = descriptorVersion >= 34;
                binding.mActorEffectLifecycle = descriptorVersion >= 35;
                binding.mConstantEffects = descriptorVersion >= 36;
                binding.mGeneralConstants = descriptorVersion >= 37;
                binding.mDurableCasters = descriptorVersion >= 38;
                binding.mAutomaticNpcSpells = descriptorVersion >= 39;
                binding.mNpcWeaponCompetition = descriptorVersion >= 40;
                binding.mNpcFullSelection = descriptorVersion >= 41;
                binding.mNpcCastLifecycle = descriptorVersion >= 42;
                if (descriptorVersion >= 43) binding.mBoundHits.emplace(descriptorVersion >= 66 ? 7
                    : descriptorVersion >= 63 ? 6
                    : descriptorVersion >= 62 ? 5
                    : descriptorVersion >= 61 ? 4 : 3);
            }
            binding.mNpcRespawnDelayTicks = respawnDelayTicks;
            if (navigation) { binding.mTravelerCellBudget = navigation->cells; binding.mTravelerStepBudget = navigation->steps; }
            if (movingActor || version == "native-inventory-15") binding.mActorSelections.emplace();
            binding.mLootLevel = lootLevel;
            binding.mLootSeed = uint32_t(lootSeed);
            if (version == "native-inventory-11" || version == "native-inventory-12" || exteriorCells
                || binding.mPlayerTravel) binding.mTeleportDoors.emplace();
            // Hash semantic bindings, never local configuration paths or
            // descriptor whitespace, so moving the same loadout preserves saves.
            std::ostringstream semantic;
            semantic << version << '\n' << identity << registration << '\n'
                << actorA << ':' << countA << '\n' << actorB << ':' << countB << '\n'
                << shirt << ':' << (itemId ? itemId->value() : 0) << '\n' << cellText << '\n' << lootLevel << ':' << lootSeed << '\n';
            if (streaming) semantic << cell.serializeText() << ":" << areaCount << '\n';
            if (twoCells) semantic << secondCellText << '\n';
            if (navigation) semantic << std::setprecision(9) << navigation->record << ':' << navigation->speed << ':'
                << navigation->destination[0] << ':' << navigation->destination[1] << ':' << navigation->destination[2] << '\n';
            if (meleeCampaign) semantic << navigation->meleeGroup << ':' << navigation->meleeAttack << ':'
                << navigation->meleeSpeed << '\n';
            if (binding.mNpcLifecycle) semantic << "respawn:" << respawnDelayTicks << '\n';
            for (const auto& item : startingItems)
                semantic << "starting:" << item.record << ':' << item.count << ':'
                    << (item.slot ? int(*item.slot) : -1) << '\n';
            if (!sharedScript.empty())
            {
                semantic << "shared-script:" << sharedScript << '\n';
                for (const auto& global : sharedGlobals) semantic << "shared-global:" << global << '\n';
            }
            for (const auto& entry : personalScripts)
            {
                semantic << "personal-story:" << entry.story << ':' << entry.script << '\n';
                for (const auto& [script, running] : entry.bank)
                    semantic << "personal-member:" << script << ':' << running << '\n';
                for (const auto& global : entry.globals) semantic << "personal-global:" << global << '\n';
            }
            return {semantic.str(), std::move(options), std::move(binding), cell, plugin, uint32_t(index), cells->front(),
                version == "native-inventory-6" || version == "native-inventory-7" || version == "native-inventory-8" || door,
                version == "native-inventory-7" || version == "native-inventory-8" || door,
                version == "native-inventory-8" || door, std::move(doorPlugin), uint32_t(doorIndex), std::move(secondCell), secondWireCell, std::move(additionalCells), std::move(navigation), std::move(startingItems), sharedScript, std::move(sharedGlobals), std::move(personalScripts), descriptorVersion >= 82, descriptorVersion >= 83};
        }
    }
    struct InventoryHost::Impl
    {
        Loadout loadout;
        std::vector<std::unique_ptr<PlacementScene>> scenes;
        std::unique_ptr<Environment> environment;
        InventoryService inventory;
        static InventoryServiceBinding bind(Startup& start, Loadout& loadout, CredentialCrypto& crypto,
            std::vector<std::unique_ptr<PlacementScene>>& scenes, std::span<const std::byte> restored)
        {
            if (!start.sharedScript.empty())
            {
                try
                {
                    start.binding.mSharedScript = admitSharedWorldScript(loadout, start.sharedScript, start.sharedGlobals, crypto);
                }
                catch (const std::invalid_argument&)
                {
                    const ESM::NPC* scriptedNpc = nullptr;
                    for (const auto& candidate : loadout.store().get<ESM::NPC>())
                        if (candidate.mScript == start.sharedScript) { scriptedNpc = &candidate; break; }
                    if (!scriptedNpc) throw;
                    const auto* primary = loadout.store().get<ESM::Script>().search(start.sharedScript);
                    if (!primary) throw;
                    std::vector<ESM::Script> sharedBank{*primary};
                    for (const auto* companionId : {"TR_m0_TelvanniHortDeathSc", "TR_m1_HT_UnseatingDralSc"})
                        if (const auto* companion = loadout.store().get<ESM::Script>().search(ESM::RefId::stringRefId(companionId)))
                            if (companion->mId != primary->mId
                                && primary->mScriptText.find(companionId) != std::string::npos)
                                sharedBank.push_back(*companion);
                    start.binding.mSharedScript = admitSharedWorldScript(
                        loadout, start.sharedScript, start.sharedGlobals, crypto, sharedBank, scriptedNpc->mId, true);
                }
            }
            if (!start.startingItems.empty())
            {
                MWClass::registerClasses();
                const auto items = MWWorld::inventoryRecords(loadout.store());
                for (const auto& item : start.startingItems)
                {
                    const auto prototype = MWWorld::inventoryRecordId(item.record);
                    if (!items.contains(prototype) || items.at(prototype) != item.record)
                        throw std::invalid_argument("Native starting item missing from winning loadout");
                    MWWorld::ManualRef reference(loadout.store(), item.record);
                    const auto ptr = reference.getPtr();
                    if (!ptr.getClass().getScript(ptr).empty())
                        throw std::invalid_argument("Native starting item requires unavailable script services");
                    if (item.slot)
                    {
                        const auto slots = ptr.getClass().getEquipmentSlots(ptr).first;
                        if (std::ranges::find(slots, int(*item.slot)) == slots.end())
                            throw std::invalid_argument("Native starting equipment slot incompatible with record");
                    }
                    start.binding.mStartingInventory.push_back({ItemPrototypeId::fromValue(prototype).value(), item.count, item.slot});
                }
            }
            std::optional<std::vector<ActorSpawnSelection>> savedSpawns;
            if (start.binding.mActorSelections && !restored.empty())
            {
                if (restored.size() > MaximumNativeInventoryImageBytes)
                    throw std::invalid_argument("Native spawn image exceeds bound");
                auto bytes = std::span(reinterpret_cast<const char*>(restored.data()), restored.size());
                if (start.navigation) bytes = readActorCampaign(bytes).inventory;
                size_t offset = 0;
                if (getAreaWord(bytes, offset) != SpawnAreaMagic)
                    throw std::invalid_argument("Leveled actor recovery requires a V15 campaign image");
                savedSpawns = readActorSpawns(bytes, offset);
            }
            Misc::Rng::Generator spawnRng{start.binding.mLootSeed};
            std::vector<ESM::RefId> names{start.cell};
            std::vector<CellId> wireCells{start.wireCell};
            if (start.secondWireCell) { names.push_back(start.secondCell); wireCells.push_back(*start.secondWireCell); }
            for (const auto& [name, wire] : start.additionalCells) { names.push_back(name); wireCells.push_back(wire); }
            std::set<ESM::RefId> uniqueNames;
            std::set<CellId> uniqueWire;
            for (size_t i = 0; i < names.size(); ++i)
                if (!uniqueNames.insert(names[i]).second || !uniqueWire.insert(wireCells[i]).second)
                    throw std::invalid_argument("Duplicate native area mapping");
            if (start.binding.mObjectTravelFamily)
            {
                auto model = std::make_shared<MWWorld::WorldModel>(loadout.store(), loadout.readers(), 1);
                start.binding.mIntervention = [model, names, wireCells](const Transform& player, ESM::RefId effect)
                    -> std::optional<Transform> {
                    const auto origin = std::ranges::find(wireCells, player.cell());
                    if (origin == wireCells.end()) throw std::invalid_argument("Intervention player outside bound cells");
                    const auto& p = player.position();
                    const auto marker = MWWorld::closestInterventionMarker(*model,
                        names[size_t(origin - wireCells.begin())],
                        {float(double(p.x()) / 1024), float(double(p.y()) / 1024), float(double(p.z()) / 1024)},
                        ESM::RefId::stringRefId(effect == ESM::MagicEffect::DivineIntervention ? "divinemarker" : "templemarker"), 256);
                    if (marker.isEmpty()) return {};
                    const auto destination = std::ranges::find(names, marker.getCell()->getCell()->getId());
                    if (destination == names.end()) throw std::invalid_argument("Intervention destination outside bound cells");
                    const auto& position = marker.getRefData().getPosition();
                    for (size_t axis = 0; axis < 3; ++axis)
                        if (!std::isfinite(position.pos[axis]) || std::abs(position.pos[axis]) > 1e7f
                            || !std::isfinite(position.rot[axis]))
                            throw std::invalid_argument("Intervention marker transform outside bounds");
                    const auto angle = [](float radians) {
                        double turns = -double(radians) / (2 * std::numbers::pi); turns -= std::floor(turns);
                        return Turn32::fromValue(uint32_t(uint64_t(std::llround(turns * 4294967296.0))));
                    };
                    return Transform(wireCells[size_t(destination - names.begin())],
                        Position3(std::llround(double(position.pos[0]) * 1024), std::llround(double(position.pos[1]) * 1024),
                            std::llround(double(position.pos[2]) * 1024)),
                        Orientation3(angle(position.rot[0]), angle(position.rot[1]), angle(position.rot[2])));
                };
            }
            scenes.resize(names.size());
            start.binding.mAdditionalWorldItems.reserve(start.additionalCells.size());
            if (start.secondWireCell && start.cell == start.secondCell)
                throw std::invalid_argument("Native cell mappings resolve to the same cell");
            std::vector<std::vector<ESM::CellRef>> domains(names.size());
            std::vector<std::string> fingerprints(names.size());
            std::vector<size_t> actorCounts(names.size());
            std::ostringstream placement;
            for (size_t cellIndex = 0; cellIndex < names.size(); ++cellIndex)
            {
                const auto& cell = names[cellIndex];
                const auto wireCell = wireCells[cellIndex];
                InventoryServiceBinding::WorldItems* world = nullptr;
                auto references = start.plugin.empty()
                    ? (start.worldActors ? loadout.placedContainers(cell) : loadout.resolveContainers(cell, MaxEquipmentContainers))
                    : std::vector{loadout.resolveContainer(cell, start.plugin, start.index)};
                if (start.navigation)
                {
                    // This descriptor deliberately binds one living NPC in a
                    // frozen interior. Other inventory/door/script services are
                    // outside its domain, just as in the collision probe.
                    references = loadout.placedActors(cell);
                    if (start.binding.mSocialLifecycle && (cellIndex == 0 || start.binding.mCellActors))
                    {
                        for (const auto& placed : references)
                        {
                            if (placed.mScripted || placed.mLeveled
                                || !loadout.store().get<ESM::NPC>().search(placed.mRef.mRefID)) continue;
                            if (start.binding.mCrimeWitnesses.size() >= 128)
                                throw std::invalid_argument("Native crime witness capacity exceeded");
                            const auto& p = placed.mRef.mPos.pos;
                            start.binding.mCrimeWitnesses.push_back({placed.mIdentity, wireCell,
                                Position3(std::llround(double(p[0]) * 1024),
                                    std::llround(double(p[1]) * 1024),
                                    std::llround(double(p[2]) * 1024)), placed.mRef.mRefID, placed.mRef.mPos.rot[2]});
                            placement << "\nwitness:" << placed.mIdentity << ':' << placed.mRef.mRefID
                                << ':' << p[0] << ':' << p[1] << ':' << p[2] << ':' << placed.mRef.mPos.rot[2];
                        }
                    }
                    const auto admittedScriptedNpc = [&](const Loadout::PlacedInventory& ref) {
                        if (!ref.mScripted || ref.mLeveled) return false;
                        const auto* npc = loadout.store().get<ESM::NPC>().search(ref.mRef.mRefID);
                        if (!npc || npc->mScript.empty() || !loadout.store().get<ESM::Script>().search(npc->mScript))
                            return false;
                        return !start.personalScripts.empty() || start.sharedScript == npc->mScript;
                    };
                    if (start.binding.mCellActors)
                    {
                        for (const auto& ref : references)
                            if ((ref.mScripted && !admittedScriptedNpc(ref)) || ref.mLeveled)
                                throw std::invalid_argument("Cell actor requires unsupported script/leveled services");
                        if (references.size() + std::accumulate(actorCounts.begin(), actorCounts.end(), size_t{}) > 37)
                            throw std::invalid_argument("Cell actor campaign capacity exceeded");
                    }
                    else
                    {
                        if (start.binding.mNeighborAi)
                            std::ranges::sort(references, {}, &Loadout::PlacedInventory::mIdentity);
                        const auto selectedBase = ESM::RefId::stringRefId(start.navigation->record);
                        const auto eligibleNpc = [&](const auto& ref) {
                            return ref.mRef.mRefID != selectedBase && (!ref.mScripted || admittedScriptedNpc(ref)) && !ref.mLeveled
                                && loadout.store().get<ESM::NPC>().search(ref.mRef.mRefID);
                        };
                        const auto eligibleCreature = [&](const auto& ref) {
                            if (ref.mRef.mRefID == selectedBase || ref.mScripted || ref.mLeveled) return false;
                            const auto* creature = loadout.store().get<ESM::Creature>().search(ref.mRef.mRefID);
                            return creature && (creature->mFlags & ESM::Creature::Bipedal)
                                && !(creature->mFlags & ESM::Creature::Flies);
                        };
                        const auto firstNeighbor = start.binding.mNeighborAi && cellIndex == 0
                            ? std::ranges::find_if(references, eligibleNpc) : references.end();
                        std::vector<uint64_t> neighborIds;
                        if (firstNeighbor != references.end()) neighborIds.push_back(firstNeighbor->mIdentity);
                        if (start.binding.mNeighborCombat)
                            for (auto next = firstNeighbor == references.end() ? references.end() : firstNeighbor + 1;
                                next != references.end() && neighborIds.size() < start.binding.mNeighborLimit; ++next)
                                if (eligibleNpc(*next))
                                    neighborIds.push_back(next->mIdentity);
                        if (start.binding.mNeighborLimit >= 4 && start.binding.mNeighborCombat && cellIndex == 0)
                        {
                            const auto creature = std::ranges::find_if(references, eligibleCreature);
                            if (creature != references.end())
                            {
                                if (neighborIds.size() == start.binding.mNeighborLimit) neighborIds.pop_back();
                                neighborIds.push_back(creature->mIdentity);
                                std::ranges::sort(neighborIds);
                            }
                        }
                        std::erase_if(references, [&](const auto& ref) {
                            return ref.mRef.mRefID != selectedBase
                                && std::ranges::find(neighborIds, ref.mIdentity) == neighborIds.end();
                        });
                        if (cellIndex != 0) references.clear();
                        if (cellIndex == 0)
                        {
                            const auto selected = std::ranges::find_if(references, [&](const auto& ref) {
                                return ref.mRef.mRefID == selectedBase;
                            });
                            if (references.size() != 1u + neighborIds.size()
                                || (start.binding.mNeighborAi && neighborIds.empty())
                                || std::ranges::count(references, selectedBase, [](const auto& ref) {
                                    return ref.mRef.mRefID;
                                }) != 1 || selected == references.end()
                                || (selected->mScripted && !admittedScriptedNpc(*selected)) || selected->mLeveled)
                                throw std::invalid_argument("Native navigating actor must be one unscripted placement");
                        }
                    }
                    actorCounts[cellIndex]=references.size();
                    if (start.binding.mContainerMagic && (cellIndex == 0 || start.binding.mObjectTravelFamily))
                    {
                        auto placed = loadout.placedContainers(cell);
                        if (!start.binding.mObjectTravelFamily && placed.size() != 1)
                            throw std::invalid_argument("V56c requires one ordinary placed container");
                        for (const auto& container : placed)
                        {
                            if (container.mScripted || !container.mRef.mTrap.empty() || !container.mRef.mKey.empty())
                                throw std::invalid_argument("Object magic requires an ordinary untrapped unkeyed container");
                            if (references.size() + start.binding.mContainers.size() >= MaxEquipmentContainers)
                                throw std::invalid_argument("Object magic inventory owner budget exhausted");
                            references.push_back(container);
                        }
                    }
                }
                else if (start.worldActors)
                {
                    const auto actors = start.binding.mActorSelections
                        ? loadout.resolveActors(cell, 128, start.binding.mLootLevel, spawnRng,
                            *start.binding.mActorSelections, savedSpawns ? &*savedSpawns : nullptr)
                        : loadout.resolveActors(cell, MaxEquipmentContainers);
                    actorCounts[cellIndex] = actors.size();
                    references.insert(references.end(), actors.begin(), actors.end());
                    std::ranges::sort(references, {}, &Loadout::PlacedInventory::mIdentity);
                    if ((!start.worldItems && references.empty()) || references.size() + start.binding.mContainers.size() > (start.binding.mStreamExteriors ? MaxEquipmentContainers : 32))
                        throw std::invalid_argument("Native interior shared inventory count is empty or exceeds the startup budget");
                    if (start.binding.mStreamExteriors && references.size() > 128)
                        throw std::invalid_argument("Native cell inventory baseline exceeds 128 owners in " + cell.toDebugString());
                    for (const auto& ref : references)
                    {
                        const auto* npc = loadout.store().get<ESM::NPC>().search(ref.mRef.mRefID);
                        const bool admittedNpcScript = ref.mScripted && !ref.mLeveled && npc && !npc->mScript.empty()
                            && loadout.store().get<ESM::Script>().search(npc->mScript)
                            && (!start.personalScripts.empty() || start.sharedScript == npc->mScript);
                        if ((ref.mScripted && !admittedNpcScript) || !ref.mRef.mTrap.empty()
                            || (ref.mRef.mIsLocked && (!start.binding.mContainerMagic
                                || !loadout.store().get<ESM::Container>().search(ref.mRef.mRefID))))
                            throw std::invalid_argument("Native placed inventory " + std::to_string(ref.mIdentity)
                                + " (" + ref.mRef.mRefID.toDebugString() + ") in " + cell.toDebugString()
                                + " requires services: script=" + std::to_string(ref.mScripted)
                                + " locked=" + std::to_string(ref.mRef.mIsLocked) + " trap=" + ref.mRef.mTrap.toDebugString());
                    }
                }
                for (const auto& placed : references)
                {
                    const auto& p = placed.mRef.mPos.pos;
                    const Position3 position(std::llround(double(p[0]) * 1024),
                        std::llround(double(p[1]) * 1024), std::llround(double(p[2]) * 1024));
                    start.binding.mContainers.push_back({ContainerId::fromValue(placed.mIdentity).value(),
                        wireCell, position, placed.mRef.mRefID, placed.mRef});
                    if (start.binding.mContainers.size() > 1) placement << '\n';
                    placement << std::quoted(cell.serializeText())
                        << ':' << placed.mIdentity << ':' << placed.mRef.mRefID << ':'
                        << position.x() << ':' << position.y() << ':' << position.z();
                }
                if (start.worldItems)
                {
                    if (cellIndex < 2)
                    {
                        auto& slot = cellIndex ? start.binding.mSecondWorldItems : start.binding.mWorldItems;
                        world = &slot.emplace(InventoryServiceBinding::WorldItems{wireCell, {}});
                    }
                    else world = &start.binding.mAdditionalWorldItems.emplace_back(InventoryServiceBinding::WorldItems{wireCell, {}});
                    const auto* resolvedCell = loadout.store().get<ESM::Cell>().find(cell);
                    if (resolvedCell->isExterior() || resolvedCell->hasWater())
                    {
                        if (!std::isfinite(resolvedCell->mWater)) throw std::invalid_argument("Native cell water invalid");
                        world->mWaterLevel = resolvedCell->mWater;
                    }
                    world->mSunExposed = resolvedCell->isExterior()
                        || (resolvedCell->mData.mFlags & ESM::Cell::QuasiEx);
                    world->mSunRegion = resolvedCell->mRegion;
                    if (start.binding.mSpecialConditions && world->mSunExposed && world->mSunRegion.empty())
                        throw std::invalid_argument("Native exposed cell lacks a weather region");
                    if (start.binding.mActorSelections && !start.navigation)
                        for (const auto& marker : loadout.placedActors(cell))
                            if (marker.mLeveled)
                            {
                                const auto selected = std::ranges::find(*start.binding.mActorSelections,
                                    marker.mIdentity, &ActorSpawnSelection::mPlacement);
                                world->mActorSpawns.push_back({marker.mIdentity, selected->mRecord});
                                if (!selected->mRecord) ++actorCounts[cellIndex];
                            }
                    if (!start.navigation || start.binding.mCellActors) for (const auto& item : loadout.placedItems(cell, start.binding.mStreamExteriors ? MaximumGroundItemBaselineChunkItems : 64))
                    {
                        world->mPlacements.emplace_back(item.mIdentity, item.mRef);
                        placement << "\nworld-item:" << item.mIdentity << ':' << item.mRef.mRefID;
                    }
                }
                start.binding.mEngineCells.emplace(wireCell, cell);
                if (start.stockPlacement && (!start.navigation || start.binding.mCellActors))
                {
                    for (const auto& [id, ref] : world->mPlacements) domains[cellIndex].push_back(ref);
                    scenes[cellIndex] = std::make_unique<PlacementScene>(loadout, cell, domains[cellIndex]);
                    fingerprints[cellIndex] = scenes[cellIndex]->fingerprint();
                    placement << fingerprints[cellIndex];
                    if (start.binding.mStreamExteriors) scenes[cellIndex].reset();
                    world->mPlacement = [&scenes, cellIndex](const auto& actor, const auto& item,
                        const auto& view, auto world) {
                        if (!scenes[cellIndex]) throw std::invalid_argument("Native cell is not active");
                        return scenes[cellIndex]->resolve(actor, item, view, world);
                    };
                }
                if (start.binding.mStreamExteriors && (!start.navigation || start.navigation->doors))
                    for (const auto& door : loadout.ordinaryDoors(cell, 128))
                    {
                        if (start.binding.mDoors.size() == 4096)
                            throw std::invalid_argument("Native ordinary door campaign budget exceeded");
                        start.binding.mDoors.push_back({door.mIdentity, wireCell, door.mRef});
                        placement << "\nordinary:" << cellIndex << ":" << door.mIdentity << ":" << door.mRef.mRefID;
                    }
                if (!cellIndex && !start.doorPlugin.empty())
                {
                    const auto door = loadout.resolveDoor(start.cell, start.doorPlugin, start.doorIndex);
                    start.binding.mDoor = door.mRef;
                    start.binding.mDoorId = door.mIdentity;
                    placement << "\ndoor:" << std::quoted(start.cell.serializeText())
                        << ':' << door.mIdentity << ':' << door.mRef.mRefID;
                }
                // Bind even an empty second cell's engine identity.
                if (start.secondWireCell)
                    placement << "\narea:" << cellIndex << ':' << std::quoted(cell.serializeText());
                if (start.binding.mTeleportDoors && (!start.navigation || start.binding.mPlayerTravel))
                {
                    const auto angle = [](float radians) {
                        double turns = -double(radians) / (2 * std::numbers::pi);
                        turns -= std::floor(turns);
                        return Turn32::fromValue(uint32_t(uint64_t(std::llround(turns * 4294967296.0))));
                    };
                    for (const auto& door : loadout.teleportDoors(cell, names))
                    {
                        auto& doors = *start.binding.mTeleportDoors;
                        if (doors.size() == 32 * names.size()) throw std::invalid_argument("Native teleport campaign budget exceeded");
                        const auto& dest = door.mRef.mDoorDest;
                        const Position3 position(std::llround(double(door.mRef.mPos.pos[0]) * 1024),
                            std::llround(double(door.mRef.mPos.pos[1]) * 1024), std::llround(double(door.mRef.mPos.pos[2]) * 1024));
                        doors.push_back({door.mIdentity, wireCell, position,
                            Transform(wireCells.at(size_t(std::ranges::find(names, door.mDestination) - names.begin())),
                                Position3(std::llround(double(dest.pos[0]) * 1024), std::llround(double(dest.pos[1]) * 1024),
                                    std::llround(double(dest.pos[2]) * 1024)),
                                Orientation3(angle(dest.rot[0]), angle(dest.rot[1]), angle(dest.rot[2])))});
                        placement << "\nteleport:" << cellIndex << ':' << door.mIdentity << ':' << door.mRef.mRefID;
                    }
                }
            }
            if ((start.secondWireCell || start.binding.mStreamExteriors) && (!start.navigation || start.binding.mCellActors))
            {
                if (start.binding.mStreamExteriors)
                    for (const auto& cell : wireCells)
                    {
                        size_t visibleActors = 0;
                        for (size_t i = 0; i < wireCells.size(); ++i)
                            if (sharesCellNeighborhood(cell, wireCells[i])) visibleActors += actorCounts[i];
                        if (visibleActors > MaximumEquipmentSnapshotActors)
                            throw std::invalid_argument("Native player neighborhood exceeds actor appearance budget");
                    }
                size_t itemCount = 0;
                for (const auto& domain : domains) itemCount += domain.size();
                if (itemCount > (start.binding.mStreamExteriors ? PlainEquipmentValues::MaxWorldItems : 64))
                    throw std::invalid_argument("Native area world placement budget exceeded");
                start.binding.mAreaActivity = [&loadout, &scenes, names, domains, fingerprints](const auto& active) {
                    std::vector<std::unique_ptr<PlacementScene>> staged(names.size());
                    for (size_t i = 0; i < names.size(); ++i)
                        if (active[i] && !scenes[i])
                        {
                            staged[i] = std::make_unique<PlacementScene>(loadout, names[i], domains[i]);
                            if (staged[i]->fingerprint() != fingerprints[i])
                                throw std::invalid_argument("Native cell resources changed after binding");
                        }
                    for (size_t i = 0; i < names.size(); ++i)
                    {
                        if (staged[i]) scenes[i].swap(staged[i]);
                        if (!active[i]) scenes[i].reset();
                    }
                };
                // Startup discovery validates both; occupancy controls loaded
                // OpenMW CellStores/model scenes thereafter. Canonical stores stay.
                for (auto& scene : scenes) scene.reset();
            }
            if (start.binding.mActorSelections)
            {
                auto& selections = *start.binding.mActorSelections;
                std::ranges::sort(selections, {}, &ActorSpawnSelection::mPlacement);
                validateActorSpawns(selections);
                if (savedSpawns && *savedSpawns != selections)
                    throw std::invalid_argument("Saved actor spawn domain differs from content placements");
                for (const auto& spawn : selections)
                    placement << "\nspawn:" << spawn.mPlacement << ':' << spawn.mRecord;
            }
            if (start.navigation)
            {
                const auto base = ESM::RefId::stringRefId(start.navigation->record);
                const auto matching = [&](const auto& owner) { return start.binding.mCellActors
                    ? (loadout.store().get<ESM::NPC>().search(owner.mBase) || loadout.store().get<ESM::Creature>().search(owner.mBase))
                    : owner.mCell == start.wireCell && owner.mBase == base; };
                if (start.binding.mCellActors ? std::ranges::none_of(start.binding.mContainers, matching)
                    : std::ranges::count_if(start.binding.mContainers, matching) != 1)
                    throw std::invalid_argument("Navigating NPC must resolve to one authoritative inventory owner");
                const auto& owner = *std::ranges::find_if(start.binding.mContainers, matching);
                if (start.binding.mCellActors)
                {
                    std::copy(std::begin(owner.mPlacement->mPos.pos), std::end(owner.mPlacement->mPos.pos),
                        start.navigation->destination.begin());
                }
                std::ranges::sort(start.binding.mDoors, {}, &InventoryServiceBinding::OrdinaryDoorPlacement::mId);
                std::vector<uint64_t> doors;
                for (const auto& door : start.binding.mDoors)
                    if (start.binding.mCellActors || start.binding.mTravelerNeighborhood || door.mCell == start.wireCell)
                        doors.push_back(door.mId);
                std::ranges::sort(doors);
                if (start.navigation->doors && !start.binding.mCellActors && !start.binding.mTravelerNeighborhood && doors.empty())
                    throw std::invalid_argument("V17-V19 require at least one ordinary door");
                std::vector<InventoryContainerBinding> neighborOwners;
                for (const auto& value : start.binding.mContainers)
                    if (start.binding.mNeighborAi && (start.binding.mCellActors || value.mCell == start.wireCell)
                        && value.mId != owner.mId
                        && (loadout.store().get<ESM::NPC>().search(value.mBase)
                            || loadout.store().get<ESM::Creature>().search(value.mBase))) neighborOwners.push_back(value);
                std::ranges::sort(neighborOwners, {}, [](const auto& value) { return value.mId.value(); });
                if (start.binding.mNeighborAi && !start.binding.mCellActors && neighborOwners.empty())
                    throw std::invalid_argument("Native neighbor witness has no inventory owner");
                std::vector<uint64_t> neighborIds;
                for (const auto& adjacent : neighborOwners) neighborIds.push_back(adjacent.mId.value());
                std::vector<std::pair<uint64_t, std::vector<ESM::RefId>>> processing;
                if (start.binding.mCellActors)
                {
                    const auto regions = actorProcessingRegions(names);
                    const auto add = [&](const auto& actor) {
                        const auto cell = names[std::ranges::find(wireCells, actor.mCell) - wireCells.begin()];
                        const auto region = std::ranges::find_if(regions, [&](const auto& cells) {
                            return std::ranges::find(cells, cell) != cells.end();
                        });
                        processing.emplace_back(actor.mId.value(), *region);
                    };
                    add(owner);
                    for (const auto& adjacent : neighborOwners) add(adjacent);
                    if (start.binding.mBoundHits) start.binding.mBoundHits->resize(processing.size() + 2);
                }
                const auto actorCells = start.binding.mTravelerNeighborhood ? names : std::vector{start.cell};
                const auto createScene = [&loadout, actorCells, neighborhood = start.binding.mTravelerNeighborhood,
                    movementEffects = start.binding.mMovementEffects,
                    id = owner.mId.value(), neighborIds, processing, names, navigation = *start.navigation, doors](std::span<const DynamicActorBody> dynamic = {}) {
                    auto actors = neighborIds;
                    for (const auto& body : dynamic) actors.push_back(body.actor);
                    auto domains = processing;
                    for (const auto& body : dynamic) if (!domains.empty())
                    {
                        auto cells = std::vector{body.cell};
                        if (body.cell.is<ESM::ESM3ExteriorCellRefId>())
                            for (const auto& domain : processing)
                                if (std::ranges::find(domain.second, body.cell) != domain.second.end())
                                { cells = domain.second; break; }
                        domains.emplace_back(body.actor, std::move(cells));
                    }
                    auto scene = domains.empty() ? std::make_shared<InteriorActorScene>(loadout, actorCells, id,
                        "meshes/base_anim.nif", "meshes/base_animkna.nif", actors, dynamic)
                        : std::make_shared<InteriorActorScene>(loadout, domains,
                            "meshes/base_anim.nif", "meshes/base_animkna.nif", dynamic, names);
                    if (navigation.doors) scene->bindDoors(doors, navigation.avoidance);
                    if (movementEffects) scene->enableMovementEffects();
                    scene->enableNavigation(navigation.settings);
                    scene->travelTo(navigation.destination, neighborhood);
                    return scene;
                };
                auto scene = createScene();
                start.binding.mCreateActorSet = createScene;
                start.binding.mBindPhysicsPlayers = [&loadout](InteriorActorScene& scene,
                    std::span<const DynamicActorBody> bodies) { scene.bindPlayers(loadout, bodies); };
                for (const auto* domain : start.binding.worldDomains())
                    if (start.binding.mCellActors || start.binding.mTravelerNeighborhood || domain->mCell == start.wireCell)
                        start.binding.mCollisionCells.push_back(domain->mCell);
                start.binding.mTravelDestination = start.navigation->destination;
                start.binding.mEnchantedWeaponsAreMagical = scene->enchantedWeaponsAreMagical();
                start.binding.mOnlyAppropriateAmmunitionBypassesResistance
                    = scene->onlyAppropriateAmmunitionBypassesResistance();
                start.binding.mUncappedDamageFatigue = scene->uncappedDamageFatigue();
                start.binding.mClassicReflectedAbsorb = scene->classicReflectedAbsorb();
                if (!start.navigation->meleeGroup.empty())
                {
                    auto bound = scene->bindMeleeAnimation(start.navigation->meleeGroup,
                        start.navigation->meleeAttack, start.navigation->meleeSpeed);
                    placement << bound.mResourceIdentity;
                    start.binding.mBoundMelee = std::move(bound);
                }
                if (start.binding.mNpcCastLifecycle)
                {
                    start.binding.mBoundCasts = scene->bindCastAnimations();
                    placement << start.binding.mBoundCasts->resourceIdentity;
                }
                if (start.binding.mPlayerCastLifecycle)
                    for (size_t i = 0; i < 2; ++i)
                    {
                        start.binding.mPlayerCasts[i] = scene->bindCastAnimations(start.binding.mActors[i].mBase);
                        placement << "\ncast-participant:" << i << ':' << start.binding.mPlayerCasts[i].resourceIdentity;
                    }
                if (start.binding.mBoundHits)
                {
                    auto& hits = *start.binding.mBoundHits;
                    std::vector<ESM::RefId> participants{start.binding.mActors[0].mBase,
                        start.binding.mActors[1].mBase, owner.mBase};
                    if (start.binding.mPlacementCombat)
                        for (const auto& adjacent : neighborOwners) participants.push_back(adjacent.mBase);
                    if (hits.size() != participants.size())
                        throw std::invalid_argument("Native hit resource placement domain invalid");
                    for (size_t i = 0; i < participants.size(); ++i)
                    {
                        hits[i] = scene->bindHitAnimations(participants[i], start.binding.mKnockoutAnimation);
                        placement << "\nhit-participant:" << i << ':' << hits[i].resourceIdentity;
                    }
                }
                start.binding.mRangedFlight = start.text.starts_with("native-inventory-49") || start.binding.mKnockoutAnimation;
                start.binding.mRangedRelease = start.text.starts_with("native-inventory-48") || start.binding.mRangedFlight;
                start.binding.mBowRelease = start.text.starts_with("native-inventory-47") || start.binding.mRangedRelease;
                const bool playerSwings = start.text.starts_with("native-inventory-46") || start.binding.mBowRelease;
                start.binding.mGeneralAttackModes = start.text.starts_with("native-inventory-45") || playerSwings;
                if (start.text.starts_with("native-inventory-44") || start.binding.mGeneralAttackModes)
                {
                    start.binding.mWeaponMelee = [scene, actor = owner.mBase, attack = start.navigation->meleeAttack,
                        cache = std::map<std::pair<ESM::RefId, std::string>, MeleeAnimation>{}]
                        (const ESM::Weapon* weapon, std::string_view direction) mutable {
                        const std::string mode(direction.empty() ? std::string_view(attack) : direction);
                        const auto id = std::pair{weapon ? weapon->mId : ESM::RefId{}, mode};
                        if (const auto found = cache.find(id); found != cache.end()) return found->second;
                        if (cache.size() >= 3 * (PlainEquipmentValues::MaxItems + 1))
                            throw std::invalid_argument("Native weapon animation cache exceeded inventory bound");
                        return cache.emplace(id, scene->bindWeaponMeleeAnimation(actor, weapon, mode)).first->second;
                    };
                }
                if (start.binding.mNeighborCombat)
                {
                    for (const auto& adjacent : neighborOwners)
                    {
                        if (start.binding.mCellActors)
                        {
                            const auto casts = scene->bindCastAnimations(adjacent.mBase);
                            start.binding.mDynamicCasts.emplace(adjacent.mId.value(), casts);
                            placement << "\ncell-caster:" << adjacent.mId.value() << ':' << casts.resourceIdentity;
                        }
                        auto bound = [scene, actor = adjacent.mBase,
                            cache = std::map<std::pair<ESM::RefId, std::string>, MeleeAnimation>{}]
                            (const ESM::Weapon* weapon, std::string_view direction) mutable {
                            const std::string mode(direction.empty() ? std::string_view("chop") : direction);
                            const auto key = std::pair{weapon ? weapon->mId : ESM::RefId{}, mode};
                            if (const auto found = cache.find(key); found != cache.end()) return found->second;
                            if (cache.size() >= 3 * (PlainEquipmentValues::MaxItems + 1))
                                throw std::invalid_argument("Native neighbor animation cache exceeded bound");
                            return cache.emplace(key, scene->bindWeaponMeleeAnimation(actor, weapon, mode)).first->second;
                        };
                        placement << "\nneighbor-melee:" << adjacent.mId.value() << ':' << bound(nullptr, "chop").identity();
                        start.binding.mNeighborMeleeSet.push_back(bound);
                    }
                    if (!start.binding.mNeighborMeleeSet.empty()) start.binding.mNeighborWeaponMelee = start.binding.mNeighborMeleeSet.front();
                }
                if (playerSwings)
                    for (size_t i = 0; i < start.binding.mPlayerMelee.size(); ++i)
                    {
                        start.binding.mPlayerMelee[i] = [scene, actor = start.binding.mActors[i].mBase, bows = start.binding.mBowRelease,
                            ranged = start.binding.mRangedRelease,
                            cache = std::map<std::pair<ESM::RefId, std::string>, MeleeAnimation>{}]
                            (const ESM::Weapon* weapon, std::string_view direction) mutable {
                            const auto key = std::pair{weapon ? weapon->mId : ESM::RefId{},
                                weapon && ((bows && weapon->mData.mType == ESM::Weapon::MarksmanBow)
                                    || (ranged && (weapon->mData.mType == ESM::Weapon::MarksmanCrossbow
                                        || weapon->mData.mType == ESM::Weapon::MarksmanThrown)))
                                    ? std::string("shoot") : std::string(direction)};
                            if (const auto found = cache.find(key); found != cache.end()) return found->second;
                            if (cache.size() >= 3 * (PlainEquipmentValues::MaxItems + 1))
                                throw std::invalid_argument("Native player animation cache exceeded bound");
                            return cache.emplace(key, scene->bindWeaponMeleeAnimation(actor, weapon, key.second)).first->second;
                        };
                    }
                if (start.binding.mRetainTraveler || start.binding.mCellActors)
                {
                    const auto meleeIdentity = start.binding.mBoundMelee
                        ? start.binding.mBoundMelee->mResourceIdentity : std::string{};
                    const auto meleeGroup = start.navigation->meleeGroup;
                    const auto meleeAttack = start.navigation->meleeAttack;
                    const auto meleeSpeed = start.navigation->meleeSpeed;
                    start.binding.mNavigationActivity = [scene, createScene, meleeIdentity,
                        meleeGroup, meleeAttack, meleeSpeed, &loadout, hits = start.binding.mBoundHits,
                        knockout = start.binding.mKnockoutAnimation,
                        participants = [&] {
                            std::vector<ESM::RefId> actors{start.binding.mActors[0].mBase,
                                start.binding.mActors[1].mBase, owner.mBase};
                            if (start.binding.mPlacementCombat)
                                for (const auto& adjacent : neighborOwners) actors.push_back(adjacent.mBase);
                            return actors;
                        }()](bool active, const DynamicActorSet& actors, const ActorSceneWorld& world) {
                        if (active && !scene->loaded())
                        {
                            std::vector<DynamicActorBody> bodies;
                            for (const auto& body : actors.bodies) bodies.push_back(body.collision);
                            auto fresh = createScene(bodies);
                            if (!bodies.empty() && DynamicActorSet::resources(*fresh) != actors.collisionResources)
                                throw std::invalid_argument("Summon collision resources changed after unloading");
                            if (!meleeIdentity.empty() && fresh->bindMeleeAnimation(
                                    meleeGroup, meleeAttack, meleeSpeed).mResourceIdentity != meleeIdentity)
                                throw std::invalid_argument("Native melee resource changed after binding");
                            if (hits)
                                for (size_t i = 0; i < participants.size(); ++i)
                                {
                                    const auto rebound = fresh->bindHitAnimations(participants[i], knockout);
                                    if (rebound.resourceIdentity != (*hits)[i].resourceIdentity
                                        || rebound.animations != (*hits)[i].animations
                                        || rebound.knockout != (*hits)[i].knockout || rebound.knockdown != (*hits)[i].knockdown
                                        || rebound.deaths != (*hits)[i].deaths)
                                        throw std::invalid_argument("Native hit resource changed after binding");
                                }
                            for (const auto& body : actors.bodies)
                            {
                                const auto rebound = fresh->bindHitAnimations(body.collision.record, knockout);
                                std::istringstream animation(rebound.resourceIdentity);
                                if (Files::getHash("native-summon-animation", animation) != body.animationResources)
                                    throw std::invalid_argument("Summon animation resources changed after unloading");
                                const auto casts = fresh->bindCastAnimations(body.collision.record);
                                if (body.casting)
                                {
                                    const auto& cast = *body.casting;
                                    const auto timing = casts.ranges.at(cast.range);
                                    if (cast.elapsed >= timing.stopTicks || (cast.phase >= ActorCampaignCast::Released
                                            ? cast.elapsed < timing.releaseTicks : cast.elapsed >= timing.releaseTicks))
                                        throw std::invalid_argument("Summon cast clock changed after unloading");
                                }
                            }
                            fresh->replaceWorld(world);
                            scene->copyPlayersTo(loadout, *fresh);
                            scene->reload(*fresh);
                        }
                        else if (!active) scene->unload();
                    };
                }
                if (start.binding.mCellActors)
                    start.binding.mGeometryActivity = [scene, createScene, names, &loadout,
                        retained = std::vector<bool>{}](const std::vector<bool>& active,
                        const DynamicActorSet& actors, const ActorSceneWorld& world,
                        std::span<const ActorSceneDoor> doors) mutable {
                        std::vector<ESM::RefId> demanded;
                        for (size_t i = 0; i < active.size(); ++i) if (active[i]) demanded.push_back(names.at(i));
                        if (active == retained && (!scene->loaded() || scene->geometryMatches(demanded))) return;
                        if (demanded.empty())
                            scene->unload();
                        else
                        {
                            std::vector<DynamicActorBody> bodies;
                            for (const auto& body : actors.bodies) bodies.push_back(body.collision);
                            auto fresh = createScene(bodies);
                            if (fresh->fingerprint() != scene->fingerprint()
                                || (!bodies.empty() && DynamicActorSet::resources(*fresh) != actors.collisionResources))
                                throw std::invalid_argument("Streamed collision resources changed after binding");
                            fresh->replaceWorld(world);
                            scene->copyPlayersTo(loadout, *fresh);
                            auto restored = fresh->prepareRestore(scene->image(), doors);
                            fresh->install(*restored);
                            fresh->retainGeometry(demanded);
                            scene->installActorSet(*fresh);
                        }
                        retained = active;
                    };
                placement << scene->fingerprint();
                start.binding.mNavigationSpeed = start.navigation->speed;
                start.binding.mNavigatingActor = std::move(scene);
            }
            // Re-resolve before recovery. The image envelope binds resolved
            // placement plus actual ordered file bytes, encoding and player roles.
            auto material = start.text + placement.str() + '\n' + loadout.contentFingerprint();
            CredentialDigest digest;
            if (!crypto.sha256(std::as_bytes(std::span(material)), digest))
                throw std::runtime_error("Native content binding digest unavailable");
            for (size_t i = 0; i < digest.bytes.size(); ++i)
                start.binding.mContent[i] = std::to_integer<unsigned char>(digest.bytes[i]);
            if (!start.personalScripts.empty())
            {
                uint64_t campaign = 0;
                for (size_t i = 0; i < 8; ++i) campaign |= uint64_t(start.binding.mContent[i]) << (8 * i);
                std::vector<const InventoryContainerBinding*> npcContainers;
                for (const auto& container : start.binding.mContainers)
                    if (loadout.store().get<ESM::NPC>().search(container.mBase))
                        npcContainers.push_back(&container);
                std::vector<PersonalScripts::Definition> definitions;
                for (size_t storyIdx = 0; storyIdx < start.personalScripts.size(); ++storyIdx)
                {
                    const auto& entry = start.personalScripts[storyIdx];
                    std::vector<ESM::Dialogue> dialogues;
                    for (const auto& id : entry.dialogues)
                    {
                        const auto* dialogue = loadout.store().get<ESM::Dialogue>().search(id);
                        if (!dialogue) throw std::invalid_argument("Personal dialogue absent from winning content");
                        dialogues.push_back(*dialogue);
                    }
                    const InventoryContainerBinding* targetOwner = nullptr;
                    for (const auto* candidate : npcContainers)
                    {
                        const auto* npc = loadout.store().get<ESM::NPC>().search(candidate->mBase);
                        if (npc && !npc->mScript.empty()
                            && (entry.script == npc->mScript
                                || std::ranges::any_of(entry.bank, [&](const auto& member) { return member.first == npc->mScript; })))
                        {
                            targetOwner = candidate;
                            break;
                        }
                    }
                    if (!targetOwner)
                        for (const auto* candidate : npcContainers)
                            if (std::ranges::any_of(dialogues, [&](const auto& d) {
                                    return d.mType == ESM::Dialogue::Topic
                                        && std::ranges::any_of(d.mInfo, [&](const auto& info) { return info.mActor == candidate->mBase; });
                                }))
                            {
                                targetOwner = candidate;
                                break;
                            }
                    if (!targetOwner && !npcContainers.empty())
                        targetOwner = npcContainers[storyIdx < npcContainers.size() ? storyIdx : 0];
                    auto bank = entry.bank;
                    auto primaryScript = entry.script;
                    const auto* targetNpc = targetOwner ? loadout.store().get<ESM::NPC>().search(targetOwner->mBase) : nullptr;
                    if (targetNpc && !targetNpc->mScript.empty()
                        && std::ranges::none_of(bank, [&](const auto& member) { return member.first == targetNpc->mScript; }))
                    {
                        if (start.personalBanks)
                            bank.emplace_back(targetNpc->mScript, true);
                        else
                        {
                            primaryScript = targetNpc->mScript;
                            bank = {{targetNpc->mScript, true}};
                        }
                    }
                    std::vector<ESM::Script> records;
                    for (const auto& [id, running] : bank)
                    {
                        const auto* record = loadout.store().get<ESM::Script>().search(id);
                        if (!record) throw std::invalid_argument("Personal bank script absent from winning content");
                        records.push_back(*record);
                    }
                    const bool parties = start.text.starts_with("native-inventory-96\n");
                    const bool npcBanks = start.text.starts_with("native-inventory-95\n") || parties;
                    const bool lifeEvents = start.text.starts_with("native-inventory-92\n")
                        || start.text.starts_with("native-inventory-93\n") || npcBanks;
                    const bool sourceWaits = start.text.starts_with("native-inventory-91\n") || lifeEvents;
                    const auto source = sourceWaits
                        ? (targetOwner ? targetOwner->mBase : start.binding.mContainers.at(0).mBase)
                        : ESM::RefId{};
                    auto script = admitSharedWorldScript(loadout, primaryScript, entry.globals, crypto, records, source, lifeEvents);
                    PersonalScripts::Definition definition{0, entry.story, script};
                    definition.dialogues = std::move(dialogues);
                    if (start.personalBanks)
                    {
                        definition.running = bank.front().second;
                        for (size_t i = 1; i < bank.size(); ++i)
                            definition.members.push_back({admitSharedWorldScript(loadout, bank[i].first,
                                entry.globals, crypto, records, source, lifeEvents), bank[i].second});
                    }
                    if (start.text.starts_with("native-inventory-86\n") || start.text.starts_with("native-inventory-87\n")
                        || start.text.starts_with("native-inventory-88\n") || start.text.starts_with("native-inventory-90\n")
                        || start.text.starts_with("native-inventory-89\n") || start.text.starts_with("native-inventory-94\n") || sourceWaits)
                    {
                        const auto& binding = start.binding;
                        if (!binding.mNavigatingActor || !targetOwner)
                            throw std::invalid_argument("Story encounter requires one dry interior, one NPC and immutable geometry");
                        const auto& owner = *targetOwner;
                        const auto* npc = loadout.store().get<ESM::NPC>().search(owner.mBase);
                        const bool inScene = owner.mId.value() == binding.mNavigatingActor->snapshot().mActor
                            || std::ranges::any_of(binding.mNavigatingActor->neighborSnapshots(),
                                [&](const auto& snapshot) { return snapshot.mActor == owner.mId.value(); });
                        if (!npc || !inScene)
                            throw std::invalid_argument("Story encounter source must be a living unscripted NPC placement");
                        const auto ownerCell = binding.mEngineCells.at(owner.mCell);
                        definition.encounter = PersonalScripts::EncounterBinding{owner.mId.value(), owner.mBase, ownerCell};
                        loadout.validateDisableEncounter(ownerCell, owner.mId.value());
                    }
                    for (auto player : start.binding.mPlayers)
                    { definition.character = player.value(); definitions.push_back(definition); }
                }
                const bool parties = start.text.starts_with("native-inventory-96\n");
                const bool npcBanks = start.text.starts_with("native-inventory-95\n") || parties;
                start.binding.mPersonalScripts
                    = std::make_shared<const PersonalScripts>(campaign, std::move(definitions), crypto,
                        start.personalScheduling, start.personalBanks,
                        (start.text.starts_with("native-inventory-85\n") || start.text.starts_with("native-inventory-86\n")
                            || start.text.starts_with("native-inventory-87\n") || start.text.starts_with("native-inventory-88\n")
                            || start.text.starts_with("native-inventory-89\n") || start.text.starts_with("native-inventory-90\n")
                            || start.text.starts_with("native-inventory-91\n") || start.text.starts_with("native-inventory-92\n")
                            || start.text.starts_with("native-inventory-93\n") || start.text.starts_with("native-inventory-94\n")
                            || npcBanks),
                        (start.text.starts_with("native-inventory-87\n") || start.text.starts_with("native-inventory-88\n")
                            || start.text.starts_with("native-inventory-90\n") || npcBanks),
                        start.text.starts_with("native-inventory-88\n") || npcBanks,
                        (start.text.starts_with("native-inventory-89\n") || start.text.starts_with("native-inventory-90\n")
                            || start.text.starts_with("native-inventory-94\n") || npcBanks),
                        start.text.starts_with("native-inventory-91\n") || start.text.starts_with("native-inventory-92\n")
                            || start.text.starts_with("native-inventory-93\n") || npcBanks,
                        start.text.starts_with("native-inventory-92\n") || start.text.starts_with("native-inventory-93\n") || npcBanks,
                        start.text.starts_with("native-inventory-93\n") || npcBanks,
                        start.text.starts_with("native-inventory-94\n") || npcBanks,
                        npcBanks,
                        parties);
            }
            if (start.binding.mSummons)
            {
                start.binding.mStaticContainers = start.binding.mContainers.size();
                start.binding.mStaticNeighbors = start.binding.mNeighborMeleeSet.size();
                start.binding.mNeighborLimit = 36;
                if (!restored.empty())
                {
                    const auto campaign = readActorCampaign({reinterpret_cast<const char*>(restored.data()), restored.size()});
                    if (campaign.dynamicActors.empty()) throw std::invalid_argument("Summon campaign version differs from descriptor");
                    auto actors = DynamicActorSet::restore(campaign.dynamicActors);
                    if (start.binding.mPersonalScripts && start.binding.mPersonalScripts->encounters() && !actors.bodies.empty())
                        throw std::invalid_argument("Terminal encounter recovery cannot expand actor dependencies");
                    std::vector<DynamicActorBody> bodies;
                    for (const auto& body : actors.bodies) bodies.push_back(body.collision);
                    auto fresh = start.binding.mCreateActorSet(bodies);
                    if (!actors.bodies.empty() && DynamicActorSet::resources(*fresh) != actors.collisionResources)
                        throw std::invalid_argument("Summon collision resources changed on recovery");
                    start.binding.mNavigatingActor->installActorSet(*fresh);
                    bindDynamicActorSet(start.binding, actors);
                }
            }
            if (start.binding.mCharacterCreation)
            {
                std::optional<ActorCampaign> campaign;
                if (!restored.empty())
                {
                    campaign = readActorCampaign({reinterpret_cast<const char*>(restored.data()), restored.size()});
                    if (!campaign->characters) throw std::invalid_argument("Character campaign version differs from descriptor");
                }
                for (size_t i = 0; i < 2; ++i)
                {
                    if (!campaign) continue;
                    if (campaign->characterPlayers[i] != start.binding.mPlayers[i].value())
                        throw std::invalid_argument("Character campaign player binding changed");
                    if (campaign->characterProfiles[i].empty()) continue;
                    auto profile = decodeNativeCharacterProfile(start.binding.mPlayers[i], campaign->characterProfiles[i]);
                    auto character = constructNativeCharacter(profile,
                        *loadout.store().get<ESM::NPC>().find(start.binding.mActors[i].mBase), loadout.store());
                    start.binding.mCharacters[i] = std::move(profile);
                    bindCharacterResources(start.binding, i, std::make_shared<const ESM::NPC>(std::move(character.body)));
                    if (!std::ranges::equal(start.binding.mCharacterResources[i], campaign->characterResources[i]))
                        throw std::invalid_argument("Character resources changed on recovery");
                }
            }
            return start.binding;
        }
        Impl(Startup start, ContentManifestId manifest, CredentialCrypto& crypto, std::span<const std::byte> restored)
            : loadout(std::move(start.options)),
              environment(start.navigation || start.text.starts_with("native-inventory-12")
                    || start.text.starts_with("native-inventory-13") || start.text.starts_with("native-inventory-14")
                    || start.text.starts_with("native-inventory-15")
                    ? std::make_unique<Environment>(loadout, manifest, crypto, start.binding.mLootSeed,
                        start.binding.mSpecialConditions) : nullptr),
              inventory(loadout.store(), loadout.readers(), bind(start, loadout, crypto, scenes, restored), !restored.empty())
        {
            if (start.binding.mSpecialConditions)
                inventory.bindSunDamageScale([this](const CanonicalWorldState& world, ESM::RefId region) {
                    return environment->sunDamageScale(world, region);
                });
            if (!restored.empty())
            {
                std::vector<ESM::RefId> references{start.binding.mActors[0].mBase, start.binding.mActors[1].mBase};
                for (const auto& shared : start.binding.mContainers) references.push_back(shared.mBase);
                for (const auto& [id, record] : MWWorld::inventoryRecords(loadout.store())) references.push_back(record);
                for (const auto& [id, record] : MWWorld::inventorySoulRecords(loadout.store())) references.push_back(record);
                for (const auto* domain : start.binding.worldDomains())
                    if (domain) for (const auto& [identity, ref] : domain->mPlacements)
                        for (auto id : {ref.mRefID, ref.mOwner, ref.mSoul, ref.mFaction, ref.mKey, ref.mTrap})
                            if (!id.empty()) references.push_back(id);
                inventory.recover(restored, references);
            }
        }
    };
    InventoryHost::InventoryHost(const std::filesystem::path& descriptor, const ContentManifest& manifest,
        const PlayerIdentityRegistry& players, CredentialCrypto& crypto, std::span<const std::byte> restored)
        : mImpl(std::make_unique<Impl>(startup(descriptor, manifest, players), manifest.id(), crypto, restored))
    {
        if (mImpl->inventory.characterCreationEnabled())
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& binding = mImpl->inventory.characterBinding();
                const auto* registered = players.characterProfile(binding.mPlayers[i]);
                if (!registered || (binding.mCharacters[i] ? *registered != *binding.mCharacters[i]
                    : registered->lifecycle() == CharacterLifecycle::EstablishedCharacter))
                    throw std::invalid_argument("Registered profile differs from committed native character");
            }
    }
    std::optional<CharacterContentCatalog> InventoryHost::characterContent(ContentManifestId manifest,
        Transform creation, Transform completion) const
    {
        if (!mImpl->inventory.characterCreationEnabled()) return {};
        const auto& binding = mImpl->inventory.characterBinding();
        const auto bound = [&](CellId cell) {
            return std::ranges::any_of(binding.worldDomains(), [cell](const auto* world) { return world->mCell == cell; });
        };
        if (!bound(creation.cell()) || !bound(completion.cell()))
            throw std::invalid_argument("Character safe point outside native domain");
        auto result = discoverNativeCharacterContent(manifest, creation, completion, binding.mStartingInventory, mImpl->loadout.store());
        if (!result) throw std::invalid_argument("Native character choice discovery unavailable or outside bounds");
        return result;
    }
    InventoryHost::~InventoryHost() = default;
    ServerApp::NativeEnvironmentService* InventoryHost::environment() noexcept { return mImpl->environment.get(); }
    ServerApp::NativeInventoryService& InventoryHost::service() noexcept { return mImpl->inventory; }
}
