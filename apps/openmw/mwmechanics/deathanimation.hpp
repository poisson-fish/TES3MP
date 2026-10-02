#ifndef OPENMW_MWMECHANICS_DEATHANIMATION_HPP
#define OPENMW_MWMECHANICS_DEATHANIMATION_HPP
#include <array>
#include <string_view>
namespace MWMechanics
{
    inline constexpr std::array<std::string_view, 10> DeathAnimationGroups{
        "death1", "death2", "death3", "death4", "death5", "deathknockdown", "deathknockout",
        "swimdeath", "swimdeathknockdown", "swimdeathknockout"};
    // Preserve stock hit-state precedence over swimming and random-death fallback.
    template<class Has, class Pick>
    unsigned selectDeathAnimation(bool knockout, bool knockdown, bool hitSwimming, bool swimming,
        Has has, Pick pick)
    {
        const unsigned preferred = knockdown ? (hitSwimming ? 9 : 6)
            : knockout ? (hitSwimming ? 10 : 7) : swimming ? 8 : 0;
        if (preferred && has(DeathAnimationGroups[preferred - 1])) return preferred;
        unsigned count = 0;
        while (count < 5 && has(DeathAnimationGroups[count])) ++count;
        return count ? 1 + pick(count) : 0;
    }
}
#endif
