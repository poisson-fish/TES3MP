#include <tes3mp/client_session.hpp>

#include <type_traits>
#include <utility>

namespace
{
    TES3MP::ClientSessionEventKind eventKind(const TES3MP::ClientSessionEvent& event) noexcept
    {
        return std::visit(
            [](const auto& value) {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, TES3MP::ClientEncryptedTransportReady>)
                    return TES3MP::ClientSessionEventKind::EncryptedTransportReady;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientServerHelloReceived>)
                    return TES3MP::ClientSessionEventKind::ServerHelloReceived;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientSessionRejectedReceived>)
                    return TES3MP::ClientSessionEventKind::SessionRejectedReceived;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationSubmitted>)
                    return TES3MP::ClientSessionEventKind::AuthenticationSubmitted;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationAccepted>)
                    return TES3MP::ClientSessionEventKind::AuthenticationAccepted;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientAuthenticationRejected>)
                    return TES3MP::ClientSessionEventKind::AuthenticationRejected;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientCheckTimeout>)
                    return TES3MP::ClientSessionEventKind::CheckTimeout;
                else if constexpr (std::is_same_v<Event, TES3MP::ClientCancel>)
                    return TES3MP::ClientSessionEventKind::Cancel;
                else
                    return TES3MP::ClientSessionEventKind::Close;
            },
            event);
    }
}

namespace TES3MP
{
    ClientSessionCreateResult ClientSessionStateMachine::create(
        MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation)
    {
        const auto deadline
            = sessionDeadline(clock.now(), timeoutPolicy.duration(SessionStage::TransportAndNegotiation));
        if (!deadline)
        {
            return SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow,
                static_cast<std::uint16_t>(ClientSessionState::AwaitingEncryptedTransport), 0,
                SessionStage::TransportAndNegotiation };
        }
        return std::unique_ptr<ClientSessionStateMachine>(
            new ClientSessionStateMachine(clock, timeoutPolicy, generation, *deadline));
    }

    ClientSessionStateMachine::ClientSessionStateMachine(MonotonicClock& clock, SessionTimeoutPolicy timeoutPolicy,
        SessionGeneration generation, MonotonicInstant deadline) noexcept
        : mClock(clock)
        , mTimeoutPolicy(timeoutPolicy)
        , mGeneration(generation)
        , mDeadline(deadline)
    {
    }

    ClientSessionTransition ClientSessionStateMachine::illegal(ClientSessionEventKind event) const noexcept
    {
        const SessionStage stage = mState == ClientSessionState::AwaitingAuthenticationInput
            ? SessionStage::AuthenticationInput
            : (mState == ClientSessionState::AwaitingAuthenticationResult ? SessionStage::AuthenticationProvider
                                                                          : SessionStage::TransportAndNegotiation);
        return { ClientSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::IllegalTransition, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event), stage } };
    }

    ClientSessionTransition ClientSessionStateMachine::deadlineOverflow(
        ClientSessionEventKind event, SessionStage stage) const noexcept
    {
        return { ClientSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event), stage } };
    }

    bool ClientSessionStateMachine::prepareDeadline(SessionStage stage, MonotonicInstant& result) const noexcept
    {
        const auto deadline = sessionDeadline(mClock.now(), mTimeoutPolicy.duration(stage));
        if (!deadline)
            return false;
        result = *deadline;
        return true;
    }

    ClientSessionTransition ClientSessionStateMachine::handle(ClientSessionEvent event) noexcept
    {
        const auto kind = eventKind(event);

        if (std::holds_alternative<ClientClose>(event))
        {
            if (mState == ClientSessionState::Closed)
                return {};
            mState = ClientSessionState::Closed;
            mDeadline.reset();
            return { ClientSessionAction::SessionClosed, std::nullopt };
        }

        if (std::holds_alternative<ClientCancel>(event))
        {
            if (mState == ClientSessionState::Cancelled || mState == ClientSessionState::Rejected
                || mState == ClientSessionState::TimedOut || mState == ClientSessionState::Closed)
                return {};
            mState = ClientSessionState::Cancelled;
            mDeadline.reset();
            return { ClientSessionAction::SessionCancelled, std::nullopt };
        }

        if (std::holds_alternative<ClientCheckTimeout>(event))
        {
            if (!mDeadline || mState == ClientSessionState::Established || mState == ClientSessionState::Rejected
                || mState == ClientSessionState::TimedOut || mState == ClientSessionState::Cancelled
                || mState == ClientSessionState::Closed)
                return {};
            if (mClock.now() < *mDeadline)
                return {};
            mState = ClientSessionState::TimedOut;
            mDeadline.reset();
            return { ClientSessionAction::SessionTimedOut, std::nullopt };
        }

        if (std::holds_alternative<ClientEncryptedTransportReady>(event))
        {
            if (mState != ClientSessionState::AwaitingEncryptedTransport)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::TransportAndNegotiation, deadline))
                return deadlineOverflow(kind, SessionStage::TransportAndNegotiation);
            mState = ClientSessionState::AwaitingServerHello;
            mDeadline = deadline;
            return { ClientSessionAction::SendClientHello, std::nullopt };
        }

        if (auto* hello = std::get_if<ClientServerHelloReceived>(&event))
        {
            if (mState != ClientSessionState::AwaitingServerHello)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationInput, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationInput);
            mNegotiatedHello = std::move(hello->hello);
            mState = ClientSessionState::AwaitingAuthenticationInput;
            mDeadline = deadline;
            return { ClientSessionAction::AuthenticationInputReady, std::nullopt };
        }

        if (auto* rejection = std::get_if<ClientSessionRejectedReceived>(&event))
        {
            if (mState != ClientSessionState::AwaitingServerHello)
                return illegal(kind);
            mProtocolRejection = std::move(rejection->rejection);
            mState = ClientSessionState::Rejected;
            mDeadline.reset();
            return { ClientSessionAction::SessionRejected, std::nullopt };
        }

        if (std::holds_alternative<ClientAuthenticationSubmitted>(event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationInput)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationProvider, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationProvider);
            mState = ClientSessionState::AwaitingAuthenticationResult;
            mDeadline = deadline;
            return { ClientSessionAction::AuthenticationSubmitted, std::nullopt };
        }

        if (std::holds_alternative<ClientAuthenticationAccepted>(event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationResult)
                return illegal(kind);
            mState = ClientSessionState::Established;
            mDeadline.reset();
            return { ClientSessionAction::SessionEstablished, std::nullopt };
        }

        if (const auto* rejected = std::get_if<ClientAuthenticationRejected>(&event))
        {
            if (mState != ClientSessionState::AwaitingAuthenticationResult)
                return illegal(kind);
            mAuthenticationRejection = rejected->reason;
            mState = ClientSessionState::Rejected;
            mDeadline.reset();
            return { ClientSessionAction::SessionRejected, std::nullopt };
        }

        return illegal(kind);
    }
}
