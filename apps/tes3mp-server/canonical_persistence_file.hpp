#ifndef TES3MP_SERVER_CANONICAL_PERSISTENCE_FILE_HPP
#define TES3MP_SERVER_CANONICAL_PERSISTENCE_FILE_HPP

#include <tes3mp/canonical_persistence.hpp>
#include <tes3mp/player_identity.hpp>

#include <filesystem>
#include <memory>
#include <variant>

namespace TES3MP::ServerApp
{
    enum class CanonicalPersistenceFileError : std::uint8_t
    {
        Unavailable,
        TooLarge,
        UnsupportedVersion,
        Truncated,
        Corrupted,
        Malformed,
        IdentityMismatch,
    };

    class CanonicalPersistenceFile final : public CanonicalDurabilityPort
    {
    public:
        static std::variant<std::unique_ptr<CanonicalPersistenceFile>, CanonicalPersistenceFileError> open(
            std::filesystem::path path, CanonicalPersistenceIdentity identity) noexcept;

        const CanonicalDurablePrefix& prefix() const noexcept { return mPrefix; }
        std::optional<CanonicalServerState> restoredState() const noexcept;
        std::optional<CanonicalStateVersion> restoredStateVersion() const noexcept;
        std::optional<CanonicalRevision> restoredCanonicalRevision() const noexcept;
        std::optional<ServerTick> restoredCheckpointTick() const noexcept;
        const CanonicalDurableInventoryState* restoredInventory() const noexcept;
        const CanonicalDurableCombatState* restoredCombat() const noexcept;
        bool bindPlayerIdentities(const PlayerIdentityRegistry& identities) noexcept;

        CanonicalDurabilityResult commit(const std::shared_ptr<const CanonicalStatePublication>& candidate,
            CanonicalRevision canonicalRevision, std::span<const DurableCommandOrder> commands,
            const CanonicalInventoryWorld* inventory = nullptr,
            const CanonicalCombatWorld* combat = nullptr) noexcept override;

    private:
        CanonicalPersistenceFile(std::filesystem::path path, CanonicalDurablePrefix prefix) noexcept
            : mPath(std::move(path))
            , mPrefix(std::move(prefix))
        {
        }

        std::filesystem::path mPath;
        CanonicalDurablePrefix mPrefix;
        const PlayerIdentityRegistry* mPlayerIdentities = nullptr;
    };
}

#endif
