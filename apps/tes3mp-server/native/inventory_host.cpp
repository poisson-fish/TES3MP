#include "inventory_host.hpp"
#include "inventory_service.hpp"
#include "loadout.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
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
            key("native-inventory-1"); key("manifest");
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
            key("actors"); const auto actorA = record(); int countA = 0; in >> countA;
            const auto actorB = record(); int countB = 0; in >> countB;
            if (countA <= 0 || countB <= 0 || countA > MaximumTransferCount || countB > MaximumTransferCount)
                throw std::invalid_argument("Native starting shirt counts out of range");
            key("shirt"); const auto shirt = record(); uint64_t itemId = 0; in >> itemId;
            key("container"); const auto container = record(); uint64_t containerId = 0; in >> containerId;
            key("cell"); std::string cellText; in >> cellText;
            const auto cells = parseContentCells(cellText);
            key("position"); int64_t x = 0, y = 0, z = 0; in >> x >> y >> z;
            if (!in || !(in >> std::ws).eof() || !ItemPrototypeId::fromValue(itemId)
                || !ContainerId::fromValue(containerId) || !cells || cells->size() != 1 || !manifest.contains(cells->front()))
                throw std::invalid_argument("Native inventory wire mapping or position invalid");
            InventoryServiceBinding binding{{*first, *second}, *ItemPrototypeId::fromValue(itemId),
                *ContainerId::fromValue(containerId), cells->front(), Position3(x, y, z),
                {{{actorA, shirt, countA}, {actorB, shirt, countB}}}, container, {}};
            // Hash semantic bindings, never local configuration paths or
            // descriptor whitespace, so moving the same loadout preserves saves.
            std::ostringstream semantic;
            semantic << "native-inventory-1\n" << identity << registration << '\n'
                << actorA << ':' << countA << '\n' << actorB << ':' << countB << '\n'
                << shirt << ':' << itemId << '\n' << container << ':' << containerId << '\n'
                << cellText << ':' << x << ':' << y << ':' << z << '\n';
            return {semantic.str(), std::move(options), std::move(binding)};
        }
    }
    struct InventoryHost::Impl
    {
        Loadout loadout;
        InventoryService inventory;
        static InventoryServiceBinding bind(Startup& start, const Loadout& loadout, CredentialCrypto& crypto)
        {
            // The descriptor explicitly binds operator-selected engine content to
            // the authenticated pack. Recovery additionally binds actual ordered
            // file bytes, encoding and every trusted role/mapping in that descriptor.
            auto material = start.text + loadout.contentFingerprint();
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
                const std::array references{start.binding.mActors[0].mBase, start.binding.mActors[1].mBase,
                    start.binding.mActors[0].mShirt};
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
