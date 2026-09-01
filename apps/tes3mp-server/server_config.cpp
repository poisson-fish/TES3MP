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
                if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 0x1f; }
                else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 0x0f; }
                else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 0x07; }
                else return false;
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

        ConfigError error(ConfigErrorCode code, std::size_t line = 0, std::string_view key = {})
        {
            return ConfigError{ code, line, std::string(key) };
        }
    }

    ConfigParseResult parseServerConfig(std::string_view text)
    {
        if (text.empty()) return error(ConfigErrorCode::Empty);
        if (text.size() > MaximumConfigBytes) return error(ConfigErrorCode::TooLarge);
        if (!validUtf8(text)) return error(ConfigErrorCode::InvalidUtf8);

        std::array<bool, 5> seen{};
        std::string bindAddress;
        std::uint16_t port = 0;
        std::uint64_t tick = 0;
        std::uint64_t grace = 0;
        std::filesystem::path passwordPath;
        std::size_t lineNumber = 0;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            ++lineNumber;
            const auto end = text.find('\n', begin);
            const auto length = (end == std::string_view::npos ? text.size() : end) - begin;
            if (length > MaximumConfigLineBytes) return error(ConfigErrorCode::LineTooLong, lineNumber);
            const auto line = trim(text.substr(begin, length));
            if (!line.empty() && line.front() != '#')
            {
                const auto equal = line.find('=');
                if (equal == std::string_view::npos || line.find('=', equal + 1) != std::string_view::npos)
                    return error(ConfigErrorCode::MalformedAssignment, lineNumber);
                const auto key = trim(line.substr(0, equal));
                const auto value = trim(line.substr(equal + 1));
                std::size_t slot = seen.size();
                if (key == "bind_address") slot = 0;
                else if (key == "port") slot = 1;
                else if (key == "tick_interval_ms") slot = 2;
                else if (key == "disconnect_grace_ms") slot = 3;
                else if (key == "join_password_file") slot = 4;
                else return error(ConfigErrorCode::UnknownKey, lineNumber, key);
                if (seen[slot]) return error(ConfigErrorCode::DuplicateKey, lineNumber, key);
                if (value.empty() || value.find('#') != std::string_view::npos
                    || value.find("${") != std::string_view::npos)
                    return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                seen[slot] = true;
                if (slot == 0) bindAddress.assign(value);
                else if (slot == 1)
                {
                    const auto parsed = unsignedValue(value, 65535);
                    if (!parsed || *parsed == 0) return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    port = static_cast<std::uint16_t>(*parsed);
                }
                else if (slot == 2)
                {
                    const auto parsed = unsignedValue(value, 1000);
                    if (!parsed || *parsed == 0) return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    tick = *parsed;
                }
                else if (slot == 3)
                {
                    const auto parsed = unsignedValue(value, 600000);
                    if (!parsed) return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    grace = *parsed;
                }
                else
                {
                    if (value.size() > MaximumPasswordPathBytes)
                        return error(ConfigErrorCode::InvalidValue, lineNumber, key);
                    passwordPath = std::filesystem::u8path(value);
                }
            }
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        for (std::size_t slot = 0; slot < seen.size(); ++slot)
            if (!seen[slot])
                return error(ConfigErrorCode::MissingKey, 0,
                    std::array<std::string_view, 5>{ "bind_address", "port", "tick_interval_ms",
                        "disconnect_grace_ms", "join_password_file" }[slot]);
        auto endpoint = ListenerEndpoint::create(bindAddress, port);
        if (!endpoint) return error(ConfigErrorCode::InvalidValue, 0, "bind_address");
        return ServerConfig{ std::move(*endpoint), tick, grace, std::move(passwordPath) };
    }

    PasswordLoadResult loadJoinPassword(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return error(ConfigErrorCode::PasswordFileUnavailable, 0, "join_password_file");
        std::vector<std::byte> bytes;
        bytes.reserve(MaximumAuthenticationMaterialBytes + 2);
        char value = 0;
        while (stream.get(value))
        {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
            if (bytes.size() > MaximumAuthenticationMaterialBytes + 1)
                return error(ConfigErrorCode::PasswordFileTooLarge, 0, "join_password_file");
        }
        if (!stream.eof()) return error(ConfigErrorCode::PasswordFileUnavailable, 0, "join_password_file");
        if (!bytes.empty() && bytes.back() == std::byte{'\n'})
        {
            bytes.pop_back();
            if (!bytes.empty() && bytes.back() == std::byte{'\r'}) bytes.pop_back();
        }
        auto material = AuthenticationMaterial::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        if (!material) return error(ConfigErrorCode::InvalidPassword, 0, "join_password_file");
        return std::move(*material);
    }

    std::string describeConfigError(const ConfigError& errorValue)
    {
        static constexpr std::array names{ "empty configuration", "configuration too large", "invalid UTF-8",
            "line too long", "malformed assignment", "unknown key", "duplicate key", "missing key",
            "invalid value", "password file unavailable", "password file too large", "invalid password" };
        std::string result = names[static_cast<std::size_t>(errorValue.code)];
        if (errorValue.line != 0) result += " at line " + std::to_string(errorValue.line);
        if (!errorValue.key.empty()) result += " for key " + errorValue.key;
        return result;
    }
}
