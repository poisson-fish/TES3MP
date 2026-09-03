#include <tes3mp/client_session_runtime.hpp>

#include <array>

namespace TES3MP
{
    ClientRuntimeCreateResult ClientSessionRuntime::create(TransportRuntime& transport, MonotonicClock& clock,
        SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, OutboundQueuePolicy outboundPolicy)
    {
        auto created = HeadlessClientSession::create(transport, clock, timeoutPolicy, generation);
        if (auto* failure = std::get_if<SessionTransitionError>(&created)) return *failure;
        return std::unique_ptr<ClientSessionRuntime>(new ClientSessionRuntime(transport, clock,
            std::get<std::unique_ptr<HeadlessClientSession>>(std::move(created)), outboundPolicy));
    }

    ClientSessionRuntime::ClientSessionRuntime(TransportRuntime& transport, MonotonicClock& clock,
        std::unique_ptr<HeadlessClientSession> session, OutboundQueuePolicy outboundPolicy) noexcept
        : mTransport(transport), mClock(clock), mSession(std::move(session)), mOutbound(outboundPolicy)
    {
    }

    HeadlessClientResult ClientSessionRuntime::connect(const ConnectionEndpoint& endpoint) noexcept
    { return mSession->connect(endpoint); }

    ClientRuntimeDrainResult ClientSessionRuntime::fail(ClientRuntimeResult result) noexcept
    {
        mOutbound.clear();
        mSession->close();
        return { result, ClientSessionAction::SessionClosed };
    }

    ClientRuntimeDrainResult ClientSessionRuntime::drainInbound()
    {
        const auto lifecycle = mSession->pump();
        if (lifecycle.result != HeadlessClientResult::Accepted)
            return fail(ClientRuntimeResult::TransportFailed);
        ClientRuntimeDrainResult result{ ClientRuntimeResult::Accepted, lifecycle.action, lifecycle.transportEvents };
        const auto connection = mSession->connection();
        if (!connection) return result;

        std::array<TransportMessage, MaximumInboundMessagesPerDrain> messages{};
        const auto received = mTransport.receive(*connection, messages);
        if (received.result != TransportResult::Accepted || received.messages > messages.size())
            return fail(ClientRuntimeResult::TransportFailed);
        result.messages.reserve(received.messages);
        for (std::size_t index = 0; index < received.messages; ++index)
        {
            auto decoded = decodeProtocolFrame(messages[index].bytes);
            auto* frame = std::get_if<DecodedFrame>(&decoded);
            if (!frame || !isMessageClassAllowedOnTransportChannel(frame->messageClass(), messages[index].channel))
                return fail(ClientRuntimeResult::ProtocolRejected);

            switch (frame->messageKind())
            {
                case MessageKind::ServerHello:
                {
                    auto value = decodeServerHello(frame->payload());
                    if (auto* typed = std::get_if<ServerHello>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::SessionRejected:
                {
                    auto value = decodeSessionRejected(frame->payload());
                    if (auto* typed = std::get_if<SessionRejected>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::AuthenticationAccepted:
                {
                    auto value = decodeAuthenticationAccepted(frame->payload());
                    if (auto* typed = std::get_if<AuthenticationAcceptedMessage>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::AuthenticationRejected:
                {
                    auto value = decodeAuthenticationRejected(frame->payload());
                    if (auto* typed = std::get_if<AuthenticationRejectedMessage>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::LatestWinsSnapshot:
                {
                    auto value = decodeLatestWinsSnapshot(frame->payload());
                    if (auto* typed = std::get_if<LatestWinsSnapshot>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                case MessageKind::ReliableObservationBatch:
                {
                    auto value = decodeReliableObservationBatch(frame->payload());
                    if (auto* typed = std::get_if<ReliableObservationBatch>(&value)) result.messages.emplace_back(std::move(*typed));
                    else return fail(ClientRuntimeResult::ProtocolRejected);
                    break;
                }
                default:
                    return fail(ClientRuntimeResult::ProtocolRejected);
            }
        }
        return result;
    }

    ClientRuntimeResult ClientSessionRuntime::queue(
        MessageClass messageClass, MessageKind kind, std::span<const std::byte> payload)
    {
        const auto channel = transportChannelFor(messageClass);
        auto encoded = encodeProtocolFrame(messageClass, kind, payload);
        auto* bytes = std::get_if<std::vector<std::byte>>(&encoded);
        if (!channel || !bytes) return ClientRuntimeResult::EncodeRejected;
        const auto queued = mOutbound.enqueue(*channel, *bytes);
        return queued == TransportResult::Accepted ? ClientRuntimeResult::Accepted : ClientRuntimeResult::QueueRejected;
    }

    ClientRuntimeResult ClientSessionRuntime::flushOutbound() noexcept
    {
        const auto connection = mSession->connection();
        if (!connection) return ClientRuntimeResult::NotConnected;
        const auto nowMilliseconds = mClock.now().nanoseconds() / 1'000'000;
        const auto result = mOutbound.pump(mTransport, *connection, nowMilliseconds);
        if (result == OutboundPumpResult::Progress || result == OutboundPumpResult::Idle
            || result == OutboundPumpResult::Blocked)
            return ClientRuntimeResult::Accepted;
        mOutbound.clear();
        mSession->close();
        return ClientRuntimeResult::TransportFailed;
    }

    HeadlessClientResult ClientSessionRuntime::close() noexcept
    {
        mOutbound.clear();
        return mSession->close();
    }
}
