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
    struct ActorCampaignMelee
    {
        std::string identity;
        MeleeAnimation::Snapshot state;
        uint64_t target = 0;
        bool contact = false;
    };
    struct ActorCampaign
    {
        std::span<const char> inventory, actor;
        uint64_t tick;
        std::array<float, 3> velocity;
        std::optional<ActorCampaignMelee> melee;
    };
    inline ActorCampaign readActorCampaign(std::span<const char> bytes)
    {
        size_t offset = 0;
        const auto magic = getAreaWord(bytes, offset);
        if (magic != ActorCampaignMagic && magic != MeleeActorCampaignMagic && magic != ContactActorCampaignMagic)
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
        if (magic == MeleeActorCampaignMagic || magic == ContactActorCampaignMagic)
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
            if (magic == ContactActorCampaignMagic)
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
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity, std::move(melee)};
    }
}
#endif
