#include "server_config.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

namespace TES3MP::ServerApp
{
    namespace
    {
        std::string_view trim(std::string_view value)
        {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
                value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
                value.remove_suffix(1);
            return value;
        }

        bool validUtf8(std::string_view value)
        {
            std::size_t index = 0;
            while (index < value.size())
            {
                const auto first = static_cast<unsigned char>(value[index]);
                if (first < 0x80)
                {
                    ++index;
                    continue;
                }
                std::size_t count = 0;
                std::uint32_t code = 0;
                if (first >= 0xc2 && first <= 0xdf)
                {
                    count = 1;
                    code = first & 0x1f;
                }
                else if (first >= 0xe0 && first <= 0xef)
                {
                    count = 2;
                    code = first & 0x0f;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    count = 3;
                    code = first & 0x07;
                }
                else
                    return false;
                if (index + count >= value.size())
                    return false;
                for (std::size_t offset = 1; offset <= count; ++offset)
                {
                    const auto next = static_cast<unsigned char>(value[index + offset]);
                    if ((next & 0xc0) != 0x80)
                        return false;
                    code = (code << 6) | (next & 0x3f);
                }
                if ((count == 2 && (code < 0x800 || (code >= 0xd800 && code <= 0xdfff)))
                    || (count == 3 && (code < 0x10000 || code > 0x10ffff)))
                    return false;
                index += count + 1;
            }
            return true;
        }

        std::optional<std::uint64_t> unsignedValue(std::string_view value, std::uint64_t maximum)
        {
            std::uint64_t result = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                || result > maximum)
                return std::nullopt;
            return result;
        }

        std::optional<std::int64_t> signedValue(std::string_view value)
        {
            std::int64_t result = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return std::nullopt;
            return result;
        }

        std::optional<std::vector<Position3>> spawnPositions(std::string_view value)
        {
            std::vector<Position3> result;
            std::size_t begin = 0;
            while (begin <= value.size())
            {
                const auto end = value.find(';', begin);
                const auto point = value.substr(begin, (end == std::string_view::npos ? value.size() : end) - begin);
                const auto first = point.find(':');
                const auto second
                    = first == std::string_view::npos ? std::string_view::npos : point.find(':', first + 1);
                if (first == std::string_view::npos || second == std::string_view::npos
                    || point.find(':', second + 1) != std::string_view::npos)
                    return std::nullopt;
                const auto x = signedValue(trim(point.substr(0, first)));
                const auto y = signedValue(trim(point.substr(first + 1, second - first - 1)));
                const auto z = signedValue(trim(point.substr(second + 1)));
                if (!x || !y || !z || result.size() >= 64)
                    return std::nullopt;
                result.emplace_back(*x, *y, *z);
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return result.empty() ? std::nullopt : std::optional<std::vector<Position3>>(std::move(result));
        }

        ConfigError error(ConfigErrorCode code, std::size_t line = 0, std::string_view key = {})
        {
            return ConfigError{ code, line, std::string(key) };
        }
    }

    ConfigParseResult parseServerConfig(std::string_view text)
    {
        if (text.empty())
            return error(ConfigErrorCode::Empty);
        if (text.size() > MaximumConfigBytes)
            return error(ConfigErrorCode::TooLarge);
        if (!validUtf8(text))
            return error(ConfigErrorCode::InvalidUtf8);

        std::array<bool, 17> seen{};
        std::string bindAddress;
        std::uint16_t port = 0;
        std::uint64_t tick = 0;
        std::uint64_t grace = 0;
        std::filesystem::path passwordPath;
        std::optional<ContentManifestId> contentManifestId;
        std::optional<std::vector<CellSpaceDeclaration>> cellSpaces;
        std::optional<std::vector<CellId>> allowedCells;
        std::optional<CellId> spawnCell;
        std::vector<Position3> configuredSpawnPositions{ Position3(10, 20, 30) };
        std::optional<AppearanceId> defaultAppearanceId;
        std::optional<MovementProfile> movementProfile;
        std::filesystem::path collisionContentPath;
        std::filesystem::path actorContentPath;
        std::filesystem::path interactiveObjectContentPath;
        std::filesystem::path inventoryContentPath;
        std::filesystem::path playerIdentityPath;
        std::size_t lineNumber = 0;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            ++lineNumber;
            const auto end = text.find('\n', begin);
            const auto length = (end == std::string_view::npos ? text.size() : end) - begin;
            if (length > MaximumConfigLineBytes)
                return error(ConfigErrorCode::LineTooLong, lineNumber);
            const auto line = trim(text.substr(begin, length));
            if (!line.empty() && line.front() != '#')
            {
                const auto equal = line.find('=');
                if (equal == std::string_view::npos || line.find('=', equal + 1) != std::string_view::npos)
                    return error(ConfigErrorCode::MalformedAssignment, lineNumber);
                const auto key = trim(line.substr(0, equal));
                const auto value = trim(line.substr(equal + 1));
                std::size_t slot = seen.size();
                if (key == "bind_address")
                    slot = 0;
                else if (key == "port")
                    slot = 1;
                else if (key == "tick_interval_ms")
                    slot = 2;
                else if (key == "disconnect_grace_ms")
                    slot = 3;
                else if (key == "join_password_file")
                    slot = 4;
                else if (key == "content_manifest_id")
                    slot = 5;
                else if (key == "cell_spaces")
                    slot = 6;
                else if (key == "allowed_cells")
                    slot = 7;
                else if (key == "spawn_cell")
                    slot = 8;
                else if (key == "default_appearance_id")
                    slot = 9;
                else if (key == "movement_profile")
                    slot = 10;
                else if (key == "collision_content_file")
                    slot = 11;
                else if (key == "actor_content_file")
                    slot = 12;
                else if (key == "player_identity_file")
                    slot = 13;
                else if (key == "spawn_positions")
                    slot = 14;
                else if (key == "interactive_object_content_file")
                    slot = 15;
                else if (key == "inventory_content_file")
                    slot = 16;
                else
                    return error(ConfigErrorCode::UnknownKey, lineNumber, key);
                if (seen[slot])
                    return error(ConfigErrorCode::DuplicateKey, lineNumber, key);
                if (value.empty() || value.find('#') != std::string_view::npos
                    || value.find("${") != std::string_view::npos)
                    return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                seen[slot] = true;
                if (slot == 0)
                    bindAddress.assign(value);
                else if (slot == 1)
                {
                    const auto parsed = unsignedValue(value, 65535);
                    if (!parsed || *parsed == 0)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    port = static_cast<std::uint16_t>(*parsed);
                }
                else if (slot == 2)
                {
                    const auto parsed = unsignedValue(value, 1000);
                    if (!parsed || *parsed == 0)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    tick = *parsed;
                }
                else if (slot == 3)
                {
                    const auto parsed = unsignedValue(value, 600000);
                    if (!parsed)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    grace = *parsed;
                }
                else if (slot == 4)
                {
                    if (value.size() > MaximumPasswordPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    passwordPath = std::filesystem::u8path(value);
                }
                else if (slot == 5)
                {
                    contentManifestId = ContentManifestId::fromHex(value);
                    if (!contentManifestId)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                }
                else if (slot == 6)
                {
                    cellSpaces = parseCellSpaceDeclarations(value);
                    if (!cellSpaces)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                }
                else if (slot == 7)
                {
                    allowedCells = parseContentCells(value);
                    if (!allowedCells)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                }
                else if (slot == 8)
                {
                    auto parsed = parseContentCells(value);
                    if (!parsed || parsed->size() != 1)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    spawnCell = parsed->front();
                }
                else if (slot == 9)
                {
                    const auto parsed = unsignedValue(value, std::numeric_limits<std::uint64_t>::max());
                    if (!parsed || *parsed == 0)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    defaultAppearanceId = AppearanceId::fromValue(*parsed);
                }
                else if (slot == 10)
                {
                    movementProfile = parseMovementProfile(value);
                    if (!movementProfile)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                }
                else if (slot == 11)
                {
                    if (value.size() > MaximumCollisionContentPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    collisionContentPath = std::filesystem::u8path(value);
                }
                else if (slot == 12)
                {
                    if (value.size() > MaximumActorContentPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    actorContentPath = std::filesystem::u8path(value);
                }
                else if (slot == 13)
                {
                    if (value.size() > MaximumIdentityPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    playerIdentityPath = std::filesystem::u8path(value);
                }
                else if (slot == 14)
                {
                    auto parsed = spawnPositions(value);
                    if (!parsed)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    configuredSpawnPositions = std::move(*parsed);
                }
                else if (slot == 15)
                {
                    if (value.size() > MaximumInteractiveObjectContentPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    interactiveObjectContentPath = std::filesystem::u8path(value);
                }
                else
                {
                    if (value.size() > MaximumInventoryContentPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    inventoryContentPath = std::filesystem::u8path(value);
                }
            }
            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        for (std::size_t slot = 0; slot < 14; ++slot)
            if (!seen[slot])
                return error(ConfigErrorCode::MissingKey, 0,
                    std::array<std::string_view, 14>{ "bind_address", "port", "tick_interval_ms", "disconnect_grace_ms",
                        "join_password_file", "content_manifest_id", "cell_spaces", "allowed_cells", "spawn_cell",
                        "default_appearance_id", "movement_profile", "collision_content_file", "actor_content_file",
                        "player_identity_file" }[slot]);
        auto endpoint = ListenerEndpoint::create(bindAddress, port);
        if (!endpoint)
            return error(ConfigErrorCode::InvalidValue, 0, "bind_address");
        auto manifest = ContentManifest::create(
            *contentManifestId, *cellSpaces, *allowedCells, *defaultAppearanceId, *movementProfile);
        if (!manifest || !manifest->contains(*spawnCell))
            return error(ConfigErrorCode::InvalidValue, 0, "content_manifest_id");
        return ServerConfig{ std::move(*endpoint), tick, grace, std::move(passwordPath), *manifest, *spawnCell,
            std::move(configuredSpawnPositions), std::move(collisionContentPath), std::move(actorContentPath),
            std::move(interactiveObjectContentPath), std::move(inventoryContentPath), std::move(playerIdentityPath) };
    }

    PasswordLoadResult loadJoinPassword(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return error(ConfigErrorCode::PasswordFileUnavailable, 0, "join_password_file");
        std::vector<std::byte> bytes;
        bytes.reserve(MaximumAuthenticationMaterialBytes + 2);
        char value = 0;
        while (stream.get(value))
        {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
            if (bytes.size() > MaximumAuthenticationMaterialBytes + 1)
                return error(ConfigErrorCode::PasswordFileTooLarge, 0, "join_password_file");
        }
        if (!stream.eof())
            return error(ConfigErrorCode::PasswordFileUnavailable, 0, "join_password_file");
        if (!bytes.empty() && bytes.back() == std::byte{ '\n' })
        {
            bytes.pop_back();
            if (!bytes.empty() && bytes.back() == std::byte{ '\r' })
                bytes.pop_back();
        }
        auto material = AuthenticationMaterial::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        if (!material)
            return error(ConfigErrorCode::InvalidPassword, 0, "join_password_file");
        return std::move(*material);
    }

    std::string describeConfigError(const ConfigError& errorValue)
    {
        static constexpr std::array names{ "empty configuration", "configuration too large", "invalid UTF-8",
            "line too long", "malformed assignment", "unknown key", "duplicate key", "missing key", "invalid value",
            "password file unavailable", "password file too large", "invalid password" };
        std::string result = names[static_cast<std::size_t>(errorValue.code)];
        if (errorValue.line != 0)
            result += " at line " + std::to_string(errorValue.line);
        if (!errorValue.key.empty())
            result += " for key " + errorValue.key;
        return result;
    }
}
