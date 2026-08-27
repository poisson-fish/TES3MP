#ifndef TES3MP_TEST_SUPPORT_MANUAL_CLOCK_HPP
#define TES3MP_TEST_SUPPORT_MANUAL_CLOCK_HPP

#include <tes3mp/monotonic_clock.hpp>

#include <cstdint>

namespace TES3MP::TestSupport
{
    class ManualClock final : public MonotonicClock
    {
    public:
        constexpr explicit ManualClock(MonotonicInstant initial) noexcept
            : mNow(initial)
        {
        }

        MonotonicInstant now() const noexcept override { return mNow; }
        bool advance(std::uint64_t nanoseconds) noexcept;

    private:
        MonotonicInstant mNow;
    };
}

#endif
