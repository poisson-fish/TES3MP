#include <tes3mp/test_support/deterministic_harness.hpp>

#include <utility>

namespace
{
    enum class TraceEvent : std::uint8_t
    {
        ClockAdvanced = 1,
        SchedulerPumped = 2,
        RandomDrawn = 3,
        LinkSent = 4,
        LinkReceived = 5,
        LinkClosed = 6,
    };

    constexpr std::uint64_t Fnv1aOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t Fnv1aPrime = 1099511628211ULL;
}

namespace TES3MP::TestSupport
{
    TestTraceDigestV1 TestTraceDigestV1::fromTrace(std::span<const std::byte> trace) noexcept
    {
        std::uint64_t digest = Fnv1aOffsetBasis;
        for (std::byte value : trace)
        {
            digest ^= std::to_integer<std::uint8_t>(value);
            digest *= Fnv1aPrime;
        }
        return TestTraceDigestV1(digest);
    }

    std::unique_ptr<DeterministicHarness> DeterministicHarness::create(MonotonicInstant epoch, ServerTick nextTick,
        std::uint64_t worldSeed, RandomStreamKey randomKey, LinkBudget aToB, LinkBudget bToA)
    {
        auto link = InMemoryDuplexLink::create(aToB, bToA);
        if (!link)
            return nullptr;
        return std::unique_ptr<DeterministicHarness>(
            new DeterministicHarness(epoch, nextTick, worldSeed, randomKey, std::move(*link)));
    }

    DeterministicHarness::DeterministicHarness(MonotonicInstant epoch, ServerTick nextTick, std::uint64_t worldSeed,
        RandomStreamKey randomKey, InMemoryDuplexLink link)
        : mClock(epoch)
        , mScheduler(mClock, epoch, nextTick)
        , mRandomKey(randomKey)
        , mRandom(Xoshiro256StarStar::fromWorldSeed(worldSeed, randomKey))
        , mLink(std::move(link))
        , mTrace{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'T' }, std::byte{ 1 } }
    {
    }

    void DeterministicHarness::appendByte(std::uint8_t value)
    {
        mTrace.push_back(static_cast<std::byte>(value));
    }

    void DeterministicHarness::appendU64(std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            appendByte(static_cast<std::uint8_t>(value >> shift));
    }

    void DeterministicHarness::appendBytes(std::span<const std::byte> value)
    {
        mTrace.insert(mTrace.end(), value.begin(), value.end());
    }

    bool DeterministicHarness::advanceClock(std::uint64_t nanoseconds)
    {
        if (!mClock.advance(nanoseconds))
            return false;
        appendByte(static_cast<std::uint8_t>(TraceEvent::ClockAdvanced));
        appendU64(nanoseconds);
        appendU64(mClock.now().nanoseconds());
        return true;
    }

    SchedulerPumpResult DeterministicHarness::pumpScheduler()
    {
        const SchedulerPumpResult result = mScheduler.pump();
        appendByte(static_cast<std::uint8_t>(TraceEvent::SchedulerPumped));
        appendByte(static_cast<std::uint8_t>(result.error()));
        appendU64(result.dueTickLag());
        appendByte(static_cast<std::uint8_t>(result.ticks().size()));
        for (ScheduledTick tick : result.ticks())
            appendU64(tick.value().value());
        return result;
    }

    std::uint64_t DeterministicHarness::drawRandom()
    {
        const std::uint64_t value = mRandom.nextU64();
        appendByte(static_cast<std::uint8_t>(TraceEvent::RandomDrawn));
        appendU64(mRandomKey.domainId());
        appendU64(mRandomKey.subjectId());
        appendU64(value);
        return value;
    }

    LinkSendResult DeterministicHarness::send(LinkDirection direction, std::span<const std::byte> message)
    {
        const LinkSendResult result = mLink.send(direction, message);
        appendByte(static_cast<std::uint8_t>(TraceEvent::LinkSent));
        appendByte(static_cast<std::uint8_t>(direction));
        appendByte(static_cast<std::uint8_t>(result));
        appendU64(message.size());
        appendBytes(message);
        return result;
    }

    std::optional<std::vector<std::byte>> DeterministicHarness::receive(LinkDirection direction)
    {
        auto message = mLink.receive(direction);
        appendByte(static_cast<std::uint8_t>(TraceEvent::LinkReceived));
        appendByte(static_cast<std::uint8_t>(direction));
        appendByte(message.has_value() ? 1 : 0);
        appendU64(message ? message->size() : 0);
        if (message)
            appendBytes(*message);
        return message;
    }

    void DeterministicHarness::closeSend(LinkDirection direction)
    {
        mLink.closeSend(direction);
        appendByte(static_cast<std::uint8_t>(TraceEvent::LinkClosed));
        appendByte(static_cast<std::uint8_t>(direction));
    }
}
