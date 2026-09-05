#include "player_identity_file.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    constexpr std::string_view Header = "TES3MP_PLAYER_IDENTITIES_V1";
    constexpr std::size_t MaximumFileBytes = 64 * 1024;

    std::optional<std::uint64_t> number(std::string_view value) noexcept
    {
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0)
            return std::nullopt;
        return result;
    }

    std::optional<TES3MP::CredentialDigest> digestFromHex(std::string_view value) noexcept
    {
        if (value.size() != TES3MP::CredentialDigestBytes * 2)
            return std::nullopt;
        const auto nibble = [](char digit) -> std::optional<std::uint8_t> {
            if (digit >= '0' && digit <= '9') return static_cast<std::uint8_t>(digit - '0');
            if (digit >= 'a' && digit <= 'f') return static_cast<std::uint8_t>(digit - 'a' + 10);
            if (digit >= 'A' && digit <= 'F') return static_cast<std::uint8_t>(digit - 'A' + 10);
            return std::nullopt;
        };
        TES3MP::CredentialDigest result;
        for (std::size_t index = 0; index < result.bytes.size(); ++index)
        {
            const auto high = nibble(value[index * 2]);
            const auto low = nibble(value[index * 2 + 1]);
            if (!high || !low)
                return std::nullopt;
            result.bytes[index] = static_cast<std::byte>((*high << 4u) | *low);
        }
        return result;
    }

    std::string hex(std::span<const std::byte> bytes)
    {
        constexpr char Digits[] = "0123456789abcdef";
        std::string result(bytes.size() * 2, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto value = std::to_integer<std::uint8_t>(bytes[index]);
            result[index * 2] = Digits[value >> 4u];
            result[index * 2 + 1] = Digits[value & 0x0fu];
        }
        return result;
    }

    bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
    {
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
    }
}

namespace TES3MP::ServerApp
{
    std::variant<std::unique_ptr<PlayerIdentityFile>, PlayerIdentityFileError> PlayerIdentityFile::open(
        std::filesystem::path path) noexcept
    try
    {
        if (path.empty())
            return PlayerIdentityFileError::Unavailable;
        if (!std::filesystem::exists(path))
            return std::unique_ptr<PlayerIdentityFile>(new PlayerIdentityFile(std::move(path), {}));
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > MaximumFileBytes)
            return PlayerIdentityFileError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        std::string line;
        if (!stream || !std::getline(stream, line) || line != Header)
            return PlayerIdentityFileError::Malformed;
        std::vector<PersistedPlayerIdentity> records;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;
            std::istringstream fields(line);
            std::array<std::string, 5> values;
            std::string extra;
            if (!(fields >> values[0] >> values[1] >> values[2] >> values[3] >> values[4]) || fields >> extra)
                return PlayerIdentityFileError::Malformed;
            const auto player = number(values[0]);
            const auto entity = number(values[1]);
            const auto appearance = number(values[2]);
            const auto manifest = ContentManifestId::fromHex(values[3]);
            const auto digest = digestFromHex(values[4]);
            if (!player || !entity || !appearance || !manifest || !digest)
                return PlayerIdentityFileError::Malformed;
            records.push_back({ { *PlayerId::fromValue(*player), *EntityId::fromValue(*entity),
                *AppearanceId::fromValue(*appearance), *manifest }, *digest });
            if (records.size() > MaximumPlayerIdentityRecords)
                return PlayerIdentityFileError::TooLarge;
        }
        if (!stream.eof())
            return PlayerIdentityFileError::Unavailable;
        return std::unique_ptr<PlayerIdentityFile>(new PlayerIdentityFile(std::move(path), std::move(records)));
    }
    catch (...)
    {
        return PlayerIdentityFileError::Unavailable;
    }

    bool PlayerIdentityFile::replace(std::span<const PersistedPlayerIdentity> records) noexcept
    try
    {
        if (records.size() > MaximumPlayerIdentityRecords)
            return false;
        auto temporary = mPath;
        temporary += ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            stream << Header << '\n';
            for (const auto& record : records)
                stream << record.claim.player.value() << ' ' << record.claim.entity.value() << ' '
                       << record.claim.appearance.value() << ' ' << hex(record.claim.contentManifest.bytes()) << ' '
                       << hex(record.credentialDigest.bytes) << '\n';
            stream.flush();
            if (!stream)
                return false;
        }
        if (!replaceFile(temporary, mPath))
            return false;
        mRecords.assign(records.begin(), records.end());
        return true;
    }
    catch (...)
    {
        return false;
    }
}
