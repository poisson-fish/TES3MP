#include <tes3mp/fixed_tick_scheduler.hpp>

#include <algorithm>
#include <limits>
#include <optional>

namespace
{
    constexpr std::uint64_t NanosecondsPerSecond = 1'000'000'000;

    std::optional<std::uint64_t> tickOffsetNanoseconds(TES3MP::ServerTick tick) noexcept
    {
        const std::uint64_t quotient = tick.value() / TES3MP::ServerTicksPerSecond;
        const std::uint64_t remainder = tick.value() % TES3MP::ServerTicksPerSecond;
        const std::uint64_t remainderNanoseconds
            = (remainder * NanosecondsPerSecond + TES3MP::ServerTicksPerSecond - 1) / TES3MP::ServerTicksPerSecond;

        if (quotient > (std::numeric_limits<std::uint64_t>::max() - remainderNanoseconds) / NanosecondsPerSecond)
            return std::nullopt;
        return quotient * NanosecondsPerSecond + remainderNanoseconds;
    }

    std::optional<std::uint64_t> deadlineNanoseconds(TES3MP::MonotonicInstant epoch, TES3MP::ServerTick tick) noexcept
    {
        const auto offset = tickOffsetNanoseconds(tick);
        if (!offset || *offset > std::numeric_limits<std::uint64_t>::max() - epoch.nanoseconds())
            return std::nullopt;
        return epoch.nanoseconds() + *offset;
    }

    std::uint64_t maximumDueTick(std::uint64_t elapsedNanoseconds) noexcept
    {
        const std::uint64_t wholeSeconds = elapsedNanoseconds / NanosecondsPerSecond;
        const std::uint64_t remainder = elapsedNanoseconds % NanosecondsPerSecond;
        return wholeSeconds * TES3MP::ServerTicksPerSecond
            + (remainder * TES3MP::ServerTicksPerSecond) / NanosecondsPerSecond;
    }
}

namespace TES3MP
{
    FixedTickScheduler::FixedTickScheduler(
        const MonotonicClock& clock, MonotonicInstant epoch, ServerTick nextTick) noexcept
        : mClock(clock)
        , mEpoch(epoch)
        , mLastObservation(epoch)
        , mNextTick(nextTick)
    {
    }

    SchedulerPumpResult FixedTickScheduler::pump() noexcept
    {
        SchedulerPumpResult result;
        const MonotonicInstant observation = mClock.now();
        if (observation < mLastObservation)
        {
            result.mError = SchedulerError::ClockMovedBackwards;
            return result;
        }

        if (mNextTick.value() == std::numeric_limits<std::uint64_t>::max())
        {
            result.mError = SchedulerError::TickExhausted;
            return result;
        }

        const auto nextDeadline = deadlineNanoseconds(mEpoch, mNextTick);
        if (!nextDeadline)
        {
            result.mError = SchedulerError::DeadlineOverflow;
            return result;
        }

        if (observation.nanoseconds() < mEpoch.nanoseconds() || observation.nanoseconds() < *nextDeadline)
        {
            mLastObservation = observation;
            return result;
        }

        const std::uint64_t elapsed = observation.nanoseconds() - mEpoch.nanoseconds();
        const std::uint64_t latestDue = maximumDueTick(elapsed);
        result.mDueTickLag = latestDue - mNextTick.value() + 1;
        const std::uint64_t emitted = std::min<std::uint64_t>(result.mDueTickLag, MaximumCatchUpTicks);

        ServerTick tick = mNextTick;
        for (std::uint64_t index = 0; index < emitted; ++index)
        {
            result.mTicks.push(ScheduledTick(tick));
            tick = tick.next().value();
        }

        mNextTick = tick;
        mLastObservation = observation;
        return result;
    }
}
