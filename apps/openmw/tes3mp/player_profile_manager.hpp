#ifndef OPENMW_TES3MP_PLAYER_PROFILE_MANAGER_HPP
#define OPENMW_TES3MP_PLAYER_PROFILE_MANAGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TES3MP::OpenMWAdapter
{
    inline constexpr std::size_t PlayerProfileCredentialBytes = 32;
    inline constexpr std::size_t MaximumLocalPlayerProfiles = 64;
    inline constexpr std::size_t MaximumProfilePasswordBytes = 256;

    std::array<std::byte, 32> computeCredentialHash(
        std::string_view username, std::string_view password) noexcept;
    std::array<std::byte, PlayerProfileCredentialBytes> deriveEndpointCredential(
        std::span<const std::byte> profileCredential, std::string_view normalizedHost,
        std::uint16_t port) noexcept;

    struct PlayerProfile
    {
        std::string username;
        std::array<std::byte, PlayerProfileCredentialBytes> credential{};

        friend bool operator==(const PlayerProfile&, const PlayerProfile&) noexcept = default;
    };

    class PlayerProfileManager
    {
    public:
        static bool isValidUsername(std::string_view username) noexcept;
        static bool isValidPassword(std::string_view password) noexcept;

        PlayerProfileManager() = default;

        bool load(const std::filesystem::path& path) noexcept;
        bool save(const std::filesystem::path& path) const noexcept;

        const std::vector<PlayerProfile>& profiles() const noexcept { return mProfiles; }
        const std::string& activeUsername() const noexcept { return mActiveUsername; }
        std::optional<PlayerProfile> activeProfile() const noexcept;

        bool setActive(std::string_view username) noexcept;
        const PlayerProfile* getProfile(std::string_view username) const noexcept;
        bool addProfile(std::string_view username, std::string_view password) noexcept;
        bool deleteProfile(std::string_view username) noexcept;
        bool hasProfiles() const noexcept { return !mProfiles.empty(); }

    private:
        std::vector<PlayerProfile> mProfiles;
        std::string mActiveUsername;
    };
}

#endif
