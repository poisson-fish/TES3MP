#include <tes3mp/headless_client_session.hpp>

#include <array>

namespace TES3MP
{
    HeadlessClientCreateResult HeadlessClientSession::create(TransportRuntime& transport, MonotonicClock& clock,
        SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation)
    {
        auto state = ClientSessionStateMachine::create(clock, timeoutPolicy, generation);
        if (auto* failure = std::get_if<SessionTransitionError>(&state)) return *failure;
        return std::unique_ptr<HeadlessClientSession>(new HeadlessClientSession(
            transport, std::get<std::unique_ptr<ClientSessionStateMachine>>(std::move(state))));
    }

    HeadlessClientSession::HeadlessClientSession(
        TransportRuntime& transport, std::unique_ptr<ClientSessionStateMachine> state) noexcept
        : mTransport(transport), mState(std::move(state)) {}

    HeadlessClientResult HeadlessClientSession::connect(const ConnectionEndpoint& endpoint) noexcept
    {
        if (mAttempt || mConnection) return HeadlessClientResult::AlreadyStarted;
        const auto admitted = mTransport.connect(endpoint);
        if (admitted.result != TransportResult::Accepted || !admitted.id)
            return HeadlessClientResult::TransportRejected;
        mAttempt = admitted.id;
        return HeadlessClientResult::Accepted;
    }

    HeadlessClientPumpResult HeadlessClientSession::pump() noexcept
    {
        std::array<TransportEvent, 32> events{};
        const auto polled = mTransport.poll(events);
        if (polled.result != TransportResult::Accepted || polled.events > events.size())
        {
            mState->handle(ClientClose{});
            return { HeadlessClientResult::TransportFailed, ClientSessionAction::SessionClosed, 0 };
        }
        ClientSessionAction action = ClientSessionAction::None;
        for (std::size_t index = 0; index < polled.events; ++index)
        {
            const auto& event = events[index];
            if (event.kind == TransportEventKind::ConnectSucceeded && mAttempt && event.attempt == mAttempt
                && event.connection && event.security == TransportSecurity::EncryptedUnauthenticated)
            {
                mConnection = event.connection;
                mAttempt.reset();
                action = mState->handle(ClientEncryptedTransportReady{}).action;
            }
            else if ((event.kind == TransportEventKind::ConnectFailed
                         || event.kind == TransportEventKind::ConnectCancelled)
                && mAttempt && event.attempt == mAttempt)
            {
                mAttempt.reset();
                mState->handle(ClientClose{});
                return { HeadlessClientResult::TransportFailed, ClientSessionAction::SessionClosed, polled.events };
            }
            else if ((event.kind == TransportEventKind::ConnectionClosed
                         || event.kind == TransportEventKind::RuntimeFailed)
                && (event.kind == TransportEventKind::RuntimeFailed || (mConnection && event.connection == mConnection)))
            {
                mConnection.reset();
                mState->handle(ClientClose{});
                return { HeadlessClientResult::TransportFailed, ClientSessionAction::SessionClosed, polled.events };
            }
        }
        return { HeadlessClientResult::Accepted, action, polled.events };
    }

    ClientSessionTransition HeadlessClientSession::handle(ClientSessionEvent event) noexcept
    { return mState->handle(std::move(event)); }

    ClientSessionBindingResult HeadlessClientSession::bindEstablishedSession(SessionId session) noexcept
    { return mState->bindEstablishedSession(session); }

    LatestWinsSnapshotReceiveResult HeadlessClientSession::receiveLatestWinsSnapshot(LatestWinsSnapshot snapshot)
    { return mState->receiveLatestWinsSnapshot(std::move(snapshot)); }

    HeadlessClientResult HeadlessClientSession::close() noexcept
    {
        if (mAttempt) { mTransport.cancelConnect(*mAttempt); mAttempt.reset(); }
        if (mConnection) { mTransport.close(*mConnection, TransportCloseMode::Graceful); mConnection.reset(); }
        mState->handle(ClientClose{});
        return HeadlessClientResult::Accepted;
    }
}
