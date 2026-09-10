#ifndef TES3MP_AUTHENTICATED_JOIN_HPP
#define TES3MP_AUTHENTICATED_JOIN_HPP

#include "canonical_state.hpp"
#include "combat_world.hpp"
#include "inventory_world.hpp"
#include "player_identity.hpp"
#include "protocol_exchange.hpp"
#include "server_command_reducer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    enum class AuthenticatedJoinError : std::uint8_t
    {
        DuplicatePrincipal,
        CapacityExhausted,
        IdentityExhausted,
        CanonicalStateRejected,
        SnapshotRejected,
        PreparationPending,
        StalePreparation,
    };

    struct AuthenticatedJoinIdentitySeed
    {
        SessionId nextSession;
        PlayerId nextPlayer;
        EntityId nextEntity;
    };

    class PreparedCharacterCreation
    {
    public:
        PreparedCharacterCreation(PreparedCharacterCreation&&) noexcept = default;
        PreparedCharacterCreation& operator=(PreparedCharacterCreation&&) noexcept = default;
        PreparedCharacterCreation(const PreparedCharacterCreation&) = delete;
        PreparedCharacterCreation& operator=(const PreparedCharacterCreation&) = delete;

        const CharacterProfile& profile() const noexcept { return mProfile; }
        const CanonicalServerState& candidateState() const noexcept
        {
            return mSafePoint ? mSafePoint->candidateState() : *mBaseState;
        }
        CanonicalRevision candidateRevision() const noexcept
        {
            return mSafePoint ? mSafePoint->candidateRevision() : mBaseRevision;
        }
        const std::optional<CanonicalInventoryWorld>& candidateInventory() const noexcept { return mInventory; }
        const std::optional<CanonicalCombatWorld>& candidateCombat() const noexcept { return mCombat; }

    private:
        friend class AuthenticatedJoinCoordinator;
        PreparedCharacterCreation() = default;
        std::uint64_t mIdentityPreparation = 0;
        CharacterProfile mProfile = CharacterProfile::fresh();
        const CanonicalServerState* mBaseState = nullptr;
        CanonicalRevision mBaseRevision = CanonicalRevision::initial();
        std::optional<CanonicalCommandReducer::PreparedLifecycle> mSafePoint;
        CanonicalInventoryWorld* mInventoryTarget = nullptr;
        CanonicalCombatWorld* mCombatTarget = nullptr;
        std::optional<CanonicalInventoryWorld> mInventory;
        std::optional<CanonicalCombatWorld> mCombat;
    };

    using CharacterCreationPrepareOutcome = std::variant<PreparedCharacterCreation, CharacterProfileError>;

    struct AuthenticatedJoinResult
    {
        PrincipalId principal;
        SessionId session;
        PlayerId player;
        EntityId entity;
        LatestWinsSnapshot initialSnapshot;
        CharacterLifecycle characterLifecycle = CharacterLifecycle::NewCharacter;
        CharacterProfileRevision profileRevision = CharacterProfileRevision::initial();
        CharacterProfile characterProfile = CharacterProfile::fresh();
    };

    struct AuthenticatedJoinPreparation
    {
        std::uint64_t id;
        AuthenticatedJoinResult join;
    };

    using AuthenticatedJoinOutcome = std::variant<AuthenticatedJoinResult, AuthenticatedJoinError>;
    using AuthenticatedJoinPrepareOutcome = std::variant<AuthenticatedJoinPreparation, AuthenticatedJoinError>;

    class AuthenticatedJoinCoordinator
    {
    public:
        static std::optional<AuthenticatedJoinCoordinator> create(Transform spawn, AppearanceId appearance,
            AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer);
        static std::optional<AuthenticatedJoinCoordinator> create(Transform spawn, ContentManifest contentManifest,
            SessionId nextSession, PlayerIdentityRegistry& playerIdentities, CanonicalCommandReducer& reducer);
        static std::optional<AuthenticatedJoinCoordinator> create(std::span<const Transform> spawns,
            ContentManifest contentManifest, SessionId nextSession, PlayerIdentityRegistry& playerIdentities,
            CanonicalCommandReducer& reducer);

        AuthenticatedJoinOutcome join(PrincipalId principal, SessionGeneration generation, ServerTick serverTick);
        AuthenticatedJoinPrepareOutcome prepare(PrincipalId principal, SessionGeneration generation,
            ServerTick serverTick, std::optional<PlayerCredential> credential = std::nullopt,
            std::string username = {});
        AuthenticatedJoinPrepareOutcome prepareReattach(PrincipalId principal,
            AuthenticatedAdmission::PlayerClaim claim, SessionGeneration generation, ServerTick serverTick);
        AuthenticatedJoinOutcome commit(std::uint64_t preparationId, const CanonicalInventoryWorld* inventory = nullptr,
            const CanonicalCombatWorld* combat = nullptr);
        bool cancel(std::uint64_t preparationId) noexcept;
        const CanonicalServerState* candidateState(std::uint64_t preparationId) const noexcept;
        std::optional<CanonicalRevision> candidateRevision(std::uint64_t preparationId) const noexcept;
        std::optional<CanonicalStateVersion> candidateStateVersion(std::uint64_t preparationId) const noexcept;
        std::optional<PlayerCredential> copyPendingPlayerCredential(std::uint64_t preparationId) const noexcept;
        bool pendingCreatesPersistentIdentity(std::uint64_t preparationId) const noexcept;
        bool releasePrincipal(PrincipalId principal) noexcept;
        bool persistPlayer(PlayerId player) noexcept;
        bool persistPlayers(std::span<const PlayerId> players) noexcept;
        const CharacterProfile* characterProfile(PlayerId player) const noexcept;
        CharacterCreationPrepareOutcome prepareCharacterCreation(PlayerId player,
            const CharacterContentCatalog& catalog, const CharacterCreationCommand& command, ServerTick tick,
            CanonicalInventoryWorld* inventory = nullptr, CanonicalCombatWorld* combat = nullptr,
            const CanonicalPlayerCombatTemplate* playerCombatTemplate = nullptr,
            const ItemPrototypeCatalog* itemCatalog = nullptr) noexcept;
        bool commitCharacterCreation(PreparedCharacterCreation&& prepared) noexcept;
        bool cancelCharacterCreation(PreparedCharacterCreation&& prepared) noexcept;
        CharacterProfileApplyResult applyCharacterCreation(PlayerId player, const CharacterContentCatalog& catalog,
            const CharacterCreationCommand& command, ServerTick tick) noexcept;

        const CanonicalServerState& state() const noexcept { return mReducer.state(); }
        std::size_t liveBindings() const noexcept { return mPrincipals.size(); }

    private:
        AuthenticatedJoinCoordinator(std::vector<Transform> spawns, AppearanceId appearance,
            AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer, ContentManifest contentManifest,
            PlayerIdentityRegistry* playerIdentities) noexcept;
        AuthenticatedJoinPrepareOutcome prepareIdentity(PrincipalId principal,
            AuthenticatedAdmission::PlayerClaim claim, bool createsIdentity,
            std::optional<std::uint64_t> identityPreparation, SessionGeneration generation, ServerTick serverTick,
            std::optional<PrincipalId> replacedPrincipal = std::nullopt);

        std::vector<Transform> mSpawns;
        AppearanceId mAppearance;
        AuthenticatedJoinIdentitySeed mSeed;
        bool mIdentityExhausted = false;
        CanonicalCommandReducer& mReducer;
        ContentManifest mContentManifest;
        PlayerIdentityRegistry* mPlayerIdentities = nullptr;
        struct PrincipalBinding
        {
            PrincipalId principal;
            PlayerId player;
        };
        std::vector<PrincipalBinding> mPrincipals;
        struct PendingJoin
        {
            std::uint64_t id;
            CanonicalCommandReducer::PreparedJoin state;
            AuthenticatedJoinResult result;
            std::optional<std::uint64_t> identityPreparation;
            std::optional<PrincipalId> replacedPrincipal;
        };
        std::optional<PendingJoin> mPending;
        std::uint64_t mNextPreparationId = 1;
    };
}

#endif
