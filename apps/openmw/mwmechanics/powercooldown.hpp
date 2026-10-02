#ifndef OPENMW_MWMECHANICS_POWERCOOLDOWN_H
#define OPENMW_MWMECHANICS_POWERCOOLDOWN_H

#include <cstdint>

namespace MWMechanics
{
    inline constexpr double PowerCooldownHours = 24;
    inline constexpr uint64_t PowerCooldownMilliseconds = uint64_t(PowerCooldownHours * 3600000);
}

#endif
