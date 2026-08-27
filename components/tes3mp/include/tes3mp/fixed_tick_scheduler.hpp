#ifndef TES3MP_FIXED_TICK_SCHEDULER_HPP
#define TES3MP_FIXED_TICK_SCHEDULER_HPP

#include "monotonic_clock.hpp"
#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace TES3MP
{
    inline constexpr std::uint32_t ServerTicksPerSecond = 30;
    inline constexpr std::size_t MaximumCatchUpTicks = 4;

    enum class SchedulerError : std::uint8_t
    {
        None,
        ClockMovedBackwards,
        DeadlineOverflow,
        TickExhausted,
    };

    class ScheduledTick
    {
    public:
        constexpr explicit ScheduledTick(ServerTick value) noexcept
            : mValue(value)
        {
        }

        constexpr ServerTick value() const noexcept { return mValue; }
        static constexpr std::uint32_t stepNumerator() noexcept { return 1; }
        static constexpr std::uint32_t stepDenominator() noexcept { return ServerTicksPerSecond; }

        friend constexpr bool operator==(ScheduledTick, ScheduledTick) noexcept = default;

    private:
        ServerTick mValue;
    };

    class ScheduledTickBatch
    {
    public:
        constexpr std::size_t size() const noexcept { return mSize; }
        constexpr bool empty() const noexcept { return mSize == 0; }
        constexpr const ScheduledTick& operator[](std::size_t index) const noexcept { return mTicks[index]; }
        constexpr const ScheduledTick* begin() const noexcept { return mTicks.data(); }
        constexpr const ScheduledTick* end() const noexcept { return mTicks.data() + mSize; }

    private:
        friend class FixedTickScheduler;

        constexpr void push(ScheduledTick value) noexcept { mTicks[mSize++] = value; }

        std::array<ScheduledTick, MaximumCatchUpTicks> mTicks{
            ScheduledTick(ServerTick::initial()),
            ScheduledTick(ServerTick::initial()),
            ScheduledTick(ServerTick::initial()),
            ScheduledTick(ServerTick::initial()),
        };
        std::size_t mSize = 0;
    };

    class SchedulerPumpResult
    {
    public:
        constexpr SchedulerError error() const noexcept { return mError; }
        constexpr const ScheduledTickBatch& ticks() const noexcept { return mTicks; }
        constexpr std::uint64_t dueTickLag() const noexcept { return mDueTickLag; }
        constexpr explicit operator bool() const noexcept { return mError == SchedulerError::None; }

    private:
        friend class FixedTickScheduler;

        SchedulerError mError = SchedulerError::None;
        ScheduledTickBatch mTicks;
        std::uint64_t mDueTickLag = 0;
    };

    class FixedTickScheduler
    {
    public:
        FixedTickScheduler(const MonotonicClock& clock, MonotonicInstant epoch, ServerTick nextTick) noexcept;

        SchedulerPumpResult pump() noexcept;
        ServerTick nextTick() const noexcept { return mNextTick; }

    private:
        const MonotonicClock& mClock;
        MonotonicInstant mEpoch;
        MonotonicInstant mLastObservation;
        ServerTick mNextTick;
    };
}

#endif
