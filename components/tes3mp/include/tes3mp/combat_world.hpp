#ifndef TES3MP_COMBAT_WORLD_HPP
#define TES3MP_COMBAT_WORLD_HPP

#include "actor_simulation.hpp"
#include "canonical_state.hpp"
#include "deterministic_random.hpp"
#include "inventory_world.hpp"
#include "melee_combat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumPlayerCombatants = 256;
    inline constexpr std::size_t MaximumActorCombatants = MaximumActorCatalogEntries;

    enum class MeleeWeaponSkill : std::uint8_t
    {
        ShortBlade = 0,
        LongBlade = 1,
        BluntWeapon = 2,
        Axe = 3,
        Spear = 4,
        Count = 5,
    };

    struct MeleeWeaponProfile
    {
        ItemPrototypeId prototypeId;
        MeleeWeaponSkill skill = MeleeWeaponSkill::ShortBlade;
        float chopMinimum = 0.f;
        float chopMaximum = 0.f;
        float slashMinimum = 0.f;
        float slashMaximum = 0.f;
        float thrustMinimum = 0.f;
        float thrustMaximum = 0.f;
        float weight = 0.f;
        float reach = 0.f;
        bool normalWeapon = true;

        friend constexpr bool operator==(MeleeWeaponProfile, MeleeWeaponProfile) noexcept = default;
    };

    class MeleeWeaponCatalog
    {
    public:
        static std::optional<MeleeWeaponCatalog> create(
            const ItemPrototypeCatalog& items, std::span<const MeleeWeaponProfile> profiles) noexcept;

        ContentManifestId contentManifestId() const noexcept { return mContentManifestId; }
        std::span<const MeleeWeaponProfile> profiles() const noexcept { return mProfiles; }
        const MeleeWeaponProfile* find(ItemPrototypeId id) const noexcept;

        friend bool operator==(const MeleeWeaponCatalog&, const MeleeWeaponCatalog&) noexcept = default;

    private:
        MeleeWeaponCatalog(ContentManifestId contentManifestId, std::vector<MeleeWeaponProfile> profiles) noexcept
            : mContentManifestId(contentManifestId), mProfiles(std::move(profiles)) {}

        ContentManifestId mContentManifestId;
        std::vector<MeleeWeaponProfile> mProfiles;
    };

    struct CanonicalPlayerCombatTemplate
    {
        OpenMwMeleeAttacker stats;
        std::array<float, static_cast<std::size_t>(MeleeWeaponSkill::Count)> weaponSkills{};
        std::uint64_t maximumEncumbranceWeightUnits = 1;

        friend constexpr bool operator==(const CanonicalPlayerCombatTemplate&,
            const CanonicalPlayerCombatTemplate&) noexcept = default;
    };

    struct CanonicalPlayerCombatState
    {
        PlayerId playerId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeAttacker stats;
        std::array<float, static_cast<std::size_t>(MeleeWeaponSkill::Count)> weaponSkills{};
        std::uint64_t maximumEncumbranceWeightUnits = 1;
        std::optional<ServerTick> lastAttackTick;

        friend constexpr bool operator==(const CanonicalPlayerCombatState&,
            const CanonicalPlayerCombatState&) noexcept = default;
    };

    struct CanonicalActorCombatState
    {
        ActorId actorId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeVictim stats;

        friend constexpr bool operator==(const CanonicalActorCombatState&,
            const CanonicalActorCombatState&) noexcept = default;
    };

    enum class CanonicalCombatWorldErrorCode : std::uint8_t
    {
        PlayerLimitExceeded,
        ActorLimitExceeded,
        PlayersNotStrictlySorted,
        ActorsNotStrictlySorted,
        InvalidStat,
    };

    struct CanonicalCombatWorldError
    {
        CanonicalCombatWorldErrorCode code;
        std::size_t index = 0;

        friend constexpr bool operator==(CanonicalCombatWorldError, CanonicalCombatWorldError) noexcept = default;
    };

    class CanonicalCombatWorld
    {
    public:
        std::span<const CanonicalPlayerCombatState> players() const noexcept { return mPlayers; }
        std::span<const CanonicalActorCombatState> actors() const noexcept { return mActors; }
        const CanonicalPlayerCombatState* findPlayer(PlayerId id) const noexcept;
        const CanonicalActorCombatState* findActor(ActorId id) const noexcept;
        RandomStateV1 randomState() const noexcept { return mRandomState; }
        bool ensurePlayer(PlayerId id, const CanonicalPlayerCombatTemplate& source,
            std::uint64_t inventoryWeightUnits) noexcept;
        bool advancePlayerInventoryBinding(PlayerId id, std::uint64_t inventoryWeightUnits) noexcept;

        friend bool operator==(const CanonicalCombatWorld&, const CanonicalCombatWorld&) noexcept = default;

    private:
        friend std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
            std::span<const CanonicalPlayerCombatState>, std::span<const CanonicalActorCombatState>, RandomStateV1);
        CanonicalCombatWorld(std::vector<CanonicalPlayerCombatState> players,
            std::vector<CanonicalActorCombatState> actors, RandomStateV1 randomState) noexcept
            : mPlayers(std::move(players)), mActors(std::move(actors)), mRandomState(randomState) {}

        std::vector<CanonicalPlayerCombatState> mPlayers;
        std::vector<CanonicalActorCombatState> mActors;
        RandomStateV1 mRandomState;
    };

    std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
        std::span<const CanonicalPlayerCombatState> players, std::span<const CanonicalActorCombatState> actors,
        RandomStateV1 randomState);

    enum class MeleeContactValidation : std::uint8_t
    {
        Accepted,
        NoContact,
        HistoryUnavailable,
    };

    struct ServerMeleeContactRequest
    {
        PlayerId attacker;
        ActorId target;
        ServerTick sourceTick;
        MeleeAttackType attackType;
        std::optional<OpenMwMeleeWeapon> weapon;
        std::optional<float> weaponReach;
    };

    class ServerMeleeContactQuery
    {
    public:
        virtual ~ServerMeleeContactQuery() = default;
        virtual MeleeContactValidation validate(const ServerMeleeContactRequest& request,
            const CanonicalPlayerEntityState& attacker, const CanonicalActorEntityState& target) noexcept = 0;
    };

    struct MeleeAuthorityPolicy
    {
        std::uint64_t minimumAttackIntervalTicks = 1;
        std::uint64_t maximumRewindTicks = 8;
    };

    struct AuthoritativeMeleeAttack
    {
        PlayerId attacker;
        std::optional<ActorId> target;
        CombatRevision expectedAttackerRevision = CombatRevision::initial();
        CombatRevision expectedTargetRevision = CombatRevision::initial();
        ServerTick sourceTick = ServerTick::initial();
        MeleeAttackType attackType = MeleeAttackType::Chop;
        float attackStrength = 0.f;
        bool blocked = false;
        bool strengthInfluencesHandToHand = false;
        float werewolfClawMultiplier = 1.f;
    };

    enum class AuthoritativeMeleeDisposition : std::uint8_t
    {
        Applied,
        UnknownAttacker,
        UnknownTarget,
        StaleAttackerRevision,
        StaleTargetRevision,
        SpatialIdentityMismatch,
        DifferentCell,
        FutureSourceTick,
        RewindWindowExceeded,
        RateLimited,
        InvalidAttempt,
        NoContact,
        HistoryUnavailable,
        RevisionExhausted,
    };

    struct AuthoritativeMeleeEvent
    {
        ServerTick serverTick = ServerTick::initial();
        PlayerId attacker;
        ActorId target;
        CombatRevision attackerRevision = CombatRevision::initial();
        CombatRevision targetRevision = CombatRevision::initial();
        OpenMwMeleeResolution resolution;
    };

    struct PreparedMeleeAttack
    {
        AuthoritativeMeleeDisposition disposition = AuthoritativeMeleeDisposition::InvalidAttempt;
        std::optional<CanonicalCombatWorld> candidate;
        std::optional<CanonicalInventoryWorld> candidateInventory;
        std::optional<AuthoritativeMeleeEvent> event;
    };

    PreparedMeleeAttack prepareAuthoritativeMeleeAttack(const CanonicalCombatWorld& combat,
        const CanonicalInventoryWorld& inventory, const ItemPrototypeCatalog& items,
        const MeleeWeaponCatalog& weapons, const CanonicalServerState& players,
        const CanonicalActorWorld& actors, const OpenMwMeleeSettings& settings, MeleeAuthorityPolicy policy,
        ServerMeleeContactQuery& contact, ServerTick serverTick, const AuthoritativeMeleeAttack& attack) noexcept;
}

#endif
