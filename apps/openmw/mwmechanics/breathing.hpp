#ifndef OPENMW_MWMECHANICS_BREATHING_H
#define OPENMW_MWMECHANICS_BREATHING_H

#include <algorithm>

namespace MWMechanics
{
    struct BreathStep
    {
        float remaining;
        bool drowning;
    };

    // Stock breath timer without World, sound or UI ownership. The caller
    // applies damage and presentation after staging the timer.
    inline BreathStep advanceBreath(float remaining, float holdTime, float seconds,
        bool submerged, bool knockedOutUnderwater, bool waterBreathing)
    {
        if (remaining < 0.f) remaining = holdTime;
        if ((!submerged && !knockedOutUnderwater) || waterBreathing)
            return {holdTime, false};
        remaining = knockedOutUnderwater ? 0.f : std::max(0.f, remaining - seconds);
        return {remaining, remaining == 0.f};
    }
}

#endif
