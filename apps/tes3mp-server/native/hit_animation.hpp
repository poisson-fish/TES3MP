#ifndef TES3MP_NATIVE_HIT_ANIMATION_HPP
#define TES3MP_NATIVE_HIT_ANIMATION_HPP

#include <array>
#include <span>
#include <string>
#include <components/sceneutil/textkeymap.hpp>

namespace TES3MP::Native
{
    inline constexpr size_t MaximumHitSources = 64;
    inline constexpr size_t MaximumHitResourceIdentity = 16 * 1024;
    struct HitAnimations
    {
        std::array<unsigned, 16> ticks{};
        unsigned count = 0;
        bool operator==(const HitAnimations&) const = default;
    };

    // Sources are in stock load order. The last source containing each group
    // wins independently; a female/custom layer need not replace every group.
    HitAnimations readHitAnimations(std::span<const SceneUtil::TextKeyMap* const> sources);

    struct BoundHitAnimations
    {
        HitAnimations animations;
        std::string resourceIdentity;
    };
}
#endif
