#ifndef OPENMW_MWCLASS_NPCMOVEMENT_H
#define OPENMW_MWCLASS_NPCMOVEMENT_H

#include <algorithm>
#include <cmath>

namespace MWClass
{
    inline float creatureWalkSpeed(float speed, float minimum, float maximum)
    { return minimum + 0.01f * speed * (maximum - minimum); }
    // Context-free parts of the stock NPC movement rules. The single-player
    // class and the detached server actor supply their own live stats/settings.
    inline float normalizedEncumbrance(float weight, float capacity)
    {
        if (weight <= 0.f) return 0.f;
        if (capacity == 0.f) return 1.f + 1e-6f;
        return weight / capacity;
    }

    inline float npcWalkSpeed(float speed, float encumbrance, float minimum, float maximum,
        float encumberedEffect, float sneakMultiplier = 1.f)
    {
        return std::max(0.f, (minimum + 0.01f * speed * (maximum - minimum))
            * (1.f - encumberedEffect * encumbrance)) * sneakMultiplier;
    }

    inline float npcRunSpeed(float walk, float athletics, float athleticsBonus, float baseMultiplier)
    { return walk * (0.01f * athletics * athleticsBonus + baseMultiplier); }

    inline float npcSwimSpeed(float base, float swiftSwim, float athletics,
        float swimBase, float swimAthleticsMultiplier)
    { return base * (1.f + 0.01f * swiftSwim) * (swimBase + 0.01f * athletics * swimAthleticsMultiplier); }

    inline float npcFlySpeed(float speed, float levitate, float encumbrance,
        float minimum, float maximum, float encumberedEffect)
    {
        const float fly = minimum + 0.01f * (speed + levitate) * (maximum - minimum);
        return std::max(0.f, fly * (1.f - encumberedEffect * encumbrance));
    }

    inline float npcSlowFall(float magnitude)
    { return 1.f - std::clamp(magnitude * 0.005f, 0.f, 1.f); }

    inline float npcJumpSpeed(float encumbrance, float acrobatics, float jumpMagnitude,
        float fatigueTerm, bool running, float encumbranceBase, float encumbranceMultiplier,
        float acrobaticsBase, float acrobaticsMultiplier, float runMultiplier, float gravity)
    {
        if (encumbrance > 1.f) return 0.f;
        const float encumbranceTerm = encumbranceBase + encumbranceMultiplier * (1.f - encumbrance);
        float first = std::min(acrobatics, 50.f);
        const float second = std::max(0.f, acrobatics - 50.f);
        float result = acrobaticsBase + std::pow(first / 15.f, acrobaticsMultiplier);
        result += 3.f * second * acrobaticsMultiplier;
        result += jumpMagnitude * 64.f;
        result *= encumbranceTerm;
        if (running) result *= runMultiplier;
        return (result * fatigueTerm + gravity) / 3.f;
    }

    struct NpcMovementProfile
    {
        float walk, swim, fly, jump, airControl;
    };
    template<class Setting>
    float movementFatigueCost(float encumbrance, bool sneaking, bool swimming, bool running,
        float speedFactor, float seconds, const Setting& setting)
    {
        if (encumbrance > 1.f) return 0.f;
        const char* base = sneaking ? "fFatigueSneakBase" : swimming
            ? (running ? "fFatigueSwimRunBase" : "fFatigueSwimWalkBase") : "fFatigueRunBase";
        const char* mult = sneaking ? "fFatigueSneakMult" : swimming
            ? (running ? "fFatigueSwimRunMult" : "fFatigueSwimWalkMult") : "fFatigueRunMult";
        if (!sneaking && !swimming && !running) return 0.f;
        return (setting(base) + encumbrance * setting(mult)) * seconds * speedFactor;
    }
    template<class Setting>
    float jumpFatigueCost(float encumbrance, const Setting& setting)
    { return setting("fFatigueJumpBase") + std::min(1.f, encumbrance) * setting("fFatigueJumpMult"); }
    // Explicit player context shared by authoritative stepping and prediction.
    template<class Setting>
    NpcMovementProfile npcMovementProfile(float speed, float athletics, float acrobatics,
        float encumbrance, float fatigueTerm, float swiftSwim, float levitate, float jump,
        bool running, bool sneaking, bool werewolfRunning, bool inert, const Setting& setting, float gravity)
    {
        const float walk = npcWalkSpeed(speed, encumbrance, setting("fMinWalkSpeed"),
            setting("fMaxWalkSpeed"), setting("fEncumberedMoveEffect"));
        const float run = npcRunSpeed(walk, athletics, setting("fAthleticsRunBonus"), setting("fBaseRunMultiplier"));
        const float multiplier = werewolfRunning ? setting("fWereWolfRunMult") : 1.f;
        return {
            inert ? 0.f : (sneaking ? walk * setting("fSneakSpeedMultiplier") : running ? run : walk) * multiplier,
            inert ? 0.f : npcSwimSpeed(running ? run : walk, swiftSwim, athletics,
                setting("fSwimRunBase"), setting("fSwimRunAthleticsMult")) * multiplier,
            inert ? 0.f : npcFlySpeed(speed, levitate, encumbrance, setting("fMinFlySpeed"),
                setting("fMaxFlySpeed"), setting("fEncumberedMoveEffect")) * multiplier,
            inert || sneaking ? 0.f : npcJumpSpeed(encumbrance, acrobatics, jump, fatigueTerm, running,
                setting("fJumpEncumbranceBase"), setting("fJumpEncumbranceMultiplier"), setting("fJumpAcrobaticsBase"),
                setting("fJumpAcroMultiplier"), setting("fJumpRunMultiplier"), gravity),
            std::clamp(setting("fJumpMoveBase") + setting("fJumpMoveMult") * acrobatics / 100.f, 0.f, 1.f)};
    }
}

#endif
