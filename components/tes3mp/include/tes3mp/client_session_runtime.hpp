#ifndef TES3MP_CLIENT_SESSION_RUNTIME_HPP
#define TES3MP_CLIENT_SESSION_RUNTIME_HPP

#include "authentication.hpp"
#include "headless_client_session.hpp"
#include "protocol_handshake.hpp"

#include <memory>
#include <optional>
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

    struct ClientRuntimeAdvanceResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        ClientSessionAction action = ClientSessionAction::None;
        std::size_t transportEvents = 0;
        bool snapshotApplied = false;
        bool observationApplied = false;
        bool authenticationAccepted = false;
    };

    struct ClientRuntimeQueueResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        std::optional<CommandSequence> sequence;
    };

    using ClientRuntimeCreateResult = std::variant<std::unique_ptr<class ClientSessionRuntime>, SessionTransitionError>;

    class ClientSessionRuntime
    {
    public:
        static constexpr std::size_t MaximumInboundMessagesPerDrain = 32;

        static ClientRuntimeCreateResult create(TransportRuntime& transport, MonotonicClock& clock,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, OutboundQueuePolicy outboundPolicy);

        HeadlessClientResult connect(const ConnectionEndpoint& endpoint) noexcept;
        HeadlessClientResult start(
            const ConnectionEndpoint& endpoint, ClientHello hello, AuthenticationRequest authentication) noexcept;
        ClientRuntimeAdvanceResult advance();
        ClientRuntimeQueueResult queueMotionIntent(PlayerMotionIntent intent);
        ClientRuntimeQueueResult queueCellTransition(FixtureCellTransition transition);
        ClientRuntimeDrainResult drainInbound();
        ClientRuntimeResult queue(MessageClass messageClass, MessageKind kind, std::span<const std::byte> payload);
        ClientRuntimeResult flushOutbound() noexcept;
        HeadlessClientResult close() noexcept;

        HeadlessClientSession& session() noexcept { return *mSession; }
        const HeadlessClientSession& session() const noexcept { return *mSession; }
        std::optional<ResumeToken> takeResumeToken() noexcept;
        std::uint64_t resumeLifetimeMilliseconds() const noexcept { return mResumeLifetimeMilliseconds; }

    private:
        ClientSessionRuntime(TransportRuntime& transport, MonotonicClock& clock,
            std::unique_ptr<HeadlessClientSession> session, OutboundQueuePolicy outboundPolicy) noexcept;
        ClientRuntimeDrainResult fail(ClientRuntimeResult result) noexcept;
        ClientRuntimeQueueResult queueReliable(ReliableOperationBody body);

        TransportRuntime& mTransport;
        MonotonicClock& mClock;
        std::unique_ptr<HeadlessClientSession> mSession;
        OutboundTransportQueue mOutbound;
        std::optional<ClientHello> mClientHello;
        std::optional<AuthenticationRequest> mAuthentication;
        std::optional<ResumeToken> mResumeToken;
        std::uint64_t mResumeLifetimeMilliseconds = 0;
        std::vector<ReliableObservationBatch> mPendingObservations;
        std::optional<CommandSequence> mLastQueuedSequence;
    };
}

#endif
