#ifndef TES3MP_COMBAT_WORLD_HPP
#define TES3MP_COMBAT_WORLD_HPP

#include "actor_simulation.hpp"
#include "canonical_state.hpp"
#include "character_profile.hpp"
#include "deterministic_random.hpp"
#include "direct_magic.hpp"
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
    inline constexpr std::size_t MaximumAuthoritativeActorMeleeEventsPerTick = MaximumActorCombatants;

    enum class CombatProgressionSkill : std::uint8_t
    {
        Block = 0,
        ShortBlade = 1,
        LongBlade = 2,
        BluntWeapon = 3,
        Axe = 4,
        Spear = 5,
        HandToHand = 6,
        LightArmor = 7,
        MediumArmor = 8,
        HeavyArmor = 9,
        Unarmored = 10,
        Count = 11,
    };

    struct CombatSkillProgressionRule
    {
        ClassSpecialization specialization = ClassSpecialization::Combat;
        float useGain = 0.f;

        friend constexpr bool operator==(CombatSkillProgressionRule,
            CombatSkillProgressionRule) noexcept = default;
    };

    struct CombatSkillProgressionState
    {
        float progress = 0.f;
        float requirementFactor = 1.f;

        friend constexpr bool operator==(CombatSkillProgressionState,
            CombatSkillProgressionState) noexcept = default;
    };

    struct CombatSkillProgressionSettings
    {
        float miscellaneousFactor = 1.f;
        float minorFactor = 1.f;
        float majorFactor = 1.f;
        float specializationFactor = 1.f;

        friend constexpr bool operator==(CombatSkillProgressionSettings,
            CombatSkillProgressionSettings) noexcept = default;
    };

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

    enum class ArmorSkill : std::uint8_t
    {
        LightArmor = 0,
        MediumArmor = 1,
        HeavyArmor = 2,
    };

    struct MeleeArmorProfile
    {
        ItemPrototypeId prototypeId;
        ArmorSkill skill = ArmorSkill::LightArmor;
        float baseArmor = 0.f;

        friend constexpr bool operator==(MeleeArmorProfile, MeleeArmorProfile) noexcept = default;
    };

    class MeleeWeaponCatalog
    {
    public:
        static std::optional<MeleeWeaponCatalog> create(
            const ItemPrototypeCatalog& items, std::span<const MeleeWeaponProfile> profiles,
            std::span<const MeleeArmorProfile> armor = {}) noexcept;

        ContentManifestId contentManifestId() const noexcept { return mContentManifestId; }
        std::span<const MeleeWeaponProfile> profiles() const noexcept { return mProfiles; }
        const MeleeWeaponProfile* find(ItemPrototypeId id) const noexcept;
        const MeleeArmorProfile* findArmor(ItemPrototypeId id) const noexcept;

        friend bool operator==(const MeleeWeaponCatalog&, const MeleeWeaponCatalog&) noexcept = default;

    private:
        MeleeWeaponCatalog(ContentManifestId contentManifestId, std::vector<MeleeWeaponProfile> profiles,
            std::vector<MeleeArmorProfile> armor) noexcept
            : mContentManifestId(contentManifestId), mProfiles(std::move(profiles)), mArmor(std::move(armor)) {}

        ContentManifestId mContentManifestId;
        std::vector<MeleeWeaponProfile> mProfiles;
        std::vector<MeleeArmorProfile> mArmor;
    };

    struct CanonicalPlayerCombatTemplate
    {
        OpenMwMeleeAttacker stats;
        std::array<float, static_cast<std::size_t>(MeleeWeaponSkill::Count)> weaponSkills{};
        float blockSkill = 0.f;
        std::uint64_t maximumEncumbranceWeightUnits = 1;
        OpenMwMeleeVictim victim;
        float maximumHealth = 0.f;
        float maximumFatigue = 0.f;
        float magicka = 0.f;
        float maximumMagicka = 0.f;
        float healthRecoveryPerSecond = 0.f;
        float magickaRecoveryPerSecond = 0.f;
        std::array<CombatSkillProgressionRule,
            static_cast<std::size_t>(CombatProgressionSkill::Count)> skillRules{};
        CombatSkillProgressionSettings skillSettings;
        std::array<CombatSkillProgressionState,
            static_cast<std::size_t>(CombatProgressionSkill::Count)> skillProgression{};
        float intelligence = 1.f;
        std::array<float, 4> armorSkills{};
        DirectMagicDefense magicDefense;

        friend constexpr bool operator==(const CanonicalPlayerCombatTemplate&,
            const CanonicalPlayerCombatTemplate&) noexcept = default;
    };

    struct CanonicalPlayerCombatState
    {
        PlayerId playerId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeAttacker stats;
        std::array<float, static_cast<std::size_t>(MeleeWeaponSkill::Count)> weaponSkills{};
        float blockSkill = 0.f;
        std::uint64_t maximumEncumbranceWeightUnits = 1;
        std::optional<ServerTick> lastAttackTick;
        std::optional<CharacterProfileRevision> initializedCharacterProfile = std::nullopt;
        OpenMwMeleeVictim victim;
        OpenMwMeleeVictim respawnVictim;
        std::optional<ServerTick> deathTick;
        float maximumHealth = 0.f;
        float maximumFatigue = 0.f;
        float magicka = 0.f;
        float maximumMagicka = 0.f;
        float healthRecoveryPerSecond = 0.f;
        float magickaRecoveryPerSecond = 0.f;
        std::array<CombatSkillProgressionRule,
            static_cast<std::size_t>(CombatProgressionSkill::Count)> skillRules{};
        std::array<CombatSkillProgressionState,
            static_cast<std::size_t>(CombatProgressionSkill::Count)> skillProgression{};
        std::array<float, 4> armorSkills{};
        DirectMagicDefense magicDefense;
        std::vector<SpellRecordId> contractedDiseases;

        friend constexpr bool operator==(const CanonicalPlayerCombatState&,
            const CanonicalPlayerCombatState&) noexcept = default;
    };

    struct CanonicalActorCombatState
    {
        ActorId actorId;
        CombatRevision revision = CombatRevision::initial();
        OpenMwMeleeVictim stats;
        OpenMwMeleeVictim respawnStats;
        OpenMwMeleeAttacker attacker;
        std::optional<OpenMwMeleeWeapon> naturalWeapon;
        std::uint32_t attackReachQuanta = 0;
        std::optional<PlayerId> aggressionTarget;
        std::optional<ServerTick> lastAttackTick;
        std::optional<ServerTick> deathTick;
        float maximumHealth = 0.f;
        float maximumFatigue = 0.f;
        bool creature = false;
        DirectMagicDefense magicDefense;

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
        std::optional<ServerTick> lastSimulationTick() const noexcept { return mLastSimulationTick; }
        bool ensurePlayer(PlayerId id, const CanonicalPlayerCombatTemplate& source,
            std::uint64_t inventoryWeightUnits) noexcept;
        bool initializePlayerFromCharacter(PlayerId id, const CanonicalPlayerCombatTemplate& source,
            std::uint64_t inventoryWeightUnits, CharacterProfileRevision profileRevision) noexcept;
        bool advancePlayerInventoryBinding(PlayerId id, std::uint64_t inventoryWeightUnits) noexcept;

        friend bool operator==(const CanonicalCombatWorld&, const CanonicalCombatWorld&) noexcept = default;

    private:
        friend std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
            std::span<const CanonicalPlayerCombatState>, std::span<const CanonicalActorCombatState>, RandomStateV1,
            std::optional<ServerTick>);
        CanonicalCombatWorld(std::vector<CanonicalPlayerCombatState> players,
            std::vector<CanonicalActorCombatState> actors, RandomStateV1 randomState,
            std::optional<ServerTick> lastSimulationTick) noexcept
            : mPlayers(std::move(players)), mActors(std::move(actors)), mRandomState(randomState),
              mLastSimulationTick(lastSimulationTick) {}

        std::vector<CanonicalPlayerCombatState> mPlayers;
        std::vector<CanonicalActorCombatState> mActors;
        RandomStateV1 mRandomState;
        std::optional<ServerTick> mLastSimulationTick;
    };

    std::variant<CanonicalCombatWorld, CanonicalCombatWorldError> createCanonicalCombatWorld(
        std::span<const CanonicalPlayerCombatState> players, std::span<const CanonicalActorCombatState> actors,
        RandomStateV1 randomState, std::optional<ServerTick> lastSimulationTick = std::nullopt);
    std::optional<CanonicalPlayerCombatTemplate> deriveCharacterCombatTemplate(
        const CharacterProfile& profile, const CanonicalPlayerCombatTemplate& base,
        const CharacterContentCatalog* characterContent = nullptr) noexcept;

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
        // OpenMW world coordinates use 1,024 canonical quanta per unit. This
        // is the stock 128-unit melee distance before weapon reach scaling.
        std::uint32_t baseReachQuanta = 128 * 1024;
        std::int16_t difficulty = 0;
    };

    class ServerMeleeContactHistory : public ServerMeleeContactQuery
    {
    public:
        virtual bool capture(ServerTick tick, const CanonicalServerState& players,
            const CanonicalActorWorld& actors) noexcept = 0;
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

    struct AuthoritativeActorMeleeEvent
    {
        ServerTick serverTick = ServerTick::initial();
        ActorId attacker;
        PlayerId target;
        CombatRevision attackerRevision = CombatRevision::initial();
        CombatRevision targetRevision = CombatRevision::initial();
        OpenMwMeleeResolution resolution;
    };

    struct CombatSimulationPolicy
    {
        std::uint64_t minimumActorAttackIntervalTicks = 63;
        std::uint64_t respawnDelayTicks = 1875;
        float secondsPerTick = 0.016f;
        std::int16_t difficulty = 0;
    };

    enum class CombatSimulationErrorCode : std::uint8_t
    {
        TickRegression,
        RevisionExhausted,
        InvalidWorld,
        EventLimitExceeded,
    };

    struct CombatSimulationError
    {
        CombatSimulationErrorCode code = CombatSimulationErrorCode::InvalidWorld;
        std::size_t index = 0;
    };

    struct CombatSimulationStep
    {
        CanonicalCombatWorld combat;
        std::optional<CanonicalInventoryWorld> inventory;
        std::vector<AuthoritativeActorMeleeEvent> events;
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
        ServerMeleeContactQuery& contact, ServerTick serverTick, const AuthoritativeMeleeAttack& attack,
        const DirectMagicCatalog* magic = nullptr) noexcept;
    std::variant<CombatSimulationStep, CombatSimulationError> advanceAuthoritativeCombat(
        const CanonicalCombatWorld& combat, const CanonicalInventoryWorld& inventory,
        const ItemPrototypeCatalog& items, const MeleeWeaponCatalog& weapons,
        const CanonicalServerState& players, const CanonicalActorWorld& actors,
        const OpenMwMeleeSettings& settings, CombatSimulationPolicy policy, ServerTick tick,
        const DirectMagicCatalog* magic = nullptr) noexcept;
}

#endif
