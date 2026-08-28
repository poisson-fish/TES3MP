#ifndef TES3MP_SERVER_SESSION_HPP
#define TES3MP_SERVER_SESSION_HPP

#include "authentication.hpp"
#include "monotonic_clock.hpp"
#include "observability.hpp"
#include "protocol_exchange.hpp"
#include "protocol_handshake.hpp"
#include "session_types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace TES3MP
{
    enum class ServerSessionState : std::uint8_t
    {
        AwaitingEncryptedTransport,
        AwaitingClientHello,
        AwaitingAuthenticationInput,
        AuthenticationPending,
        Established,
        Rejected,
        TimedOut,
        Cancelled,
        Closed,
    };

    enum class ServerSessionEventKind : std::uint8_t
    {
        EncryptedTransportReady,
        ClientHelloReceived,
        AuthenticationSubmitted,
        PollAuthentication,
        CheckTimeout,
        Cancel,
        Close,
    };

    struct ServerEncryptedTransportReady
    {
    };

    struct ServerClientHelloReceived
    {
        ClientHello hello;
    };

    struct ServerAuthenticationSubmitted
    {
        AuthenticationMaterial material;
    };

    struct ServerPollAuthentication
    {
    };

    struct ServerCheckTimeout
    {
    };

    struct ServerCancel
    {
    };

    struct ServerClose
    {
    };

    using ServerSessionEvent = std::variant<ServerEncryptedTransportReady, ServerClientHelloReceived,
        ServerAuthenticationSubmitted, ServerPollAuthentication, ServerCheckTimeout, ServerCancel, ServerClose>;

    enum class ServerSessionAction : std::uint8_t
    {
        None,
        SendServerHello,
        SendSessionRejected,
        AuthenticationStarted,
        AuthenticationPending,
        AuthenticationRejected,
        AuthenticationStaleCompletion,
        SessionEstablished,
        SessionTimedOut,
        SessionCancelled,
        SessionClosed,
    };

    struct ServerSessionTransition
    {
        ServerSessionAction action = ServerSessionAction::None;
        std::optional<SessionTransitionError> error;

        bool accepted() const noexcept { return !error.has_value(); }
    };

    enum class ServerSessionBindingResult : std::uint8_t
    {
        Bound,
        NotEstablished,
        AlreadyBound,
    };

    enum class ReliableOperationReceiveResult : std::uint8_t
    {
        Delivered,
        NotEstablished,
        SessionNotBound,
        SessionMismatch,
        GenerationMismatch,
    };

    using ServerSessionCreateResult
        = std::variant<std::unique_ptr<class ServerSessionStateMachine>, SessionTransitionError>;

    class ServerSessionStateMachine
    {
    public:
        static ServerSessionCreateResult create(MonotonicClock& clock, Observability& observability,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, CapabilityOffer serverOffer,
            AuthenticationProvider& authenticationProvider);

        ServerSessionStateMachine(const ServerSessionStateMachine&) = delete;
        ServerSessionStateMachine& operator=(const ServerSessionStateMachine&) = delete;
        ServerSessionStateMachine(ServerSessionStateMachine&&) = delete;
        ServerSessionStateMachine& operator=(ServerSessionStateMachine&&) = delete;
        ~ServerSessionStateMachine();

        ServerSessionTransition handle(ServerSessionEvent event) noexcept;
        ServerSessionBindingResult bindEstablishedSession(SessionId sessionId) noexcept;
        ReliableOperationReceiveResult receiveReliableOperation(const ReliableOperation& operation) const noexcept;

        ServerSessionState state() const noexcept { return mState; }
        SessionGeneration generation() const noexcept { return mGeneration; }
        std::optional<MonotonicInstant> deadline() const noexcept { return mDeadline; }
        const std::optional<ServerHello>& negotiatedHello() const noexcept { return mNegotiatedHello; }
        const std::optional<SessionRejected>& protocolRejection() const noexcept { return mProtocolRejection; }
        const std::optional<AuthenticationRejected>& authenticationRejection() const noexcept
        {
            return mAuthenticationRejection;
        }
        const std::optional<AuthenticatedPrincipal>& principal() const noexcept { return mPrincipal; }
        std::optional<SessionId> sessionId() const noexcept { return mSessionId; }

    private:
        ServerSessionStateMachine(MonotonicClock& clock, Observability& observability,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, CapabilityOffer serverOffer,
            AuthenticationProvider& authenticationProvider, MonotonicInstant deadline);

        ServerSessionTransition illegal(ServerSessionEventKind event) noexcept;
        ServerSessionTransition deadlineOverflow(ServerSessionEventKind event, SessionStage stage) noexcept;
        bool prepareDeadline(SessionStage stage, MonotonicInstant& result) const noexcept;
        void cancelAuthentication() noexcept;
        void observe(SessionObservationOutcome outcome, SessionObservationStage stage) noexcept;

        MonotonicClock& mClock;
        Observability& mObservability;
        SessionTimeoutPolicy mTimeoutPolicy;
        SessionGeneration mGeneration;
        CapabilityOffer mServerOffer;
        AuthenticationProvider& mAuthenticationProvider;
        ServerSessionState mState = ServerSessionState::AwaitingEncryptedTransport;
        std::optional<MonotonicInstant> mDeadline;
        AuthenticationAttempt mActiveAttempt;
        std::unique_ptr<AuthenticationOperation> mAuthenticationOperation;
        std::optional<ServerHello> mNegotiatedHello;
        std::optional<SessionRejected> mProtocolRejection;
        std::optional<AuthenticationRejected> mAuthenticationRejection;
        std::optional<AuthenticatedPrincipal> mPrincipal;
        std::optional<SessionId> mSessionId;
    };
}

#endif
