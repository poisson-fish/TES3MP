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
    struct ActorCampaign
    {
        std::span<const char> inventory, actor;
        uint64_t tick;
        std::array<float, 3> velocity;
        std::optional<ActorCampaignMelee> melee;
        std::optional<ActorCampaignCombat> combat;
        std::optional<ActorCampaignLife> life;
    };
    inline ActorCampaign readActorCampaign(std::span<const char> bytes)
    {
        size_t offset = 0;
        const auto magic = getAreaWord(bytes, offset);
        if (magic != ActorCampaignMagic && magic != MeleeActorCampaignMagic
            && magic != ContactActorCampaignMagic && magic != CombatActorCampaignMagic
            && magic != LifeActorCampaignMagic)
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
            || magic == CombatActorCampaignMagic || magic == LifeActorCampaignMagic)
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
            if (magic == ContactActorCampaignMagic || magic == CombatActorCampaignMagic || magic == LifeActorCampaignMagic)
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
        if (magic == CombatActorCampaignMagic || magic == LifeActorCampaignMagic)
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
        if (magic == LifeActorCampaignMagic)
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
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity, std::move(melee), std::move(combat), std::move(life)};
    }
}
#endif
