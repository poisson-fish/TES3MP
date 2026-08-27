#ifndef TES3MP_MONOTONIC_CLOCK_HPP
#define TES3MP_MONOTONIC_CLOCK_HPP

#include <compare>
#include <cstdint>

namespace TES3MP
{
    class MonotonicInstant
    {
    public:
        using Value = std::uint64_t;

        static constexpr MonotonicInstant fromNanoseconds(Value value) noexcept { return MonotonicInstant(value); }

        constexpr Value nanoseconds() const noexcept { return mNanoseconds; }

        friend constexpr bool operator==(MonotonicInstant, MonotonicInstant) noexcept = default;
        friend constexpr auto operator<=>(MonotonicInstant, MonotonicInstant) noexcept = default;

    private:
        constexpr explicit MonotonicInstant(Value nanoseconds) noexcept
            : mNanoseconds(nanoseconds)
        {
        }

        Value mNanoseconds;
    };

    class MonotonicClock
    {
    public:
        virtual ~MonotonicClock() = default;
        virtual MonotonicInstant now() const noexcept = 0;
    };
}

#endif
