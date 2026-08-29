#include <tes3mp/test_support/phase4_in_memory_peer.hpp>

#include <tes3mp/authentication.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/observability.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <tes3mp/server_session.hpp>
#include <tes3mp/test_support/in_memory_link.hpp>
#include <tes3mp/test_support/manual_clock.hpp>

#include <array>
#include <utility>

namespace TES3MP::TestSupport
{
    namespace
    {
        constexpr std::uint64_t FixtureTimeoutNanoseconds = 1'000'000;
        constexpr LinkBudget FixtureLinkBudget{ 8, 128 * 1024 };

        class ImmediateOperation final : public AuthenticationOperation
        {
        public:
            ImmediateOperation(AuthenticationAttempt attempt, PrincipalId principalId) noexcept
                : mAttempt(attempt)
                , mPrincipalId(principalId)
            {
            }

            AuthenticationPollResult poll() noexcept override
            {
                return AuthenticationCompletion{ mAttempt, AuthenticatedAdmission::initial(mPrincipalId) };
            }

            void cancel() noexcept override {}

        private:
            AuthenticationAttempt mAttempt;
            PrincipalId mPrincipalId;
        };

        class ImmediateProvider final : public AuthenticationProvider
        {
        public:
            explicit ImmediateProvider(PrincipalId principalId) noexcept
                : mPrincipalId(principalId)
            {
            }

            std::unique_ptr<AuthenticationOperation> begin(
                AuthenticationAttempt attempt, AuthenticationMaterial) noexcept override
            {
                return std::make_unique<ImmediateOperation>(attempt, mPrincipalId);
            }

        private:
            PrincipalId mPrincipalId;
        };

        ProtocolVersionRange versions()
        {
            return std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 1));
        }

        CapabilityOffer emptyOffer()
        {
            return std::get<CapabilityOffer>(CapabilityOffer::create(versions(), {}, {}));
        }

        SessionTimeoutPolicy timeoutPolicy()
        {
            return *SessionTimeoutPolicy::create(
                FixtureTimeoutNanoseconds, FixtureTimeoutNanoseconds, FixtureTimeoutNanoseconds);
        }
    }

    class Phase4InMemoryPeer::Impl
    {
    public:
        Impl(SessionId sessionId, SessionGeneration generation, PrincipalId principalId, InMemoryDuplexLink link)
            : sessionId(sessionId)
            , generation(generation)
            , provider(principalId)
            , link(std::move(link))
        {
        }

        static std::variant<std::unique_ptr<Impl>, Phase4PeerError> create(
            SessionId sessionId, SessionGeneration generation, PrincipalId principalId)
        {
            auto link = InMemoryDuplexLink::create(FixtureLinkBudget, FixtureLinkBudget);
            if (!link)
                return Phase4PeerError::InvalidFixture;

            auto impl = std::unique_ptr<Impl>(new Impl(sessionId, generation, principalId, std::move(*link)));
            auto clientResult = ClientSessionStateMachine::create(impl->clock, timeoutPolicy(), generation);
            if (const auto* failure = std::get_if<SessionTransitionError>(&clientResult))
            {
                (void)failure;
                return Phase4PeerError::SessionFailure;
            }

            impl->client = std::get<std::unique_ptr<ClientSessionStateMachine>>(std::move(clientResult));
            auto serverResult = ServerSessionStateMachine::create(
                impl->clock, impl->observability, timeoutPolicy(), generation, emptyOffer(), impl->provider);
            if (std::holds_alternative<SessionTransitionError>(serverResult))
                return Phase4PeerError::SessionFailure;
            impl->server = std::get<std::unique_ptr<ServerSessionStateMachine>>(std::move(serverResult));
            if (impl->establish() != Phase4PeerError{})
                return Phase4PeerError::SessionFailure;
            return impl;
        }

        Phase4PeerError establish()
        {
            if (client->handle(ClientEncryptedTransportReady{}).action != ClientSessionAction::SendClientHello
                || server->handle(ServerEncryptedTransportReady{}).action != ServerSessionAction::None)
                return Phase4PeerError::SessionFailure;

            const auto clientHelloPayload = encodeClientHello(ClientHello::fromOffer(emptyOffer()));
            const auto clientFrame
                = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ClientHello, clientHelloPayload);
            if (const auto* failure = std::get_if<FrameError>(&clientFrame))
            {
                (void)failure;
                return Phase4PeerError::FrameFailure;
            }
            if (link.send(LinkDirection::AtoB, std::get<std::vector<std::byte>>(clientFrame))
                != LinkSendResult::Accepted)
                return Phase4PeerError::LinkFailure;
            trace.push_back(Phase4PeerTraceStep::ClientHelloSent);

            auto receivedClientFrame = link.receive(LinkDirection::AtoB);
            if (!receivedClientFrame)
                return Phase4PeerError::LinkFailure;
            auto decodedClientFrame = decodeProtocolFrame(*receivedClientFrame);
            const auto* framedClientHello = std::get_if<DecodedFrame>(&decodedClientFrame);
            if (framedClientHello == nullptr || framedClientHello->messageKind() != MessageKind::ClientHello)
                return Phase4PeerError::FrameFailure;
            auto decodedClientHello = decodeClientHello(framedClientHello->payload());
            if (!std::holds_alternative<ClientHello>(decodedClientHello)
                || server->handle(ServerClientHelloReceived{ std::get<ClientHello>(std::move(decodedClientHello)) })
                        .action
                    != ServerSessionAction::SendServerHello)
                return Phase4PeerError::ProtocolFailure;

            const auto serverHelloPayload = encodeServerHello(*server->negotiatedHello());
            const auto serverFrame
                = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ServerHello, serverHelloPayload);
            if (!std::holds_alternative<std::vector<std::byte>>(serverFrame)
                || link.send(LinkDirection::BtoA, std::get<std::vector<std::byte>>(serverFrame))
                    != LinkSendResult::Accepted)
                return Phase4PeerError::LinkFailure;
            auto receivedServerFrame = link.receive(LinkDirection::BtoA);
            if (!receivedServerFrame)
                return Phase4PeerError::LinkFailure;
            auto decodedServerFrame = decodeProtocolFrame(*receivedServerFrame);
            const auto* framedServerHello = std::get_if<DecodedFrame>(&decodedServerFrame);
            if (framedServerHello == nullptr || framedServerHello->messageKind() != MessageKind::ServerHello)
                return Phase4PeerError::FrameFailure;
            auto decodedServerHello = decodeServerHello(framedServerHello->payload());
            if (!std::holds_alternative<ServerHello>(decodedServerHello)
                || client->handle(ClientServerHelloReceived{ std::get<ServerHello>(std::move(decodedServerHello)) })
                        .action
                    != ClientSessionAction::AuthenticationInputReady)
                return Phase4PeerError::ProtocolFailure;
            trace.push_back(Phase4PeerTraceStep::ServerHelloAccepted);

            auto authenticationMaterial = AuthenticationMaterial::create({});
            if (!authenticationMaterial
                || server->handle(ServerAuthenticationSubmitted{ std::move(*authenticationMaterial) }).action
                    != ServerSessionAction::AuthenticationStarted
                || client->handle(ClientAuthenticationSubmitted{}).action
                    != ClientSessionAction::AuthenticationSubmitted
                || server->handle(ServerPollAuthentication{}).action != ServerSessionAction::SessionEstablished
                || client->handle(ClientAuthenticationAccepted{}).action != ClientSessionAction::SessionEstablished)
                return Phase4PeerError::SessionFailure;
            trace.push_back(Phase4PeerTraceStep::AuthenticationSucceeded);

            if (server->bindEstablishedSession(sessionId) != ServerSessionBindingResult::Bound
                || client->bindEstablishedSession(sessionId) != ClientSessionBindingResult::Bound)
                return Phase4PeerError::SessionFailure;
            trace.push_back(Phase4PeerTraceStep::SessionBound);
            return Phase4PeerError{};
        }

        Phase4PeerError exchange(ReliableOperation operation, LatestWinsSnapshot snapshot)
        {
            auto operationPayload = encodeReliableOperation(operation);
            auto operationFrame = encodeProtocolFrame(
                MessageClass::ReliableOperation, MessageKind::ReliableOperation, operationPayload);
            if (!std::holds_alternative<std::vector<std::byte>>(operationFrame)
                || link.send(LinkDirection::AtoB, std::get<std::vector<std::byte>>(operationFrame))
                    != LinkSendResult::Accepted)
                return Phase4PeerError::LinkFailure;
            auto receivedOperationFrame = link.receive(LinkDirection::AtoB);
            if (!receivedOperationFrame)
                return Phase4PeerError::LinkFailure;
            auto decodedOperationFrame = decodeProtocolFrame(*receivedOperationFrame);
            const auto* framedOperation = std::get_if<DecodedFrame>(&decodedOperationFrame);
            if (framedOperation == nullptr || framedOperation->messageKind() != MessageKind::ReliableOperation)
                return Phase4PeerError::FrameFailure;
            auto decodedOperation = decodeReliableOperation(framedOperation->payload());
            const auto* ownedOperation = std::get_if<ReliableOperation>(&decodedOperation);
            if (ownedOperation == nullptr
                || server->receiveReliableOperation(*ownedOperation) != ReliableOperationReceiveResult::Delivered)
                return Phase4PeerError::ProtocolFailure;
            deliveredOperation = std::move(*ownedOperation);
            trace.push_back(Phase4PeerTraceStep::ReliableOperationDelivered);

            auto snapshotPayload = encodeLatestWinsSnapshot(snapshot);
            auto snapshotFrame = encodeProtocolFrame(
                MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot, snapshotPayload);
            if (!std::holds_alternative<std::vector<std::byte>>(snapshotFrame)
                || link.send(LinkDirection::BtoA, std::get<std::vector<std::byte>>(snapshotFrame))
                    != LinkSendResult::Accepted)
                return Phase4PeerError::LinkFailure;
            auto receivedSnapshotFrame = link.receive(LinkDirection::BtoA);
            if (!receivedSnapshotFrame)
                return Phase4PeerError::LinkFailure;
            auto decodedSnapshotFrame = decodeProtocolFrame(*receivedSnapshotFrame);
            const auto* framedSnapshot = std::get_if<DecodedFrame>(&decodedSnapshotFrame);
            if (framedSnapshot == nullptr || framedSnapshot->messageKind() != MessageKind::LatestWinsSnapshot)
                return Phase4PeerError::FrameFailure;
            auto decodedSnapshot = decodeLatestWinsSnapshot(framedSnapshot->payload());
            auto* ownedSnapshot = std::get_if<LatestWinsSnapshot>(&decodedSnapshot);
            if (ownedSnapshot == nullptr
                || client->receiveLatestWinsSnapshot(std::move(*ownedSnapshot))
                    != LatestWinsSnapshotReceiveResult::Applied)
                return Phase4PeerError::ProtocolFailure;
            trace.push_back(Phase4PeerTraceStep::LatestWinsSnapshotApplied);
            return Phase4PeerError{};
        }

        ManualClock clock{ MonotonicInstant::fromNanoseconds(0) };
        SessionId sessionId;
        SessionGeneration generation;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        ImmediateProvider provider;
        InMemoryDuplexLink link;
        std::unique_ptr<ClientSessionStateMachine> client;
        std::unique_ptr<ServerSessionStateMachine> server;
        std::vector<Phase4PeerTraceStep> trace;
        std::optional<ReliableOperation> deliveredOperation;
    };

    Phase4InMemoryPeer::Phase4InMemoryPeer(std::unique_ptr<Impl> impl) noexcept
        : mImpl(std::move(impl))
    {
    }

    Phase4InMemoryPeer::~Phase4InMemoryPeer() = default;

    std::variant<std::unique_ptr<Phase4InMemoryPeer>, Phase4PeerError> Phase4InMemoryPeer::create(
        SessionId sessionId, SessionGeneration generation, PrincipalId principalId)
    {
        auto impl = Impl::create(sessionId, generation, principalId);
        if (const auto* failure = std::get_if<Phase4PeerError>(&impl))
            return *failure;
        return std::unique_ptr<Phase4InMemoryPeer>(
            new Phase4InMemoryPeer(std::get<std::unique_ptr<Impl>>(std::move(impl))));
    }

    Phase4PeerError Phase4InMemoryPeer::exchange(ReliableOperation operation, LatestWinsSnapshot snapshot)
    {
        return mImpl->exchange(std::move(operation), std::move(snapshot));
    }

    std::span<const Phase4PeerTraceStep> Phase4InMemoryPeer::trace() const noexcept
    {
        return mImpl->trace;
    }

    const std::optional<ReliableOperation>& Phase4InMemoryPeer::deliveredOperation() const noexcept
    {
        return mImpl->deliveredOperation;
    }

    const std::optional<LatestWinsSnapshot>& Phase4InMemoryPeer::confirmedSnapshot() const noexcept
    {
        return mImpl->client->confirmedSnapshot();
    }
}
