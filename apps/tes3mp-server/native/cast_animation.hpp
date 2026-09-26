#ifndef TES3MP_NATIVE_CAST_ANIMATION_HPP
#define TES3MP_NATIVE_CAST_ANIMATION_HPP
#include <array>
#include <cstdint>
#include <string>
#include <components/sceneutil/textkeymap.hpp>

namespace TES3MP::Native
{
    // CPU timing from the same spellcast range keys as CharacterController.
    // Time advances only with committed active simulation ticks.
    struct CastAnimation
    {
        uint32_t releaseTicks = 0, stopTicks = 0;
        bool operator==(const CastAnimation&) const = default;
    };
    std::array<CastAnimation, 3> readCastAnimations(const SceneUtil::TextKeyMap& keys);
    struct BoundCastAnimations
    {
        std::array<CastAnimation, 3> ranges;
        std::string resourceIdentity;
    };
}
#endif
