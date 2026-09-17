#ifndef OPENMW_MWWORLD_PLACEDREFID_HPP
#define OPENMW_MWWORLD_PLACEDREFID_HPP

#include <components/esm3/cellref.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace MWWorld
{
    // Stable within an ordered gameplay loadout. Script-list entries occupy local
    // reader slots but contain no placed references (desktop builtin.omwscripts
    // is commonly absent on the server). Keep those slots out of wire identity.
    // The high bit separates placed containers from legacy operator mappings.
    inline constexpr uint64_t PlacedRefTag = uint64_t{1} << 63;

    inline std::optional<uint64_t> placedRefId(ESM::RefNum ref, std::span<const std::string> files)
    {
        if (!ref.isSet() || ref.mContentFile < 0 || size_t(ref.mContentFile) >= files.size())
            return std::nullopt;
        uint32_t plugin = 0;
        for (size_t i = 0; i <= size_t(ref.mContentFile); ++i)
        {
            if (Misc::StringUtils::ciEndsWith(files[i], ".omwscripts"))
            {
                if (i == size_t(ref.mContentFile)) return std::nullopt;
                continue;
            }
            if (i == size_t(ref.mContentFile))
                return PlacedRefTag | (uint64_t(plugin) << 32) | ref.mIndex;
            ++plugin;
        }
        return std::nullopt;
    }

    inline std::optional<ESM::RefNum> localPlacedRef(uint64_t id, std::span<const std::string> files)
    {
        if (!(id & PlacedRefTag)) return std::nullopt;
        const auto plugin = uint32_t((id & ~PlacedRefTag) >> 32);
        uint32_t current = 0;
        for (size_t i = 0; i < files.size(); ++i)
        {
            if (Misc::StringUtils::ciEndsWith(files[i], ".omwscripts")) continue;
            if (current++ == plugin)
            {
                const ESM::RefNum ref{uint32_t(id), int32_t(i)};
                return ref.isSet() ? std::optional(ref) : std::nullopt;
            }
        }
        return std::nullopt;
    }
}
#endif
