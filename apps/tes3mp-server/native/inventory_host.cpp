#include "inventory_host.hpp"
#include "placement_scene.hpp"
#include "inventory_service.hpp"
#include "loadout.hpp"
#include "environment.hpp"
#include "actor_campaign.hpp"
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <numbers>
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
            std::array<float, 3> destination;
            float speed = 120;
            bool doors = false;
            bool avoidance = false;
        };
        struct Startup
        {
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
            if (version != "native-inventory-3" && version != "native-inventory-4" && version != "native-inventory-5"
                && version != "native-inventory-6" && version != "native-inventory-7" && version != "native-inventory-8"
                && version != "native-inventory-9" && version != "native-inventory-10" && version != "native-inventory-11" && version != "native-inventory-12" && version != "native-inventory-13" && version != "native-inventory-14" && version != "native-inventory-15" && version != "native-inventory-16" && version != "native-inventory-17" && version != "native-inventory-18")
                throw std::invalid_argument("Native inventory descriptor version incompatible");
            const bool movingActor = version == "native-inventory-16" || version == "native-inventory-17" || version == "native-inventory-18";
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
                if (!profile || profile->lifecycle() != CharacterLifecycle::EstablishedCharacter)
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
                key("npc"); Navigation nav;
                nav.doors = version == "native-inventory-17" || version == "native-inventory-18";
                nav.avoidance = version == "native-inventory-18";
                in >> std::quoted(nav.record) >> std::quoted(nav.settings);
                key("destination"); in >> nav.destination[0] >> nav.destination[1] >> nav.destination[2] >> nav.speed;
                if (!in || nav.record.empty() || nav.record.size()>256 || nav.settings.empty() || nav.settings.size()>1024
                    || !std::isfinite(nav.speed) || nav.speed<=0 || nav.speed>4096 || cell.is<ESM::ESM3ExteriorCellRefId>() || areaCount != 1)
                    throw std::invalid_argument("Native navigating NPC descriptor invalid");
                for (float value : nav.destination) if (!std::isfinite(value) || std::abs(value)>1e7f)
                    throw std::invalid_argument("Native navigation destination invalid");
                auto settings = std::filesystem::u8path(nav.settings);
                if (settings.is_relative()) settings = path.parent_path()/settings;
                nav.settings = settings.string();
                navigation = std::move(nav);
            }
            if (!in || !(in >> std::ws).eof() || (!baseInventory && !itemId)
                || lootLevel < 1 || lootLevel > 1000 || lootSeed > UINT32_MAX
                || (!wholeInterior && plugin.empty()) || plugin.size() > 256 || index > UINT32_MAX
                || !cells || cells->size() != 1 || !manifest.contains(cells->front())
                || !matches(cell, cells->front()))
                throw std::invalid_argument("Native inventory placed selection or cell mapping invalid");
            InventoryServiceBinding binding{{*first, *second}, itemId,
                {{{actorA, shirt, countA, false, baseInventory}, {actorB, shirt, countB, false, baseInventory}}}, {}, {}};
            binding.mStreamExteriors = streaming;
            if (movingActor || version == "native-inventory-15") binding.mActorSelections.emplace();
            binding.mLootLevel = lootLevel;
            binding.mLootSeed = uint32_t(lootSeed);
            if (version == "native-inventory-11" || version == "native-inventory-12" || exteriorCells) binding.mTeleportDoors.emplace();
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
            return {semantic.str(), std::move(options), std::move(binding), cell, plugin, uint32_t(index), cells->front(),
                version == "native-inventory-6" || version == "native-inventory-7" || version == "native-inventory-8" || door,
                version == "native-inventory-7" || version == "native-inventory-8" || door,
                version == "native-inventory-8" || door, std::move(doorPlugin), uint32_t(doorIndex), std::move(secondCell), secondWireCell, std::move(additionalCells), std::move(navigation)};
        }
    }
    struct InventoryHost::Impl
    {
        Loadout loadout;
        std::vector<std::unique_ptr<PlacementScene>> scenes;
        InventoryService inventory;
        std::unique_ptr<Environment> environment;
        static InventoryServiceBinding bind(Startup& start, Loadout& loadout, CredentialCrypto& crypto,
            std::vector<std::unique_ptr<PlacementScene>>& scenes, std::span<const std::byte> restored)
        {
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
                    std::erase_if(references, [&](const auto& ref) { return ref.mRef.mRefID != ESM::RefId::stringRefId(start.navigation->record); });
                    if (references.size()!=1 || references.front().mScripted || references.front().mLeveled)
                        throw std::invalid_argument("Native navigating actor must be one unscripted placement");
                    actorCounts[cellIndex]=1;
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
                        if (ref.mScripted || ref.mRef.mIsLocked || !ref.mRef.mTrap.empty())
                            throw std::invalid_argument("Native placed inventory " + std::to_string(ref.mIdentity)
                                + " (" + ref.mRef.mRefID.toDebugString() + ") in " + cell.toDebugString()
                                + " requires services: script=" + std::to_string(ref.mScripted)
                                + " locked=" + std::to_string(ref.mRef.mIsLocked) + " trap=" + ref.mRef.mTrap.toDebugString());
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
                    if (start.binding.mActorSelections)
                        for (const auto& marker : loadout.placedActors(cell))
                            if (marker.mLeveled)
                            {
                                const auto selected = std::ranges::find(*start.binding.mActorSelections,
                                    marker.mIdentity, &ActorSpawnSelection::mPlacement);
                                world->mActorSpawns.push_back({marker.mIdentity, selected->mRecord});
                                if (!selected->mRecord) ++actorCounts[cellIndex];
                            }
                    if (!start.navigation) for (const auto& item : loadout.placedItems(cell, start.binding.mStreamExteriors ? MaximumGroundItemBaselineChunkItems : 64))
                    {
                        world->mPlacements.emplace_back(item.mIdentity, item.mRef);
                        placement << "\nworld-item:" << item.mIdentity << ':' << item.mRef.mRefID;
                    }
                }
                if (start.stockPlacement && !start.navigation)
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
                if (start.binding.mTeleportDoors && !start.navigation)
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
            if ((start.secondWireCell || start.binding.mStreamExteriors) && !start.navigation)
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
                const auto matching = [&](const auto& owner) { return owner.mCell == start.wireCell && owner.mBase == base; };
                if (std::ranges::count_if(start.binding.mContainers, matching) != 1)
                    throw std::invalid_argument("Navigating NPC must resolve to one authoritative inventory owner");
                const auto& owner = *std::ranges::find_if(start.binding.mContainers, matching);
                auto scene = std::make_shared<InteriorActorScene>(loadout, std::string(start.cell.getRefIdString()), owner.mId.value(),
                    "meshes/base_anim.nif", "meshes/base_animkna.nif");
                if (start.navigation->doors)
                {
                    std::vector<uint64_t> doors;
                    for (const auto& door : start.binding.mDoors) doors.push_back(door.mId);
                    scene->bindDoors(doors, start.navigation->avoidance);
                }
                scene->enableNavigation(start.navigation->settings);
                scene->travelTo(start.navigation->destination);
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
            return start.binding;
        }
        Impl(Startup start, ContentManifestId manifest, CredentialCrypto& crypto, std::span<const std::byte> restored)
            : loadout(std::move(start.options)),
              inventory(loadout.store(), loadout.readers(), bind(start, loadout, crypto, scenes, restored), !restored.empty())
        {
            if (start.text.starts_with("native-inventory-12") || start.text.starts_with("native-inventory-13") || start.text.starts_with("native-inventory-14") || start.text.starts_with("native-inventory-15") || start.text.starts_with("native-inventory-16") || start.text.starts_with("native-inventory-17") || start.text.starts_with("native-inventory-18"))
                environment = std::make_unique<Environment>(loadout, manifest, crypto, start.binding.mLootSeed);
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
        : mImpl(std::make_unique<Impl>(startup(descriptor, manifest, players), manifest.id(), crypto, restored)) {}
    InventoryHost::~InventoryHost() = default;
    ServerApp::NativeEnvironmentService* InventoryHost::environment() noexcept { return mImpl->environment.get(); }
    ServerApp::NativeInventoryService& InventoryHost::service() noexcept { return mImpl->inventory; }
}
