#include "transport_gns_detail.hpp"

#include <algorithm>
#include <limits>

namespace TES3MP::Detail
{
    void HappyEyeballsAttempt::addResolution(
        std::span<const NumericAddress> addresses, ResolutionCompletion completion, TimePoint now)
    {
        if (mCancelled || mSucceeded || resolutionDone())
            return;

        for (const NumericAddress& address : addresses)
        {
            if (mCandidates.size() >= TransportLimits::MaxResolvedAddresses)
                break;
            const bool duplicate = std::ranges::any_of(
                mCandidates, [&](const Candidate& candidate) { return candidate.address == address; });
            if (!duplicate)
            {
                const bool preferLateIpv6
                    = address.family == NumericAddressFamily::Ipv6 && mFirstIpv4Available && !mLastLaunch;
                if (preferLateIpv6)
                {
                    const auto firstIpv4 = std::ranges::find_if(mCandidates, [](const Candidate& candidate) {
                        return candidate.address.family == NumericAddressFamily::Ipv4;
                    });
                    mCandidates.insert(firstIpv4, Candidate{ address });
                }
                else
                    mCandidates.push_back({ address });
            }
        }

        if (!mFirstIpv4Available && !hasIpv6() && std::ranges::any_of(mCandidates, [](const Candidate& candidate) {
                return candidate.address.family == NumericAddressFamily::Ipv4;
            }))
            mFirstIpv4Available = now;

        if (completion != ResolutionCompletion::Pending)
            mResolution = completion;
    }

    std::optional<CandidateLaunch> HappyEyeballsAttempt::nextLaunch(TimePoint now, bool retryImmediately)
    {
        if (mCancelled || mSucceeded || activeCandidateCount() >= TransportLimits::MaxCandidateHandlesPerAttempt)
            return std::nullopt;

        const auto candidate
            = std::ranges::find_if(mCandidates, [](const Candidate& value) { return !value.launched; });
        if (candidate == mCandidates.end())
            return std::nullopt;

        if (!mLastLaunch)
        {
            const bool waitingForIpv6 = candidate->address.family == NumericAddressFamily::Ipv4 && !resolutionDone()
                && !hasIpv6() && mFirstIpv4Available && now < *mFirstIpv4Available + Ipv6PreferenceDelay;
            if (waitingForIpv6)
                return std::nullopt;
        }
        else if (!retryImmediately && activeCandidateCount() != 0 && now < *mLastLaunch + CandidateStagger)
            return std::nullopt;

        candidate->ordinal = mNextOrdinal++;
        candidate->launched = true;
        candidate->active = true;
        mLastLaunch = now;
        return CandidateLaunch{ candidate->ordinal, candidate->address };
    }

    bool HappyEyeballsAttempt::candidateFailed(std::uint64_t ordinal) noexcept
    {
        const auto candidate = std::ranges::find_if(
            mCandidates, [&](const Candidate& value) { return value.ordinal == ordinal && value.active; });
        if (candidate == mCandidates.end())
            return false;
        candidate->active = false;
        return true;
    }

    bool HappyEyeballsAttempt::candidateSucceeded(std::uint64_t ordinal) noexcept
    {
        const auto candidate = std::ranges::find_if(
            mCandidates, [&](const Candidate& value) { return value.ordinal == ordinal && value.active; });
        if (candidate == mCandidates.end())
            return false;
        candidate->active = false;
        mSucceeded = true;
        return true;
    }

    void HappyEyeballsAttempt::cancel() noexcept
    {
        mCancelled = true;
        for (Candidate& candidate : mCandidates)
            candidate.active = false;
    }

    bool HappyEyeballsAttempt::shouldFail() const noexcept
    {
        return !mCancelled && !mSucceeded && resolutionDone() && !hasUnlaunched() && activeCandidateCount() == 0;
    }

    std::size_t HappyEyeballsAttempt::activeCandidateCount() const noexcept
    {
        return std::ranges::count_if(mCandidates, [](const Candidate& candidate) { return candidate.active; });
    }

    bool HappyEyeballsAttempt::hasUnlaunched() const noexcept
    {
        return std::ranges::any_of(mCandidates, [](const Candidate& candidate) { return !candidate.launched; });
    }

    bool HappyEyeballsAttempt::hasIpv6() const noexcept
    {
        return std::ranges::any_of(mCandidates,
            [](const Candidate& candidate) { return candidate.address.family == NumericAddressFamily::Ipv6; });
    }

    std::optional<std::uint64_t> GenerationCounter::allocate() noexcept
    {
        if (!mNext)
            return std::nullopt;
        const std::uint64_t result = *mNext;
        mNext = result == std::numeric_limits<std::uint64_t>::max() ? std::nullopt : std::optional(result + 1);
        return result;
    }
}
