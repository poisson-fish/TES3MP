#include <tes3mp/test_support/fault_injecting_link.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace
{
    constexpr std::uint64_t LossDomainAtoB = 0x5446334c41000001ULL;
    constexpr std::uint64_t DuplicationDomainAtoB = 0x5446334441000001ULL;
    constexpr std::uint64_t JitterDomainAtoB = 0x5446334a41000001ULL;
    constexpr std::uint64_t ReorderDomainAtoB = 0x5446335241000001ULL;
    constexpr std::uint64_t LossDomainBtoA = 0x5446334c42000001ULL;
    constexpr std::uint64_t DuplicationDomainBtoA = 0x5446334442000001ULL;
    constexpr std::uint64_t JitterDomainBtoA = 0x5446334a42000001ULL;
    constexpr std::uint64_t ReorderDomainBtoA = 0x5446335242000001ULL;

    TES3MP::RandomStreamKey streamKey(
        TES3MP::TestSupport::FaultPath path, std::uint64_t aToBDomain, std::uint64_t bToADomain)
    {
        const std::uint64_t domain
            = path.direction == TES3MP::TestSupport::LinkDirection::AtoB ? aToBDomain : bToADomain;
        return TES3MP::RandomStreamKey::fromValues(domain, path.channel.value()).value();
    }
}

namespace TES3MP::TestSupport
{
    std::optional<FaultProfile> FaultProfile::create(std::uint64_t minimumLatencyNanoseconds,
        std::uint64_t maximumJitterNanoseconds, std::uint64_t maximumReorderNanoseconds,
        std::uint32_t lossPartsPerMillion, std::uint32_t duplicationPartsPerMillion, std::size_t maximumPendingMessages,
        std::size_t maximumPendingBytes) noexcept
    {
        if (lossPartsPerMillion > FaultRateScale || duplicationPartsPerMillion > FaultRateScale
            || maximumPendingMessages == 0 || maximumPendingBytes == 0
            || maximumJitterNanoseconds == std::numeric_limits<std::uint64_t>::max()
            || maximumReorderNanoseconds == std::numeric_limits<std::uint64_t>::max()
            || maximumJitterNanoseconds > std::numeric_limits<std::uint64_t>::max() - minimumLatencyNanoseconds
            || maximumReorderNanoseconds
                > std::numeric_limits<std::uint64_t>::max() - minimumLatencyNanoseconds - maximumJitterNanoseconds)
            return std::nullopt;
        return FaultProfile(minimumLatencyNanoseconds, maximumJitterNanoseconds, maximumReorderNanoseconds,
            lossPartsPerMillion, duplicationPartsPerMillion, maximumPendingMessages, maximumPendingBytes);
    }

    FaultProfile::FaultProfile(std::uint64_t minimumLatencyNanoseconds, std::uint64_t maximumJitterNanoseconds,
        std::uint64_t maximumReorderNanoseconds, std::uint32_t lossPartsPerMillion,
        std::uint32_t duplicationPartsPerMillion, std::size_t maximumPendingMessages,
        std::size_t maximumPendingBytes) noexcept
        : mMinimumLatencyNanoseconds(minimumLatencyNanoseconds)
        , mMaximumJitterNanoseconds(maximumJitterNanoseconds)
        , mMaximumReorderNanoseconds(maximumReorderNanoseconds)
        , mLossPartsPerMillion(lossPartsPerMillion)
        , mDuplicationPartsPerMillion(duplicationPartsPerMillion)
        , mMaximumPendingMessages(maximumPendingMessages)
        , mMaximumPendingBytes(maximumPendingBytes)
    {
    }

    FaultInjectingLink::PathState::PathState(FaultPath pathValue, FaultProfile profileValue, std::uint64_t seed)
        : path(pathValue)
        , profile(profileValue)
        , lossRandom(Xoshiro256StarStar::fromWorldSeed(seed, streamKey(path, LossDomainAtoB, LossDomainBtoA)))
        , duplicationRandom(
              Xoshiro256StarStar::fromWorldSeed(seed, streamKey(path, DuplicationDomainAtoB, DuplicationDomainBtoA)))
        , jitterRandom(Xoshiro256StarStar::fromWorldSeed(seed, streamKey(path, JitterDomainAtoB, JitterDomainBtoA)))
        , reorderRandom(Xoshiro256StarStar::fromWorldSeed(seed, streamKey(path, ReorderDomainAtoB, ReorderDomainBtoA)))
    {
    }

    std::unique_ptr<FaultInjectingLink> FaultInjectingLink::create(MonotonicClock& clock, std::uint64_t faultSeed,
        InMemoryDuplexLink link, std::span<const FaultPathConfiguration> configurations)
    {
        if (configurations.empty() || configurations.size() > MaximumFaultPaths)
            return nullptr;

        std::vector<PathState> paths;
        paths.reserve(configurations.size());
        for (const FaultPathConfiguration& configuration : configurations)
        {
            if (std::any_of(paths.begin(), paths.end(),
                    [&](const PathState& state) { return state.path == configuration.path; }))
                return nullptr;
            paths.emplace_back(configuration.path, configuration.profile, faultSeed);
        }
        return std::unique_ptr<FaultInjectingLink>(new FaultInjectingLink(clock, std::move(link), std::move(paths)));
    }

    FaultInjectingLink::FaultInjectingLink(MonotonicClock& clock, InMemoryDuplexLink link, std::vector<PathState> paths)
        : mClock(clock)
        , mLink(std::move(link))
        , mPaths(std::move(paths))
    {
    }

    FaultInjectingLink::PathState* FaultInjectingLink::findPath(FaultPath path) noexcept
    {
        const auto found
            = std::find_if(mPaths.begin(), mPaths.end(), [&](const PathState& state) { return state.path == path; });
        return found == mPaths.end() ? nullptr : &*found;
    }

    const FaultInjectingLink::PathState* FaultInjectingLink::findPath(FaultPath path) const noexcept
    {
        const auto found
            = std::find_if(mPaths.begin(), mPaths.end(), [&](const PathState& state) { return state.path == path; });
        return found == mPaths.end() ? nullptr : &*found;
    }

    bool FaultInjectingLink::rateHit(Xoshiro256StarStar& random, std::uint32_t partsPerMillion) noexcept
    {
        if (partsPerMillion == 0)
            return false;
        if (partsPerMillion == FaultRateScale)
            return true;
        return random.uniformBelow(FaultRateScale).value() < partsPerMillion;
    }

    std::uint64_t FaultInjectingLink::boundedDelay(Xoshiro256StarStar& random, std::uint64_t maximum) noexcept
    {
        if (maximum == 0)
            return 0;
        return random.uniformBelow(maximum + 1).value();
    }

    FaultSendResult FaultInjectingLink::send(FaultPath path, std::span<const std::byte> message)
    {
        PathState* state = findPath(path);
        if (state == nullptr)
            return FaultSendResult::UnconfiguredPath;
        if (state->disconnected)
            return FaultSendResult::Disconnected;

        const LinkBudget baseBudget = mLink.budget(path.direction);
        if (message.size() > state->profile.maximumPendingBytes() || message.size() > baseBudget.maximumBytes)
            return FaultSendResult::MessageTooLarge;
        if (rateHit(state->lossRandom, state->profile.lossPartsPerMillion()))
            return FaultSendResult::Dropped;

        const std::size_t copies
            = rateHit(state->duplicationRandom, state->profile.duplicationPartsPerMillion()) ? 2 : 1;
        if (message.size() != 0 && copies > std::numeric_limits<std::size_t>::max() / message.size())
            return FaultSendResult::WouldBlock;
        const std::size_t addedBytes = copies * message.size();
        if (copies > state->profile.maximumPendingMessages() - state->pending.size()
            || addedBytes > state->profile.maximumPendingBytes() - state->pendingByteCount)
            return FaultSendResult::WouldBlock;
        if (copies > std::numeric_limits<std::uint64_t>::max() - mNextOrdinal)
            return FaultSendResult::TimeOverflow;

        std::vector<ScheduledMessage> scheduled;
        scheduled.reserve(copies);
        const std::uint64_t now = mClock.now().nanoseconds();
        for (std::size_t copy = 0; copy < copies; ++copy)
        {
            const std::uint64_t jitter = boundedDelay(state->jitterRandom, state->profile.maximumJitterNanoseconds());
            const std::uint64_t reorder
                = boundedDelay(state->reorderRandom, state->profile.maximumReorderNanoseconds());
            const std::uint64_t totalDelay = state->profile.minimumLatencyNanoseconds() + jitter + reorder;
            if (totalDelay > std::numeric_limits<std::uint64_t>::max() - now)
                return FaultSendResult::TimeOverflow;
            scheduled.push_back(
                { now + totalDelay, mNextOrdinal + copy, std::vector<std::byte>(message.begin(), message.end()) });
        }

        mNextOrdinal += copies;
        state->pending.insert(
            state->pending.end(), std::make_move_iterator(scheduled.begin()), std::make_move_iterator(scheduled.end()));
        state->pendingByteCount += addedBytes;
        return FaultSendResult::Accepted;
    }

    FaultPumpResult FaultInjectingLink::pump()
    {
        FaultPumpResult result;
        bool aToBBlocked = false;
        bool bToABlocked = false;
        const std::uint64_t now = mClock.now().nanoseconds();
        while (true)
        {
            PathState* selectedPath = nullptr;
            std::size_t selectedIndex = 0;
            for (PathState& state : mPaths)
            {
                if (state.stalled || state.disconnected || (state.path.direction == LinkDirection::AtoB && aToBBlocked)
                    || (state.path.direction == LinkDirection::BtoA && bToABlocked))
                    continue;
                for (std::size_t index = 0; index < state.pending.size(); ++index)
                {
                    const ScheduledMessage& candidate = state.pending[index];
                    if (candidate.dueNanoseconds > now)
                        continue;
                    if (selectedPath == nullptr
                        || candidate.dueNanoseconds < selectedPath->pending[selectedIndex].dueNanoseconds
                        || (candidate.dueNanoseconds == selectedPath->pending[selectedIndex].dueNanoseconds
                            && candidate.ordinal < selectedPath->pending[selectedIndex].ordinal))
                    {
                        selectedPath = &state;
                        selectedIndex = index;
                    }
                }
            }

            if (selectedPath == nullptr)
                break;

            ScheduledMessage& selected = selectedPath->pending[selectedIndex];
            const LinkDirection direction = selectedPath->path.direction;
            const LinkSendResult sendResult = mLink.send(direction, selected.bytes);
            if (sendResult != LinkSendResult::Accepted)
            {
                if (direction == LinkDirection::AtoB)
                {
                    aToBBlocked = true;
                    result.aToBWouldBlock = sendResult == LinkSendResult::WouldBlock;
                }
                else
                {
                    bToABlocked = true;
                    result.bToAWouldBlock = sendResult == LinkSendResult::WouldBlock;
                }
                continue;
            }

            ++result.deliveredMessages;
            result.deliveredBytes += selected.bytes.size();
            selectedPath->pendingByteCount -= selected.bytes.size();
            selectedPath->pending.erase(selectedPath->pending.begin() + static_cast<std::ptrdiff_t>(selectedIndex));
        }
        return result;
    }

    std::optional<std::vector<std::byte>> FaultInjectingLink::receive(LinkDirection direction)
    {
        return mLink.receive(direction);
    }

    bool FaultInjectingLink::setStalled(FaultPath path, bool stalled) noexcept
    {
        PathState* state = findPath(path);
        if (state == nullptr || state->disconnected)
            return false;
        state->stalled = stalled;
        return true;
    }

    bool FaultInjectingLink::disconnect(FaultPath path) noexcept
    {
        PathState* state = findPath(path);
        if (state == nullptr || state->disconnected)
            return false;
        state->pending.clear();
        state->pendingByteCount = 0;
        state->stalled = false;
        state->disconnected = true;
        return true;
    }

    bool FaultInjectingLink::isStalled(FaultPath path) const noexcept
    {
        const PathState* state = findPath(path);
        return state != nullptr && state->stalled;
    }

    bool FaultInjectingLink::isDisconnected(FaultPath path) const noexcept
    {
        const PathState* state = findPath(path);
        return state != nullptr && state->disconnected;
    }

    std::size_t FaultInjectingLink::pendingMessages(FaultPath path) const noexcept
    {
        const PathState* state = findPath(path);
        return state == nullptr ? 0 : state->pending.size();
    }

    std::size_t FaultInjectingLink::pendingBytes(FaultPath path) const noexcept
    {
        const PathState* state = findPath(path);
        return state == nullptr ? 0 : state->pendingByteCount;
    }

    std::size_t FaultInjectingLink::queuedMessages(LinkDirection direction) const noexcept
    {
        return mLink.queuedMessages(direction);
    }

    std::size_t FaultInjectingLink::queuedBytes(LinkDirection direction) const noexcept
    {
        return mLink.queuedBytes(direction);
    }
}
