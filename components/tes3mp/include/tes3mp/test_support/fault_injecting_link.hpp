#ifndef TES3MP_TEST_SUPPORT_FAULT_INJECTING_LINK_HPP
#define TES3MP_TEST_SUPPORT_FAULT_INJECTING_LINK_HPP

#include "in_memory_link.hpp"

#include <tes3mp/deterministic_random.hpp>
#include <tes3mp/monotonic_clock.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace TES3MP::TestSupport
{
    inline constexpr std::uint32_t FaultRateScale = 1'000'000;
    inline constexpr std::size_t MaximumFaultPaths = 64;

    class FaultChannelId
    {
    public:
        static constexpr std::optional<FaultChannelId> fromValue(std::uint64_t value) noexcept
        {
            if (value == 0)
                return std::nullopt;
            return FaultChannelId(value);
        }

        constexpr std::uint64_t value() const noexcept { return mValue; }

        friend constexpr bool operator==(FaultChannelId, FaultChannelId) noexcept = default;

    private:
        constexpr explicit FaultChannelId(std::uint64_t value) noexcept
            : mValue(value)
        {
        }

        std::uint64_t mValue;
    };

    struct FaultPath
    {
        LinkDirection direction;
        FaultChannelId channel;

        friend constexpr bool operator==(FaultPath, FaultPath) noexcept = default;
    };

    class FaultProfile
    {
    public:
        static std::optional<FaultProfile> create(std::uint64_t minimumLatencyNanoseconds,
            std::uint64_t maximumJitterNanoseconds, std::uint64_t maximumReorderNanoseconds,
            std::uint32_t lossPartsPerMillion, std::uint32_t duplicationPartsPerMillion,
            std::size_t maximumPendingMessages, std::size_t maximumPendingBytes) noexcept;

        constexpr std::uint64_t minimumLatencyNanoseconds() const noexcept { return mMinimumLatencyNanoseconds; }
        constexpr std::uint64_t maximumJitterNanoseconds() const noexcept { return mMaximumJitterNanoseconds; }
        constexpr std::uint64_t maximumReorderNanoseconds() const noexcept { return mMaximumReorderNanoseconds; }
        constexpr std::uint32_t lossPartsPerMillion() const noexcept { return mLossPartsPerMillion; }
        constexpr std::uint32_t duplicationPartsPerMillion() const noexcept { return mDuplicationPartsPerMillion; }
        constexpr std::size_t maximumPendingMessages() const noexcept { return mMaximumPendingMessages; }
        constexpr std::size_t maximumPendingBytes() const noexcept { return mMaximumPendingBytes; }

    private:
        FaultProfile(std::uint64_t minimumLatencyNanoseconds, std::uint64_t maximumJitterNanoseconds,
            std::uint64_t maximumReorderNanoseconds, std::uint32_t lossPartsPerMillion,
            std::uint32_t duplicationPartsPerMillion, std::size_t maximumPendingMessages,
            std::size_t maximumPendingBytes) noexcept;

        std::uint64_t mMinimumLatencyNanoseconds;
        std::uint64_t mMaximumJitterNanoseconds;
        std::uint64_t mMaximumReorderNanoseconds;
        std::uint32_t mLossPartsPerMillion;
        std::uint32_t mDuplicationPartsPerMillion;
        std::size_t mMaximumPendingMessages;
        std::size_t mMaximumPendingBytes;
    };

    struct FaultPathConfiguration
    {
        FaultPath path;
        FaultProfile profile;
    };

    enum class FaultSendResult : std::uint8_t
    {
        Accepted,
        Dropped,
        WouldBlock,
        MessageTooLarge,
        Disconnected,
        UnconfiguredPath,
        TimeOverflow,
    };

    struct FaultPumpResult
    {
        std::size_t deliveredMessages = 0;
        std::size_t deliveredBytes = 0;
        bool aToBWouldBlock = false;
        bool bToAWouldBlock = false;
    };

    class FaultInjectingLink
    {
    public:
        static std::unique_ptr<FaultInjectingLink> create(MonotonicClock& clock, std::uint64_t faultSeed,
            InMemoryDuplexLink link, std::span<const FaultPathConfiguration> configurations);

        FaultInjectingLink(const FaultInjectingLink&) = delete;
        FaultInjectingLink& operator=(const FaultInjectingLink&) = delete;
        FaultInjectingLink(FaultInjectingLink&&) = delete;
        FaultInjectingLink& operator=(FaultInjectingLink&&) = delete;

        FaultSendResult send(FaultPath path, std::span<const std::byte> message);
        FaultPumpResult pump();
        std::optional<std::vector<std::byte>> receive(LinkDirection direction);

        bool setStalled(FaultPath path, bool stalled) noexcept;
        bool disconnect(FaultPath path) noexcept;
        bool isStalled(FaultPath path) const noexcept;
        bool isDisconnected(FaultPath path) const noexcept;
        std::size_t pendingMessages(FaultPath path) const noexcept;
        std::size_t pendingBytes(FaultPath path) const noexcept;
        std::size_t queuedMessages(LinkDirection direction) const noexcept;
        std::size_t queuedBytes(LinkDirection direction) const noexcept;

    private:
        struct ScheduledMessage
        {
            std::uint64_t dueNanoseconds;
            std::uint64_t ordinal;
            std::vector<std::byte> bytes;
        };

        struct PathState
        {
            PathState(FaultPath path, FaultProfile profile, std::uint64_t seed);

            FaultPath path;
            FaultProfile profile;
            Xoshiro256StarStar lossRandom;
            Xoshiro256StarStar duplicationRandom;
            Xoshiro256StarStar jitterRandom;
            Xoshiro256StarStar reorderRandom;
            std::vector<ScheduledMessage> pending;
            std::size_t pendingByteCount = 0;
            bool stalled = false;
            bool disconnected = false;
        };

        FaultInjectingLink(MonotonicClock& clock, InMemoryDuplexLink link, std::vector<PathState> paths);

        PathState* findPath(FaultPath path) noexcept;
        const PathState* findPath(FaultPath path) const noexcept;
        static bool rateHit(Xoshiro256StarStar& random, std::uint32_t partsPerMillion) noexcept;
        static std::uint64_t boundedDelay(Xoshiro256StarStar& random, std::uint64_t maximum) noexcept;

        MonotonicClock& mClock;
        InMemoryDuplexLink mLink;
        std::vector<PathState> mPaths;
        std::uint64_t mNextOrdinal = 0;
    };
}

#endif
