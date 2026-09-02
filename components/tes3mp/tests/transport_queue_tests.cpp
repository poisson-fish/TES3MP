#include <tes3mp/transport.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool check(bool value, std::string_view message)
    {
        if (!value)
            std::cerr << "FAILED: " << message << '\n';
        return value;
    }

    struct FakeRuntime final : TES3MP::TransportRuntime
    {
        TES3MP::TransportResult sendResult = TES3MP::TransportResult::Accepted;
        bool blockReliable = false;
        bool blockLatest = false;
        bool closed = false;
        std::vector<TES3MP::TransportMessage> sent;

        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            return {};
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        {
            return {};
        }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        {
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportResult send(
            TES3MP::TransportConnectionId, TES3MP::TransportChannel channel, std::span<const std::byte> bytes) override
        {
            if ((channel == TES3MP::TransportChannel::ReliableOrdered && blockReliable)
                || (channel == TES3MP::TransportChannel::LatestWins && blockLatest))
                return TES3MP::TransportResult::WouldBlock;
            if (sendResult != TES3MP::TransportResult::Accepted)
                return sendResult;
            sent.push_back({ channel, { bytes.begin(), bytes.end() } });
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage>) override
        {
            return {};
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode mode) override
        {
            closed = mode == TES3MP::TransportCloseMode::Abort;
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent>) override { return {}; }
        TES3MP::TransportResult shutdown() override { return TES3MP::TransportResult::Accepted; }
    };

    struct RecordingSink final : TES3MP::TransportTelemetrySink
    {
        bool drop = false;
        std::vector<TES3MP::TransportTelemetryObservation> values;

        TES3MP::TransportTelemetryResult tryRecord(const TES3MP::TransportTelemetryObservation& value) noexcept override
        {
            values.push_back(value);
            return drop ? TES3MP::TransportTelemetryResult::Dropped : TES3MP::TransportTelemetryResult::Accepted;
        }
    };

    std::vector<std::byte> bytes(unsigned value, std::size_t count = 1)
    {
        return std::vector<std::byte>(count, static_cast<std::byte>(value));
    }

    TES3MP::OutboundQueuePolicy policy()
    {
        return *TES3MP::OutboundQueuePolicy::create(4, 64, 4, 2, 4, 10, 1, 10, 3, 100);
    }

    bool policyAndBounds()
    {
        using P = TES3MP::OutboundQueuePolicy;
        const auto valid = P::create(256, 4 * 1024 * 1024, 32, 32, 32, 1, 32, 1, 1, 1);
        return check(valid.has_value(), "hard ceilings rejected")
            && check(P::MaxConnections == 256, "connection ceiling changed")
            && check(!P::create(257, 1, 1, 1, 1, 1, 1, 1, 1, 1), "message ceiling exceeded")
            && check(!P::create(1, P::MaxReliableBytes + 1, 1, 1, 1, 1, 1, 1, 1, 1), "byte ceiling exceeded")
            && check(!P::create(1, 1, 33, 1, 1, 1, 1, 1, 1, 1), "work ceiling exceeded");
    }

    bool connectionSetBounds()
    {
        const auto invalid
            = TES3MP::OutboundQueueSet::create(policy(), TES3MP::OutboundQueuePolicy::MaxConnections + 1);
        auto queues = TES3MP::OutboundQueueSet::create(policy(), 2);
        const auto first = TES3MP::TransportConnectionId::initial();
        const auto second = *first.next();
        const auto third = *second.next();
        return check(!invalid, "connection hard ceiling exceeded")
            && check(queues.has_value(), "valid queue set rejected")
            && check(queues->attach(first) == TES3MP::TransportResult::Accepted, "first connection rejected")
            && check(
                queues->attach(first) == TES3MP::TransportResult::AlreadyFinalized, "duplicate connection attached")
            && check(queues->attach(second) == TES3MP::TransportResult::Accepted, "second connection rejected")
            && check(queues->attach(third) == TES3MP::TransportResult::AtCapacity, "connection capacity exceeded")
            && check(queues->detach(first) == TES3MP::TransportResult::Accepted, "connection detach failed")
            && check(queues->connections() == 1, "connection queue retained after detach");
    }

    bool orderingCoalescingAndFairness()
    {
        TES3MP::OutboundTransportQueue queue(policy());
        FakeRuntime runtime;
        const auto connection = TES3MP::TransportConnectionId::initial();
        queue.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(1));
        queue.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(2));
        for (unsigned value = 10; value < 20; ++value)
            queue.enqueue(TES3MP::TransportChannel::LatestWins, bytes(value));
        const auto result = queue.pump(runtime, connection, 0);
        return check(result == TES3MP::OutboundPumpResult::Progress, "pump made no progress")
            && check(runtime.sent.size() == 3, "bounded reliable burst/latest opportunity changed")
            && check(runtime.sent[0].bytes[0] == std::byte{ 1 } && runtime.sent[1].bytes[0] == std::byte{ 2 },
                "reliable FIFO order changed")
            && check(runtime.sent[2].channel == TES3MP::TransportChannel::LatestWins
                    && runtime.sent[2].bytes[0] == std::byte{ 19 },
                "latest-wins coalescing failed")
            && check(queue.reliableMessages() == 0 && !queue.hasLatest(), "sent values retained");
    }

    bool pairAdmissionIsAtomic()
    {
        auto queues = TES3MP::OutboundQueueSet::create(policy(), 1);
        const auto connection = TES3MP::TransportConnectionId::initial();
        FakeRuntime runtime;
        if (!check(queues && queues->attach(connection) == TES3MP::TransportResult::Accepted,
                "pair queue setup failed"))
            return false;
        for (unsigned value = 0; value < 4; ++value)
            queues->enqueue(connection, TES3MP::TransportChannel::ReliableOrdered, bytes(value));
        const auto blocked = queues->enqueuePair(connection, TES3MP::TransportChannel::LatestWins, bytes(9),
            TES3MP::TransportChannel::ReliableOrdered, bytes(5));
        queues->pump(runtime, connection, 0);
        const bool noPartialLatest = std::ranges::none_of(runtime.sent,
            [](const auto& message) { return message.channel == TES3MP::TransportChannel::LatestWins; });
        const auto sentBeforeAcceptedPair = runtime.sent.size();

        auto admitted = TES3MP::OutboundQueueSet::create(policy(), 1);
        admitted->attach(connection);
        const auto accepted = admitted->enqueuePair(connection, TES3MP::TransportChannel::ReliableOrdered, bytes(7),
            TES3MP::TransportChannel::LatestWins, bytes(8));
        admitted->pump(runtime, connection, 10);
        return check(blocked == TES3MP::TransportResult::WouldBlock, "full pair was not rejected")
            && check(noPartialLatest, "rejected pair admitted one lane")
            && check(accepted == TES3MP::TransportResult::Accepted, "valid pair was rejected")
            && check(admitted->enqueuePair(*connection.next(), TES3MP::TransportChannel::ReliableOrdered, bytes(1),
                         TES3MP::TransportChannel::LatestWins, bytes(2)) == TES3MP::TransportResult::UnknownId,
                "unknown connection pair was admitted")
            && check(runtime.sent.size() == sentBeforeAcceptedPair + 2
                    && runtime.sent[sentBeforeAcceptedPair].bytes[0] == std::byte{ 7 }
                    && runtime.sent[sentBeforeAcceptedPair + 1].bytes[0] == std::byte{ 8 },
                "accepted pair did not preserve both frames");
    }

    bool limitsRateAndTime()
    {
        TES3MP::OutboundTransportQueue queue(policy());
        FakeRuntime runtime;
        const auto connection = TES3MP::TransportConnectionId::initial();
        for (unsigned value = 0; value < 4; ++value)
            if (queue.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(value, 16))
                != TES3MP::TransportResult::Accepted)
                return false;
        const auto extra = queue.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(5));
        queue.pump(runtime, connection, 50);
        TES3MP::OutboundTransportQueue rateQueue(policy());
        rateQueue.enqueue(TES3MP::TransportChannel::LatestWins, bytes(7));
        rateQueue.pump(runtime, connection, 50);
        rateQueue.enqueue(TES3MP::TransportChannel::LatestWins, bytes(8));
        const auto noToken = rateQueue.pump(runtime, connection, 50);
        const auto refill = rateQueue.pump(runtime, connection, 60);
        const auto backwards = rateQueue.pump(runtime, connection, 59);
        return check(extra == TES3MP::TransportResult::WouldBlock, "reliable overflow accepted")
            && check(noToken == TES3MP::OutboundPumpResult::Blocked, "rate exhaustion not bounded")
            && check(refill == TES3MP::OutboundPumpResult::Progress, "exact refill edge rejected")
            && check(backwards == TES3MP::OutboundPumpResult::InvalidTime, "clock regression accepted");
    }

    bool isolatedSlowPeerEviction()
    {
        TES3MP::OutboundTransportQueue slow(policy());
        TES3MP::OutboundTransportQueue healthy(policy());
        FakeRuntime slowRuntime;
        FakeRuntime healthyRuntime;
        slowRuntime.blockReliable = true;
        const auto slowId = TES3MP::TransportConnectionId::initial();
        const auto healthyId = *slowId.next();
        slow.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(1));
        slow.enqueue(TES3MP::TransportChannel::LatestWins, bytes(9));
        healthy.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(2));
        const auto first = slow.pump(slowRuntime, slowId, 0);
        const auto healthyResult = healthy.pump(healthyRuntime, healthyId, 0);
        slow.pump(slowRuntime, slowId, 10);
        const auto evicted = slow.pump(slowRuntime, slowId, 20);
        return check(first == TES3MP::OutboundPumpResult::Progress, "blocked reliable prevented snapshot opportunity")
            && check(healthyResult == TES3MP::OutboundPumpResult::Progress && !healthyRuntime.closed,
                "slow peer affected healthy peer")
            && check(evicted == TES3MP::OutboundPumpResult::SlowPeerEvicted && slowRuntime.closed,
                "consecutive slow-peer threshold did not evict")
            && check(slow.reliableMessages() == 0 && !slow.hasLatest(), "eviction retained queued bytes");
    }

    bool telemetryIsExactBoundedAndIsolated()
    {
        RecordingSink sink;
        TES3MP::OutboundTransportQueue queue(policy(), sink);
        FakeRuntime runtime;
        const auto connection = TES3MP::TransportConnectionId::initial();
        queue.enqueue(TES3MP::TransportChannel::ReliableOrdered, bytes(1, 4));
        queue.enqueue(TES3MP::TransportChannel::LatestWins, bytes(2, 3));
        queue.enqueue(TES3MP::TransportChannel::LatestWins, bytes(3, 2));
        sink.drop = true;
        const auto result = queue.pump(runtime, connection, 0);
        const auto has
            = [&](TES3MP::TransportTelemetryKind kind, TES3MP::TransportChannel channel, std::uint64_t value) {
                  return std::ranges::find(sink.values,
                             TES3MP::TransportTelemetryObservation{
                                 kind, TES3MP::TransportTelemetryDirection::Outbound, channel, value })
                      != sink.values.end();
              };
        return check(result == TES3MP::OutboundPumpResult::Progress, "dropping sink changed queue behavior")
            && check(has(TES3MP::TransportTelemetryKind::Submitted, TES3MP::TransportChannel::LatestWins, 2),
                "submitted counter is not cumulative")
            && check(has(TES3MP::TransportTelemetryKind::Coalesced, TES3MP::TransportChannel::LatestWins, 1),
                "latest replacement was not counted")
            && check(has(TES3MP::TransportTelemetryKind::QueuedBytes, TES3MP::TransportChannel::ReliableOrdered, 4),
                "reliable byte gauge missing")
            && check(has(TES3MP::TransportTelemetryKind::QueuedMessages, TES3MP::TransportChannel::LatestWins, 0),
                "latest queue drain gauge missing")
            && check(TES3MP::saturatingTelemetryAdd(UINT64_MAX - 1, 2) == UINT64_MAX, "counter did not saturate");
    }
}

int main()
{
    return policyAndBounds() && connectionSetBounds() && orderingCoalescingAndFairness() && pairAdmissionIsAtomic()
            && limitsRateAndTime()
            && isolatedSlowPeerEviction() && telemetryIsExactBoundedAndIsolated()
        ? 0
        : 1;
}
