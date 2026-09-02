#include "connection_session_coordinator.hpp"
#include "authenticated_join_composition.hpp"
#include "resume_token_context.hpp"

#include "tes3mp/authentication.hpp"
#include "tes3mp/protocol_frame.hpp"
#include "tes3mp/protocol_handshake.hpp"

#include <variant>

namespace TES3MP::ServerApp
{
    ConnectionSessionCoordinator::ConnectionSessionCoordinator(MonotonicClock& clock, Observability& observability,
        SessionTimeoutPolicy timeouts, CapabilityOffer offer, ServerAuthenticationService& authentication,
        OutboundQueueSet& queues, std::size_t capacity) noexcept
        : mClock(clock), mObservability(observability), mTimeouts(timeouts), mOffer(std::move(offer)),
          mAuthentication(authentication), mQueues(queues), mCapacity(capacity)
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

    ConnectionSessionResult ConnectionSessionCoordinator::dispatch(TransportConnectionId connection,
        const TransportMessage& message, AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto,
        ServerTick tick) noexcept
    {
        auto* state = session(connection);
        const auto* scope = admissionScope(connection);
        if (state == nullptr || scope == nullptr) return ConnectionSessionResult::UnknownConnection;
        if (message.channel != TransportChannel::ReliableOrdered)
            return ConnectionSessionResult::ProtocolRejected;

        auto decoded = decodeProtocolFrame(message.bytes);
        auto* frame = std::get_if<DecodedFrame>(&decoded);
        if (frame == nullptr || frame->messageClass() != MessageClass::SessionControl)
            return ConnectionSessionResult::ProtocolRejected;

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
        auto context = makePhase7ResumeTokenContext(*state->negotiatedHello(), crypto);
        if (value == nullptr || !context
            || state->handle(ServerAuthenticationSubmitted{ ServerAuthenticationSubmission(
                   std::move(*value), *scope, *context) }).action != ServerSessionAction::AuthenticationStarted)
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
        auto context = makePhase7ResumeTokenContext(*state->negotiatedHello(), crypto);
        if (!context) return ConnectionSessionResult::ProtocolRejected;
        TransportJoinResponseQueue responses(mQueues, connection);
        AuthenticatedJoinComposition composition(joins, mAuthentication, responses);
        auto outcome = composition.join(*state->principal(), state->generation(), tick, *context);
        if (outcome.result != JoinCompositionResult::Committed || !outcome.committed)
            return ConnectionSessionResult::ProtocolRejected;
        if (state->bindPreissuedInitialSession(outcome.committed->session)
            != PreissuedInitialSessionBindingResult::Bound)
            return ConnectionSessionResult::ProtocolRejected;
        return ConnectionSessionResult::Joined;
    }
}
