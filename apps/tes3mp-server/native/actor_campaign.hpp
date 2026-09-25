#ifndef TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#define TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#include "actor_spawns.hpp"
#include "melee_animation.hpp"
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
    // OpenMW attribute, dynamic and skill StatState<float> fields for both
    // players and the selected NPC. Equipped item condition remains in the
    // nested equipment image, committed with this wrapper.
    struct ActorCampaignCombat
    {
        static constexpr size_t StatCount = 8 + 3 + 27;
        std::array<std::array<std::array<float, 5>, StatCount>, 3> actors{};
        uint32_t rng = 1;
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
        bool operator==(const ActorCampaignProjectile&) const = default;
    };
    struct ActorCampaignTimedEffect
    {
        uint64_t actor = 0;
        float magnitude = 0;
        uint64_t expiresTick = 0;
        bool operator==(const ActorCampaignTimedEffect&) const = default;
    };
    inline constexpr size_t MaximumActorTimedEffects = 16;
    struct ActorCampaign
    {
        std::span<const char> inventory, actor;
        uint64_t tick;
        std::array<float, 3> velocity;
        std::optional<ActorCampaignMelee> melee;
        std::optional<ActorCampaignCombat> combat;
        std::optional<ActorCampaignLife> life;
        std::optional<ActorCampaignProjectile> projectile;
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
            && magic != AreaActorCampaignMagic && magic != PlayerTargetActorCampaignMagic)
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
            || magic == PlayerTargetActorCampaignMagic)
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
                || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic)
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
            || magic == PlayerTargetActorCampaignMagic)
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
        }
        std::optional<ActorCampaignLife> life;
        if (magic == LifeActorCampaignMagic || magic == ProjectileActorCampaignMagic
            || magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
            || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic)
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
            if (count > ActorCampaignLife::MaximumDeaths || count > (bytes.size() - offset) / 24)
                throw std::invalid_argument("Native NPC death history bound invalid");
            state.deaths.reserve(size_t(count));
            for (size_t i = 0; i < count; ++i)
            {
                ActorDeathEvent event{getAreaWord(bytes, offset), getAreaWord(bytes, offset), getAreaWord(bytes, offset)};
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
        std::optional<ActorCampaignProjectile> projectile;
        if (magic == ProjectileActorCampaignMagic || magic == EnchantedProjectileActorCampaignMagic
            || magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic)
        {
            const auto present = getAreaWord(bytes, offset);
            if (present > 1) throw std::invalid_argument("Native projectile presence invalid");
            if (present)
            {
                auto& value = projectile.emplace();
                value.caster = getAreaWord(bytes, offset); value.source = getAreaWord(bytes, offset);
                value.target = getAreaWord(bytes, offset); value.generation = getAreaWord(bytes, offset);
                value.expiresTick = getAreaWord(bytes, offset);
                if (magic == EnchantedProjectileActorCampaignMagic || magic == TimedActorCampaignMagic
                    || magic == AreaActorCampaignMagic || magic == PlayerTargetActorCampaignMagic)
                {
                    value.sourceKind = getAreaWord(bytes, offset);
                    value.effectSource = getAreaWord(bytes, offset);
                    if (value.sourceKind > 1 || !value.effectSource
                        || (value.sourceKind == 0 && value.effectSource != value.source))
                        throw std::invalid_argument("Native projectile source identity invalid");
                }
                else value.effectSource = value.source;
                if (magic == PlayerTargetActorCampaignMagic)
                {
                    value.targetKind = getAreaWord(bytes, offset);
                    if (value.targetKind != 1 && value.targetKind != 2)
                        throw std::invalid_argument("Native projectile target kind invalid");
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
                if (!value.caster || !value.source || !value.target || !value.generation
                    || (value.targetKind == 2 && value.generation != life->generation)
                    || value.expiresTick <= tick
                    || value.expiresTick - tick > 90 || length2 < 1.f || length2 > 1e6f)
                    throw std::invalid_argument("Native projectile identity or lifetime invalid");
            }
        }
        std::vector<ActorCampaignTimedEffect> timedEffects;
        if (magic == TimedActorCampaignMagic || magic == AreaActorCampaignMagic
            || magic == PlayerTargetActorCampaignMagic)
        {
            const auto count = getAreaWord(bytes, offset);
            if (count > MaximumActorTimedEffects || count > (bytes.size() - offset) / 24)
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
                if (effect.actor >= 3 || !std::isfinite(effect.magnitude)
                    || effect.magnitude <= 0 || effect.magnitude > 1000
                    || effect.expiresTick <= tick || effect.expiresTick - tick > 108000)
                    throw std::invalid_argument("Native timed effect state invalid");
                timedEffects.push_back(effect);
            }
        }
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity, std::move(melee), std::move(combat), std::move(life), std::move(projectile), std::move(timedEffects)};
    }
}
#endif
