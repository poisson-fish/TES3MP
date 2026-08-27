#include <tes3mp/test_support/manual_clock.hpp>

#include <limits>

namespace TES3MP::TestSupport
{
    bool ManualClock::advance(std::uint64_t nanoseconds) noexcept
    {
        if (nanoseconds > std::numeric_limits<std::uint64_t>::max() - mNow.nanoseconds())
            return false;
        mNow = MonotonicInstant::fromNanoseconds(mNow.nanoseconds() + nanoseconds);
        return true;
    }
}
