#ifndef TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#define TES3MP_NATIVE_ACTOR_CAMPAIGN_HPP
#include "actor_spawns.hpp"
#include <array>
#include <bit>
#include <cmath>

namespace TES3MP::Native
{
    inline constexpr uint64_t ActorCampaignMagic = 0x3150434154335354;
    struct ActorCampaign
    {
        std::span<const char> inventory, actor;
        uint64_t tick;
        std::array<float, 3> velocity;
    };
    inline ActorCampaign readActorCampaign(std::span<const char> bytes)
    {
        size_t offset = 0;
        if (getAreaWord(bytes, offset) != ActorCampaignMagic) throw std::invalid_argument("Native actor campaign version invalid");
        const auto inventorySize = getAreaWord(bytes, offset), actorSize = getAreaWord(bytes, offset), tick = getAreaWord(bytes, offset);
        std::array<float, 3> velocity;
        for (float& value : velocity)
        {
            const auto bits = getAreaWord(bytes, offset);
            value = std::bit_cast<float>(uint32_t(bits));
            if (bits > UINT32_MAX || !std::isfinite(value) || std::abs(value) > 4096)
                throw std::invalid_argument("Native actor velocity invalid");
        }
        if (!inventorySize || !actorSize || actorSize > 65536 || inventorySize > bytes.size()-offset
            || actorSize != bytes.size()-offset-inventorySize)
            throw std::invalid_argument("Native actor campaign lengths invalid");
        return {bytes.subspan(offset, size_t(inventorySize)), bytes.subspan(offset+size_t(inventorySize), size_t(actorSize)), tick, velocity};
    }
}
#endif
