#ifndef TES3MP_NATIVE_WORLD_SCRIPT_CAMPAIGN_HPP
#define TES3MP_NATIVE_WORLD_SCRIPT_CAMPAIGN_HPP
#include "actor_spawns.hpp"

namespace TES3MP::Native
{
    inline constexpr uint64_t WorldScriptCampaignMagic = 0x3053435754335354;
    inline constexpr size_t MaximumWorldScriptImageBytes = 32768;
    inline std::pair<std::span<const char>, std::span<const char>> readWorldScriptCampaign(
        std::span<const char> bytes)
    {
        size_t offset = 0;
        if (bytes.size() > MaximumNativeInventoryImageBytes
            || getAreaWord(bytes, offset) != WorldScriptCampaignMagic)
            throw std::invalid_argument("Shared script campaign version or size invalid");
        const auto length = getAreaWord(bytes, offset);
        if (!length || length > MaximumWorldScriptImageBytes || length > bytes.size() - offset)
            throw std::invalid_argument("Shared script campaign field outside bound");
        return {bytes.subspan(offset, size_t(length)), bytes.subspan(offset + size_t(length))};
    }
    inline std::vector<char> sealWorldScriptCampaign(std::span<const char> inner, std::span<const char> script)
    {
        if (script.empty() || script.size() > MaximumWorldScriptImageBytes
            || inner.size() > MaximumNativeInventoryImageBytes - 16 - script.size())
            throw std::invalid_argument("Shared script campaign exceeds image budget");
        std::vector<char> bytes;
        bytes.reserve(16 + script.size() + inner.size());
        putAreaWord(bytes, WorldScriptCampaignMagic); putAreaWord(bytes, script.size());
        bytes.insert(bytes.end(), script.begin(), script.end());
        bytes.insert(bytes.end(), inner.begin(), inner.end());
        return bytes;
    }
}
#endif
