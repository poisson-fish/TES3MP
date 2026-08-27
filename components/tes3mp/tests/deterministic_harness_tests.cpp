#include <tes3mp/test_support/deterministic_harness.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace
{
    constexpr std::array<std::byte, 2> FirstMessage{ std::byte{ 1 }, std::byte{ 2 } };
    constexpr std::array<std::byte, 2> SecondMessage{ std::byte{ 3 }, std::byte{ 4 } };

    bool in_memory_link_enforces_independent_message_and_byte_budgets()
    {
        using namespace TES3MP::TestSupport;
        if (InMemoryDuplexLink::create({ 0, 4 }, { 1, 4 }) || InMemoryDuplexLink::create({ 1, 4 }, { 1, 0 }))
            return false;

        auto link = InMemoryDuplexLink::create({ 3, 4 }, { 1, 8 }).value();
        const std::array<std::byte, 5> oversized{};
        const std::array<std::byte, 8> reverse{};
        return link.send(LinkDirection::AtoB, oversized) == LinkSendResult::MessageTooLarge
            && link.send(LinkDirection::AtoB, FirstMessage) == LinkSendResult::Accepted
            && link.send(LinkDirection::AtoB, SecondMessage) == LinkSendResult::Accepted
            && link.send(LinkDirection::AtoB, FirstMessage) == LinkSendResult::WouldBlock
            && link.queuedMessages(LinkDirection::AtoB) == 2 && link.queuedBytes(LinkDirection::AtoB) == 4
            && link.send(LinkDirection::BtoA, reverse) == LinkSendResult::Accepted
            && link.send(LinkDirection::BtoA, FirstMessage) == LinkSendResult::WouldBlock
            && link.queuedMessages(LinkDirection::BtoA) == 1 && link.queuedBytes(LinkDirection::BtoA) == 8;
    }

    bool in_memory_link_preserves_fifo_message_boundaries_and_explicit_close()
    {
        using namespace TES3MP::TestSupport;
        auto link = InMemoryDuplexLink::create({ 2, 8 }, { 2, 8 }).value();
        link.send(LinkDirection::AtoB, FirstMessage);
        link.send(LinkDirection::AtoB, SecondMessage);
        link.closeSend(LinkDirection::AtoB);
        const auto first = link.receive(LinkDirection::AtoB);
        const auto second = link.receive(LinkDirection::AtoB);
        const auto empty = link.receive(LinkDirection::AtoB);
        return link.isSendClosed(LinkDirection::AtoB)
            && link.send(LinkDirection::AtoB, FirstMessage) == LinkSendResult::Closed
            && link.send(LinkDirection::BtoA, FirstMessage) == LinkSendResult::Accepted
            && first == std::vector<std::byte>(FirstMessage.begin(), FirstMessage.end())
            && second == std::vector<std::byte>(SecondMessage.begin(), SecondMessage.end()) && !empty
            && link.queuedMessages(LinkDirection::AtoB) == 0 && link.queuedBytes(LinkDirection::AtoB) == 0;
    }

    struct ScriptResult
    {
        std::vector<std::byte> trace;
        TES3MP::TestSupport::TestTraceDigestV1 digest;
        std::array<std::uint64_t, 2> mainDraws;
        std::array<std::uint64_t, 8> emittedTicks;
    };

    ScriptResult runScript(bool exerciseUnrelatedStream)
    {
        using namespace TES3MP;
        using namespace TES3MP::TestSupport;
        const auto mainKey = RandomStreamKey::fromValues(11, 7).value();
        auto harness = DeterministicHarness::create(MonotonicInstant::fromNanoseconds(250),
            ServerTick::fromValue(1).value(), 0xfeedfaceULL, mainKey, { 1, 3 }, { 1, 3 });
        auto unrelated = Xoshiro256StarStar::fromWorldSeed(0xfeedfaceULL, RandomStreamKey::fromValues(12, 7).value());

        harness->advanceClock(1'000'000'000);
        const auto firstBatch = harness->pumpScheduler();
        if (exerciseUnrelatedStream)
        {
            for (int index = 0; index < 20; ++index)
                unrelated.nextU64();
        }
        const std::uint64_t firstDraw = harness->drawRandom();
        const auto accepted = harness->send(LinkDirection::AtoB, FirstMessage);
        const auto blocked = harness->send(LinkDirection::AtoB, SecondMessage);
        const auto received = harness->receive(LinkDirection::AtoB);
        const std::uint64_t secondDraw = harness->drawRandom();
        const auto secondBatch = harness->pumpScheduler();
        harness->closeSend(LinkDirection::AtoB);

        std::array<std::uint64_t, 8> ticks{};
        for (std::size_t index = 0; index < 4; ++index)
        {
            ticks[index] = firstBatch.ticks()[index].value().value();
            ticks[index + 4] = secondBatch.ticks()[index].value().value();
        }
        if (accepted != LinkSendResult::Accepted || blocked != LinkSendResult::WouldBlock
            || received != std::vector<std::byte>(FirstMessage.begin(), FirstMessage.end()))
            return { {}, harness->traceDigest(), {}, {} };

        return { std::vector<std::byte>(harness->traceBytes().begin(), harness->traceBytes().end()),
            harness->traceDigest(), { firstDraw, secondDraw }, ticks };
    }

    bool same_script_seed_and_clock_produce_identical_trace_bytes_and_digest()
    {
        const auto first = runScript(false);
        const auto second = runScript(false);
        const auto isolated = runScript(true);
        const bool matches = !first.trace.empty() && first.trace == second.trace && first.digest == second.digest
            && first.trace == isolated.trace && first.digest == isolated.digest && first.mainDraws == isolated.mainDraws
            && first.trace.size() == 198 && first.digest.value() == 0x75802a50e6b6fc66ULL
            && first.mainDraws == std::array<std::uint64_t, 2>{ 0x0d0aead09c39c5feULL, 0x074b7a5b64ee455fULL }
            && first.emittedTicks == std::array<std::uint64_t, 8>{ 1, 2, 3, 4, 5, 6, 7, 8 };
        if (!matches)
        {
            std::cerr << "trace_size=" << first.trace.size() << " trace_digest=" << std::hex << first.digest.value()
                      << " draw0=" << first.mainDraws[0] << " draw1=" << first.mainDraws[1] << '\n';
        }
        return matches;
    }

    bool test_trace_digest_is_not_a_server_core_or_protocol_dependency()
    {
        const std::array<std::byte, 4> bytes{ std::byte{ 't' }, std::byte{ 'e' }, std::byte{ 's' }, std::byte{ 't' } };
        return TES3MP::TestSupport::TestTraceDigestV1::fromTrace(bytes).value() == 0xf9e6e6ef197c2b25ULL;
    }
}

int main()
{
    return in_memory_link_enforces_independent_message_and_byte_budgets()
            && in_memory_link_preserves_fifo_message_boundaries_and_explicit_close()
            && same_script_seed_and_clock_produce_identical_trace_bytes_and_digest()
            && test_trace_digest_is_not_a_server_core_or_protocol_dependency()
        ? 0
        : 1;
}
