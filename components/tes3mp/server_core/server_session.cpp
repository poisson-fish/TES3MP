#include <tes3mp/server_session.hpp>

#include <array>
#include <type_traits>
#include <utility>

namespace
{
    TES3MP::ServerSessionEventKind eventKind(const TES3MP::ServerSessionEvent& event) noexcept
    {
        return std::visit(
            [](const auto& value) {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, TES3MP::ServerEncryptedTransportReady>)
                    return TES3MP::ServerSessionEventKind::EncryptedTransportReady;
                else if constexpr (std::is_same_v<Event, TES3MP::ServerClientHelloReceived>)
                    return TES3MP::ServerSessionEventKind::ClientHelloReceived;
                else if constexpr (std::is_same_v<Event, TES3MP::ServerAuthenticationSubmitted>)
                    return TES3MP::ServerSessionEventKind::AuthenticationSubmitted;
                else if constexpr (std::is_same_v<Event, TES3MP::ServerPollAuthentication>)
                    return TES3MP::ServerSessionEventKind::PollAuthentication;
                else if constexpr (std::is_same_v<Event, TES3MP::ServerCheckTimeout>)
                    return TES3MP::ServerSessionEventKind::CheckTimeout;
                else if constexpr (std::is_same_v<Event, TES3MP::ServerCancel>)
                    return TES3MP::ServerSessionEventKind::Cancel;
                else
                    return TES3MP::ServerSessionEventKind::Close;
            },
            event);
    }

    TES3MP::SessionObservationStage observationStage(TES3MP::ServerSessionState state) noexcept
    {
        using TES3MP::ServerSessionState;
        switch (state)
        {
            case ServerSessionState::AwaitingEncryptedTransport:
            case ServerSessionState::AwaitingClientHello:
                return TES3MP::SessionObservationStage::TransportAndNegotiation;
            case ServerSessionState::AwaitingAuthenticationInput:
                return TES3MP::SessionObservationStage::AuthenticationInput;
            case ServerSessionState::AuthenticationPending:
                return TES3MP::SessionObservationStage::AuthenticationProvider;
            case ServerSessionState::Established:
            case ServerSessionState::Rejected:
            case ServerSessionState::TimedOut:
            case ServerSessionState::Cancelled:
            case ServerSessionState::Closed:
                return TES3MP::SessionObservationStage::Terminal;
        }
        return TES3MP::SessionObservationStage::Terminal;
    }

    TES3MP::MetricDimensionValue metricValue(TES3MP::SessionObservationOutcome outcome) noexcept
    {
        using TES3MP::MetricDimensionValue;
        using TES3MP::SessionObservationOutcome;
        switch (outcome)
        {
            case SessionObservationOutcome::TransitionAccepted:
                return MetricDimensionValue::TransitionAccepted;
            case SessionObservationOutcome::IllegalTransition:
                return MetricDimensionValue::IllegalTransition;
            case SessionObservationOutcome::AuthenticationSucceeded:
                return MetricDimensionValue::AuthenticationSucceeded;
            case SessionObservationOutcome::AuthenticationRejected:
                return MetricDimensionValue::AuthenticationRejected;
            case SessionObservationOutcome::TimedOut:
                return MetricDimensionValue::TimedOut;
            case SessionObservationOutcome::Cancelled:
                return MetricDimensionValue::Cancelled;
            case SessionObservationOutcome::StaleCompletion:
                return MetricDimensionValue::StaleCompletion;
        }
        return MetricDimensionValue::IllegalTransition;
    }

    TES3MP::MetricKey metricKey(TES3MP::SessionObservationOutcome outcome) noexcept
    {
        using TES3MP::MetricKey;
        using TES3MP::SessionObservationOutcome;
        switch (outcome)
        {
            case SessionObservationOutcome::AuthenticationSucceeded:
            case SessionObservationOutcome::AuthenticationRejected:
                return MetricKey::SessionAuthenticationOutcomes;
            case SessionObservationOutcome::TimedOut:
                return MetricKey::SessionTimeouts;
            case SessionObservationOutcome::Cancelled:
                return MetricKey::SessionCancellations;
            case SessionObservationOutcome::StaleCompletion:
                return MetricKey::SessionStaleCompletions;
            case SessionObservationOutcome::TransitionAccepted:
            case SessionObservationOutcome::IllegalTransition:
                return MetricKey::SessionTransitions;
        }
        return MetricKey::SessionTransitions;
    }
}

namespace TES3MP
{
    ServerSessionCreateResult ServerSessionStateMachine::create(MonotonicClock& clock, Observability& observability,
        SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, CapabilityOffer serverOffer,
        AuthenticationProvider& authenticationProvider)
    {
        const auto deadline
            = sessionDeadline(clock.now(), timeoutPolicy.duration(SessionStage::TransportAndNegotiation));
        if (!deadline)
        {
            return SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow,
                static_cast<std::uint16_t>(ServerSessionState::AwaitingEncryptedTransport), 0,
                SessionStage::TransportAndNegotiation };
        }
        return std::unique_ptr<ServerSessionStateMachine>(new ServerSessionStateMachine(clock, observability,
            timeoutPolicy, generation, std::move(serverOffer), authenticationProvider, *deadline));
    }

    ServerSessionStateMachine::ServerSessionStateMachine(MonotonicClock& clock, Observability& observability,
        SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, CapabilityOffer serverOffer,
        AuthenticationProvider& authenticationProvider, MonotonicInstant deadline)
        : mClock(clock)
        , mObservability(observability)
        , mTimeoutPolicy(timeoutPolicy)
        , mGeneration(generation)
        , mServerOffer(std::move(serverOffer))
        , mAuthenticationProvider(authenticationProvider)
        , mDeadline(deadline)
        , mActiveAttempt{ AuthenticationAttemptId::initial(), generation }
    {
    }

    ServerSessionStateMachine::~ServerSessionStateMachine()
    {
        cancelAuthentication();
    }

    ServerSessionTransition ServerSessionStateMachine::illegal(ServerSessionEventKind event) noexcept
    {
        observe(SessionObservationOutcome::IllegalTransition, observationStage(mState));
        return { ServerSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::IllegalTransition, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event),
                mState == ServerSessionState::AwaitingAuthenticationInput
                    ? SessionStage::AuthenticationInput
                    : (mState == ServerSessionState::AuthenticationPending ? SessionStage::AuthenticationProvider
                                                                           : SessionStage::TransportAndNegotiation) } };
    }

    ServerSessionTransition ServerSessionStateMachine::deadlineOverflow(
        ServerSessionEventKind event, SessionStage stage) noexcept
    {
        return { ServerSessionAction::None,
            SessionTransitionError{ SessionTransitionErrorCode::DeadlineOverflow, static_cast<std::uint16_t>(mState),
                static_cast<std::uint16_t>(event), stage } };
    }

    bool ServerSessionStateMachine::prepareDeadline(SessionStage stage, MonotonicInstant& result) const noexcept
    {
        const auto deadline = sessionDeadline(mClock.now(), mTimeoutPolicy.duration(stage));
        if (!deadline)
            return false;
        result = *deadline;
        return true;
    }

    void ServerSessionStateMachine::cancelAuthentication() noexcept
    {
        if (!mAuthenticationOperation)
            return;
        mAuthenticationOperation->cancel();
        mAuthenticationOperation.reset();
    }

    void ServerSessionStateMachine::observe(SessionObservationOutcome outcome, SessionObservationStage stage) noexcept
    {
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::SessionOutcome, metricValue(outcome) },
        };
        if (const auto metric = MetricObservation::create(metricKey(outcome), CounterAddition{ 1 }, dimensions))
            (void)mObservability.metrics().tryRecord(*metric);
        const auto severity = outcome == SessionObservationOutcome::TransitionAccepted
                || outcome == SessionObservationOutcome::AuthenticationSucceeded
            ? EventSeverity::Info
            : EventSeverity::Warning;
        if (const auto event = StructuredEvent::create(
                severity, std::nullopt, SessionLifecycleEvent{ SessionObservationRole::Server, outcome, stage }))
            (void)mObservability.events().tryRecord(*event);
    }

    ServerSessionTransition ServerSessionStateMachine::handle(ServerSessionEvent event) noexcept
    {
        const auto kind = eventKind(event);

        if (std::holds_alternative<ServerClose>(event))
        {
            if (mState == ServerSessionState::Closed)
                return {};
            cancelAuthentication();
            mState = ServerSessionState::Closed;
            mDeadline.reset();
            observe(SessionObservationOutcome::TransitionAccepted, SessionObservationStage::Terminal);
            return { ServerSessionAction::SessionClosed, std::nullopt };
        }

        if (std::holds_alternative<ServerCancel>(event))
        {
            if (mState == ServerSessionState::Cancelled || mState == ServerSessionState::Rejected
                || mState == ServerSessionState::TimedOut || mState == ServerSessionState::Closed)
                return {};
            cancelAuthentication();
            mState = ServerSessionState::Cancelled;
            mDeadline.reset();
            observe(SessionObservationOutcome::Cancelled, SessionObservationStage::Terminal);
            return { ServerSessionAction::SessionCancelled, std::nullopt };
        }

        if (std::holds_alternative<ServerCheckTimeout>(event))
        {
            if (!mDeadline || mState == ServerSessionState::Established || mState == ServerSessionState::Rejected
                || mState == ServerSessionState::TimedOut || mState == ServerSessionState::Cancelled
                || mState == ServerSessionState::Closed)
                return {};
            if (mClock.now() < *mDeadline)
                return {};
            const auto stage = observationStage(mState);
            cancelAuthentication();
            mState = ServerSessionState::TimedOut;
            mDeadline.reset();
            observe(SessionObservationOutcome::TimedOut, stage);
            return { ServerSessionAction::SessionTimedOut, std::nullopt };
        }

        if (std::holds_alternative<ServerEncryptedTransportReady>(event))
        {
            if (mState != ServerSessionState::AwaitingEncryptedTransport)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::TransportAndNegotiation, deadline))
                return deadlineOverflow(kind, SessionStage::TransportAndNegotiation);
            mState = ServerSessionState::AwaitingClientHello;
            mDeadline = deadline;
            observe(SessionObservationOutcome::TransitionAccepted, SessionObservationStage::TransportAndNegotiation);
            return {};
        }

        if (auto* hello = std::get_if<ServerClientHelloReceived>(&event))
        {
            if (mState != ServerSessionState::AwaitingClientHello)
                return illegal(kind);
            auto negotiation = negotiateClientHello(hello->hello, mServerOffer);
            if (auto* rejected = std::get_if<SessionRejected>(&negotiation))
            {
                mProtocolRejection = std::move(*rejected);
                mState = ServerSessionState::Rejected;
                mDeadline.reset();
                observe(SessionObservationOutcome::TransitionAccepted, SessionObservationStage::Terminal);
                return { ServerSessionAction::SendSessionRejected, std::nullopt };
            }
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationInput, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationInput);
            mNegotiatedHello = std::get<ServerHello>(std::move(negotiation));
            mState = ServerSessionState::AwaitingAuthenticationInput;
            mDeadline = deadline;
            observe(SessionObservationOutcome::TransitionAccepted, SessionObservationStage::TransportAndNegotiation);
            return { ServerSessionAction::SendServerHello, std::nullopt };
        }

        if (auto* submitted = std::get_if<ServerAuthenticationSubmitted>(&event))
        {
            if (mState != ServerSessionState::AwaitingAuthenticationInput)
                return illegal(kind);
            MonotonicInstant deadline = mClock.now();
            if (!prepareDeadline(SessionStage::AuthenticationProvider, deadline))
                return deadlineOverflow(kind, SessionStage::AuthenticationProvider);
            auto operation = mAuthenticationProvider.begin(mActiveAttempt, std::move(submitted->material));
            if (!operation)
            {
                mAuthenticationRejection = AuthenticationRejected{ AuthenticationRejectionReason::ProviderUnavailable };
                mState = ServerSessionState::Rejected;
                mDeadline.reset();
                observe(
                    SessionObservationOutcome::AuthenticationRejected, SessionObservationStage::AuthenticationProvider);
                return { ServerSessionAction::AuthenticationRejected, std::nullopt };
            }
            mAuthenticationOperation = std::move(operation);
            mState = ServerSessionState::AuthenticationPending;
            mDeadline = deadline;
            observe(SessionObservationOutcome::TransitionAccepted, SessionObservationStage::AuthenticationProvider);
            return { ServerSessionAction::AuthenticationStarted, std::nullopt };
        }

        if (std::holds_alternative<ServerPollAuthentication>(event))
        {
            if (mState != ServerSessionState::AuthenticationPending || !mAuthenticationOperation)
                return illegal(kind);
            auto poll = mAuthenticationOperation->poll();
            if (std::holds_alternative<AuthenticationPending>(poll))
                return { ServerSessionAction::AuthenticationPending, std::nullopt };
            auto completion = std::get<AuthenticationCompletion>(std::move(poll));
            if (completion.attempt != mActiveAttempt)
            {
                observe(SessionObservationOutcome::StaleCompletion, SessionObservationStage::AuthenticationProvider);
                return { ServerSessionAction::AuthenticationStaleCompletion, std::nullopt };
            }
            mAuthenticationOperation.reset();
            mDeadline.reset();
            if (auto* admission = std::get_if<AuthenticatedAdmission>(&completion.result))
            {
                mPrincipal = admission->principal();
                mResumeGrant = admission->takeResumeGrant();
                mState = ServerSessionState::Established;
                observe(SessionObservationOutcome::AuthenticationSucceeded, SessionObservationStage::Terminal);
                return { ServerSessionAction::SessionEstablished, std::nullopt };
            }
            mAuthenticationRejection = std::get<AuthenticationRejected>(completion.result);
            mState = ServerSessionState::Rejected;
            observe(SessionObservationOutcome::AuthenticationRejected, SessionObservationStage::Terminal);
            return { ServerSessionAction::AuthenticationRejected, std::nullopt };
        }

        return illegal(kind);
    }

    ServerSessionBindingResult ServerSessionStateMachine::bindEstablishedSession(SessionId sessionId) noexcept
    {
        if (mState != ServerSessionState::Established)
            return ServerSessionBindingResult::NotEstablished;
        if (mSessionId)
            return ServerSessionBindingResult::AlreadyBound;
        mSessionId = sessionId;
        return ServerSessionBindingResult::Bound;
    }

    ReliableOperationReceiveResult ServerSessionStateMachine::receiveReliableOperation(
        const ReliableOperation& operation) const noexcept
    {
        if (mState != ServerSessionState::Established)
            return ReliableOperationReceiveResult::NotEstablished;
        if (!mSessionId)
            return ReliableOperationReceiveResult::SessionNotBound;
        const auto& command = operation.header().commandHeader();
        if (command.sessionId() != *mSessionId)
            return ReliableOperationReceiveResult::SessionMismatch;
        if (command.sessionGeneration() != mGeneration)
            return ReliableOperationReceiveResult::GenerationMismatch;
        return ReliableOperationReceiveResult::Delivered;
    }
}
