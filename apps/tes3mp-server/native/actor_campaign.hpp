#ifndef TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#define TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#include "actor_spawns.hpp"
#include <apps/openmw/mwmechanics/boundequipment.hpp>
#include "melee_animation.hpp"
#include "cast_animation.hpp"
#include <tes3mp/spatial_types.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadskil.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <string>

namespace TES3MP::Native
{
    inline constexpr uint64_t ActorCampaignMagic = 0x3150434154335354;
    inline constexpr uint64_t MeleeActorCampaignMagic = 0x3250434154335354;
    inline constexpr uint64_t ContactActorCampaignMagic = 0x3350434154335354;
    inline constexpr uint64_t CombatActorCampaignMagic = 0x3450434154335354;
    inline constexpr uint64_t LifeActorCampaignMagic = 0x3550434154335354;
    inline constexpr uint64_t ProjectileActorCampaignMagic = 0x3650434154335354;
    inline constexpr uint64_t EnchantedProjectileActorCampaignMagic = 0x3750434154335354;
    inline constexpr uint64_t TimedActorCampaignMagic = 0x3850434154335354;
    inline constexpr uint64_t AreaActorCampaignMagic = 0x3950434154335354;
    inline constexpr uint64_t PlayerTargetActorCampaignMagic = 0x4150434154335354;
    inline constexpr uint64_t MultipleProjectileActorCampaignMagic = 0x4250434154335354;
    inline constexpr uint64_t KnockoutActorCampaignMagic = 0x4350434154335354;
    inline constexpr uint64_t MeleeDefenseActorCampaignMagic = 0x4450434154335354;
    inline constexpr uint64_t EffectActorCampaignMagic = 0x4550434154335354;
    inline constexpr uint64_t ConstantActorCampaignMagic = 0x4650434154335354;
    inline constexpr uint64_t GeneralConstantActorCampaignMagic = 0x4750434154335354;
    inline constexpr uint64_t CasterActorCampaignMagic = 0x4850434154335354;
    inline constexpr uint64_t CastLifecycleCampaignMagic = 0x4950434154335354;
    inline constexpr uint64_t WeaponExecutionCampaignMagic = 0x4a50434154335354;
    inline constexpr uint64_t PlayerSwingCampaignMagic = 0x4b50434154335354;
    inline constexpr uint64_t BowReleaseCampaignMagic = 0x4c50434154335354;
    inline constexpr uint64_t RangedReleaseCampaignMagic = 0x4d50434154335354;
    inline constexpr uint64_t RangedFlightCampaignMagic = 0x4e50434154335354;
    inline constexpr uint64_t KnockoutAnimationCampaignMagic = 0x4f50434154335354;
    inline constexpr uint64_t ExpandedEffectsCampaignMagic = 0x5050434154335354;
    inline constexpr uint64_t ActorPresentationCampaignMagic = 0x5150434154335354;
    inline constexpr uint64_t PlayerCastCampaignMagic = 0x5250434154335354;
    inline constexpr uint64_t PersistentConditionsCampaignMagic = 0x5350434154335354;
    inline constexpr uint64_t SpecialConditionsCampaignMagic = 0x5450434154335354;
    inline constexpr uint64_t MovementEffectsCampaignMagic = 0x5550434154335354;
    inline constexpr uint64_t AiDecisionCampaignMagic = 0x5650434154335354;
    inline constexpr uint64_t PlayerAiCampaignMagic = 0x5750434154335354;
    inline constexpr uint64_t SocialLifecycleCampaignMagic = 0x5850434154335354;
    inline constexpr uint64_t PlacementCombatCampaignMagic = 0x5950434154335354;
    inline constexpr uint64_t NeighborCombatCampaignMagic = 0x5a50434154335354;
    inline constexpr uint64_t NpcRangedCampaignMagic = 0x5b50434154335354;
    inline constexpr uint64_t AuthoritativeAimCampaignMagic = 0x5c50434154335354;
    inline constexpr uint64_t MovementRuleCampaignMagic = 0x5d50434154335354;
    inline constexpr uint64_t PlayerTravelCampaignMagic = 0x5e50434154335354;
    inline constexpr uint64_t TravelRuleCampaignMagic = 0x5f50434154335354;
    inline constexpr uint64_t ObjectTravelCampaignMagic = 0x6050434154335354;
    inline constexpr uint64_t SummonsActorSetMagic = 0x6250434154335354;
    inline constexpr uint64_t EquipmentFamilyCampaignMagic = 0x6150434154335354;
    inline constexpr bool hasObjectTravel(uint64_t magic)
    { return magic == ObjectTravelCampaignMagic || magic == EquipmentFamilyCampaignMagic; }
    inline constexpr bool hasTravelRules(uint64_t magic)
    { return magic == TravelRuleCampaignMagic || hasObjectTravel(magic); }
    inline constexpr bool hasPlayerTravel(uint64_t magic)
    { return magic == PlayerTravelCampaignMagic || hasTravelRules(magic); }
    inline constexpr bool hasMovementRules(uint64_t magic)
    { return magic == MovementRuleCampaignMagic || hasPlayerTravel(magic); }
    inline constexpr bool hasAuthoritativeAim(uint64_t magic)
    { return magic == AuthoritativeAimCampaignMagic || hasMovementRules(magic); }
    inline constexpr bool hasNpcRanged(uint64_t magic)
    { return magic == NpcRangedCampaignMagic || hasAuthoritativeAim(magic); }
    inline constexpr bool hasNeighborCombat(uint64_t magic)
    { return magic == NeighborCombatCampaignMagic || hasNpcRanged(magic); }
    inline constexpr bool hasPlacementCombat(uint64_t magic)
    { return magic == PlacementCombatCampaignMagic || hasNeighborCombat(magic); }
    inline constexpr bool hasSocialLifecycle(uint64_t magic)
    { return magic == SocialLifecycleCampaignMagic || hasPlacementCombat(magic); }
    inline constexpr bool hasPlayerAi(uint64_t magic)
    { return magic == PlayerAiCampaignMagic || hasSocialLifecycle(magic); }
    inline constexpr bool hasAiDecisions(uint64_t magic)
    { return magic == AiDecisionCampaignMagic || hasPlayerAi(magic); }
    inline constexpr bool hasMovementEffects(uint64_t magic)
    { return magic == MovementEffectsCampaignMagic || hasAiDecisions(magic); }
    inline constexpr bool hasPlayerCasts(uint64_t magic)
    { return magic == PlayerCastCampaignMagic || magic == PersistentConditionsCampaignMagic
        || magic == SpecialConditionsCampaignMagic || hasMovementEffects(magic); }
    inline constexpr bool hasActorPresentation(uint64_t magic)
    { return magic == ActorPresentationCampaignMagic || hasPlayerCasts(magic); }
    inline constexpr bool hasExpandedEffects(uint64_t magic)
    { return magic == ExpandedEffectsCampaignMagic || hasActorPresentation(magic); }
    inline constexpr bool hasKnockoutAnimation(uint64_t magic)
    { return magic == KnockoutAnimationCampaignMagic || hasExpandedEffects(magic); }
    inline constexpr bool hasRangedFlight(uint64_t magic)
    { return magic == RangedFlightCampaignMagic || hasKnockoutAnimation(magic); }
    inline constexpr bool hasRangedRelease(uint64_t magic)
    { return magic == BowReleaseCampaignMagic || magic == RangedReleaseCampaignMagic || hasRangedFlight(magic); }
    inline constexpr bool hasPlayerSwings(uint64_t magic)
    { return magic == PlayerSwingCampaignMagic || hasRangedRelease(magic); }
    inline constexpr bool hasWeaponExecution(uint64_t magic)
    { return magic == WeaponExecutionCampaignMagic || hasPlayerSwings(magic); }
    inline constexpr bool hasCastLifecycle(uint64_t magic)
    { return magic == CastLifecycleCampaignMagic || hasWeaponExecution(magic); }
    inline constexpr bool hasCasterState(uint64_t magic)
    { return magic == CasterActorCampaignMagic || hasCastLifecycle(magic); }
    inline constexpr bool hasGeneralConstantState(uint64_t magic)
    { return magic == GeneralConstantActorCampaignMagic || hasCasterState(magic); }
    inline constexpr bool hasConstantState(uint64_t magic)
    { return magic == ConstantActorCampaignMagic || hasGeneralConstantState(magic); }
    inline constexpr bool hasKnockoutState(uint64_t magic)
    { return magic == KnockoutActorCampaignMagic || magic == MeleeDefenseActorCampaignMagic
        || magic == EffectActorCampaignMagic || hasConstantState(magic); }
    inline constexpr size_t MaximumActorProjectiles = 8;
    // Kind 1 is a durable player identity (one life until player respawn exists),
    // kind 2 is a content placement/life. Zero metadata belongs only to legacy images.
    struct ActorCasterIdentity
    {
        uint64_t id = 0, kind = 0, life = 0;
        bool operator==(const ActorCasterIdentity&) const = default;
    };
    inline void validateActorCaster(ActorCasterIdentity caster, uint64_t generation)
    {
        if (!caster.id || (caster.kind != 1 && caster.kind != 2) || !caster.life
            || (caster.kind == 1 ? caster.life != 1 : caster.life > generation))
            throw std::invalid_argument("Native caster kind or life invalid");
    }
    // OpenMW attribute, dynamic and skill StatState<float> fields for both
    // players and the selected NPC. Equipped item condition remains in the
    // nested equipment image, committed with this wrapper.
    struct PlayerSwing
    {
        enum Interruption : uint64_t { None, Disconnected, SourceChanged, Incapacitated, TargetLost };
        uint64_t command = 0, source = 0, targetLife = 0, direction = 0, interruption = None;
        float strength = 0;
        std::string weapon, identity;
        MeleeAnimation::Snapshot state;
        uint64_t ammunition = 0;
        std::string ammoRecord;
        uint64_t target = 0; // V62 placement; earlier campaigns always target the selected NPC.
        std::array<float, 3> aim{}; // V64 frozen world-space flight direction; zero for melee.
        bool pending() const { return interruption == None && state.mPhase != MeleeAnimation::Phase::Complete; }
        bool operator==(const PlayerSwing&) const = default;
    };
    // V47/V48 retain frozen releases. V49 advances durable 60 Hz physical flight.
    // The consumed instance may no longer exist; thrown source == ammunition.
    struct BowProjectile
    {
        uint64_t caster = 0, command = 0, source = 0, ammunition = 0;
        uint64_t target = 0, targetLife = 0, releaseTick = 0;
        float strength = 0;
        std::string weapon, ammoRecord;
        std::array<float, 3> position{}, direction{};
        std::array<float, 3> velocity{};
        float condition = 1;
        uint64_t steps = 0, terminal = 0; // 0 flying, 1 collided, 2 expired.
        uint64_t casterKind = 1, casterLife = 1;
        uint64_t targetKind = 2; // Player targets use kind 1.
        bool operator==(const BowProjectile&) const = default;
    };
    struct ActorCampaignCast
    {
        enum Phase : uint64_t { Selected = 1, Prepared, WindUp, Released, Recovery };
        uint64_t actor = 0, life = 0, cast = 0, sourceKind = 0, source = 0;
        uint64_t targetKind = 0, target = 0, range = 0, elapsed = 0, phase = Selected;
        uint64_t targetLife = 0; // V53 player target generation.
        bool operator==(const ActorCampaignCast&) const = default;
    };
    inline bool advanceCast(ActorCampaignCast& cast, const CastAnimation& timing)
    {
        if (cast.phase >= ActorCampaignCast::Released)
        { ++cast.elapsed; cast.phase = ActorCampaignCast::Recovery; }
        else if (cast.phase == ActorCampaignCast::Selected) cast.phase = ActorCampaignCast::Prepared;
        else if (cast.phase == ActorCampaignCast::Prepared) cast.phase = ActorCampaignCast::WindUp;
        else if (++cast.elapsed >= timing.releaseTicks)
        { cast.phase = ActorCampaignCast::Released; return true; }
        return false;
    }
    struct ActorCampaignCombat
    {
        struct NeighborAttack
        {
            std::string identity;
            std::string weapon;
            MeleeAnimation::Snapshot state;
            uint64_t target = 0, action = 0, source = 0, direction = 0;
            bool contact = false;
            uint64_t targetKind = 0, targetLife = 0;
            bool operator==(const NeighborAttack&) const = default;
        };
        struct PlayerAi
        {
            struct Faction
            {
                ESM::RefId id;
                int rank = 0;
                bool expelled = false;
                bool operator==(const Faction&) const = default;
            };
            std::vector<Faction> factions;
            int bounty = 0, crimeDisposition = 0;
            uint64_t drawState = 0;
            bool werewolf = false, knownWerewolf = false;
            std::optional<std::array<float, ESM::Skill::Length>> normalSkills;
            std::optional<std::array<float, ESM::Attribute::Length>> normalAttributes;
            struct CrimeEngagement
            {
                uint64_t witness = 0, tick = 0;
                int fight = 0;
                bool operator==(const CrimeEngagement&) const = default;
            };
            std::vector<CrimeEngagement> engagements;
            ESM::RefId selectedSpell;
            uint64_t selectedEnchantedItem = 0;
            std::optional<Transform> mark;
            bool operator==(const PlayerAi&) const = default;
        };
        std::array<PlayerAi, 2> players;
        struct ConditionSource
        {
            uint64_t actor = 0, source = 0;
            uint64_t nextWorseningMs = 0, lastObservedMs = 0, worsenings = 0;
            bool operator==(const ConditionSource&) const = default;
        };
        static constexpr size_t MaximumConditionSources = 128;
        std::vector<ConditionSource> conditions;
        static constexpr size_t StatCount = 8 + 3 + 27;
        using Stats = std::array<std::array<float, 5>, StatCount>;
        // Players occupy indices 0/1. V61 NPC indices follow this ordered,
        // placement-keyed domain; index 2 remains the selected legacy actor.
        std::vector<uint64_t> npcPlacements;
        std::vector<NeighborAttack> neighborAttacks;
        std::vector<Stats> actors = std::vector<Stats>(3);
        uint32_t rng = 1;
        bool levitationEnabled = true;
        bool teleportingEnabled = true;
        std::vector<bool> knockedDown = std::vector<bool>(3);
        std::vector<uint32_t> knockoutFrame = std::vector<uint32_t>(3);
        std::vector<bool> hitKnockdown = std::vector<bool>(3);
        // Remaining CPU hit animation frames; paused for inactive actors.
        std::vector<uint32_t> hitRecoveryTicks = std::vector<uint32_t>(3);
        std::array<std::optional<PlayerSwing>, 2> swings;
        std::vector<BowProjectile> arrows;
        std::array<std::optional<ActorCampaignCast>, 2> playerCasts;
        std::array<std::string, 2> playerCastResources;
        uint64_t npcAction = 0;
        // A stock flee decision lasts through its blind-run interval. The
        // destination and expiry commit with combat and navigation state.
        uint64_t fleeTarget = 0, fleeUntil = 0;
        std::array<float, 3> fleeDestination{};
        std::vector<uint64_t> bodyAction = std::vector<uint64_t>(3), hitGroup = std::vector<uint64_t>(3);
        bool operator==(const ActorCampaignCombat&) const = default;
    };
    struct ActorCampaignMelee
    {
        std::string identity;
        MeleeAnimation::Snapshot state;
        uint64_t target = 0;
        bool contact = false;
    };
    struct ActorDeathEvent
    {
        uint64_t life = 0;
        uint64_t tick = 0;
        uint64_t killer = 0;
        uint64_t killerKind = 0, killerLife = 0;
        bool operator==(const ActorDeathEvent&) const = default;
    };
    struct ActorCampaignLife
    {
        uint64_t generation = 1;
        uint64_t bornTick = 0;
        uint64_t respawnTick = 0;
        std::array<std::array<float, 5>, ActorCampaignCombat::StatCount> spawnStats{};
        std::vector<char> spawnActor;
        std::vector<char> spawnInventory;
        // Complete life-indexed attribution, bounded by the enclosing save's byte
        // budget rather than a gameplay death count. Never evict reward identities.
        std::vector<ActorDeathEvent> deaths;
    };
    struct ActorCampaignProjectile
    {
        uint64_t caster = 0, source = 0, target = 0, generation = 0, expiresTick = 0;
        uint64_t sourceKind = 0, effectSource = 0;
        std::array<float, 3> position{}, step{};
        uint64_t targetKind = 2; // Older campaign images target the selected actor.
        uint64_t commandId = 0; // V32 identifies a pending cast across retries.
        uint64_t casterKind = 0, casterLife = 0;
        bool operator==(const ActorCampaignProjectile&) const = default;
    };
    struct ActorCampaignTimedEffect
    {
        uint64_t actor = 0;
        float magnitude = 0;
        uint64_t expiresTick = 0;
        // V35: ESM effect index and durable launch identity. The old image
        // carries only Resist Magicka; its zero index is decoded as that effect.
        // sourceKind: 0 cast spell, 1 used item, 2 strike item, 3 equipment,
        // 4 condition, 5 authored passive ability.
        uint64_t effectIndex = 0, caster = 0, source = 0, sourceKind = 0;
        float resistance = 0;
        uint64_t startTick = 0, durationTicks = 0;
        // 0: no argument; 1..8: attribute; 9..35: skill. Ordinal preserves duplicate effects.
        uint64_t argument = 0, ordinal = 0;
        uint64_t casterKind = 0, casterLife = 0;
        uint64_t beneficiary = 0; // V51: combat index + 1; 0 if the caster life is unavailable.
        bool equipmentApplied = false;
        std::array<MWMechanics::BoundEquipmentItem, 2> boundItems{};
        bool operator==(const ActorCampaignTimedEffect&) const = default;
    };
    inline constexpr size_t MaximumActorTimedEffects = 512;
    struct ActorCampaign
    {
        std::span<const char> inventory, actor;
        uint64_t tick;
        std::array<float, 3> velocity;
        std::optional<ActorCampaignMelee> melee;
        std::optional<ActorCampaignCombat> combat;
        std::optional<ActorCampaignLife> life;
        std::vector<ActorCampaignLife> neighborLives;
        std::optional<ActorCampaignProjectile> projectile;
        std::vector<ActorCampaignProjectile> projectiles;
        std::vector<ActorCampaignTimedEffect> timedEffects;
        std::string castResource;
        std::optional<ActorCampaignCast> casting;
        std::span<const char> dynamicActors;
    };
    inline ActorCampaign readActorCampaign(std::span<const char> bytes, bool dynamicDomain = false)
    {
        size_t offset = 0;
        const auto magic = getAreaWord(bytes, offset);
        if (magic == SummonsActorSetMagic)
        {
            if (dynamicDomain) throw std::invalid_argument("Nested dynamic actor envelope");
            const auto size = getAreaWord(bytes, offset);
            if (size > 256 * 1024 || size > bytes.size() - offset)
                throw std::invalid_argument("Dynamic actor envelope length invalid");
            const auto actors = bytes.subspan(offset, size_t(size));
            offset += size_t(size);
            size_t inner = offset;
            if (getAreaWord(bytes, inner) != EquipmentFamilyCampaignMagic)
                throw std::invalid_argument("Dynamic actors require equipment campaign");
            auto result = readActorCampaign(bytes.subspan(offset), true);
            result.dynamicActors = actors;
            return result;
        }
        if (magic != ActorCampaignMagic && magic != MeleeActorCampaignMagic
            && magic != ContactActorCampaignMagic && magic != CombatActorCampaignMagic
            && magic != LifeActorCampaignMagic && magic != ProjectileActorCampaignMagic
            && magic != EnchantedProjectileActorCampaignMagic && magic != TimedActorCampaignMagic
            && magic != AreaActorCampaignMagic && magic != PlayerTargetActorCampaignMagic
            && magic != MultipleProjectileActorCampaignMagic && !hasKnockoutState(magic))
            throw std::invalid_argument("Native actor campaign version invalid");
        const auto inventorySize = getAreaWord(bytes, offset), actorSize = getAreaWord(bytes, offset), tick = getAreaWord(bytes, offset);
        std::array<float, 3> velocity;
        for (float& value : velocity)
        {
            const auto bits = getAreaWord(bytes, offset);
            value = std::bit_cast<float>(uint32_t(bits));
            if (bits > UINT32_MAX || !std::isfinite(value) || std::abs(value) > 4096)
                throw std::invalid_argument("Native actor velocity invalid");
        }
        std::optional<ActorCampaignMelee> melee;
        if (magic == MeleeActorCampaignMagic || magic == ContactActorCampaignMagic
            || magic == CombatActorCampaignMagic || magic == LifeActorCampaignMagic
            || magic == ProjectileActorCampaignMagic || magic == EnchantedProjectileActorCampaignMagic
            || magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
            || hasKnockoutState(magic))
        {
            const auto length = getAreaWord(bytes, offset);
            if (!length || length > 512 || length > bytes.size() - offset)
                throw std::invalid_argument("Native melee resource identity length invalid");
            ActorCampaignMelee value;
            value.identity.assign(bytes.data() + offset, size_t(length));
            offset += size_t(length);
            if (value.identity.find('\0') != std::string::npos)
                throw std::invalid_argument("Native melee resource identity invalid");
            const auto phase = getAreaWord(bytes, offset);
            if (phase > uint64_t(MeleeAnimation::Phase::Complete))
                throw std::invalid_argument("Native melee phase invalid");
            value.state.mPhase = MeleeAnimation::Phase(phase);
            const auto time = getAreaWord(bytes, offset), strength = getAreaWord(bytes, offset);
            const auto released = getAreaWord(bytes, offset), hit = getAreaWord(bytes, offset);
            if (time > UINT32_MAX || strength > UINT32_MAX || released > 1 || hit > 1)
                throw std::invalid_argument("Native melee state bits invalid");
            value.state.mTime = std::bit_cast<float>(uint32_t(time));
            value.state.mStrength = std::bit_cast<float>(uint32_t(strength));
            value.state.mReleased = bool(released); value.state.mHit = bool(hit);
            if (!std::isfinite(value.state.mTime) || !std::isfinite(value.state.mStrength))
                throw std::invalid_argument("Native melee state nonfinite");
            if (magic == ContactActorCampaignMagic || magic == CombatActorCampaignMagic
                || magic == LifeActorCampaignMagic || magic == ProjectileActorCampaignMagic
                || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
                || hasKnockoutState(magic))
            {
                value.target = getAreaWord(bytes, offset);
                const auto contact = getAreaWord(bytes, offset);
                if (contact > 1 || (contact && (!value.target || !value.state.mHit))
                    || (value.state.mReleased && !value.target)
                    || (!hasWeaponExecution(magic) && value.state.mReleased != bool(value.target)))
                    throw std::invalid_argument("Native melee contact state invalid");
                value.contact = bool(contact);
            }
            melee = std::move(value);
        }
        std::optional<ActorCampaignCombat> combat;
        if (magic == CombatActorCampaignMagic || magic == LifeActorCampaignMagic
            || magic == ProjectileActorCampaignMagic || magic == EnchantedProjectileActorCampaignMagic
            || magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
            || hasKnockoutState(magic))
        {
            auto& state = combat.emplace();
            const auto rng = getAreaWord(bytes, offset);
            if (rng < 1 || rng > 2147483646)
                throw std::invalid_argument("Native combat RNG state invalid");
            state.rng = uint32_t(rng);
            if (hasMovementRules(magic))
            {
                const auto enabled = getAreaWord(bytes, offset);
                if (enabled > 1) throw std::invalid_argument("Native Levitate world rule invalid");
                state.levitationEnabled = enabled != 0;
            }
            if (hasTravelRules(magic))
            {
                const auto enabled = getAreaWord(bytes, offset);
                if (enabled > 1) throw std::invalid_argument("Native teleport world rule invalid");
                state.teleportingEnabled = enabled != 0;
            }
            if (hasPlacementCombat(magic))
            {
                const auto count = getAreaWord(bytes, offset);
                if (count < 1 || count > (dynamicDomain ? 37u : 8u) || count > (bytes.size() - offset) / 8)
                    throw std::invalid_argument("Native NPC placement count invalid");
                state.npcPlacements.reserve(size_t(count));
                for (uint64_t i = 0; i < count; ++i)
                {
                    const auto placement = getAreaWord(bytes, offset);
                    if (!placement || std::ranges::find(state.npcPlacements, placement) != state.npcPlacements.end())
                        throw std::invalid_argument("Native NPC placement identity invalid");
                    state.npcPlacements.push_back(placement);
                }
                const size_t slots = 2 + size_t(count);
                state.actors.resize(slots);
                state.knockedDown.resize(slots);
                state.knockoutFrame.resize(slots);
                state.hitKnockdown.resize(slots);
                state.hitRecoveryTicks.resize(slots);
                state.bodyAction.resize(slots);
                state.hitGroup.resize(slots);
            }
            for (auto& actor : state.actors)
                for (auto& stat : actor)
                    for (float& value : stat)
                    {
                        const auto bits = getAreaWord(bytes, offset);
                        if (bits > UINT32_MAX) throw std::invalid_argument("Native combat stat bits invalid");
                        value = std::bit_cast<float>(uint32_t(bits));
                        if (!std::isfinite(value) || std::abs(value) > 1'000'000)
                            throw std::invalid_argument("Native combat stat invalid");
                    }
            if (hasKnockoutState(magic))
                for (size_t actor = 0; actor < state.knockedDown.size(); ++actor)
                {
                    const auto value = getAreaWord(bytes, offset);
                    const bool expected = state.actors[actor][8][2] > 0
                        && MWMechanics::isFatigueKnockout(hasKnockoutAnimation(magic)
                            ? state.actors[actor][10][0] : 1.f, state.actors[actor][10][2]);
                    if (value > 1 || (hasKnockoutAnimation(magic)
                            ? (expected && !value) || (state.actors[actor][8][2] <= 0 && value)
                            : bool(value) != expected))
                        throw std::invalid_argument("Native knockout state invalid");
                    state.knockedDown[actor] = value != 0;
                }
            if (magic == MeleeDefenseActorCampaignMagic || magic == EffectActorCampaignMagic
                || hasConstantState(magic))
                for (size_t actor = 0; actor < state.hitRecoveryTicks.size(); ++actor)
                {
                    const auto value = getAreaWord(bytes, offset);
                    if (value > 1800 || (state.actors[actor][8][2] <= 0 && value))
                        throw std::invalid_argument("Native hit recovery state invalid");
                    state.hitRecoveryTicks[actor] = uint32_t(value);
                }
            if (hasKnockoutAnimation(magic))
                for (size_t actor = 0; actor < state.knockoutFrame.size(); ++actor)
                {
                    const auto frame = getAreaWord(bytes, offset);
                    if (frame >= 1800 || (!state.knockedDown[actor] && frame)
                        || (state.knockedDown[actor] && state.hitRecoveryTicks[actor]))
                        throw std::invalid_argument("Native knockout animation state invalid");
                    state.knockoutFrame[actor] = uint32_t(frame);
                }
            if (hasKnockoutAnimation(magic))
                for (size_t actor = 0; actor < state.hitKnockdown.size(); ++actor)
                {
                    const auto value = getAreaWord(bytes, offset);
                    if (value > 1 || (value && !state.knockedDown[actor]))
                        throw std::invalid_argument("Native hit knockdown state invalid");
                    state.hitKnockdown[actor] = value != 0;
                }
        }
        if (hasActorPresentation(magic))
        {
            auto& state = *combat;
            state.npcAction = getAreaWord(bytes, offset);
            if (state.npcAction > tick) throw std::invalid_argument("Future NPC action");
            for (auto& action : state.bodyAction)
            { action = getAreaWord(bytes, offset); if (action > tick) throw std::invalid_argument("Future body action"); }
            for (auto& group : state.hitGroup)
            { group = getAreaWord(bytes, offset); if (group > 16) throw std::invalid_argument("Invalid hit group"); }
            if (hasNeighborCombat(magic))
            {
                state.neighborAttacks.reserve(state.npcPlacements.size() - 1);
                for (size_t i = 1; i < state.npcPlacements.size(); ++i)
                {
                    ActorCampaignCombat::NeighborAttack attack;
                    const auto length = getAreaWord(bytes, offset);
                    if (!length || length > 512 || length > bytes.size() - offset)
                        throw std::invalid_argument("Native neighbor attack resource invalid");
                    attack.identity.assign(bytes.data() + offset, size_t(length));
                    offset += size_t(length);
                    if (attack.identity.find('\0') != std::string::npos)
                        throw std::invalid_argument("Native neighbor attack identity invalid");
                    const auto weaponLength = getAreaWord(bytes, offset);
                    if (weaponLength > 256 || weaponLength > bytes.size() - offset)
                        throw std::invalid_argument("Native neighbor weapon identity length invalid");
                    attack.weapon.assign(bytes.data() + offset, size_t(weaponLength));
                    offset += size_t(weaponLength);
                    if (attack.weapon.find('\0') != std::string::npos)
                        throw std::invalid_argument("Native neighbor weapon identity invalid");
                    const auto phase = getAreaWord(bytes, offset);
                    const auto time = getAreaWord(bytes, offset), strength = getAreaWord(bytes, offset);
                    const auto released = getAreaWord(bytes, offset), hit = getAreaWord(bytes, offset);
                    attack.target = getAreaWord(bytes, offset);
                    const auto contact = getAreaWord(bytes, offset);
                    attack.action = getAreaWord(bytes, offset);
                    attack.source = getAreaWord(bytes, offset);
                    attack.direction = getAreaWord(bytes, offset);
                    if (dynamicDomain)
                    {
                        attack.targetKind = getAreaWord(bytes, offset);
                        attack.targetLife = getAreaWord(bytes, offset);
                        if (attack.target ? (attack.targetKind < 1 || attack.targetKind > 2 || !attack.targetLife)
                            : (attack.targetKind || attack.targetLife))
                            throw std::invalid_argument("Dynamic actor attack target life invalid");
                    }
                    if (phase > uint64_t(MeleeAnimation::Phase::Complete) || time > UINT32_MAX
                        || strength > UINT32_MAX || released > 1 || hit > 1 || contact > 1
                        || attack.action > tick || attack.direction > 2
                        || (attack.source == 0) != attack.weapon.empty()
                        || (contact && (!attack.target || !hit))
                        || (released && !attack.target))
                        throw std::invalid_argument("Native neighbor attack state invalid");
                    attack.state = {MeleeAnimation::Phase(phase), std::bit_cast<float>(uint32_t(time)),
                        std::bit_cast<float>(uint32_t(strength)), bool(released), bool(hit)};
                    if (!std::isfinite(attack.state.mTime) || !std::isfinite(attack.state.mStrength))
                        throw std::invalid_argument("Native neighbor attack number invalid");
                    attack.contact = bool(contact);
                    state.neighborAttacks.push_back(std::move(attack));
                }
            }
        }
        if (hasAiDecisions(magic))
        {
            auto& state = *combat;
            state.fleeTarget = getAreaWord(bytes, offset);
            state.fleeUntil = getAreaWord(bytes, offset);
            for (float& value : state.fleeDestination)
            {
                const auto bits = getAreaWord(bytes, offset);
                if (bits > UINT32_MAX) throw std::invalid_argument("Native flee coordinate bits invalid");
                value = std::bit_cast<float>(uint32_t(bits));
                if (!std::isfinite(value) || std::abs(value) > 1e7f)
                    throw std::invalid_argument("Native flee destination invalid");
            }
            if ((!state.fleeTarget && (state.fleeUntil || state.fleeDestination != std::array<float, 3>{}))
                || (state.fleeTarget && (!state.fleeUntil
                    || (state.fleeUntil > tick && state.fleeUntil - tick > 30))))
                throw std::invalid_argument("Native flee state invalid");
        }
        if (hasPlayerAi(magic))
            for (auto& player : combat->players)
            {
                const auto count = getAreaWord(bytes, offset);
                if (count > 256 || count > (bytes.size() - offset) / 24)
                    throw std::invalid_argument("Native player faction count invalid");
                player.factions.reserve(size_t(count));
                for (uint64_t i = 0; i < count; ++i)
                {
                    const auto length = getAreaWord(bytes, offset);
                    if (!length || length > 256 || length > bytes.size() - offset)
                        throw std::invalid_argument("Native player faction identity invalid");
                    const auto id = ESM::RefId::deserializeText(
                        std::string_view(bytes.data() + offset, size_t(length)));
                    offset += size_t(length);
                    const auto rank = getAreaWord(bytes, offset), expelled = getAreaWord(bytes, offset);
                    if (id.empty() || rank > 9 || expelled > 1
                        || std::ranges::any_of(player.factions, [&](const auto& entry) { return entry.id == id; }))
                        throw std::invalid_argument("Native player faction state invalid");
                    player.factions.push_back({id, int(rank), bool(expelled)});
                }
                const auto bounty = getAreaWord(bytes, offset), crime = getAreaWord(bytes, offset);
                player.drawState = getAreaWord(bytes, offset);
                const auto werewolf = getAreaWord(bytes, offset), known = getAreaWord(bytes, offset);
                const auto spellLength = getAreaWord(bytes, offset);
                if (bounty > INT32_MAX || crime > UINT32_MAX || player.drawState > 2
                    || werewolf > 1 || known > 1 || spellLength > 256
                    || spellLength > bytes.size() - offset)
                    throw std::invalid_argument("Native player AI state invalid");
                player.bounty = int(bounty); player.crimeDisposition = std::bit_cast<int32_t>(uint32_t(crime));
                player.werewolf = bool(werewolf); player.knownWerewolf = bool(known);
                if (spellLength)
                    player.selectedSpell = ESM::RefId::deserializeText(
                        std::string_view(bytes.data() + offset, size_t(spellLength)));
                offset += size_t(spellLength);
                player.selectedEnchantedItem = getAreaWord(bytes, offset);
                if (hasPlayerTravel(magic))
                {
                    const auto marked = getAreaWord(bytes, offset);
                    if (marked > 1) throw std::invalid_argument("Native player mark flag invalid");
                    if (marked)
                    {
                        const auto kind = getAreaWord(bytes, offset);
                        const auto space = getAreaWord(bytes, offset);
                        const auto gridX = getAreaWord(bytes, offset), gridY = getAreaWord(bytes, offset);
                        if (kind > 1 || !space || space > 0x7fffffff
                            || gridX > UINT32_MAX || gridY > UINT32_MAX
                            || (kind == 0 && (gridX || gridY)))
                            throw std::invalid_argument("Native player mark cell invalid");
                        std::array<int64_t, 3> position;
                        for (auto& axis : position) axis = std::bit_cast<int64_t>(getAreaWord(bytes, offset));
                        std::array<uint32_t, 3> rotation;
                        for (auto& axis : rotation)
                        {
                            const auto value = getAreaWord(bytes, offset);
                            if (value > UINT32_MAX) throw std::invalid_argument("Native player mark rotation invalid");
                            axis = uint32_t(value);
                        }
                        const auto cell = kind == 0 ? CellId::interior(*CellSpaceId::fromValue(space))
                            : CellId::exterior(*CellSpaceId::fromValue(space),
                                std::bit_cast<int32_t>(uint32_t(gridX)), std::bit_cast<int32_t>(uint32_t(gridY)));
                        player.mark.emplace(cell, Position3(position[0], position[1], position[2]),
                            Orientation3(Turn32::fromValue(rotation[0]), Turn32::fromValue(rotation[1]),
                                Turn32::fromValue(rotation[2])));
                    }
                }
                if (hasSocialLifecycle(magic) && player.werewolf)
                {
                    auto& skills = player.normalSkills.emplace();
                    auto& attributes = player.normalAttributes.emplace();
                    for (float& value : skills)
                    {
                        const auto bits = getAreaWord(bytes, offset);
                        if (bits > UINT32_MAX) throw std::invalid_argument("Native werewolf skill bits invalid");
                        value = std::bit_cast<float>(uint32_t(bits));
                        if (!std::isfinite(value) || std::abs(value) > 1'000'000.f)
                            throw std::invalid_argument("Native werewolf skill invalid");
                    }
                    for (float& value : attributes)
                    {
                        const auto bits = getAreaWord(bytes, offset);
                        if (bits > UINT32_MAX) throw std::invalid_argument("Native werewolf attribute bits invalid");
                        value = std::bit_cast<float>(uint32_t(bits));
                        if (!std::isfinite(value) || std::abs(value) > 1'000'000.f)
                            throw std::invalid_argument("Native werewolf attribute invalid");
                    }
                }
                if (hasSocialLifecycle(magic))
                {
                    const auto count = getAreaWord(bytes, offset);
                    if (count > 128 || count > (bytes.size() - offset) / 24)
                        throw std::invalid_argument("Native crime engagement count invalid");
                    player.engagements.reserve(size_t(count));
                    for (uint64_t i = 0; i < count; ++i)
                    {
                        const auto witness = getAreaWord(bytes, offset);
                        const auto at = getAreaWord(bytes, offset);
                        const auto fight = getAreaWord(bytes, offset);
                        if (!witness || !at || at > tick || fight > 100
                            || (!player.engagements.empty() && player.engagements.back().witness >= witness))
                            throw std::invalid_argument("Native crime engagement invalid");
                        player.engagements.push_back({witness, at, int(fight)});
                    }
                }
            }
        std::optional<ActorCampaignLife> life;
        if (magic == LifeActorCampaignMagic || magic == ProjectileActorCampaignMagic
            || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
            || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
            || hasKnockoutState(magic))
        {
            auto& state = life.emplace();
            state.generation = getAreaWord(bytes, offset);
            state.bornTick = getAreaWord(bytes, offset);
            state.respawnTick = getAreaWord(bytes, offset);
            if (!state.generation || state.generation > UINT32_MAX || state.bornTick > tick)
                throw std::invalid_argument("Native NPC life generation or deadline invalid");
            for (auto& stat : state.spawnStats)
                for (float& value : stat)
                {
                    const auto bits = getAreaWord(bytes, offset);
                    if (bits > UINT32_MAX) throw std::invalid_argument("Native NPC spawn stat bits invalid");
                    value = std::bit_cast<float>(uint32_t(bits));
                    if (!std::isfinite(value) || std::abs(value) > 1'000'000)
                        throw std::invalid_argument("Native NPC spawn stat invalid");
                }
            const auto actorLength = getAreaWord(bytes, offset);
            const auto inventoryLength = getAreaWord(bytes, offset);
            if (!actorLength || actorLength > 65536 || !inventoryLength || inventoryLength > 2 * 1024 * 1024
                || actorLength > bytes.size() - offset || inventoryLength > bytes.size() - offset - actorLength)
                throw std::invalid_argument("Native NPC spawn image length invalid");
            state.spawnActor.assign(bytes.data() + offset, bytes.data() + offset + actorLength);
            offset += size_t(actorLength);
            state.spawnInventory.assign(bytes.data() + offset, bytes.data() + offset + inventoryLength);
            offset += size_t(inventoryLength);
            const auto count = getAreaWord(bytes, offset);
            // Validate count and backing bytes before reserve; the inventory and
            // actor payloads cannot supply bytes for alleged historical deaths.
            const auto remaining = bytes.size() - offset;
            if (count > state.generation
                || (hasCasterState(magic) && count != state.generation - (state.respawnTick ? 0 : 1))
                || inventorySize > remaining || actorSize > remaining - inventorySize
                || count > (remaining - inventorySize - actorSize) / (hasCasterState(magic) ? 40 : 24))
                throw std::invalid_argument("Native NPC death history bound invalid");
            state.deaths.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                ActorDeathEvent event{getAreaWord(bytes, offset), getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
                if (hasCasterState(magic))
                {
                    event.killerKind = getAreaWord(bytes, offset);
                    event.killerLife = getAreaWord(bytes, offset);
                    validateActorCaster({event.killer, event.killerKind, event.killerLife}, event.life);
                }
                if (event.life != state.deaths.size() + 1 || event.life > state.generation || !event.tick || !event.killer
                    || event.tick > tick || !state.deaths.empty() && (event.life <= state.deaths.back().life
                        || event.tick <= state.deaths.back().tick))
                    throw std::invalid_argument("Native NPC death history invalid");
                state.deaths.push_back(event);
            }
            if ((state.respawnTick != 0) != (state.deaths.size() == state.generation))
                throw std::invalid_argument("Native NPC life/death deadline inconsistent");
            if (!state.deaths.empty() && (state.respawnTick
                    ? state.respawnTick <= state.deaths.back().tick
                    : state.bornTick <= state.deaths.back().tick))
                throw std::invalid_argument("Native NPC life chronology invalid");
        }
        std::vector<ActorCampaignLife> neighborLives;
        if (hasNeighborCombat(magic))
        {
            const auto count = getAreaWord(bytes, offset);
            if (!combat || count != combat->npcPlacements.size() - 1)
                throw std::invalid_argument("Native neighbor life count differs from placements");
            neighborLives.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                const auto placement = getAreaWord(bytes, offset);
                if (placement != combat->npcPlacements[i + 1])
                    throw std::invalid_argument("Native neighbor life placement invalid");
                ActorCampaignLife state;
                state.generation = getAreaWord(bytes, offset);
                state.bornTick = getAreaWord(bytes, offset);
                state.respawnTick = getAreaWord(bytes, offset);
                if (!state.generation || state.generation > UINT32_MAX || state.bornTick > tick)
                    throw std::invalid_argument("Native neighbor life generation invalid");
                for (auto& stat : state.spawnStats)
                    for (float& value : stat)
                    {
                        const auto bits = getAreaWord(bytes, offset);
                        value = std::bit_cast<float>(uint32_t(bits));
                        if (bits > UINT32_MAX || !std::isfinite(value) || std::abs(value) > 1'000'000)
                            throw std::invalid_argument("Native neighbor spawn stat invalid");
                    }
                const auto actorLength = getAreaWord(bytes, offset);
                const auto inventoryLength = getAreaWord(bytes, offset);
                if (!actorLength || actorLength > 65536 || !inventoryLength
                    || inventoryLength > 2 * 1024 * 1024 || actorLength > bytes.size() - offset
                    || inventoryLength > bytes.size() - offset - actorLength)
                    throw std::invalid_argument("Native neighbor spawn image length invalid");
                state.spawnActor.assign(bytes.data() + offset, bytes.data() + offset + actorLength);
                offset += size_t(actorLength);
                state.spawnInventory.assign(bytes.data() + offset, bytes.data() + offset + inventoryLength);
                offset += size_t(inventoryLength);
                const auto deaths = getAreaWord(bytes, offset);
                if (deaths != state.generation - (state.respawnTick ? 0 : 1)
                    || deaths > (bytes.size() - offset) / 40)
                    throw std::invalid_argument("Native neighbor death history bound invalid");
                state.deaths.reserve(size_t(deaths));
                for (size_t d = 0; d < deaths; ++d)
                {
                    ActorDeathEvent event{getAreaWord(bytes, offset), getAreaWord(bytes, offset),
                        getAreaWord(bytes, offset), getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
                    validateActorCaster({event.killer, event.killerKind, event.killerLife}, UINT32_MAX);
                    if (event.life != d + 1 || !event.tick || event.tick > tick
                        || (!state.deaths.empty() && event.tick <= state.deaths.back().tick))
                        throw std::invalid_argument("Native neighbor death history invalid");
                    state.deaths.push_back(event);
                }
                if (!state.deaths.empty() && (state.respawnTick
                        ? state.respawnTick <= state.deaths.back().tick
                        : state.bornTick <= state.deaths.back().tick))
                    throw std::invalid_argument("Native neighbor life chronology invalid");
                neighborLives.push_back(std::move(state));
            }
        }
        std::optional<ActorCampaignProjectile> projectile; // Legacy single-flight decoded view.
        std::vector<ActorCampaignProjectile> projectiles;
        if (magic == ProjectileActorCampaignMagic || magic == EnchantedProjectileActorCampaignMagic
            || magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
            || hasKnockoutState(magic))
        {
            const auto count = getAreaWord(bytes, offset);
            if (count > ((magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic))
                    ? MaximumActorProjectiles : 1))
                throw std::invalid_argument("Native projectile count invalid");
            projectiles.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                ActorCampaignProjectile value;
                value.caster = getAreaWord(bytes, offset); value.source = getAreaWord(bytes, offset);
                value.target = getAreaWord(bytes, offset); value.generation = getAreaWord(bytes, offset);
                value.expiresTick = getAreaWord(bytes, offset);
                if (magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
                    || hasKnockoutState(magic))
                {
                    value.sourceKind = getAreaWord(bytes, offset);
                    value.effectSource = getAreaWord(bytes, offset);
                    if (value.sourceKind > 1 || !value.effectSource
                        || (value.sourceKind == 0 && value.effectSource != value.source))
                        throw std::invalid_argument("Native projectile source identity invalid");
                }
                else value.effectSource = value.source;
                if (magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
                    || hasKnockoutState(magic))
                {
                    value.targetKind = getAreaWord(bytes, offset);
                    if (value.targetKind != 1 && value.targetKind != 2
                        && !(hasObjectTravel(magic) && (value.targetKind == 3 || value.targetKind == 4)))
                        throw std::invalid_argument("Native projectile target kind invalid");
                }
                if (magic == MultipleProjectileActorCampaignMagic || hasKnockoutState(magic))
                {
                    value.commandId = getAreaWord(bytes, offset);
                    if (!value.commandId) throw std::invalid_argument("Native projectile command identity invalid");
                }
                for (float& component : value.position)
                {
                    const auto bits = getAreaWord(bytes, offset);
                    if (bits > UINT32_MAX) throw std::invalid_argument("Native projectile position bits invalid");
                    component = std::bit_cast<float>(uint32_t(bits));
                    if (!std::isfinite(component) || std::abs(component) > 1e7f)
                        throw std::invalid_argument("Native projectile position invalid");
                }
                float length2 = 0;
                for (float& component : value.step)
                {
                    const auto bits = getAreaWord(bytes, offset);
                    if (bits > UINT32_MAX) throw std::invalid_argument("Native projectile step bits invalid");
                    component = std::bit_cast<float>(uint32_t(bits));
                    if (!std::isfinite(component) || std::abs(component) > 1000.f)
                        throw std::invalid_argument("Native projectile step invalid");
                    length2 += component * component;
                }
                if (hasCasterState(magic))
                {
                    value.casterKind = getAreaWord(bytes, offset);
                    value.casterLife = getAreaWord(bytes, offset);
                    validateActorCaster({value.caster, value.casterKind, value.casterLife}, life->generation);
                }
                const auto target = hasNeighborCombat(magic) && value.targetKind == 2
                    ? std::ranges::find(combat->npcPlacements, value.target) - combat->npcPlacements.begin() + 2 : 2;
                const auto targetGeneration = value.targetKind == 2
                    ? target == 2 ? life->generation
                        : target < combat->actors.size() ? neighborLives[target - 3].generation : 0 : 1;
                if (!value.caster || !value.source || !value.target || !value.generation
                    || (value.targetKind == 2 && value.generation != targetGeneration)
                    || value.expiresTick <= tick
                    || value.expiresTick - tick > 90 || length2 < 1.f || length2 > 1e6f)
                    throw std::invalid_argument("Native projectile identity or lifetime invalid");
                if (value.commandId && std::ranges::any_of(projectiles, [&](const auto& previous) {
                        return previous.commandId == value.commandId && previous.caster == value.caster
                            && previous.casterKind == value.casterKind && previous.casterLife == value.casterLife;
                    })) throw std::invalid_argument("Native duplicate pending cast identity");
                projectiles.push_back(value);
            }
            if (magic != MultipleProjectileActorCampaignMagic && !hasKnockoutState(magic)
                && !projectiles.empty()) projectile = projectiles.front();
        }
        std::vector<ActorCampaignTimedEffect> timedEffects;
        if (magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic || magic == MultipleProjectileActorCampaignMagic
            || hasKnockoutState(magic))
        {
            const auto count = getAreaWord(bytes, offset);
            const size_t effectBytes = magic == EquipmentFamilyCampaignMagic ? 192 : hasExpandedEffects(magic) ? 120 : hasCasterState(magic) ? 112 : hasGeneralConstantState(magic) ? 96 : magic == EffectActorCampaignMagic || hasConstantState(magic) ? 80 : 24;
            if (count > (hasGeneralConstantState(magic) ? MaximumActorTimedEffects : 16) || count > (bytes.size() - offset) / effectBytes)
                throw std::invalid_argument("Native timed effect count invalid");
            timedEffects.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                ActorCampaignTimedEffect effect;
                effect.actor = getAreaWord(bytes, offset);
                const auto bits = getAreaWord(bytes, offset);
                if (bits > UINT32_MAX) throw std::invalid_argument("Native timed effect magnitude bits invalid");
                effect.magnitude = std::bit_cast<float>(uint32_t(bits));
                effect.expiresTick = getAreaWord(bytes, offset);
                if (magic == EffectActorCampaignMagic || hasConstantState(magic))
                {
                    effect.effectIndex = getAreaWord(bytes, offset);
                    effect.caster = getAreaWord(bytes, offset);
                    effect.source = getAreaWord(bytes, offset);
                    effect.sourceKind = getAreaWord(bytes, offset);
                    const auto resistanceBits = getAreaWord(bytes, offset);
                    if (resistanceBits > UINT32_MAX)
                        throw std::invalid_argument("Native effect resistance bits invalid");
                    effect.resistance = std::bit_cast<float>(uint32_t(resistanceBits));
                    effect.startTick = getAreaWord(bytes, offset);
                    effect.durationTicks = getAreaWord(bytes, offset);
                    if (hasGeneralConstantState(magic))
                    {
                        effect.argument = getAreaWord(bytes, offset);
                        effect.ordinal = getAreaWord(bytes, offset);
                        if (effect.argument > 35 || effect.ordinal >= 8
                            || (!hasCastLifecycle(magic) && effect.sourceKind != 3 && (effect.argument || effect.ordinal)))
                            throw std::invalid_argument("Native effect argument or ordinal invalid");
                    }
                    else if (effect.sourceKind == 3) effect.argument = 8; // V36 Luck.
                    if (hasCasterState(magic))
                    {
                        effect.casterKind = getAreaWord(bytes, offset);
                        effect.casterLife = getAreaWord(bytes, offset);
                        uint64_t maximumLife = life->generation;
                        if (hasNeighborCombat(magic))
                            for (const auto& adjacent : neighborLives)
                                maximumLife = std::max(maximumLife, adjacent.generation);
                        validateActorCaster({effect.caster, effect.casterKind, effect.casterLife}, maximumLife);
                    }
                    if (hasExpandedEffects(magic))
                    {
                        effect.beneficiary = getAreaWord(bytes, offset);
                        if (effect.beneficiary > (hasNeighborCombat(magic) ? combat->actors.size() : 3))
                            throw std::invalid_argument("Native effect beneficiary invalid");
                    }
                    if ((!effect.effectIndex && !hasMovementEffects(magic))
                        || effect.effectIndex > 255 || !effect.caster || !effect.source
                        || effect.sourceKind > (hasMovementEffects(magic) ? 5u : (magic == PersistentConditionsCampaignMagic
                            || magic == SpecialConditionsCampaignMagic)
                            ? 4u : hasConstantState(magic) ? 3u : 2u)
                        || !std::isfinite(effect.resistance)
                        || effect.resistance < -20000 || effect.resistance > 100
                        || effect.startTick > tick
                        || (effect.sourceKind >= 3
                            ? (!hasConstantState(magic) || effect.durationTicks != 0
                                || effect.expiresTick != UINT64_MAX)
                            : (!effect.durationTicks || effect.durationTicks > 108000)))
                        throw std::invalid_argument("Native effect identity or duration invalid");
                }
                if (magic == EquipmentFamilyCampaignMagic)
                {
                    const auto applied = getAreaWord(bytes, offset);
                    if (applied > 1) throw std::invalid_argument("Native equipment application flag invalid");
                    effect.equipmentApplied = applied != 0;
                    for (auto& item : effect.boundItems)
                    {
                        const auto identity = getAreaWord(bytes, offset), previous = getAreaWord(bytes, offset);
                        const auto slot = getAreaWord(bytes, offset), length = getAreaWord(bytes, offset);
                        if (identity > UINT32_MAX || previous > UINT32_MAX || slot > MWWorld::InventoryStore::Slots
                            || length > 256 || length > bytes.size() - offset)
                            throw std::invalid_argument("Native bound item identity or restoration invalid");
                        item.item = {uint32_t(identity), -1}; item.previous = {uint32_t(previous), -1};
                        item.slot = int(slot) - 1;
                        item.previousRecord = ESM::RefId::deserializeText(std::string_view(bytes.data() + offset, size_t(length)));
                        offset += size_t(length);
                        if (!identity && (previous || slot || !item.previousRecord.empty()))
                            throw std::invalid_argument("Native empty bound item carries restoration state");
                    }
                    if (effect.equipmentApplied && !MWMechanics::equipmentMagicEffect(
                            ESM::MagicEffect::indexToRefId(int(effect.effectIndex))))
                        throw std::invalid_argument("Native equipment source application mismatch");
                }
                if (effect.actor >= (hasNeighborCombat(magic) ? combat->actors.size() : 3)
                    || !std::isfinite(effect.magnitude)
                    || (effect.magnitude < 0 || (effect.magnitude == 0 && effect.sourceKind < 3)) || effect.magnitude > (hasKnockoutState(magic) && effect.effectIndex ? 100000 : 1000)
                    || effect.expiresTick <= tick
                    || (effect.sourceKind < 3 && effect.expiresTick - tick > 108000))
                    throw std::invalid_argument("Native timed effect state invalid");
                timedEffects.push_back(effect);
            }
        }
        std::string castResource;
        std::optional<ActorCampaignCast> casting;
        if (hasCastLifecycle(magic))
        {
            const auto size = getAreaWord(bytes, offset);
            if (!size || size > 1024 || size > bytes.size() - offset)
                throw std::invalid_argument("Native cast resource identity invalid");
            castResource.assign(bytes.data() + offset, size_t(size)); offset += size_t(size);
            const auto count = getAreaWord(bytes, offset);
            if (count > 1) throw std::invalid_argument("Native pending cast count invalid");
            if (count)
            {
                ActorCampaignCast value;
                for (auto* field : {&value.actor, &value.life, &value.cast, &value.sourceKind, &value.source,
                        &value.targetKind, &value.target, &value.range, &value.elapsed, &value.phase})
                    *field = getAreaWord(bytes, offset);
                if (!value.actor || !life || value.life != life->generation || !value.cast || value.cast > tick
                    || !value.source || value.sourceKind > 1
                    || value.targetKind > 1
                    || ((value.targetKind == 0) != (value.target == 0))
                    || value.range > 2 || value.elapsed > 1800 || value.phase < 1 || value.phase > 5
                    || (value.phase <= ActorCampaignCast::Prepared && value.elapsed))
                    throw std::invalid_argument("Native pending cast state invalid");
                casting = value;
            }
        }
        if (hasPlayerCasts(magic))
            for (size_t i = 0; i < 2; ++i)
            {
                const auto size = getAreaWord(bytes, offset);
                if (!size || size > 1024 || size > bytes.size() - offset)
                    throw std::invalid_argument("Native player cast resource invalid");
                combat->playerCastResources[i].assign(bytes.data() + offset, size_t(size)); offset += size_t(size);
                const auto present = getAreaWord(bytes, offset);
                if (present > 1) throw std::invalid_argument("Native player cast presence invalid");
                if (!present) continue;
                ActorCampaignCast value;
                for (auto* field : {&value.actor, &value.life, &value.cast, &value.sourceKind, &value.source,
                        &value.targetKind, &value.target, &value.range, &value.elapsed, &value.phase, &value.targetLife})
                    *field = getAreaWord(bytes, offset);
                const auto targetIndex = hasNeighborCombat(magic) && value.targetKind == 2
                    ? std::ranges::find(combat->npcPlacements, value.target) - combat->npcPlacements.begin() + 2 : 2;
                const auto targetGeneration = value.targetKind == 2
                    ? targetIndex == 2 ? life->generation
                        : targetIndex < combat->actors.size() ? neighborLives[targetIndex - 3].generation : 0 : 1;
                if (!value.actor || value.life != 1 || !value.cast || !value.source || value.sourceKind > 1
                    || value.targetKind > 4 || ((value.targetKind == 0) != (value.target == 0))
                    || (value.targetKind == 2 ? (!targetGeneration || !value.targetLife || value.targetLife > targetGeneration
                        || (value.phase < ActorCampaignCast::Released && value.targetLife != targetGeneration))
                        : value.targetKind == 3 || value.targetKind == 4 ? !value.targetLife
                        : value.targetLife != 1)
                    || value.range > 2 || value.elapsed > 1800 || value.phase < 1 || value.phase > 5
                    || (value.phase <= ActorCampaignCast::Prepared && value.elapsed))
                    throw std::invalid_argument("Native player cast state invalid");
                combat->playerCasts[i] = value;
            }
        if (magic == PersistentConditionsCampaignMagic || magic == SpecialConditionsCampaignMagic
            || hasMovementEffects(magic))
        {
            const auto count = getAreaWord(bytes, offset);
            const size_t sourceBytes = magic == SpecialConditionsCampaignMagic
                || hasMovementEffects(magic) ? 40 : 16;
            if (count > ActorCampaignCombat::MaximumConditionSources || count > (bytes.size() - offset) / sourceBytes)
                throw std::invalid_argument("Native condition source count invalid");
            for (size_t i = 0; i < count; ++i)
            {
                ActorCampaignCombat::ConditionSource value{getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
                if (magic == SpecialConditionsCampaignMagic || hasMovementEffects(magic))
                {
                    value.nextWorseningMs = getAreaWord(bytes, offset);
                    value.lastObservedMs = getAreaWord(bytes, offset);
                    value.worsenings = getAreaWord(bytes, offset);
                }
                if (value.actor >= (hasNeighborCombat(magic) ? combat->actors.size() : 3) || !value.source
                    || value.worsenings > 100000
                    || (value.nextWorseningMs && (!value.lastObservedMs
                        || value.nextWorseningMs <= value.lastObservedMs
                        || value.nextWorseningMs - value.lastObservedMs > 86400000))
                    || (!value.nextWorseningMs && (value.lastObservedMs || value.worsenings))
                    || std::ranges::any_of(combat->conditions, [&](const auto& prior) {
                        return prior.actor == value.actor && prior.source == value.source;
                    }))
                    throw std::invalid_argument("Native condition source identity invalid");
                combat->conditions.push_back(value);
            }
        }
        if (hasPlayerSwings(magic))
            for (auto& swing : combat->swings)
            {
                const auto present = getAreaWord(bytes, offset);
                if (present > 1) throw std::invalid_argument("Native player swing presence invalid");
                if (!present) continue;
                auto& value = swing.emplace();
                for (auto* field : {&value.command, &value.source, &value.targetLife, &value.direction, &value.interruption})
                    *field = getAreaWord(bytes, offset);
                if (hasNeighborCombat(magic)) value.target = getAreaWord(bytes, offset);
                const auto number = [&] {
                    const auto bits = getAreaWord(bytes, offset);
                    const float result = std::bit_cast<float>(uint32_t(bits));
                    if (bits > UINT32_MAX || !std::isfinite(result))
                        throw std::invalid_argument("Native player swing number invalid");
                    return result;
                };
                value.strength = number();
                for (auto* text : {&value.weapon, &value.identity})
                {
                    const auto size = getAreaWord(bytes, offset);
                    if (size > 512 || size > bytes.size() - offset)
                        throw std::invalid_argument("Native player swing identity length invalid");
                    text->assign(bytes.data() + offset, size_t(size)); offset += size_t(size);
                    if (text->find('\0') != std::string::npos)
                        throw std::invalid_argument("Native player swing identity invalid");
                }
                const auto phase = getAreaWord(bytes, offset);
                value.state.mTime = number(); value.state.mStrength = number();
                const auto released = getAreaWord(bytes, offset), hit = getAreaWord(bytes, offset);
                value.state.mPhase = MeleeAnimation::Phase(phase);
                value.state.mReleased = bool(released); value.state.mHit = bool(hit);
                if (hasRangedRelease(magic))
                {
                    value.ammunition = getAreaWord(bytes, offset);
                    const auto size = getAreaWord(bytes, offset);
                    if (size > 256 || size > bytes.size() - offset)
                        throw std::invalid_argument("Native arrow identity length invalid");
                    value.ammoRecord.assign(bytes.data() + offset, size_t(size)); offset += size_t(size);
                    if (bool(value.ammunition) != !value.ammoRecord.empty()
                        || value.ammoRecord.find('\0') != std::string::npos)
                        throw std::invalid_argument("Native arrow identity invalid");
                }
                if (hasAuthoritativeAim(magic)) for (auto& axis : value.aim) axis = number();
                const bool worldShot = hasAuthoritativeAim(magic) && value.ammunition && !value.target;
                const auto targetIndex = worldShot ? 0 : hasNeighborCombat(magic)
                    ? std::ranges::find(combat->npcPlacements, value.target) - combat->npcPlacements.begin() + 2 : 2;
                const auto targetGeneration = targetIndex == 2 ? life->generation
                    : targetIndex > 2 && targetIndex < combat->actors.size()
                        ? neighborLives[targetIndex - 3].generation : 0;
                if (!value.command || (worldShot ? value.targetLife != 0
                        : !value.targetLife || !targetGeneration || value.targetLife > targetGeneration)
                    || value.direction > 2 || value.interruption > PlayerSwing::TargetLost
                    || value.strength < 0 || value.strength > 1 || value.identity.empty()
                    || bool(value.source) != !value.weapon.empty()
                    || phase > uint64_t(MeleeAnimation::Phase::Complete) || released > 1 || hit > 1)
                    throw std::invalid_argument("Native player swing state invalid");
                if (hasAuthoritativeAim(magic))
                {
                    const float norm = std::sqrt(value.aim[0] * value.aim[0]
                        + value.aim[1] * value.aim[1] + value.aim[2] * value.aim[2]);
                    if ((value.ammunition && std::abs(norm - 1.f) > .001f)
                        || (!value.ammunition && norm != 0.f))
                        throw std::invalid_argument("Native player aim invalid");
                }
            }
        if (hasRangedRelease(magic))
        {
            const auto count = getAreaWord(bytes, offset);
            if (count > MaximumActorProjectiles)
                throw std::invalid_argument("Native arrow capacity exceeded");
            for (uint64_t i = 0; i < count; ++i)
            {
                BowProjectile value;
                for (auto* field : {&value.caster, &value.command, &value.source, &value.ammunition,
                        &value.target, &value.targetLife, &value.releaseTick})
                    *field = getAreaWord(bytes, offset);
                const auto number = [&] {
                    const auto bits = getAreaWord(bytes, offset);
                    const float result = std::bit_cast<float>(uint32_t(bits));
                    if (bits > UINT32_MAX || !std::isfinite(result))
                        throw std::invalid_argument("Native arrow number invalid");
                    return result;
                };
                value.strength = number();
                for (auto* text : {&value.weapon, &value.ammoRecord})
                {
                    const auto size = getAreaWord(bytes, offset);
                    if (!size || size > 256 || size > bytes.size() - offset)
                        throw std::invalid_argument("Native arrow source length invalid");
                    text->assign(bytes.data() + offset, size_t(size)); offset += size_t(size);
                    if (text->find('\0') != std::string::npos)
                        throw std::invalid_argument("Native arrow source invalid");
                }
                for (auto& v : value.position) v = number();
                for (auto& v : value.direction) v = number();
                if (hasRangedFlight(magic))
                {
                    for (auto& v : value.velocity) v = number();
                    value.condition = number();
                    value.steps = getAreaWord(bytes, offset);
                    value.terminal = getAreaWord(bytes, offset);
                    if (hasNpcRanged(magic))
                    {
                        value.casterKind = getAreaWord(bytes, offset);
                        value.casterLife = getAreaWord(bytes, offset);
                        value.targetKind = getAreaWord(bytes, offset);
                    }
                    if (value.condition < 0 || value.condition > 1 || value.steps > 3600 || value.terminal > 2
                        || (value.steps == 3600 && !value.terminal)
                        || (value.terminal == 2 && value.steps != 3600)
                        || std::ranges::any_of(value.position, [](float v) { return std::abs(v) > 10'000'000; })
                        || std::ranges::any_of(value.velocity, [](float v) { return std::abs(v) > 100000; }))
                        throw std::invalid_argument("Native physical flight state invalid");
                }
                float norm = 0; for (float v : value.direction) norm += v * v;
                const bool worldShot = hasAuthoritativeAim(magic) && value.casterKind == 1
                    && value.targetKind == 2 && !value.target && !value.targetLife;
                const size_t targetIndex = worldShot || value.targetKind == 1 ? 0 : hasNeighborCombat(magic)
                    ? size_t(std::ranges::find(combat->npcPlacements, value.target)
                        - combat->npcPlacements.begin()) + 2 : 2;
                const auto targetGeneration = value.targetKind == 1 ? 1 : targetIndex == 2 ? life->generation
                    : targetIndex > 2 && targetIndex < combat->actors.size()
                        ? neighborLives[targetIndex - 3].generation : 0;
                const size_t casterIndex = value.casterKind == 2 && hasNeighborCombat(magic)
                    ? size_t(std::ranges::find(combat->npcPlacements, value.caster)
                        - combat->npcPlacements.begin()) + 2 : 0;
                const auto casterGeneration = casterIndex == 2 ? life->generation
                    : casterIndex > 2 && casterIndex < combat->actors.size()
                        ? neighborLives[casterIndex - 3].generation : 0;
                if (!value.caster || !value.command || !value.source || !value.ammunition
                    || (!worldShot && (!value.target || !value.targetLife || !targetGeneration
                        || value.targetLife > targetGeneration))
                    || !((value.casterKind == 1 && value.targetKind == 2)
                        || (value.casterKind == 2 && value.targetKind == 1))
                    || (value.casterKind == 1 ? value.casterLife != 1
                        : value.casterKind != 2 || !casterGeneration || value.casterLife > casterGeneration)
                    || !value.releaseTick || value.releaseTick > tick || value.strength < 0 || value.strength > 1
                    || std::abs(norm - 1.f) > .001f
                    || std::ranges::any_of(value.position, [](float v) { return std::abs(v) > 100'000'000; })
                    || std::ranges::any_of(combat->arrows, [&](const auto& prior) {
                        return prior.casterKind == value.casterKind && prior.caster == value.caster
                            && prior.casterLife == value.casterLife && prior.command == value.command;
                    })) throw std::invalid_argument("Native arrow state invalid");
                combat->arrows.push_back(std::move(value));
            }
        }
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity, std::move(melee), std::move(combat), std::move(life), std::move(neighborLives), std::move(projectile), std::move(projectiles), std::move(timedEffects), std::move(castResource), casting};
    }
}
#endif
