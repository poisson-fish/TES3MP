#include "server_application.hpp"
#include "authenticated_join_composition.hpp"
#include "resume_token_context.hpp"
#include "connection_session_coordinator.hpp"
#include "phase7_proof_profile.hpp"
#include "server_config.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class Value>
    Value id(std::uint64_t value) { return Value::fromValue(value).value(); }

    void require(bool condition)
    {
        if (!condition)
            std::abort();
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition))

    constexpr std::string_view validConfig =
        "bind_address = 127.0.0.1\nport = 25565\ntick_interval_ms = 16\n"
        "disconnect_grace_ms = 30000\njoin_password_file = password.txt\n";

    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            calls += 'L';
            if (rejectListen) return { TES3MP::TransportResult::AtCapacity, std::nullopt };
            return { TES3MP::TransportResult::Accepted, TES3MP::ListenerId::initial() };
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override
        { calls += 'S'; return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        { return { TES3MP::TransportResult::InvalidInput, std::nullopt }; }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportResult send(TES3MP::TransportConnectionId, TES3MP::TransportChannel,
            std::span<const std::byte> bytes) override
        { sent.emplace_back(bytes.begin(), bytes.end()); return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage> output) override
        {
            if (receiveResult != TES3MP::TransportResult::Accepted) return { receiveResult, 0 };
            const auto count = std::min(output.size(), incoming.size());
            for (std::size_t index = 0; index < count; ++index) output[index] = std::move(incoming[index]);
            incoming.erase(incoming.begin(), incoming.begin() + static_cast<std::ptrdiff_t>(count));
            return { TES3MP::TransportResult::Accepted, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        { ++closes; return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            calls += 'P';
            const auto count = std::min(output.size(), events.size());
            for (std::size_t index = 0; index < count; ++index) output[index] = events[index];
            events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
            return { pollResult, count };
        }
        TES3MP::TransportResult shutdown() override
        { calls += 'X'; return TES3MP::TransportResult::Accepted; }

        bool rejectListen = false;
        TES3MP::TransportResult pollResult = TES3MP::TransportResult::Accepted;
        TES3MP::TransportResult receiveResult = TES3MP::TransportResult::Accepted;
        std::string calls;
        std::vector<std::vector<std::byte>> sent;
        std::vector<TES3MP::TransportEvent> events;
        std::vector<TES3MP::TransportMessage> incoming;
        std::size_t closes = 0;
    };

    class AcceptedOperation final : public AuthenticationOperation
    {
    public:
        explicit AcceptedOperation(AuthenticationAttempt attempt) : mAttempt(attempt) {}
        AuthenticationPollResult poll() noexcept override
        { return AuthenticationCompletion{ mAttempt, AuthenticatedAdmission::initial(id<PrincipalId>(9)) }; }
        void cancel() noexcept override {}
    private:
        AuthenticationAttempt mAttempt;
    };

    class FakeAuthentication final : public ServerAuthenticationService
    {
    public:
        std::unique_ptr<AuthenticationOperation> begin(
            AuthenticationAttempt attempt, ServerAuthenticationSubmission) noexcept override
        { return std::make_unique<AcceptedOperation>(attempt); }

        ResumeTokenIssueResult issueInitial(
            PrincipalId, SessionId, SessionGeneration, ResumeTokenContext) noexcept override
        {
            ++issues;
            if (reject) return ResumeTokenStoreError::RandomUnavailable;
            std::array<std::byte, ResumeTokenBytes> bytes{};
            auto token = ResumeToken::create(bytes);
            return std::move(*AuthenticationAcceptedMessage::create(
                std::move(*token), MinimumResumeTokenLifetimeMilliseconds));
        }

        bool reject = false;
        std::size_t issues = 0;
    };

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); }
    };

    class RecordingCrypto final : public CredentialCrypto
    {
    public:
        bool randomBytes(std::span<std::byte>) noexcept override { return false; }
        bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept override
        {
            inputs.emplace_back(source.begin(), source.end());
            if (failOnCall == inputs.size()) return false;
            std::byte folded{};
            for (const auto byte : source) folded ^= byte;
            destination.bytes.fill(folded);
            destination.bytes[0] = static_cast<std::byte>(source.size() & 0xff);
            return true;
        }
        bool constantTimeEqual(std::span<const std::byte>, std::span<const std::byte>) noexcept override
        { return false; }

        std::size_t failOnCall = 0;
        std::vector<std::vector<std::byte>> inputs;
    };

    CapabilityOffer emptyOffer()
    {
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
        return std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, {}));
    }

    AdmissionScopeId scope(std::byte value)
    {
        std::array<std::byte, AdmissionScopeIdBytes> bytes{};
        bytes.fill(value);
        return *AdmissionScopeId::create(bytes);
    }

    class FakeJoinQueue final : public TES3MP::ServerApp::JoinResponseQueue
    {
    public:
        bool enqueueJoinResponses(std::span<const std::byte> authentication,
            std::span<const std::byte> snapshot) noexcept override
        {
            ++attempts;
            if (reject) return false;
            auto authenticationFrame = decodeProtocolFrame(authentication);
            auto snapshotFrame = decodeProtocolFrame(snapshot);
            valid = std::holds_alternative<DecodedFrame>(authenticationFrame)
                && std::get<DecodedFrame>(authenticationFrame).messageKind() == MessageKind::AuthenticationAccepted
                && std::holds_alternative<DecodedFrame>(snapshotFrame)
                && std::get<DecodedFrame>(snapshotFrame).messageKind() == MessageKind::LatestWinsSnapshot;
            return valid;
        }

        bool reject = false;
        bool valid = false;
        std::size_t attempts = 0;
    };

    AuthenticatedJoinCoordinator joinCoordinator()
    {
        const auto zero = Turn32::fromValue(0);
        auto spawn = Transform(CellId::interior(id<CellSpaceId>(7)), Position3(10, 20, 30),
            Orientation3(zero, zero, zero));
        return *AuthenticatedJoinCoordinator::create(spawn,
            { id<SessionId>(1), id<PlayerId>(1), id<EntityId>(1) });
    }

    TES3MP::ServerApp::ServerConfig parsedConfig()
    {
        auto result = TES3MP::ServerApp::parseServerConfig(validConfig);
        assert(std::holds_alternative<TES3MP::ServerApp::ServerConfig>(result));
        return std::get<TES3MP::ServerApp::ServerConfig>(std::move(result));
    }
}

int main()
{
    using namespace TES3MP::ServerApp;
    static_assert(Phase7ProtocolMajor == 1 && Phase7ProtocolMinor == 0 && Phase7ProtocolPatch == 0);
    static_assert(Phase7SourceAuthenticationBurst == 4 && Phase7GlobalAuthenticationBurst == 32
        && Phase7AuthenticationRefillMilliseconds == 1'000 && Phase7ConnectionCapacity == 8);
    static_assert(!phase7ProofDisconnectGraceAccepted(MinimumResumeTokenLifetimeMilliseconds - 1));
    static_assert(phase7ProofDisconnectGraceAccepted(MinimumResumeTokenLifetimeMilliseconds));
    static_assert(phase7ProofDisconnectGraceAccepted(MaximumResumeTokenLifetimeMilliseconds));
    static_assert(!phase7ProofDisconnectGraceAccepted(MaximumResumeTokenLifetimeMilliseconds + 1));
    {
        const auto negotiated
            = std::get<ServerHello>(negotiateClientHello(ClientHello::fromOffer(emptyOffer()), emptyOffer()));
        RecordingCrypto first;
        RecordingCrypto second;
        const auto firstContext = makePhase7ResumeTokenContext(negotiated, first);
        const auto secondContext = makePhase7ResumeTokenContext(negotiated, second);
        assert(firstContext && secondContext && *firstContext == *secondContext);
        assert(first.inputs.size() == 2);
        const auto expectedContent
            = std::as_bytes(std::span(Phase7FixtureContentId.data(), Phase7FixtureContentId.size()));
        assert(first.inputs[1] == std::vector<std::byte>(expectedContent.begin(), expectedContent.end()));

        auto newerVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 1, 1));
        auto newerClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(newerVersions, {}, {}));
        auto newerServerOffer = std::get<CapabilityOffer>(CapabilityOffer::create(newerVersions, {}, {}));
        const auto newer = std::get<ServerHello>(negotiateClientHello(
            ClientHello::fromOffer(std::move(newerClientOffer)), newerServerOffer));
        RecordingCrypto changed;
        const auto changedContext = makePhase7ResumeTokenContext(newer, changed);
        assert(changedContext && changedContext->protocol != firstContext->protocol
            && changedContext->content == firstContext->content);

        RecordingCrypto failed;
        failed.failOnCall = 2;
        assert(!makePhase7ResumeTokenContext(negotiated, failed));
    }
    {
        auto result = parseServerConfig(validConfig);
        assert(std::holds_alternative<ServerConfig>(result));
        const auto& config = std::get<ServerConfig>(result);
        assert(config.endpoint.address() == "127.0.0.1" && config.endpoint.port() == 25565);
        assert(config.tickIntervalMilliseconds == 16 && config.disconnectGraceMilliseconds == 30000);
    }
    for (const auto invalid : { std::string{}, std::string("unknown = x\n"),
             std::string(validConfig) + "port = 2\n",
             std::string("bind_address = host\nport = 1\ntick_interval_ms = 1\n"
                         "disconnect_grace_ms = 0\njoin_password_file = p\n"),
             std::string("bind_address = 127.0.0.1 # no inline comment\nport = 1\n"
                         "tick_interval_ms = 1\ndisconnect_grace_ms = 0\njoin_password_file = p\n") })
        assert(std::holds_alternative<ConfigError>(parseServerConfig(invalid)));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string(MaximumConfigLineBytes + 1, 'x'))));
    assert(std::holds_alternative<ConfigError>(parseServerConfig(std::string("\xc0\x80", 2))));

    const auto temporary = std::filesystem::temp_directory_path() / "tes3mp-server-password-test";
    { std::ofstream stream(temporary, std::ios::binary); stream << "secret\r\n"; }
    auto password = loadJoinPassword(temporary);
    assert(std::holds_alternative<TES3MP::AuthenticationMaterial>(password));
    assert(std::get<TES3MP::AuthenticationMaterial>(password).size() == 6);
    std::filesystem::remove(temporary);
    assert(std::holds_alternative<ConfigError>(loadJoinPassword(temporary)));

    auto config = parsedConfig();
    FakeRuntime runtime;
    ServerApplication application(runtime, config);
    assert(application.start() && application.pump() && application.stop());
    assert(runtime.calls == "LPSX");
    assert(application.stop() && runtime.calls == "LPSX");

    FakeRuntime rejected;
    rejected.rejectListen = true;
    ServerApplication rejectedApplication(rejected, config);
    assert(!rejectedApplication.start());
    assert(rejected.calls == "LX");

    FakeRuntime failed;
    ServerApplication failedApplication(failed, config);
    assert(failedApplication.start());
    failed.pollResult = TES3MP::TransportResult::RuntimeFailed;
    assert(!failedApplication.pump());
    assert(failed.calls == "LPSX");

    {
        auto joins = joinCoordinator();
        FakeAuthentication authentication;
        FakeJoinQueue responses;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        assert(composition.join(id<PrincipalId>(1), SessionGeneration::initial(),
                   ServerTick::initial(), ResumeTokenContext{}).result == JoinCompositionResult::Committed);
        assert(composition.join(id<PrincipalId>(2), SessionGeneration::initial(),
                   id<ServerTick>(1), ResumeTokenContext{}).result == JoinCompositionResult::Committed);
        assert(authentication.issues == 2 && responses.attempts == 2 && responses.valid);
        assert(joins.liveBindings() == 2 && joins.state().players().size() == 2);
    }
    {
        auto joins = joinCoordinator();
        FakeAuthentication authentication;
        FakeJoinQueue responses;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        authentication.reject = true;
        assert(composition.join(id<PrincipalId>(3), SessionGeneration::initial(),
                   ServerTick::initial(), ResumeTokenContext{}).result == JoinCompositionResult::TokenRejected);
        assert(joins.liveBindings() == 0 && joins.state().players().empty());
        authentication.reject = false;
        responses.reject = true;
        assert(composition.join(id<PrincipalId>(3), SessionGeneration::initial(),
                   ServerTick::initial(), ResumeTokenContext{}).result == JoinCompositionResult::QueueRejected);
        assert(joins.liveBindings() == 0 && joins.state().players().empty());
        responses.reject = false;
        assert(composition.join(id<PrincipalId>(3), SessionGeneration::initial(),
                   ServerTick::initial(), ResumeTokenContext{}).result == JoinCompositionResult::Committed);
        assert(joins.liveBindings() == 1);
    }
    {
        auto queues = OutboundQueueSet::create(
            *OutboundQueuePolicy::create(1, 64 * 1024, 4, 2, 4, 1, 1, 1, 3, 100), 1);
        const auto connection = TransportConnectionId::initial();
        assert(queues && queues->attach(connection) == TransportResult::Accepted);
        TransportJoinResponseQueue responses(*queues, connection);
        auto joins = joinCoordinator();
        FakeAuthentication authentication;
        AuthenticatedJoinComposition composition(joins, authentication, responses);
        auto joined = composition.join(id<PrincipalId>(4), SessionGeneration::initial(), ServerTick::initial(),
            ResumeTokenContext{});
        assert(joined.result == JoinCompositionResult::Committed && joined.committed
            && joined.committed->session == id<SessionId>(1));
        assert(joins.liveBindings() == 1);

        auto rejectedJoins = joinCoordinator();
        TransportJoinResponseQueue missing(*queues, *connection.next());
        AuthenticatedJoinComposition rejectedComposition(rejectedJoins, authentication, missing);
        assert(rejectedComposition.join(id<PrincipalId>(5), SessionGeneration::initial(), ServerTick::initial(),
                   ResumeTokenContext{}).result == JoinCompositionResult::QueueRejected);
        assert(rejectedJoins.liveBindings() == 0 && rejectedJoins.state().players().empty());
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, emptyOffer(), authentication, *queues, 1);
        const auto first = TransportConnectionId::initial();
        const auto second = *first.next();
        assert(sessions.accept(first, scope(std::byte{ 1 })) == ConnectionSessionResult::Accepted);
        assert(sessions.size() == 1 && queues->connections() == 1);
        assert(sessions.session(first)->state() == ServerSessionState::AwaitingClientHello);
        assert(*sessions.admissionScope(first) == scope(std::byte{ 1 }));
        assert(sessions.accept(first, scope(std::byte{ 2 })) == ConnectionSessionResult::Duplicate);
        assert(sessions.accept(second, scope(std::byte{ 2 })) == ConnectionSessionResult::AtCapacity);
        assert(sessions.close(first) == ConnectionSessionResult::Accepted);
        assert(sessions.size() == 0 && queues->connections() == 0);
        assert(sessions.close(first) == ConnectionSessionResult::UnknownConnection);
        assert(sessions.accept(second, scope(std::byte{ 2 })) == ConnectionSessionResult::Accepted);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, emptyOffer(), authentication, *queues, 2);
        auto joins = joinCoordinator();
        const auto connection = TransportConnectionId::initial();
        assert(sessions.accept(connection, scope(std::byte{ 4 })) == ConnectionSessionResult::Accepted);

        const auto helloPayload = encodeClientHello(ClientHello::fromOffer(emptyOffer()));
        const auto helloFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::ClientHello, helloPayload));
        assert(sessions.dispatch(connection,
                   TransportMessage{ TransportChannel::ReliableOrdered, helloFrame }, joins, crypto,
                   ServerTick::initial()) == ConnectionSessionResult::Accepted);

        auto material = AuthenticationMaterial::create({});
        const auto authenticationPayload = encodeAuthenticationRequest(
            AuthenticationRequest::join(std::move(*material)));
        const auto authenticationFrame = std::get<std::vector<std::byte>>(encodeProtocolFrame(
            MessageClass::SessionControl, MessageKind::AuthenticationRequest, authenticationPayload));
        assert(sessions.dispatch(connection,
                   TransportMessage{ TransportChannel::ReliableOrdered, authenticationFrame }, joins, crypto,
                   ServerTick::initial()) == ConnectionSessionResult::Joined);
        assert(joins.liveBindings() == 1 && sessions.session(connection)->sessionId() == id<SessionId>(1));

        FakeRuntime runtime;
        assert(queues->pump(runtime, connection, 0) == OutboundPumpResult::Progress);
        assert(runtime.sent.size() == 3);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[0])).messageKind() == MessageKind::ServerHello);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[1])).messageKind()
            == MessageKind::AuthenticationAccepted);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(runtime.sent[2])).messageKind()
            == MessageKind::LatestWinsSnapshot);

        assert(sessions.dispatch(connection,
                   TransportMessage{ TransportChannel::LatestWins, { std::byte{ 1 } } }, joins, crypto,
                   ServerTick::initial()) == ConnectionSessionResult::ProtocolRejected);
    }
    {
        FixedClock clock;
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability(metrics, events);
        FakeAuthentication authentication;
        RecordingCrypto crypto;
        auto queues = OutboundQueueSet::create(OutboundQueuePolicy{}, 2);
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        ConnectionSessionCoordinator sessions(
            clock, observability, timeouts, emptyOffer(), authentication, *queues, 2);
        auto joins = joinCoordinator();
        FakeRuntime wiredRuntime;
        const auto connection = TransportConnectionId::initial();
        wiredRuntime.events.push_back({ TransportEventKind::ConnectionAccepted, TransportFailure::None,
            std::nullopt, std::nullopt, connection, std::nullopt,
            TransportSecurity::EncryptedUnauthenticated, scope(std::byte{ 8 }) });
        const auto helloPayload = encodeClientHello(ClientHello::fromOffer(emptyOffer()));
        wiredRuntime.incoming.push_back({ TransportChannel::ReliableOrdered,
            std::get<std::vector<std::byte>>(encodeProtocolFrame(
                MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)) });
        ServerApplication wired(wiredRuntime, config,
            ServerApplicationWiring{ sessions, joins, crypto, *queues, clock });
        assert(wired.start() && wired.pump(ServerTick::initial()));
        assert(sessions.size() == 1 && wiredRuntime.sent.size() == 1);
        assert(std::get<DecodedFrame>(decodeProtocolFrame(wiredRuntime.sent[0])).messageKind()
            == MessageKind::ServerHello);

        wiredRuntime.incoming.push_back(
            { TransportChannel::LatestWins, { std::byte{ 1 } } });
        assert(wired.pump(ServerTick::initial()));
        assert(sessions.size() == 0 && queues->connections() == 0 && wiredRuntime.closes == 1);
    }
}
