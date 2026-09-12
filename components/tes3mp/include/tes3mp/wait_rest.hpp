#ifndef TES3MP_WAIT_REST_HPP
#define TES3MP_WAIT_REST_HPP

#include <cstdint>

namespace TES3MP
{
    inline constexpr std::uint8_t MaximumWaitRestHours = 24;

    enum class WaitRestMode : std::uint8_t
    {
        Wait = 1,
        Rest = 2,
    };
}

#endif
