#ifndef TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#define TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#include "actor_spawns.hpp"
#include "melee_animation.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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
    inline constexpr bool hasGeneralConstantState(uint64_t magic)
    { return magic == GeneralConstantActorCampaignMagic || magic == CasterActorCampaignMagic; }
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
    struct ActorCampaignCombat
    {
        static constexpr size_t StatCount = 8 + 3 + 27;
        std::array<std::array<std::array<float, 5>, StatCount>, 3> actors{};
        uint32_t rng = 1;
        std::array<bool, 3> knockedDown{};
        // Remaining CPU hit animation frames; paused for inactive actors.
        std::array<uint32_t, 3> hitRecoveryTicks{};
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
        static constexpr size_t MaximumDeaths = 1024;
        uint64_t generation = 1;
        uint64_t bornTick = 0;
        uint64_t respawnTick = 0;
        std::array<std::array<float, 5>, ActorCampaignCombat::StatCount> spawnStats{};
        std::vector<char> spawnActor;
        std::vector<char> spawnInventory;
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
        uint64_t effectIndex = 0, caster = 0, source = 0, sourceKind = 0;
        float resistance = 0;
        uint64_t startTick = 0, durationTicks = 0;
        // 0: no argument; 1..8: attribute; 9..35: skill. Ordinal preserves duplicate effects.
        uint64_t argument = 0, ordinal = 0;
        uint64_t casterKind = 0, casterLife = 0;
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
        std::optional<ActorCampaignProjectile> projectile;
        std::vector<ActorCampaignProjectile> projectiles;
        std::vector<ActorCampaignTimedEffect> timedEffects;
    };
    inline ActorCampaign readActorCampaign(std::span<const char> bytes)
    {
        size_t offset = 0;
        const auto magic = getAreaWord(bytes, offset);
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
                    || (value.state.mReleased != bool(value.target)))
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
                        && state.actors[actor][10][2] < 0;
                    if (value > 1 || bool(value) != expected)
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
            if (count > ActorCampaignLife::MaximumDeaths
                || count > (bytes.size() - offset) / (magic == CasterActorCampaignMagic ? 40 : 24))
                throw std::invalid_argument("Native NPC death history bound invalid");
            state.deaths.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                ActorDeathEvent event{getAreaWord(bytes, offset), getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
                if (magic == CasterActorCampaignMagic)
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
            if (magic == CasterActorCampaignMagic
                && state.deaths.size() != state.generation - (state.respawnTick ? 0 : 1))
                throw std::invalid_argument("Native caster life history incomplete");
            if (!state.deaths.empty() && (state.respawnTick
                    ? state.respawnTick <= state.deaths.back().tick
                    : state.bornTick <= state.deaths.back().tick))
                throw std::invalid_argument("Native NPC life chronology invalid");
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
                    if (value.targetKind != 1 && value.targetKind != 2)
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
                if (magic == CasterActorCampaignMagic)
                {
                    value.casterKind = getAreaWord(bytes, offset);
                    value.casterLife = getAreaWord(bytes, offset);
                    validateActorCaster({value.caster, value.casterKind, value.casterLife}, life->generation);
                }
                if (!value.caster || !value.source || !value.target || !value.generation
                    || (value.targetKind == 2 && value.generation != life->generation)
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
            const size_t effectBytes = magic == CasterActorCampaignMagic ? 112 : hasGeneralConstantState(magic) ? 96 : magic == EffectActorCampaignMagic || hasConstantState(magic) ? 80 : 24;
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
                            || (effect.sourceKind != 3 && (effect.argument || effect.ordinal)))
                            throw std::invalid_argument("Native effect argument or ordinal invalid");
                    }
                    else if (effect.sourceKind == 3) effect.argument = 8; // V36 Luck.
                    if (magic == CasterActorCampaignMagic)
                    {
                        effect.casterKind = getAreaWord(bytes, offset);
                        effect.casterLife = getAreaWord(bytes, offset);
                        validateActorCaster({effect.caster, effect.casterKind, effect.casterLife}, life->generation);
                    }
                    if (!effect.effectIndex || effect.effectIndex > 255 || !effect.caster || !effect.source
                        || effect.sourceKind > (hasConstantState(magic) ? 3u : 2u)
                        || !std::isfinite(effect.resistance)
                        || effect.resistance < -20000 || effect.resistance > 100
                        || effect.startTick > tick
                        || (effect.sourceKind == 3
                            ? (!hasConstantState(magic) || effect.durationTicks != 0
                                || effect.expiresTick != UINT64_MAX)
                            : (!effect.durationTicks || effect.durationTicks > 108000)))
                        throw std::invalid_argument("Native effect identity or duration invalid");
                }
                if (effect.actor >= 3 || !std::isfinite(effect.magnitude)
                    || (effect.magnitude < 0 || (effect.magnitude == 0 && effect.sourceKind != 3)) || effect.magnitude > (hasKnockoutState(magic) && effect.effectIndex ? 100000 : 1000)
                    || effect.expiresTick <= tick
                    || (effect.sourceKind != 3 && effect.expiresTick - tick > 108000))
                    throw std::invalid_argument("Native timed effect state invalid");
                timedEffects.push_back(effect);
            }
        }
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity, std::move(melee), std::move(combat), std::move(life), std::move(projectile), std::move(projectiles), std::move(timedEffects)};
    }
}
#endif
