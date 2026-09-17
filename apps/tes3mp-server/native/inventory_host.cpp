#include "inventory_host.hpp"
#include "inventory_service.hpp"
#include "loadout.hpp"
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        std::string readDescriptor(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            const auto size = input.tellg();
            if (size <= 0 || size > 16 * 1024) throw std::invalid_argument("Native inventory descriptor unavailable or oversized");
            std::string text(static_cast<size_t>(size), '\0');
            input.seekg(0);
            if (!input.read(text.data(), size)) throw std::invalid_argument("Native inventory descriptor read failed");
            return text;
        }
        struct Startup
        {
            std::string text;
            LoadoutOptions options;
            InventoryServiceBinding binding;
            std::string cell, plugin;
            uint32_t index;
            CellId wireCell;
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
            if (version != "native-inventory-3" && version != "native-inventory-4" && version != "native-inventory-5")
                throw std::invalid_argument("Native inventory descriptor version incompatible");
            const bool baseInventory = version == "native-inventory-5";
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
            std::string cell, plugin; uint64_t index = 0;
            if (wholeInterior) { key("interior"); in >> std::quoted(cell); }
            else { key("container"); in >> std::quoted(cell) >> std::quoted(plugin) >> index; }
            key("cell"); std::string cellText; in >> cellText;
            const auto cells = parseContentCells(cellText);
            if (!in || !(in >> std::ws).eof() || (!baseInventory && !itemId)
                || lootLevel < 1 || lootLevel > 1000 || lootSeed > UINT32_MAX
                || cell.empty() || cell.size() > 256 || (!wholeInterior && plugin.empty()) || plugin.size() > 256 || index > UINT32_MAX
                || !cells || cells->size() != 1 || !manifest.contains(cells->front())
                || cells->front().kind() != CellId::Kind::Interior)
                throw std::invalid_argument("Native inventory placed selection or cell mapping invalid");
            InventoryServiceBinding binding{{*first, *second}, itemId,
                {{{actorA, shirt, countA, false, baseInventory}, {actorB, shirt, countB, false, baseInventory}}}, {}, {}};
            binding.mLootLevel = lootLevel;
            binding.mLootSeed = uint32_t(lootSeed);
            // Hash semantic bindings, never local configuration paths or
            // descriptor whitespace, so moving the same loadout preserves saves.
            std::ostringstream semantic;
            semantic << version << '\n' << identity << registration << '\n'
                << actorA << ':' << countA << '\n' << actorB << ':' << countB << '\n'
                << shirt << ':' << (itemId ? itemId->value() : 0) << '\n' << cellText << '\n' << lootLevel << ':' << lootSeed << '\n';
            return {semantic.str(), std::move(options), std::move(binding), cell, plugin, uint32_t(index), cells->front()};
        }
    }
    struct InventoryHost::Impl
    {
        Loadout loadout;
        InventoryService inventory;
        static InventoryServiceBinding bind(Startup& start, Loadout& loadout, CredentialCrypto& crypto)
        {
            const auto references = start.plugin.empty()
                ? loadout.resolveContainers(start.cell, MaxEquipmentContainers)
                : std::vector{loadout.resolveContainer(start.cell, start.plugin, start.index)};
            std::ostringstream placement;
            for (const auto& placed : references)
            {
                const auto& p = placed.mRef.mPos.pos;
                const Position3 position(std::llround(double(p[0]) * 1024),
                    std::llround(double(p[1]) * 1024), std::llround(double(p[2]) * 1024));
                start.binding.mContainers.push_back({ContainerId::fromValue(placed.mIdentity).value(),
                    start.wireCell, position, placed.mRef.mRefID, placed.mRef});
                if (start.binding.mContainers.size() > 1) placement << '\n';
                placement << std::quoted(loadout.store().get<ESM::Cell>().find(start.cell)->mId.serializeText())
                    << ':' << placed.mIdentity << ':' << placed.mRef.mRefID << ':'
                    << position.x() << ':' << position.y() << ':' << position.z();
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
        Impl(Startup start, CredentialCrypto& crypto, std::span<const std::byte> restored)
            : loadout(std::move(start.options)),
              inventory(loadout.store(), loadout.readers(), bind(start, loadout, crypto), !restored.empty())
        {
            if (!restored.empty())
            {
                std::vector<ESM::RefId> references{start.binding.mActors[0].mBase, start.binding.mActors[1].mBase};
                for (const auto& [id, record] : MWWorld::inventoryRecords(loadout.store())) references.push_back(record);
                for (const auto& [id, record] : MWWorld::inventorySoulRecords(loadout.store())) references.push_back(record);
                inventory.recover(restored, references);
            }
        }
    };
    InventoryHost::InventoryHost(const std::filesystem::path& descriptor, const ContentManifest& manifest,
        const PlayerIdentityRegistry& players, CredentialCrypto& crypto, std::span<const std::byte> restored)
        : mImpl(std::make_unique<Impl>(startup(descriptor, manifest, players), crypto, restored)) {}
    InventoryHost::~InventoryHost() = default;
    ServerApp::NativeInventoryService& InventoryHost::service() noexcept { return mImpl->inventory; }
}
