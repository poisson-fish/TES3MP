#ifndef TES3MP_SERVER_PLAYER_IDENTITY_FILE_HPP
#define TES3MP_SERVER_PLAYER_IDENTITY_FILE_HPP

#include <tes3mp/player_identity.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP::ServerApp
{
    enum class PlayerIdentityFileError
    {
        Unavailable,
        TooLarge,
        Malformed,
    };

    class PlayerIdentityFile final : public PlayerIdentityPersistence
    {
    public:
        static std::variant<std::unique_ptr<PlayerIdentityFile>, PlayerIdentityFileError> open(
            std::filesystem::path path) noexcept;

        std::span<const PersistedPlayerIdentity> records() const noexcept { return mRecords; }
        bool replace(std::span<const PersistedPlayerIdentity> records) noexcept override;

    private:
        PlayerIdentityFile(std::filesystem::path path, std::vector<PersistedPlayerIdentity> records) noexcept
            : mPath(std::move(path)), mRecords(std::move(records)) {}

        std::filesystem::path mPath;
        std::vector<PersistedPlayerIdentity> mRecords;
    };
}

#endif
