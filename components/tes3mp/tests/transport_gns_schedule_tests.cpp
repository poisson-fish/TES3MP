#include "transport/transport_gns_detail.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using TES3MP::Detail::HappyEyeballsAttempt;
    using TES3MP::Detail::NumericAddress;
    using TES3MP::Detail::NumericAddressFamily;
    using TES3MP::Detail::ResolutionCompletion;

    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    NumericAddress ipv4(std::string host)
    {
        return { std::move(host), 25565, NumericAddressFamily::Ipv4 };
    }

    NumericAddress ipv6(std::string host)
    {
        return { std::move(host), 25565, NumericAddressFamily::Ipv6 };
    }

    bool preferenceStaggerAndCapacityAreExact()
    {
        HappyEyeballsAttempt attempt;
        const std::array first{ ipv4("192.0.2.1") };
        attempt.addResolution(first, ResolutionCompletion::Pending, HappyEyeballsAttempt::TimePoint(10));
        if (!check(!attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(59)),
                "IPv4 launched before the 50 ms IPv6 preference delay"))
            return false;

        const std::array second{ ipv6("2001:db8::1"), ipv4("192.0.2.2"), ipv4("192.0.2.2") };
        attempt.addResolution(second, ResolutionCompletion::Success, HappyEyeballsAttempt::TimePoint(20));
        const auto firstLaunch = attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(20));
        if (!check(firstLaunch && firstLaunch->address == second[0],
                "IPv6 result did not win the bounded preference delay"))
            return false;
        if (!check(!attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(269)),
                "second candidate launched before the 250 ms stagger"))
            return false;
        const auto secondLaunch = attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(270));
        return check(secondLaunch && secondLaunch->address == first[0], "second candidate did not launch on deadline")
            && check(!attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(1000)),
                "more than two candidate handles became active")
            && check(attempt.retainedAddressCount() == 3, "duplicate resolution result was retained");
    }

    bool failureRetryWinnerAndCancellationAreTerminal()
    {
        HappyEyeballsAttempt attempt;
        const std::array addresses{ ipv6("2001:db8::1"), ipv4("192.0.2.1"), ipv4("192.0.2.2") };
        attempt.addResolution(addresses, ResolutionCompletion::Success, HappyEyeballsAttempt::TimePoint(0));
        const auto first = attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(0));
        if (!first || !attempt.candidateFailed(first->ordinal))
            return check(false, "first candidate failure was not accepted exactly once");
        const auto retry = attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(1), true);
        if (!check(retry && retry->address == addresses[1], "failed candidate did not trigger immediate fallback"))
            return false;
        if (!check(attempt.candidateSucceeded(retry->ordinal), "winner was not accepted"))
            return false;
        if (!check(
                !attempt.candidateFailed(retry->ordinal) && !attempt.nextLaunch(HappyEyeballsAttempt::TimePoint(999)),
                "delayed loser completion changed a terminal winner"))
            return false;

        HappyEyeballsAttempt cancelled;
        cancelled.addResolution(addresses, ResolutionCompletion::Success, HappyEyeballsAttempt::TimePoint(0));
        const auto live = cancelled.nextLaunch(HappyEyeballsAttempt::TimePoint(0));
        cancelled.cancel();
        return check(live && cancelled.cancelled() && cancelled.activeCandidateCount() == 0
                && !cancelled.nextLaunch(HappyEyeballsAttempt::TimePoint(1000)) && !cancelled.shouldFail(),
            "cancellation did not suppress all later work");
    }

    std::string seededTrace(std::uint32_t seed)
    {
        std::mt19937 random(seed);
        HappyEyeballsAttempt attempt;
        std::vector<NumericAddress> addresses{ ipv6("2001:db8::1"), ipv4("192.0.2.1"), ipv6("2001:db8::2"),
            ipv4("192.0.2.2") };
        std::shuffle(addresses.begin(), addresses.end(), random);
        attempt.addResolution(addresses, ResolutionCompletion::Success, HappyEyeballsAttempt::TimePoint(100));

        std::ostringstream trace;
        auto now = HappyEyeballsAttempt::TimePoint(100);
        while (!attempt.shouldFail())
        {
            const auto launch = attempt.nextLaunch(now, true);
            if (!launch)
                break;
            trace << launch->ordinal << ':' << launch->address.host << ';';
            attempt.candidateFailed(launch->ordinal);
            now += HappyEyeballsAttempt::TimePoint(random() % 17 + 1);
        }
        trace << (attempt.shouldFail() ? "failed" : "pending");
        return trace.str();
    }

    bool sameSeedReplaysAndDifferentSeedsVary()
    {
        const std::string first = seededTrace(0x61u);
        const std::string replay = seededTrace(0x61u);
        const std::string different = seededTrace(0x62u);
        return check(first == replay, "same-seed fake resolver/candidate trace diverged")
            && check(first != different, "different fake-resolver seeds produced the same trace");
    }

    bool resultBoundsAndGenerationExhaustionFailClosed()
    {
        HappyEyeballsAttempt attempt;
        std::vector<NumericAddress> addresses;
        for (std::size_t index = 0; index < 12; ++index)
            addresses.push_back(ipv4("192.0.2." + std::to_string(index + 1)));
        attempt.addResolution(addresses, ResolutionCompletion::Success, HappyEyeballsAttempt::TimePoint(0));

        TES3MP::Detail::GenerationCounter generations(std::numeric_limits<std::uint64_t>::max());
        const auto final = generations.allocate();
        const auto exhausted = generations.allocate();
        return check(attempt.retainedAddressCount() == TES3MP::TransportLimits::MaxResolvedAddresses,
                   "resolver result ceiling was exceeded")
            && check(final && *final == std::numeric_limits<std::uint64_t>::max() && !exhausted,
                "generation counter wrapped instead of exhausting");
    }

    bool reusedHandlesRejectDelayedGenerations()
    {
        TES3MP::Detail::GenerationBindingTable<int, std::string> bindings;
        if (!bindings.bind(7, "old-attempt", 41) || bindings.find(7, 41) == nullptr)
            return check(false, "initial handle generation was not bound");
        bindings.erase(7);
        if (!bindings.bind(7, "replacement-attempt", 42))
            return check(false, "reused handle could not be rebound");
        const auto stale = bindings.find(7, 41);
        const auto current = bindings.find(7, 42);
        if (!check(stale == nullptr && current && *current == "replacement-attempt",
                "delayed callback aliased a reused handle"))
            return false;

        bindings.erase(7);
        if (!bindings.bind(7, "accepted-connection", 44, 43) || bindings.find(7, 43) == nullptr
            || bindings.find(7, 44) == nullptr)
            return check(false, "inherited listener generation handoff was not bounded");
        bindings.retireInherited(7);
        return check(bindings.find(7, 43) == nullptr && bindings.find(7, 44) != nullptr,
            "retired listener generation still targeted an established connection");
    }
}

int main()
{
    return preferenceStaggerAndCapacityAreExact() && failureRetryWinnerAndCancellationAreTerminal()
            && sameSeedReplaysAndDifferentSeedsVary() && resultBoundsAndGenerationExhaustionFailClosed()
            && reusedHandlesRejectDelayedGenerations()
        ? 0
        : 1;
}
