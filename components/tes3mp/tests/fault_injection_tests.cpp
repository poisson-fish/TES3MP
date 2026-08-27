#include <tes3mp/test_support/deterministic_harness.hpp>
#include <tes3mp/test_support/fault_injecting_link.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::TestSupport;

    FaultChannelId channel(std::uint64_t value)
    {
        return FaultChannelId::fromValue(value).value();
    }

    FaultProfile profile(std::uint64_t latency, std::uint64_t jitter, std::uint64_t reorder, std::uint32_t loss,
        std::uint32_t duplication, std::size_t messages = 32, std::size_t bytes = 1024)
    {
        return FaultProfile::create(latency, jitter, reorder, loss, duplication, messages, bytes).value();
    }

    std::unique_ptr<FaultInjectingLink> makeLink(ManualClock& clock, std::uint64_t seed,
        std::span<const FaultPathConfiguration> configurations, LinkBudget aToB = { 64, 4096 },
        LinkBudget bToA = { 64, 4096 })
    {
        auto base = InMemoryDuplexLink::create(aToB, bToA).value();
        return FaultInjectingLink::create(clock, seed, std::move(base), configurations);
    }

    bool profiles_and_paths_reject_invalid_configuration()
    {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        if (FaultChannelId::fromValue(0) || !FaultChannelId::fromValue(1)
            || FaultProfile::create(0, 0, 0, FaultRateScale + 1, 0, 1, 1)
            || FaultProfile::create(0, 0, 0, 0, FaultRateScale + 1, 1, 1) || FaultProfile::create(0, 0, 0, 0, 0, 0, 1)
            || FaultProfile::create(0, 0, 0, 0, 0, 1, 0) || FaultProfile::create(0, maximum, 0, 0, 0, 1, 1)
            || FaultProfile::create(maximum, 1, 0, 0, 0, 1, 1))
            return false;

        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath path{ LinkDirection::AtoB, channel(1) };
        const std::array duplicateConfigurations{
            FaultPathConfiguration{ path, profile(0, 0, 0, 0, 0) },
            FaultPathConfiguration{ path, profile(0, 0, 0, 0, 0) },
        };
        const std::array<FaultPathConfiguration, 0> emptyConfigurations{};
        std::vector<FaultPathConfiguration> tooManyConfigurations;
        for (std::uint64_t value = 1; value <= MaximumFaultPaths + 1; ++value)
        {
            tooManyConfigurations.push_back(
                { FaultPath{ LinkDirection::AtoB, channel(value) }, profile(0, 0, 0, 0, 0) });
        }
        return !makeLink(clock, 1, duplicateConfigurations) && !makeLink(clock, 1, emptyConfigurations)
            && !makeLink(clock, 1, tooManyConfigurations);
    }

    bool latency_and_direction_channel_state_are_independent()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath delayed{ LinkDirection::AtoB, channel(1) };
        const FaultPath immediate{ LinkDirection::BtoA, channel(2) };
        const std::array configurations{
            FaultPathConfiguration{ delayed, profile(100, 0, 0, 0, 0) },
            FaultPathConfiguration{ immediate, profile(0, 0, 0, 0, 0) },
        };
        auto link = makeLink(clock, 7, configurations);
        const std::array<std::byte, 1> delayedMessage{ std::byte{ 1 } };
        const std::array<std::byte, 1> immediateMessage{ std::byte{ 2 } };
        const FaultPath unknown{ LinkDirection::AtoB, channel(99) };

        if (link->send(delayed, delayedMessage) != FaultSendResult::Accepted
            || link->send(immediate, immediateMessage) != FaultSendResult::Accepted
            || link->send(unknown, delayedMessage) != FaultSendResult::UnconfiguredPath)
            return false;
        const FaultPumpResult firstPump = link->pump();
        if (firstPump.deliveredMessages != 1
            || link->receive(LinkDirection::BtoA) != std::vector<std::byte>{ std::byte{ 2 } }
            || link->receive(LinkDirection::AtoB))
            return false;
        clock.advance(99);
        if (link->pump().deliveredMessages != 0)
            return false;
        clock.advance(1);
        return link->pump().deliveredMessages == 1
            && link->receive(LinkDirection::AtoB) == std::vector<std::byte>{ std::byte{ 1 } };
    }

    bool loss_duplication_and_pending_budgets_are_atomic()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath lossPath{ LinkDirection::AtoB, channel(1) };
        const FaultPath blockedDuplicatePath{ LinkDirection::AtoB, channel(2) };
        const FaultPath duplicatePath{ LinkDirection::BtoA, channel(3) };
        const std::array configurations{
            FaultPathConfiguration{ lossPath, profile(0, 0, 0, FaultRateScale, 0, 2, 8) },
            FaultPathConfiguration{ blockedDuplicatePath, profile(0, 0, 0, 0, FaultRateScale, 1, 8) },
            FaultPathConfiguration{ duplicatePath, profile(0, 0, 0, 0, FaultRateScale, 2, 4) },
        };
        auto link = makeLink(clock, 11, configurations);
        const std::array<std::byte, 2> message{ std::byte{ 3 }, std::byte{ 4 } };
        const std::array<std::byte, 5000> oversized{};

        if (link->send(lossPath, message) != FaultSendResult::Dropped || link->pendingMessages(lossPath) != 0
            || link->send(blockedDuplicatePath, message) != FaultSendResult::WouldBlock
            || link->pendingMessages(blockedDuplicatePath) != 0 || link->pendingBytes(blockedDuplicatePath) != 0
            || link->send(duplicatePath, oversized) != FaultSendResult::MessageTooLarge
            || link->send(duplicatePath, message) != FaultSendResult::Accepted
            || link->pendingMessages(duplicatePath) != 2 || link->pendingBytes(duplicatePath) != 4)
            return false;

        const FaultPumpResult pump = link->pump();
        const auto first = link->receive(LinkDirection::BtoA);
        const auto second = link->receive(LinkDirection::BtoA);
        return pump.deliveredMessages == 2 && first == std::vector<std::byte>(message.begin(), message.end())
            && second == first && link->pendingMessages(duplicatePath) == 0;
    }

    bool stall_resume_and_disconnect_affect_only_the_selected_path()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath firstPath{ LinkDirection::AtoB, channel(1) };
        const FaultPath secondPath{ LinkDirection::AtoB, channel(2) };
        const std::array configurations{
            FaultPathConfiguration{ firstPath, profile(0, 0, 0, 0, 0) },
            FaultPathConfiguration{ secondPath, profile(0, 0, 0, 0, 0) },
        };
        auto link = makeLink(clock, 13, configurations);
        const std::array<std::byte, 1> firstMessage{ std::byte{ 1 } };
        const std::array<std::byte, 1> secondMessage{ std::byte{ 2 } };

        link->setStalled(firstPath, true);
        link->send(firstPath, firstMessage);
        link->send(secondPath, secondMessage);
        if (!link->isStalled(firstPath) || link->pump().deliveredMessages != 1
            || link->receive(LinkDirection::AtoB) != std::vector<std::byte>{ std::byte{ 2 } }
            || link->pendingMessages(firstPath) != 1)
            return false;
        link->setStalled(firstPath, false);
        if (link->pump().deliveredMessages != 1
            || link->receive(LinkDirection::AtoB) != std::vector<std::byte>{ std::byte{ 1 } })
            return false;

        link->send(firstPath, firstMessage);
        if (!link->disconnect(firstPath) || !link->isDisconnected(firstPath) || link->isStalled(firstPath)
            || link->pendingMessages(firstPath) != 0
            || link->send(firstPath, firstMessage) != FaultSendResult::Disconnected || link->disconnect(firstPath)
            || link->setStalled(firstPath, true))
            return false;
        return link->send(secondPath, secondMessage) == FaultSendResult::Accepted && link->pump().deliveredMessages == 1
            && link->receive(LinkDirection::AtoB) == std::vector<std::byte>{ std::byte{ 2 } };
    }

    bool base_backpressure_is_bounded_without_blocking_the_reverse_direction()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath firstPath{ LinkDirection::AtoB, channel(1) };
        const FaultPath secondPath{ LinkDirection::AtoB, channel(2) };
        const FaultPath reversePath{ LinkDirection::BtoA, channel(1) };
        const std::array configurations{
            FaultPathConfiguration{ firstPath, profile(0, 0, 0, 0, 0) },
            FaultPathConfiguration{ secondPath, profile(0, 0, 0, 0, 0) },
            FaultPathConfiguration{ reversePath, profile(0, 0, 0, 0, 0) },
        };
        auto link = makeLink(clock, 17, configurations, { 1, 8 }, { 1, 8 });
        const std::array<std::byte, 1> first{ std::byte{ 1 } };
        const std::array<std::byte, 1> second{ std::byte{ 2 } };
        const std::array<std::byte, 1> reverse{ std::byte{ 3 } };
        link->send(firstPath, first);
        link->send(secondPath, second);
        link->send(reversePath, reverse);

        const FaultPumpResult firstPump = link->pump();
        if (firstPump.deliveredMessages != 2 || !firstPump.aToBWouldBlock || firstPump.bToAWouldBlock
            || link->pendingMessages(secondPath) != 1
            || link->receive(LinkDirection::AtoB) != std::vector<std::byte>{ std::byte{ 1 } }
            || link->receive(LinkDirection::BtoA) != std::vector<std::byte>{ std::byte{ 3 } })
            return false;
        return link->pump().deliveredMessages == 1
            && link->receive(LinkDirection::AtoB) == std::vector<std::byte>{ std::byte{ 2 } };
    }

    struct ScriptResult
    {
        std::vector<std::byte> trace;
        TestTraceDigestV1 digest;
    };

    void appendU64(std::vector<std::byte>& trace, std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            trace.push_back(static_cast<std::byte>(value >> shift));
    }

    ScriptResult runSeededScript(std::uint64_t seed, bool exerciseUnrelatedPath)
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(100));
        const FaultPath mainPath{ LinkDirection::AtoB, channel(5) };
        const FaultPath unrelatedPath{ LinkDirection::BtoA, channel(9) };
        const std::array configurations{
            FaultPathConfiguration{ mainPath, profile(10, 7, 31, 250'000, 500'000, 32, 128) },
            FaultPathConfiguration{ unrelatedPath, profile(5, 11, 17, 400'000, 300'000, 64, 256) },
        };
        auto link = makeLink(clock, seed, configurations);
        std::vector<std::byte> trace{ std::byte{ 'F' }, std::byte{ '3' }, std::byte{ 1 } };
        for (std::uint8_t value = 1; value <= 6; ++value)
        {
            const std::array<std::byte, 1> message{ static_cast<std::byte>(value) };
            trace.push_back(static_cast<std::byte>(link->send(mainPath, message)));
            if (exerciseUnrelatedPath)
            {
                for (std::uint8_t extra = 0; extra < value; ++extra)
                {
                    const std::array<std::byte, 1> unrelated{ static_cast<std::byte>(extra) };
                    link->send(unrelatedPath, unrelated);
                }
            }
        }
        for (std::uint64_t advance : { 9ULL, 8ULL, 10ULL, 25ULL })
        {
            clock.advance(advance);
            link->pump();
            appendU64(trace, advance);
            std::size_t deliveredMainMessages = 0;
            while (auto received = link->receive(LinkDirection::AtoB))
            {
                ++deliveredMainMessages;
                trace.push_back(static_cast<std::byte>(received->size()));
                trace.insert(trace.end(), received->begin(), received->end());
            }
            appendU64(trace, deliveredMainMessages);
            while (link->receive(LinkDirection::BtoA))
            {
            }
        }
        appendU64(trace, link->pendingMessages(mainPath));
        return { trace, TestTraceDigestV1::fromTrace(trace) };
    }

    bool same_seed_profile_and_script_reproduce_exact_fault_trace()
    {
        const ScriptResult first = runSeededScript(0x123456789abcdef0ULL, false);
        const ScriptResult repeated = runSeededScript(0x123456789abcdef0ULL, false);
        const ScriptResult isolated = runSeededScript(0x123456789abcdef0ULL, true);
        const ScriptResult changedSeed = runSeededScript(0x123456789abcdef1ULL, false);
        const bool matches = first.trace == repeated.trace && first.digest == repeated.digest
            && first.trace == isolated.trace && first.digest == isolated.digest && first.trace != changedSeed.trace
            && first.trace.size() == 97 && first.digest.value() == 0x935d71908b6496c8ULL;
        if (!matches)
            std::cerr << "fault_trace_size=" << first.trace.size() << " fault_trace_digest=" << std::hex
                      << first.digest.value() << '\n';
        return matches;
    }

    bool jitter_and_reorder_are_bounded_and_can_change_delivery_order()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const FaultPath path{ LinkDirection::AtoB, channel(7) };
        const std::array configurations{
            FaultPathConfiguration{ path, profile(10, 20, 100, 0, 0, 16, 64) },
        };
        auto link = makeLink(clock, 0x44, configurations);
        for (std::uint8_t value = 1; value <= 6; ++value)
        {
            const std::array<std::byte, 1> message{ static_cast<std::byte>(value) };
            link->send(path, message);
        }
        clock.advance(9);
        if (link->pump().deliveredMessages != 0)
            return false;
        clock.advance(121);
        if (link->pump().deliveredMessages != 6)
            return false;

        std::vector<std::byte> order;
        while (auto message = link->receive(LinkDirection::AtoB))
            order.push_back(message->front());
        const std::vector<std::byte> original{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 },
            std::byte{ 5 }, std::byte{ 6 } };
        if (order.size() != original.size() || order == original)
        {
            std::cerr << "reorder_result=";
            for (std::byte value : order)
                std::cerr << static_cast<unsigned>(std::to_integer<std::uint8_t>(value));
            std::cerr << '\n';
            return false;
        }
        return true;
    }

    bool time_overflow_is_explicit_and_atomic()
    {
        ManualClock clock(MonotonicInstant::fromNanoseconds(std::numeric_limits<std::uint64_t>::max() - 5));
        const FaultPath path{ LinkDirection::AtoB, channel(1) };
        const std::array configurations{
            FaultPathConfiguration{ path, profile(10, 0, 0, 0, 0) },
        };
        auto link = makeLink(clock, 19, configurations);
        const std::array<std::byte, 1> message{ std::byte{ 1 } };
        return link->send(path, message) == FaultSendResult::TimeOverflow && link->pendingMessages(path) == 0
            && link->pendingBytes(path) == 0;
    }
}

int main()
{
    const std::array tests{
        std::pair{ "profiles_and_paths_reject_invalid_configuration", profiles_and_paths_reject_invalid_configuration },
        std::pair{ "latency_and_direction_channel_state_are_independent",
            latency_and_direction_channel_state_are_independent },
        std::pair{ "loss_duplication_and_pending_budgets_are_atomic", loss_duplication_and_pending_budgets_are_atomic },
        std::pair{ "stall_resume_and_disconnect_affect_only_the_selected_path",
            stall_resume_and_disconnect_affect_only_the_selected_path },
        std::pair{ "base_backpressure_is_bounded_without_blocking_the_reverse_direction",
            base_backpressure_is_bounded_without_blocking_the_reverse_direction },
        std::pair{ "same_seed_profile_and_script_reproduce_exact_fault_trace",
            same_seed_profile_and_script_reproduce_exact_fault_trace },
        std::pair{ "jitter_and_reorder_are_bounded_and_can_change_delivery_order",
            jitter_and_reorder_are_bounded_and_can_change_delivery_order },
        std::pair{ "time_overflow_is_explicit_and_atomic", time_overflow_is_explicit_and_atomic },
    };
    for (const auto& [name, test] : tests)
    {
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
