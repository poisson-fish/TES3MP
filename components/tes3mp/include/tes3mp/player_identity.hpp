#ifndef TES3MP_PLAYER_IDENTITY_HPP
#define TES3MP_PLAYER_IDENTITY_HPP

#include "authentication.hpp"
#include "character_profile.hpp"
#include "canonical_state.hpp"
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
        std::optional<CanonicalPlayerEntityState> savedPlayer;
        CharacterProfile characterProfile = CharacterProfile::fresh();
        std::string username;

        bool hasUsername(std::string_view name) const noexcept;

        friend bool operator==(PersistedPlayerIdentity, PersistedPlayerIdentity) noexcept = default;
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

    struct PreparedCharacterProfile
    {
        std::uint64_t id;
        CharacterProfile profile;
    };

    using CharacterProfilePrepareResult = std::variant<PreparedCharacterProfile, CharacterProfileError>;

    class PlayerIdentityRegistry
    {
    public:
        static std::variant<std::unique_ptr<PlayerIdentityRegistry>, PlayerIdentityError> create(
            CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
            std::span<const PersistedPlayerIdentity> initialRecords,
            std::span<const EntityId> reservedEntityIds = {}) noexcept;

        PlayerIdentityPrepareResult prepareCreate(ContentManifest contentManifest,
            std::optional<PlayerCredential> credential = std::nullopt,
            std::string username = {}) noexcept;
        std::optional<PlayerCredential> copyPreparedCredential(std::uint64_t preparationId) const noexcept;
        bool commit(std::uint64_t preparationId) noexcept;
        bool finalize(std::uint64_t preparationId) noexcept;
        bool rollback(std::uint64_t preparationId) noexcept;
        bool cancel(std::uint64_t preparationId) noexcept;
        bool hasUsername(std::string_view username) const noexcept;
        std::optional<AuthenticatedAdmission::PlayerClaim> authenticate(
            const PlayerCredential& credential, ContentManifestId contentManifest,
            std::string_view username = {}) noexcept;
        const CanonicalPlayerEntityState* savedPlayer(PlayerId player) const noexcept;
        bool savePlayer(const CanonicalPlayerEntityState& player) noexcept;
        bool savePlayers(std::span<const CanonicalPlayerEntityState> players) noexcept;
        const CharacterProfile* characterProfile(PlayerId player) const noexcept;
        bool restartIncompleteCharacter(PlayerId player) noexcept;
        CharacterProfilePrepareResult prepareCharacterCreation(PlayerId player,
            const CharacterContentCatalog& catalog, const CharacterCreationCommand& command,
            const CanonicalPlayerEntityState* completionCheckpoint = nullptr) noexcept;
        bool commitCharacterCreation(std::uint64_t preparationId) noexcept;
        bool finalizeCharacterCreation(std::uint64_t preparationId) noexcept;
        bool rollbackCharacterCreation(std::uint64_t preparationId) noexcept;
        bool cancelCharacterCreation(std::uint64_t preparationId) noexcept;
        CharacterProfileApplyResult applyCharacterCreation(PlayerId player,
            const CharacterContentCatalog& catalog, const CharacterCreationCommand& command,
            const CanonicalPlayerEntityState* completionCheckpoint = nullptr) noexcept;
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
        struct PendingCharacterProfile
        {
            std::uint64_t id;
            std::vector<PersistedPlayerIdentity> prior;
            std::vector<PersistedPlayerIdentity> candidate;
            CharacterProfile profile;
            bool durable = false;
        };
        struct CommittedCharacterProfile
        {
            std::uint64_t id;
            std::vector<PersistedPlayerIdentity> prior;
            bool durable = false;
        };

        PlayerIdentityRegistry(CredentialCrypto& crypto, PlayerIdentityPersistence& persistence,
            std::vector<PersistedPlayerIdentity> records, std::vector<EntityId> reservedEntityIds,
            PlayerId nextPlayer, EntityId nextEntity) noexcept;
        bool advanceIdentities() noexcept;
        bool digest(const PlayerCredential& credential, CredentialDigest& destination) noexcept;

        CredentialCrypto& mCrypto;
        PlayerIdentityPersistence& mPersistence;
        std::vector<PersistedPlayerIdentity> mRecords;
        std::vector<EntityId> mReservedEntityIds;
        PlayerId mNextPlayer;
        EntityId mNextEntity;
        std::optional<Pending> mPending;
        std::optional<Committed> mCommitted;
        std::optional<PendingCharacterProfile> mPendingCharacterProfile;
        std::optional<CommittedCharacterProfile> mCommittedCharacterProfile;
        std::uint64_t mNextPreparationId = 1;
        bool mIdentityExhausted = false;
    };
}

#endif
