#ifndef OPENMW_MECHANICS_AIRATING_H
#define OPENMW_MECHANICS_AIRATING_H

#include <algorithm>

namespace MWMechanics
{
    // Numeric parts of the stock aggression and flee decisions. Keep these
    // independent of World so a detached authoritative actor can use them.
    inline float fightDistanceBias(float distance, int base, float multiplier)
    { return base - multiplier * distance; }

    inline float fightDispositionBias(float disposition, float multiplier)
    { return (50.f - disposition) * multiplier; }

    inline bool aggressiveAtDistance(int modifiedFight, float distanceBias, float dispositionBias)
    { return modifiedFight + static_cast<int>(distanceBias + dispositionBias) >= 100; }

    inline float fleeRating(int modifiedFlee, float healthRatio, float healthMultiplier,
        float fleeMultiplier, float distanceBias)
    {
        if (modifiedFlee >= 100) return static_cast<float>(modifiedFlee);
        float rating = (1.f - healthRatio) * healthMultiplier + modifiedFlee * fleeMultiplier;
        if (rating != 0.f) rating += distanceBias;
        return rating;
    }

    inline bool fleeOverAttack(float rating, float antiFleeRating)
    { return (rating >= 100.f ? rating : 0.f) > antiFleeRating; }

    // AiCombat starts a flee run only while the target can attack, except for
    // long-range targets. The distance is measured from actor hulls, not centers.
    inline bool fleeWithinAttackDistance(bool lineOfSight, float attackDistance,
        float centerDistance, float actorHalfExtentY, float targetHalfExtentY)
    {
        return lineOfSight && (attackDistance >= 1000.f
            || centerDistance - actorHalfExtentY - targetHalfExtentY <= attackDistance);
    }

    inline int dispositionWithCharm(float derivedWithoutCharm, float charm, bool clamp = true)
    {
        const int value = static_cast<int>(derivedWithoutCharm + charm);
        return clamp ? std::clamp(value, 0, 100) : value;
    }
}

#endif
