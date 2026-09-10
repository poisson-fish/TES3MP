#include "player_profile_manager.hpp"

#include <tes3mp/authentication.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    constexpr std::uintmax_t MaximumProfileFileBytes = 64 * 1024;

    bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
    {
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
    }

    bool equalIgnoreCase(std::string_view a, std::string_view b) noexcept
    {
        return std::ranges::equal(a, b, [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
        });
    }

    std::string escapeJson(std::string_view input)
    {
        std::string result;
        result.reserve(input.size() + 8);
        for (char c : input)
        {
            if (c == '"')
                result += "\\\"";
            else if (c == '\\')
                result += "\\\\";
            else if (c == '\n')
                result += "\\n";
            else if (c == '\r')
                result += "\\r";
            else if (c == '\t')
                result += "\\t";
            else
                result += c;
        }
        return result;
    }

    std::string unescapeJson(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            if (input[i] == '\\' && i + 1 < input.size())
            {
                ++i;
                if (input[i] == 'n') result += '\n';
                else if (input[i] == 'r') result += '\r';
                else if (input[i] == 't') result += '\t';
                else result += input[i];
            }
            else
                result += input[i];
        }
        return result;
    }

    std::optional<std::array<std::byte, TES3MP::OpenMWAdapter::PlayerProfileCredentialBytes>> credentialFromHex(
        std::string_view value) noexcept
    {
        if (value.size() != TES3MP::OpenMWAdapter::PlayerProfileCredentialBytes * 2)
            return std::nullopt;
        const auto nibble = [](char digit) -> std::optional<unsigned char> {
            if (digit >= '0' && digit <= '9') return static_cast<unsigned char>(digit - '0');
            if (digit >= 'a' && digit <= 'f') return static_cast<unsigned char>(digit - 'a' + 10);
            if (digit >= 'A' && digit <= 'F') return static_cast<unsigned char>(digit - 'A' + 10);
            return std::nullopt;
        };
        std::array<std::byte, TES3MP::OpenMWAdapter::PlayerProfileCredentialBytes> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const auto high = nibble(value[index * 2]);
            const auto low = nibble(value[index * 2 + 1]);
            if (!high || !low)
                return std::nullopt;
            result[index] = static_cast<std::byte>((*high << 4u) | *low);
        }
        return result;
    }

    std::string credentialHex(std::span<const std::byte> bytes)
    {
        constexpr char Digits[] = "0123456789abcdef";
        std::string result(bytes.size() * 2, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto value = std::to_integer<unsigned char>(bytes[index]);
            result[index * 2] = Digits[value >> 4u];
            result[index * 2 + 1] = Digits[value & 0x0fu];
        }
        return result;
    }

    inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept
    {
        return (x >> n) | (x << (32 - n));
    }

    inline std::uint32_t choose(std::uint32_t e, std::uint32_t f, std::uint32_t g) noexcept
    {
        return (e & f) ^ (~e & g);
    }

    inline std::uint32_t majority(std::uint32_t a, std::uint32_t b, std::uint32_t c) noexcept
    {
        return (a & b) ^ (a & c) ^ (b & c);
    }

    inline std::uint32_t sig0(std::uint32_t x) noexcept
    {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }

    inline std::uint32_t sig1(std::uint32_t x) noexcept
    {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }

    inline std::uint32_t theta0(std::uint32_t x) noexcept
    {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }

    inline std::uint32_t theta1(std::uint32_t x) noexcept
    {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }

    constexpr std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    void sha256Transform(std::uint32_t state[8], const std::uint8_t block[64]) noexcept
    {
        std::uint32_t w[64];
        for (std::size_t i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
                 | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
                 | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
                 | (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i)
        {
            w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t i = 0; i < 64; ++i)
        {
            const std::uint32_t t1 = h + sig1(e) + choose(e, f, g) + K[i] + w[i];
            const std::uint32_t t2 = sig0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::array<std::byte, 32> internalSha256(std::span<const std::uint8_t> input) noexcept
    {
        std::uint32_t state[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };

        const std::size_t totalBytes = input.size();
        const std::uint64_t totalBits = static_cast<std::uint64_t>(totalBytes) * 8;

        std::size_t offset = 0;
        while (offset + 64 <= totalBytes)
        {
            sha256Transform(state, input.data() + offset);
            offset += 64;
        }

        std::uint8_t buffer[128] = {0};
        const std::size_t remaining = totalBytes - offset;
        if (remaining > 0)
            std::copy_n(input.data() + offset, remaining, buffer);
        buffer[remaining] = 0x80;

        const std::size_t padBlockLen = (remaining < 56) ? 64 : 128;
        for (int i = 7; i >= 0; --i)
        {
            buffer[padBlockLen - 8 + (7 - i)] = static_cast<std::uint8_t>((totalBits >> (i * 8)) & 0xff);
        }

        sha256Transform(state, buffer);
        if (padBlockLen == 128)
            sha256Transform(state, buffer + 64);

        std::array<std::byte, 32> digest{};
        for (std::size_t i = 0; i < 8; ++i)
        {
            digest[i * 4]     = static_cast<std::byte>((state[i] >> 24) & 0xff);
            digest[i * 4 + 1] = static_cast<std::byte>((state[i] >> 16) & 0xff);
            digest[i * 4 + 2] = static_cast<std::byte>((state[i] >> 8) & 0xff);
            digest[i * 4 + 3] = static_cast<std::byte>(state[i] & 0xff);
        }
        return digest;
    }
}

namespace TES3MP::OpenMWAdapter
{
    static_assert(PlayerProfileCredentialBytes == PlayerCredentialBytes);

    std::array<std::byte, 32> computeCredentialHash(
        std::string_view username, std::string_view password) noexcept
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(username.size() + 1 + password.size());
        for (char c : username)
            bytes.push_back(static_cast<std::uint8_t>(std::tolower(static_cast<unsigned char>(c))));
        bytes.push_back(static_cast<std::uint8_t>(':'));
        for (char c : password)
            bytes.push_back(static_cast<std::uint8_t>(c));
        return internalSha256(bytes);
    }

    std::array<std::byte, PlayerProfileCredentialBytes> deriveEndpointCredential(
        std::span<const std::byte> profileCredential, std::string_view normalizedHost,
        std::uint16_t port) noexcept
    {
        constexpr std::string_view Domain = "tes3mp-endpoint-credential-v1:";
        std::vector<std::uint8_t> bytes;
        bytes.reserve(Domain.size() + profileCredential.size() + normalizedHost.size() + 8);
        for (const char value : Domain)
            bytes.push_back(static_cast<std::uint8_t>(value));
        for (const std::byte value : profileCredential)
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        bytes.push_back(0);
        for (const unsigned char value : normalizedHost)
            bytes.push_back(static_cast<std::uint8_t>(std::tolower(value)));
        bytes.push_back(0);
        bytes.push_back(static_cast<std::uint8_t>(port >> 8u));
        bytes.push_back(static_cast<std::uint8_t>(port & 0xffu));
        return internalSha256(bytes);
    }
    bool PlayerProfileManager::isValidUsername(std::string_view username) noexcept
    {
        return isValidPlayerUsername(username);
    }

    bool PlayerProfileManager::isValidPassword(std::string_view password) noexcept
    {
        return !password.empty() && password.size() <= MaximumProfilePasswordBytes;
    }

    std::optional<PlayerProfile> PlayerProfileManager::activeProfile() const noexcept
    {
        if (mActiveUsername.empty())
            return std::nullopt;
        for (const auto& profile : mProfiles)
        {
            if (equalIgnoreCase(profile.username, mActiveUsername))
                return profile;
        }
        return std::nullopt;
    }

    bool PlayerProfileManager::setActive(std::string_view username) noexcept
    {
        for (const auto& profile : mProfiles)
        {
            if (equalIgnoreCase(profile.username, username))
            {
                mActiveUsername = profile.username;
                return true;
            }
        }
        return false;
    }

    const PlayerProfile* PlayerProfileManager::getProfile(std::string_view username) const noexcept
    {
        for (const auto& profile : mProfiles)
        {
            if (equalIgnoreCase(profile.username, username))
                return &profile;
        }
        return nullptr;
    }

    bool PlayerProfileManager::addProfile(std::string_view username, std::string_view password) noexcept
    {
        if (!isValidUsername(username) || !isValidPassword(password)
            || mProfiles.size() >= MaximumLocalPlayerProfiles)
            return false;
        for (const auto& profile : mProfiles)
        {
            if (equalIgnoreCase(profile.username, username))
                return false;
        }
        mProfiles.push_back({ std::string(username), computeCredentialHash(username, password) });
        mActiveUsername = std::string(username);
        return true;
    }

    bool PlayerProfileManager::deleteProfile(std::string_view username) noexcept
    {
        const auto it = std::find_if(mProfiles.begin(), mProfiles.end(), [&](const auto& profile) {
            return equalIgnoreCase(profile.username, username);
        });
        if (it == mProfiles.end())
            return false;

        const bool wasActive = equalIgnoreCase(it->username, mActiveUsername);
        mProfiles.erase(it);

        if (wasActive)
            mActiveUsername = mProfiles.empty() ? std::string{} : mProfiles.front().username;

        return true;
    }

    bool PlayerProfileManager::load(const std::filesystem::path& path) noexcept
    try
    {
        if (path.empty() || !std::filesystem::exists(path)
            || !std::filesystem::is_regular_file(path)
            || std::filesystem::file_size(path) > MaximumProfileFileBytes)
            return false;

        std::ifstream stream(path);
        if (!stream)
            return false;

        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

        std::vector<PlayerProfile> profiles;
        std::string activeUsername;

        // Simple and robust JSON parser for profile records
        auto findStringValue = [&](std::size_t startPos, std::string_view key,
                                   std::size_t endPos = std::string::npos) -> std::pair<std::string, std::size_t> {
            if (endPos == std::string::npos)
                endPos = content.size();
            const std::string pattern = "\"" + std::string(key) + "\"";
            const auto keyPos = content.find(pattern, startPos);
            if (keyPos == std::string::npos || keyPos >= endPos)
                return { {}, std::string::npos };
            const auto colonPos = content.find(':', keyPos + pattern.size());
            if (colonPos == std::string::npos || colonPos >= endPos)
                return { {}, std::string::npos };
            const auto quoteStart = content.find('"', colonPos + 1);
            if (quoteStart == std::string::npos || quoteStart >= endPos)
                return { {}, std::string::npos };
            std::size_t quoteEnd = quoteStart + 1;
            while (quoteEnd < endPos)
            {
                if (content[quoteEnd] == '"')
                {
                    std::size_t slashes = 0;
                    for (std::size_t index = quoteEnd; index > quoteStart + 1 && content[index - 1] == '\\'; --index)
                        ++slashes;
                    if (slashes % 2 == 0)
                        break;
                }
                ++quoteEnd;
            }
            if (quoteEnd >= endPos)
                return { {}, std::string::npos };
            return { unescapeJson(content.substr(quoteStart + 1, quoteEnd - quoteStart - 1)), quoteEnd + 1 };
        };

        const auto [activeName, _] = findStringValue(0, "active");
        activeUsername = activeName;

        const auto profilesPos = content.find("\"profiles\"");
        if (profilesPos != std::string::npos)
        {
            std::size_t currentPos = profilesPos;
            while (currentPos < content.size())
            {
                const auto [user, userEnd] = findStringValue(currentPos, "username");
                if (userEnd == std::string::npos)
                    break;
                const auto objectEnd = content.find('}', userEnd);
                if (objectEnd == std::string::npos)
                    return false;
                const auto [credentialText, credentialEnd] = findStringValue(userEnd, "credential", objectEnd);
                if (credentialEnd != std::string::npos)
                {
                    auto credential = credentialFromHex(credentialText);
                    if (!isValidUsername(user) || !credential
                        || profiles.size() >= MaximumLocalPlayerProfiles
                        || std::ranges::any_of(profiles,
                            [&](const auto& profile) { return equalIgnoreCase(profile.username, user); }))
                        return false;
                    profiles.push_back({ user, *credential });
                    currentPos = credentialEnd;
                    continue;
                }

                // One-time migration from the initial plaintext profile format.
                const auto [password, passwordEnd] = findStringValue(userEnd, "password", objectEnd);
                if (passwordEnd == std::string::npos || !isValidUsername(user) || !isValidPassword(password)
                    || profiles.size() >= MaximumLocalPlayerProfiles
                    || std::ranges::any_of(profiles,
                        [&](const auto& profile) { return equalIgnoreCase(profile.username, user); }))
                    return false;
                profiles.push_back({ user, computeCredentialHash(user, password) });
                currentPos = passwordEnd;
            }
        }

        if (!profiles.empty())
        {
            const auto active = std::ranges::find_if(profiles,
                [&](const auto& profile) { return equalIgnoreCase(profile.username, activeUsername); });
            activeUsername = active == profiles.end() ? profiles.front().username : active->username;
        }
        else
            activeUsername.clear();

        mProfiles = std::move(profiles);
        mActiveUsername = std::move(activeUsername);

        return true;
    }
    catch (...)
    {
        return false;
    }

    bool PlayerProfileManager::save(const std::filesystem::path& path) const noexcept
    try
    {
        if (path.empty())
            return false;

        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;

        auto temporary = path;
        temporary += ".tmp";

        {
            std::ofstream stream(temporary, std::ios::trunc);
            if (!stream)
                return false;

            stream << "{\n";
            stream << "  \"active\": \"" << escapeJson(mActiveUsername) << "\",\n";
            stream << "  \"profiles\": [\n";
            for (std::size_t i = 0; i < mProfiles.size(); ++i)
            {
                stream << "    {\n";
                stream << "      \"username\": \"" << escapeJson(mProfiles[i].username) << "\",\n";
                stream << "      \"credential\": \"" << credentialHex(mProfiles[i].credential) << "\"\n";
                stream << "    }" << (i + 1 < mProfiles.size() ? ",\n" : "\n");
            }
            stream << "  ]\n";
            stream << "}\n";
            stream.flush();
            if (!stream)
                return false;
        }

        return replaceFile(temporary, path);
    }
    catch (...)
    {
        return false;
    }
}
