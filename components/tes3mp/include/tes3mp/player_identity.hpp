#ifndef TES3MP_PLAYER_IDENTITY_HPP
#define TES3MP_PLAYER_IDENTITY_HPP

#include "authentication.hpp"
#include "server_authentication.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumPlayerIdentityRecords = 256;

    struct PersistedPlayerIdentity
    {
        AuthenticatedAdmission::PlayerClaim claim;
        CredentialDigest credentialDigest;

        friend constexpr bool operator==(PersistedPlayerIdentity, PersistedPlayerIdentity) noexcept = default;
    };

    class PlayerIdentityPersistence
    {
    public:
        virtual ~PlayerIdentityPersistence() = default;
        virtual bool replace(std::span<const PersistedPlayerIdentity> records) noexcept = 0;
    };

    enum class PlayerIdentityError : std::uint8_t
    {
        Full,
        IdentityExhausted,
        RandomUnavailable,
        DigestUnavailable,
        DigestCollision,
        InvalidInitialState,
        PreparationPending,
        StalePreparation,
        PersistenceFailed,
        RollbackFailed,
    };

    struct PreparedPlayerIdentity
    {
        std::uint64_t id;
        AuthenticatedAdmission::PlayerClaim claim;
    };

    using PlayerIdentityPrepareResult = std::variant<PreparedPlayerIdentity, PlayerIdentityError>;

    class PlayerIdentityRegistry
    {
    public:
        static std::variant<std::unique_ptr<PlayerIdentityRegistry>, PlayerIdentityError> create(
            CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
            std::span<const PersistedPlayerIdentity> initialRecords) noexcept;

        PlayerIdentityPrepareResult prepareCreate(ContentManifest contentManifest) noexcept;
        std::optional<PlayerCredential> copyPreparedCredential(std::uint64_t preparationId) const noexcept;
        bool commit(std::uint64_t preparationId) noexcept;
        bool finalize(std::uint64_t preparationId) noexcept;
        bool rollback(std::uint64_t preparationId) noexcept;
        bool cancel(std::uint64_t preparationId) noexcept;
        std::optional<AuthenticatedAdmission::PlayerClaim> authenticate(
            const PlayerCredential& credential, ContentManifestId contentManifest) noexcept;
        std::span<const PersistedPlayerIdentity> records() const noexcept { return mRecords; }

    private:
        struct Pending
        {
            std::uint64_t id;
            PersistedPlayerIdentity record;
            PlayerCredential credential;
        };
        struct Committed
        {
            std::uint64_t id;
            std::vector<PersistedPlayerIdentity> prior;
            PlayerId priorNextPlayer;
            EntityId priorNextEntity;
            bool priorIdentityExhausted;
        };

        PlayerIdentityRegistry(CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
            std::vector<PersistedPlayerIdentity> records, PlayerId nextPlayer, EntityId nextEntity) noexcept;
        bool digest(const PlayerCredential& credential, CredentialDigest& destination) noexcept;

        CredentialCrypto& mCrypto;
        PlayerIdentityPersistence& mPersistence;
        std::vector<PersistedPlayerIdentity> mRecords;
        PlayerId mNextPlayer;
        EntityId mNextEntity;
        std::optional<Pending> mPending;
        std::optional<Committed> mCommitted;
        std::uint64_t mNextPreparationId = 1;
        bool mIdentityExhausted = false;
    };
}

#endif
