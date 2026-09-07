#include "connection_session_coordinator.hpp"
#include "authenticated_join_composition.hpp"
#include "resume_token_context.hpp"

#include "tes3mp/authentication.hpp"
#include "tes3mp/protocol_frame.hpp"
#include "tes3mp/protocol_handshake.hpp"

#include <variant>

namespace TES3MP::ServerApp
{
    ConnectionSessionResult ConnectionSessionCoordinator::dispatch(TransportConnectionId connection,
        const TransportMessage& message, AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto,
        ServerTick tick) noexcept
    {
        try
        {
            ServerCommandIntakeCoordinator unused(
                mClock, mObservability, mClock.now(), tick, IngressOrdinal::initial());
            return dispatch(connection, message, joins, crypto, unused, tick);
        }
        catch (...) { return ConnectionSessionResult::SessionRejected; }
    }

    ConnectionSessionCoordinator::ConnectionSessionCoordinator(MonotonicClock& clock, Observability& observability,
        SessionTimeoutPolicy timeouts, CapabilityOffer offer, ServerAuthenticationService& authentication,
        OutboundQueueSet& queues, std::size_t capacity, const CanonicalActorWorld* actors) noexcept
        : mClock(clock), mObservability(observability), mTimeouts(timeouts), mOffer(std::move(offer)),
          mAuthentication(authentication), mQueues(queues), mCapacity(capacity), mActors(actors)
    {
    }

    ConnectionSessionResult ConnectionSessionCoordinator::accept(
        TransportConnectionId connection, AdmissionScopeId scope) noexcept
    {
        if (mConnections.contains(connection)) return ConnectionSessionResult::Duplicate;
        if (mConnections.size() >= mCapacity) return ConnectionSessionResult::AtCapacity;
        if (mQueues.attach(connection) != TransportResult::Accepted) return ConnectionSessionResult::QueueRejected;

        try
        {
            auto created = ServerSessionStateMachine::create(mClock, mObservability, mTimeouts,
                SessionGeneration::initial(), mOffer, mAuthentication);
            if (!std::holds_alternative<std::unique_ptr<ServerSessionStateMachine>>(created))
            {
                (void)mQueues.detach(connection);
                return ConnectionSessionResult::SessionRejected;
            }
            auto state = std::get<std::unique_ptr<ServerSessionStateMachine>>(std::move(created));
            if (!state->handle(ServerEncryptedTransportReady{}).accepted())
            {
                (void)mQueues.detach(connection);
                return ConnectionSessionResult::SessionRejected;
            }
            mConnections.emplace(connection, Connection{ std::move(scope), std::move(state) });
            return ConnectionSessionResult::Accepted;
        }
        catch (...)
        {
            (void)mQueues.detach(connection);
            return ConnectionSessionResult::SessionRejected;
        }
    }

    ConnectionSessionResult ConnectionSessionCoordinator::close(TransportConnectionId connection) noexcept
    {
        const auto found = mConnections.find(connection);
        if (found == mConnections.end()) return ConnectionSessionResult::UnknownConnection;
        (void)found->second.session->handle(ServerClose{});
        mConnections.erase(found);
        (void)mQueues.detach(connection);
        return ConnectionSessionResult::Accepted;
    }

    ServerSessionStateMachine* ConnectionSessionCoordinator::session(TransportConnectionId connection) noexcept
    {
        const auto found = mConnections.find(connection);
        return found == mConnections.end() ? nullptr : found->second.session.get();
    }

    const AdmissionScopeId* ConnectionSessionCoordinator::admissionScope(TransportConnectionId connection) const noexcept
    {
        const auto found = mConnections.find(connection);
        return found == mConnections.end() ? nullptr : &found->second.scope;
    }

    std::vector<TransportConnectionId> ConnectionSessionCoordinator::connections() const
    {
        std::vector<TransportConnectionId> result;
        result.reserve(mConnections.size());
        for (const auto& [connection, ignored] : mConnections)
        {
            (void)ignored;
            result.push_back(connection);
        }
        return result;
    }

    std::optional<TransportConnectionId> ConnectionSessionCoordinator::connectionForSession(SessionId value) const noexcept
    {
        for (const auto& [connection, state] : mConnections)
            if (state.session->sessionId() == value) return connection;
        return std::nullopt;
    }

    std::optional<SessionResyncRequest> ConnectionSessionCoordinator::takeResyncRequest(
        TransportConnectionId connection) noexcept
    {
        const auto found = mConnections.find(connection);
        if (found == mConnections.end()) return std::nullopt;
        auto result = std::move(found->second.pendingResync);
        found->second.pendingResync.reset();
        return result;
    }

    ConnectionSessionResult ConnectionSessionCoordinator::dispatch(TransportConnectionId connection,
        const TransportMessage& message, AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto,
        ServerCommandIntakeCoordinator& intake, ServerTick tick) noexcept
    {
        auto* state = session(connection);
        const auto* scope = admissionScope(connection);
        if (state == nullptr || scope == nullptr) return ConnectionSessionResult::UnknownConnection;
        if (message.channel != TransportChannel::ReliableOrdered)
            return ConnectionSessionResult::ProtocolRejected;

        auto decoded = decodeProtocolFrame(message.bytes);
        auto* frame = std::get_if<DecodedFrame>(&decoded);
        if (frame == nullptr || (frame->messageClass() != MessageClass::SessionControl
            && frame->messageClass() != MessageClass::ReliableOperation))
            return ConnectionSessionResult::ProtocolRejected;

        if (frame->messageKind() == MessageKind::SessionResyncRequest)
        {
            if (frame->messageClass() != MessageClass::SessionControl
                || state->state() != ServerSessionState::Established || !state->sessionId())
                return ConnectionSessionResult::ProtocolRejected;
            auto decodedRequest = decodeSessionResyncRequest(frame->payload());
            auto* request = std::get_if<SessionResyncRequest>(&decodedRequest);
            if (!request || request->sessionId() != *state->sessionId()
                || request->sessionGeneration() != state->generation())
                return ConnectionSessionResult::ProtocolRejected;
            auto found = mConnections.find(connection);
            if (found->second.pendingResync)
                return ConnectionSessionResult::ResyncCoalesced;
            found->second.pendingResync = std::move(*request);
            return ConnectionSessionResult::ResyncRequested;
        }

        if (frame->messageKind() == MessageKind::ReliableOperation)
        {
            if (frame->messageClass() != MessageClass::ReliableOperation)
                return ConnectionSessionResult::ProtocolRejected;
            if (state->state() != ServerSessionState::Established || !state->sessionId())
                return ConnectionSessionResult::ProtocolRejected;
            auto decodedOperation = decodeReliableOperation(frame->payload());
            auto* operation = std::get_if<ReliableOperation>(&decodedOperation);
            if (!operation || !operation->header().entityPrecondition()
                || operation->header().commandHeader().sessionId() != *state->sessionId()
                || operation->header().commandHeader().sessionGeneration() != state->generation())
                return ConnectionSessionResult::ProtocolRejected;
            const auto& header = operation->header().commandHeader();
            const auto negotiated = state->negotiatedHello()->selectedVersion();
            if (const auto* transition = std::get_if<CellTransition>(&operation->body()))
            {
                ServerCommandProposal proposal(header.sessionId(), header.sessionGeneration(),
                    header.commandSequence(), header.commandId(), header.observedCanonicalRevision(),
                    *operation->header().entityPrecondition(),
                    CellTransitionCommandProposal(transition->requestedCell()));
                return intake.submit(std::move(proposal)) == CommandSubmissionResult::Accepted
                    ? ConnectionSessionResult::CommandSubmitted : ConnectionSessionResult::QueueRejected;
            }
            if (const auto* motion = std::get_if<PlayerMotionIntent>(&operation->body()))
            {
                if (negotiated.major != 1 || negotiated.minor != 2)
                    return ConnectionSessionResult::ProtocolRejected;
                ServerCommandProposal proposal(header.sessionId(), header.sessionGeneration(),
                    header.commandSequence(), header.commandId(), header.observedCanonicalRevision(),
                    *operation->header().entityPrecondition(),
                    PlayerMotionCommandProposal(motion->desiredVelocity()));
                return intake.submit(std::move(proposal)) == CommandSubmissionResult::Accepted
                    ? ConnectionSessionResult::CommandSubmitted : ConnectionSessionResult::QueueRejected;
            }
            if (const auto* locomotion = std::get_if<PlayerLocomotionInput>(&operation->body()))
            {
                if (negotiated.major != 1 || negotiated.minor < 3)
                    return ConnectionSessionResult::ProtocolRejected;
                auto& connectionState = mConnections.find(connection)->second;
                const auto expectedSequence = connectionState.lastLocomotionInputSequence
                    ? connectionState.lastLocomotionInputSequence->next()
                    : std::optional<LocomotionInputSequence>(LocomotionInputSequence::initial());
                if (!expectedSequence || locomotion->inputSequence() != *expectedSequence
                    || (connectionState.lastLocomotionInputTick
                        && locomotion->inputTick() <= *connectionState.lastLocomotionInputTick))
                    return ConnectionSessionResult::ProtocolRejected;
                ServerCommandProposal proposal(header.sessionId(), header.sessionGeneration(),
                    header.commandSequence(), header.commandId(), header.observedCanonicalRevision(),
                    *operation->header().entityPrecondition(),
                    PlayerLocomotionCommandProposal(locomotion->inputTick(), locomotion->inputSequence(),
                        locomotion->intent()));
                if (intake.submit(std::move(proposal)) != CommandSubmissionResult::Accepted)
                    return ConnectionSessionResult::QueueRejected;
                connectionState.lastLocomotionInputTick = locomotion->inputTick();
                connectionState.lastLocomotionInputSequence = locomotion->inputSequence();
                return ConnectionSessionResult::CommandSubmitted;
            }
            return ConnectionSessionResult::ProtocolRejected;
        }

        if (frame->messageKind() == MessageKind::ClientHello)
        {
            auto hello = decodeClientHello(frame->payload());
            auto* value = std::get_if<ClientHello>(&hello);
            if (value == nullptr
                || state->handle(ServerClientHelloReceived{ std::move(*value) }).action
                    != ServerSessionAction::SendServerHello
                || !state->negotiatedHello())
                return ConnectionSessionResult::ProtocolRejected;
            auto payload = encodeServerHello(*state->negotiatedHello());
            auto encoded = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ServerHello, payload);
            auto* bytes = std::get_if<std::vector<std::byte>>(&encoded);
            if (bytes == nullptr
                || mQueues.enqueue(connection, TransportChannel::ReliableOrdered, *bytes) != TransportResult::Accepted)
                return ConnectionSessionResult::QueueRejected;
            return ConnectionSessionResult::Accepted;
        }

        if (frame->messageKind() != MessageKind::AuthenticationRequest || !state->negotiatedHello())
            return ConnectionSessionResult::ProtocolRejected;
        auto request = decodeAuthenticationRequest(frame->payload());
        auto* value = std::get_if<AuthenticationRequest>(&request);
        auto context = makeResumeTokenContext(*state->negotiatedHello(), crypto);
        if (value == nullptr || !context
            || state->handle(ServerAuthenticationSubmitted{ ServerAuthenticationSubmission(std::move(*value),
                   *scope, *context, state->negotiatedHello()->contentManifest()) })
                    .action != ServerSessionAction::AuthenticationStarted)
            return ConnectionSessionResult::ProtocolRejected;
        return pollAuthentication(connection, joins, crypto, tick);
    }

    ConnectionSessionResult ConnectionSessionCoordinator::pollAuthentication(TransportConnectionId connection,
        AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto, ServerTick tick) noexcept
    {
        auto* state = session(connection);
        if (state == nullptr) return ConnectionSessionResult::UnknownConnection;
        const auto transition = state->handle(ServerPollAuthentication{});
        if (transition.action == ServerSessionAction::AuthenticationPending)
            return ConnectionSessionResult::AuthenticationPending;
        if (transition.action != ServerSessionAction::SessionEstablished || !state->principal()
            || !state->negotiatedHello())
            return ConnectionSessionResult::ProtocolRejected;
        if (state->preparedResumeId()) return ConnectionSessionResult::ResumePrepared;
        auto context = makeResumeTokenContext(*state->negotiatedHello(), crypto);
        if (!context) return ConnectionSessionResult::ProtocolRejected;
        TransportJoinResponseQueue responses(mQueues, connection, this, mActors);
        AuthenticatedJoinComposition composition(joins, mAuthentication, responses);
        auto outcome = composition.join(
            *state->principal(), state->generation(), tick, *context, state->playerClaim());
        if (outcome.result != JoinCompositionResult::Committed || !outcome.committed)
            return ConnectionSessionResult::ProtocolRejected;
        if (state->bindPreissuedInitialSession(outcome.committed->session)
            != PreissuedInitialSessionBindingResult::Bound)
            return ConnectionSessionResult::ProtocolRejected;
        return ConnectionSessionResult::Joined;
    }
}
