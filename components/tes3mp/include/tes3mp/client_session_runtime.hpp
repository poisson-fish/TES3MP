#ifndef TES3MP_CLIENT_SESSION_RUNTIME_HPP
#define TES3MP_CLIENT_SESSION_RUNTIME_HPP

#include "authentication.hpp"
#include "headless_client_session.hpp"
#include "protocol_handshake.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace TES3MP
{
    using ClientRuntimeMessage = std::variant<ServerHello, SessionRejected, AuthenticationAcceptedMessage,
        AuthenticationRejectedMessage, LatestWinsSnapshot, ReliableObservationBatch>;

    enum class ClientRuntimeResult : std::uint8_t
    {
        Accepted,
        NotConnected,
        EncodeRejected,
        QueueRejected,
        TransportFailed,
        ProtocolRejected,
    };

    struct ClientRuntimeDrainResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        ClientSessionAction action = ClientSessionAction::None;
        std::size_t transportEvents = 0;
        std::vector<ClientRuntimeMessage> messages;
    };

    using ClientRuntimeCreateResult
        = std::variant<std::unique_ptr<class ClientSessionRuntime>, SessionTransitionError>;

    class ClientSessionRuntime
    {
    public:
        static constexpr std::size_t MaximumInboundMessagesPerDrain = 32;

        static ClientRuntimeCreateResult create(TransportRuntime& transport, MonotonicClock& clock,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, OutboundQueuePolicy outboundPolicy);

        HeadlessClientResult connect(const ConnectionEndpoint& endpoint) noexcept;
        ClientRuntimeDrainResult drainInbound();
        ClientRuntimeResult queue(MessageClass messageClass, MessageKind kind, std::span<const std::byte> payload);
        ClientRuntimeResult flushOutbound() noexcept;
        HeadlessClientResult close() noexcept;

        HeadlessClientSession& session() noexcept { return *mSession; }
        const HeadlessClientSession& session() const noexcept { return *mSession; }

    private:
        ClientSessionRuntime(TransportRuntime& transport, MonotonicClock& clock,
            std::unique_ptr<HeadlessClientSession> session, OutboundQueuePolicy outboundPolicy) noexcept;
        ClientRuntimeDrainResult fail(ClientRuntimeResult result) noexcept;

        TransportRuntime& mTransport;
        MonotonicClock& mClock;
        std::unique_ptr<HeadlessClientSession> mSession;
        OutboundTransportQueue mOutbound;
    };
}

#endif
