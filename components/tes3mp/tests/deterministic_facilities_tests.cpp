#include <tes3mp/deterministic_random.hpp>
#include <tes3mp/fixed_tick_scheduler.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace
{
    class RewindableClock final : public TES3MP::MonotonicClock
    {
    public:
        explicit RewindableClock(std::uint64_t value)
            : mValue(value)
        {
        }

        TES3MP::MonotonicInstant now() const noexcept override
        {
            return TES3MP::MonotonicInstant::fromNanoseconds(mValue);
        }

        void set(std::uint64_t value) noexcept { mValue = value; }

    private:
        std::uint64_t mValue;
    };

    static_assert(!std::is_default_constructible_v<TES3MP::MonotonicInstant>);
    static_assert(!std::is_default_constructible_v<TES3MP::RandomStreamKey>);
    static_assert(!std::is_default_constructible_v<TES3MP::RandomStateV1>);
    static_assert(TES3MP::ScheduledTick::stepNumerator() == 1);
    static_assert(TES3MP::ScheduledTick::stepDenominator() == 30);
    static_assert(TES3MP::MaximumCatchUpTicks == 4);

    bool manual_clock_advances_checked_monotonic_instants_only()
    {
        TES3MP::TestSupport::ManualClock clock(
            TES3MP::MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max() - 5));
        return clock.advance(5) && clock.now().nanoseconds() == std::numeric_limits<std::uint64_t>::max()
            && !clock.advance(1) && clock.now().nanoseconds() == std::numeric_limits<std::uint64_t>::max();
    }

    bool thirty_hz_scheduler_has_no_accumulated_rounding_drift()
    {
        TES3MP::TestSupport::ManualClock boundaryClock(TES3MP::MonotonicInstant::fromNanoseconds(0));
        TES3MP::FixedTickScheduler boundaryScheduler(
            boundaryClock, boundaryClock.now(), TES3MP::ServerTick::fromValue(1).value());
        boundaryClock.advance(33'333'333);
        if (!boundaryScheduler.pump().ticks().empty())
            return false;
        boundaryClock.advance(1);
        if (boundaryScheduler.pump().ticks().size() != 1)
            return false;
        boundaryClock.advance(33'333'332);
        if (!boundaryScheduler.pump().ticks().empty())
            return false;
        boundaryClock.advance(1);
        if (boundaryScheduler.pump().ticks().size() != 1)
            return false;

        constexpr std::uint64_t Duration = 600'000'000'000ULL;
        constexpr std::array<std::uint64_t, 4> Steps{ 17'000'000, 59'000'000, 3'000'000, 91'000'000 };
        TES3MP::TestSupport::ManualClock clock(TES3MP::MonotonicInstant::fromNanoseconds(0));
        TES3MP::FixedTickScheduler scheduler(
            clock, TES3MP::MonotonicInstant::fromNanoseconds(0), TES3MP::ServerTick::fromValue(1).value());

        std::uint64_t elapsed = 0;
        std::uint64_t expectedTick = 1;
        std::size_t stepIndex = 0;
        while (elapsed < Duration)
        {
            const std::uint64_t increment = std::min(Steps[stepIndex++ % Steps.size()], Duration - elapsed);
            if (!clock.advance(increment))
                return false;
            elapsed += increment;
            const auto result = scheduler.pump();
            if (!result)
                return false;
            for (const auto tick : result.ticks())
            {
                if (tick.value().value() != expectedTick++)
                    return false;
            }
        }

        return expectedTick == 18'001 && scheduler.nextTick().value() == 18'001 && scheduler.pump().ticks().empty();
    }

    bool scheduler_pump_executes_at_most_four_due_ticks()
    {
        TES3MP::TestSupport::ManualClock clock(TES3MP::MonotonicInstant::fromNanoseconds(0));
        TES3MP::FixedTickScheduler scheduler(clock, clock.now(), TES3MP::ServerTick::fromValue(1).value());
        clock.advance(10'000'000'000ULL);
        const auto first = scheduler.pump();
        const auto second = scheduler.pump();
        return first && first.dueTickLag() == 300 && first.ticks().size() == 4 && first.ticks()[0].value().value() == 1
            && first.ticks()[3].value().value() == 4 && second && second.dueTickLag() == 296
            && second.ticks().size() == 4 && second.ticks()[0].value().value() == 5
            && second.ticks()[3].value().value() == 8;
    }

    bool stall_never_produces_variable_delta_or_tick_reordering()
    {
        TES3MP::TestSupport::ManualClock clock(TES3MP::MonotonicInstant::fromNanoseconds(100));
        TES3MP::FixedTickScheduler scheduler(clock, clock.now(), TES3MP::ServerTick::fromValue(1).value());
        clock.advance(1'000'000'000);
        std::uint64_t expected = 1;
        for (int pump = 0; pump < 8; ++pump)
        {
            const auto result = scheduler.pump();
            if (!result || result.ticks().size() > 4)
                return false;
            for (const auto scheduled : result.ticks())
            {
                if (scheduled.value().value() != expected++ || scheduled.stepNumerator() != 1
                    || scheduled.stepDenominator() != 30)
                    return false;
            }
        }
        return expected == 31;
    }

    bool scheduler_rejects_backwards_clock_deadline_overflow_and_tick_exhaustion()
    {
        RewindableClock rewindable(1'000);
        TES3MP::FixedTickScheduler rewindScheduler(
            rewindable, TES3MP::MonotonicInstant::fromNanoseconds(1'000), TES3MP::ServerTick::fromValue(1).value());
        if (!rewindScheduler.pump())
            return false;
        rewindable.set(999);
        const auto backwards = rewindScheduler.pump();

        TES3MP::TestSupport::ManualClock overflowClock(
            TES3MP::MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max() - 10));
        TES3MP::FixedTickScheduler overflowScheduler(
            overflowClock, overflowClock.now(), TES3MP::ServerTick::fromValue(1).value());
        const auto overflow = overflowScheduler.pump();

        TES3MP::TestSupport::ManualClock exhaustedClock(TES3MP::MonotonicInstant::fromNanoseconds(0));
        TES3MP::FixedTickScheduler exhaustedScheduler(exhaustedClock, exhaustedClock.now(),
            TES3MP::ServerTick::fromValue(std::numeric_limits<std::uint64_t>::max()).value());
        const auto exhausted = exhaustedScheduler.pump();

        return backwards.error() == TES3MP::SchedulerError::ClockMovedBackwards
            && rewindScheduler.nextTick().value() == 1 && overflow.error() == TES3MP::SchedulerError::DeadlineOverflow
            && overflowScheduler.nextTick().value() == 1 && exhausted.error() == TES3MP::SchedulerError::TickExhausted
            && exhaustedScheduler.nextTick().value() == std::numeric_limits<std::uint64_t>::max();
    }

    bool splitmix64_matches_version_one_test_vectors()
    {
        constexpr std::array<std::uint64_t, 5> Expected{
            0xe220a8397b1dcdafULL,
            0x6e789e6aa1b965f4ULL,
            0x06c45d188009454fULL,
            0xf88bb8a8724c81ecULL,
            0x1b39896a51a8749bULL,
        };
        TES3MP::SplitMix64 random(0);
        for (const std::uint64_t expected : Expected)
        {
            if (random.nextU64() != expected)
                return false;
        }
        return true;
    }

    bool xoshiro256_star_star_matches_version_one_test_vectors()
    {
        const auto key = TES3MP::RandomStreamKey::fromValues(7, 42).value();
        auto random = TES3MP::Xoshiro256StarStar::fromWorldSeed(0x0123456789abcdefULL, key);
        constexpr std::array<std::uint64_t, 6> Expected{
            0xcddba5003310d68bULL,
            0xea3ac57759a60030ULL,
            0x49a673d5e153bf15ULL,
            0x4718f14c988fb22fULL,
            0x89328ce7d53f5433ULL,
            0x58c06614d8a14953ULL,
        };
        for (const std::uint64_t expected : Expected)
        {
            if (random.nextU64() != expected)
                return false;
        }
        return random.snapshot().words()
            == std::array<std::uint64_t, 4>{ 0x62936ef45f3dd7b0ULL, 0x50766e7e5aaa74e5ULL, 0x83ae13a35ec20c17ULL,
                   0x07e7e3e2b7d11aadULL };
    }

    bool labeled_rng_streams_are_isolated_from_unrelated_draws()
    {
        const auto mainKey = TES3MP::RandomStreamKey::fromValues(1, 99).value();
        const auto otherKey = TES3MP::RandomStreamKey::fromValues(2, 99).value();
        auto baseline = TES3MP::Xoshiro256StarStar::fromWorldSeed(1234, mainKey);
        auto interleaved = TES3MP::Xoshiro256StarStar::fromWorldSeed(1234, mainKey);
        auto unrelated = TES3MP::Xoshiro256StarStar::fromWorldSeed(1234, otherKey);
        for (int index = 0; index < 64; ++index)
        {
            unrelated.nextU64();
            unrelated.nextU64();
            if (baseline.nextU64() != interleaved.nextU64())
                return false;
        }
        return baseline.snapshot() == interleaved.snapshot() && baseline.snapshot() != unrelated.snapshot();
    }

    bool rng_snapshot_restore_reproduces_future_values()
    {
        auto random = TES3MP::Xoshiro256StarStar::fromWorldSeed(88, TES3MP::RandomStreamKey::fromValues(4, 0).value());
        random.nextU64();
        const auto saved = random.snapshot();
        std::array<std::uint64_t, 8> future{};
        for (auto& value : future)
            value = random.nextU64();
        auto restored = TES3MP::Xoshiro256StarStar::restore(saved);
        for (const auto value : future)
        {
            if (restored.nextU64() != value)
                return false;
        }
        return !TES3MP::RandomStateV1::fromWords(0, 0, 0, 0).has_value();
    }

    bool uniform_below_is_unbiased_by_construction_and_zero_bound_does_not_consume()
    {
        auto random = TES3MP::Xoshiro256StarStar::restore(TES3MP::RandomStateV1::fromWords(1, 0, 0, 0).value());
        const auto beforeZero = random.snapshot();
        if (random.uniformBelow(0) || random.snapshot() != beforeZero)
            return false;

        const auto sampled = random.uniformBelow(10);
        auto twoDraws = TES3MP::Xoshiro256StarStar::restore(beforeZero);
        const auto rejected = twoDraws.nextU64();
        const auto accepted = twoDraws.nextU64();
        return rejected == 0 && accepted == 5760 && sampled == 0 && random.snapshot() == twoDraws.snapshot();
    }

    bool deterministic_facilities_compile_without_openmw_transport_runtime_or_platform_headers()
    {
        return !TES3MP::RandomStreamKey::fromValues(0, 1) && TES3MP::RandomStreamKey::fromValues(1, 0).has_value()
            && TES3MP::MonotonicInstant::fromNanoseconds(0).nanoseconds() == 0;
    }
}

int main()
{
    if (!manual_clock_advances_checked_monotonic_instants_only())
        return 1;
    if (!thirty_hz_scheduler_has_no_accumulated_rounding_drift())
        return 2;
    if (!scheduler_pump_executes_at_most_four_due_ticks())
        return 3;
    if (!stall_never_produces_variable_delta_or_tick_reordering())
        return 4;
    if (!scheduler_rejects_backwards_clock_deadline_overflow_and_tick_exhaustion())
        return 5;
    if (!splitmix64_matches_version_one_test_vectors())
        return 6;
    if (!xoshiro256_star_star_matches_version_one_test_vectors())
        return 7;
    if (!labeled_rng_streams_are_isolated_from_unrelated_draws())
        return 8;
    if (!rng_snapshot_restore_reproduces_future_values())
        return 9;
    if (!uniform_below_is_unbiased_by_construction_and_zero_bound_does_not_consume())
        return 10;
    if (!deterministic_facilities_compile_without_openmw_transport_runtime_or_platform_headers())
        return 11;
    return 0;
}
