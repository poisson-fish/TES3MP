#ifndef TES3MP_HEADLESS_CLIENT_SESSION_HPP
#define TES3MP_HEADLESS_CLIENT_SESSION_HPP

#include "client_session.hpp"
#include "transport.hpp"

#include <memory>
#include <optional>
#include <variant>

namespace TES3MP
{
    enum class HeadlessClientResult : std::uint8_t
    {
        Accepted,
        AlreadyStarted,
        TransportRejected,
        TransportFailed,
        NotConnected,
    };

    struct HeadlessClientPumpResult
    {
        HeadlessClientResult result = HeadlessClientResult::Accepted;
        ClientSessionAction action = ClientSessionAction::None;
        std::size_t transportEvents = 0;
    };

    using HeadlessClientCreateResult
        = std::variant<std::unique_ptr<class HeadlessClientSession>, SessionTransitionError>;

    class HeadlessClientSession
    {
    public:
        static HeadlessClientCreateResult create(TransportRuntime& transport, MonotonicClock& clock,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation);

        HeadlessClientResult connect(const ConnectionEndpoint& endpoint) noexcept;
        HeadlessClientPumpResult pump() noexcept;
        ClientSessionTransition handle(ClientSessionEvent event) noexcept;
        ClientSessionBindingResult bindEstablishedSession(SessionId session) noexcept;
        LatestWinsSnapshotReceiveResult receiveLatestWinsSnapshot(LatestWinsSnapshot snapshot);
        HeadlessClientResult close() noexcept;

        const ClientSessionStateMachine& stateMachine() const noexcept { return *mState; }
        std::optional<ConnectAttemptId> attempt() const noexcept { return mAttempt; }
        std::optional<TransportConnectionId> connection() const noexcept { return mConnection; }

    private:
        HeadlessClientSession(TransportRuntime& transport, std::unique_ptr<ClientSessionStateMachine> state) noexcept;

        TransportRuntime& mTransport;
        std::unique_ptr<ClientSessionStateMachine> mState;
        std::optional<ConnectAttemptId> mAttempt;
        std::optional<TransportConnectionId> mConnection;
    };
}

#endif
