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
    struct KnockoutAnimation
    {
        unsigned stop = 1, loopStart = 0, loopStop = 0;
        bool operator==(const KnockoutAnimation&) const = default;
        // Frame zero starts at the authored start key. Disabling the loop lets
        // the current animation continue through its authored get-up tail.
        unsigned advance(unsigned frame, bool exhausted) const;
    };
    KnockoutAnimation readKnockoutAnimation(std::span<const SceneUtil::TextKeyMap* const> sources,
        std::string_view group = "knockout");
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
        KnockoutAnimation knockout, knockdown;
        std::array<unsigned, 10> deaths{};
    };
    std::array<unsigned, 10> readDeathAnimations(std::span<const SceneUtil::TextKeyMap* const> sources);
}
#endif
