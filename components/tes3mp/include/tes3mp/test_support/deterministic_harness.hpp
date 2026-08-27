#ifndef TES3MP_TEST_SUPPORT_DETERMINISTIC_HARNESS_HPP
#define TES3MP_TEST_SUPPORT_DETERMINISTIC_HARNESS_HPP

#include "in_memory_link.hpp"
#include "manual_clock.hpp"

#include <tes3mp/deterministic_random.hpp>
#include <tes3mp/fixed_tick_scheduler.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace TES3MP::TestSupport
{
    class TestTraceDigestV1
    {
    public:
        static TestTraceDigestV1 fromTrace(std::span<const std::byte> trace) noexcept;
        constexpr std::uint64_t value() const noexcept { return mValue; }

        friend constexpr bool operator==(TestTraceDigestV1, TestTraceDigestV1) noexcept = default;

    private:
        constexpr explicit TestTraceDigestV1(std::uint64_t value) noexcept
            : mValue(value)
        {
        }

        std::uint64_t mValue;
    };

    class DeterministicHarness
    {
    public:
        static std::unique_ptr<DeterministicHarness> create(MonotonicInstant epoch, ServerTick nextTick,
            std::uint64_t worldSeed, RandomStreamKey randomKey, LinkBudget aToB, LinkBudget bToA);

        DeterministicHarness(const DeterministicHarness&) = delete;
        DeterministicHarness& operator=(const DeterministicHarness&) = delete;
        DeterministicHarness(DeterministicHarness&&) = delete;
        DeterministicHarness& operator=(DeterministicHarness&&) = delete;

        bool advanceClock(std::uint64_t nanoseconds);
        SchedulerPumpResult pumpScheduler();
        std::uint64_t drawRandom();
        LinkSendResult send(LinkDirection direction, std::span<const std::byte> message);
        std::optional<std::vector<std::byte>> receive(LinkDirection direction);
        void closeSend(LinkDirection direction);

        RandomStateV1 randomState() const noexcept { return mRandom.snapshot(); }
        const InMemoryDuplexLink& link() const noexcept { return mLink; }
        std::span<const std::byte> traceBytes() const noexcept { return mTrace; }
        TestTraceDigestV1 traceDigest() const noexcept { return TestTraceDigestV1::fromTrace(mTrace); }

    private:
        DeterministicHarness(MonotonicInstant epoch, ServerTick nextTick, std::uint64_t worldSeed,
            RandomStreamKey randomKey, InMemoryDuplexLink link);

        void appendByte(std::uint8_t value);
        void appendU64(std::uint64_t value);
        void appendBytes(std::span<const std::byte> value);

        ManualClock mClock;
        FixedTickScheduler mScheduler;
        RandomStreamKey mRandomKey;
        Xoshiro256StarStar mRandom;
        InMemoryDuplexLink mLink;
        std::vector<std::byte> mTrace;
    };
}

#endif
