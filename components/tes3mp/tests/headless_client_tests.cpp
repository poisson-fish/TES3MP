#include <tes3mp/client_session_runtime.hpp>
#include <tes3mp/headless_client_session.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/scripted_fake_client.hpp>

#include <array>
#include <cstdlib>
#include <string_view>
#include <cstdio>
#include <source_location>

namespace
{
    void require(bool value, std::source_location where = std::source_location::current())
    {
        if (!value)
        {
            std::fprintf(stderr, "headless client check failed at line %u\n", where.line());
            std::abort();
        }
    }
    template <class Value>
    Value value(std::uint64_t raw)
    {
        return *Value::fromValue(raw);
    }
    template <class T, class... Alternatives>
    T checked(std::variant<Alternatives...> input, std::source_location where = std::source_location::current())
    {
        require(std::holds_alternative<T>(input), where);
        return std::get<T>(std::move(input));
    }
    TES3MP::ResumeToken resumeToken(std::byte byte)
    {
        std::array<std::byte, TES3MP::ResumeTokenBytes> bytes{};
        bytes.fill(byte);
        return std::move(*TES3MP::ResumeToken::create(bytes));
    }
    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            return {};
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        {
            if (!acceptConnect)
                return { TES3MP::TransportResult::NotReady, std::nullopt };
            return { TES3MP::TransportResult::Accepted, TES3MP::ConnectAttemptId::initial() };
        }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        {
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportResult send(
            TES3MP::TransportConnectionId, TES3MP::TransportChannel channel, std::span<const std::byte> bytes) override
        {
            sentChannel = channel;
            sent.assign(bytes.begin(), bytes.end());
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportReceiveResult receive(
            TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage> output) override
        {
            const auto count = inbound.size() < output.size() ? inbound.size() : output.size();
            for (std::size_t index = 0; index < count; ++index)
                output[index] = std::move(inbound[index]);
            inbound.clear();
            return { TES3MP::TransportResult::Accepted, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        {
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            if (fail)
                return { TES3MP::TransportResult::RuntimeFailed, 0 };
            if (!emit)
                return { TES3MP::TransportResult::Accepted, 0 };
            emit = false;
            output[0] = { TES3MP::TransportEventKind::ConnectSucceeded, TES3MP::TransportFailure::None, std::nullopt,
                TES3MP::ConnectAttemptId::initial(), TES3MP::TransportConnectionId::initial() };
            return { TES3MP::TransportResult::Accepted, 1 };
        }
        TES3MP::TransportResult shutdown() override { return TES3MP::TransportResult::Accepted; }
        bool emit = true;
        bool fail = false;
        bool acceptConnect = true;
        std::vector<TES3MP::TransportMessage> inbound;
        std::vector<std::byte> sent;
        std::optional<TES3MP::TransportChannel> sentChannel;
    };

    void earlyNativeSnapshot()
    {
        using namespace TES3MP;
        FakeRuntime transport;
        TestSupport::ManualClock clock(MonotonicInstant::fromNanoseconds(0));
        const auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        const auto queues = *OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
        auto client = checked<std::unique_ptr<ClientSessionRuntime>>(
            ClientSessionRuntime::create(transport, clock, timeouts, SessionGeneration::initial(), queues));
        const auto versions = checked<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
        const std::array capabilities{ inventoryReplicationCapability(), nativeActorMotionCapability() };
        auto offer = checked<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
        auto hello = ClientHello::fromOffer(offer);
        auto serverHello = checked<ServerHello>(negotiateClientHello(hello, offer));
        require(client->start(*ConnectionEndpoint::create("127.0.0.1", 25565), std::move(hello),
            AuthenticationRequest::join(*AuthenticationMaterial::create({}))) == HeadlessClientResult::Accepted);
        require(client->advance().action == ClientSessionAction::SendClientHello);
        const auto receive = [&](MessageClass cls, MessageKind kind, std::vector<std::byte> payload, TransportChannel channel) {
            transport.inbound.push_back({channel, checked<std::vector<std::byte>>(encodeProtocolFrame(cls, kind, payload))});
        };
        receive(MessageClass::SessionControl, MessageKind::ServerHello, encodeServerHello(serverHello), TransportChannel::ReliableOrdered);
        require(client->advance().result == ClientRuntimeResult::Accepted);
        require(client->session().stateMachine().state() == ClientSessionState::AwaitingAuthenticationResult);
        const PublicActorEquipmentMember actor{ value<ContainerId>(0x8000000000000001ull), {} };
        const NativeActorMotion motion{ actor.actor.value(), 1, {1, 2, 3}, {}, 0 };
        const auto equipment = checked<LatestWinsEquipmentSnapshot>(LatestWinsEquipmentSnapshot::create(
            value<SessionId>(1), SessionGeneration::initial(), value<ServerTick>(1), CanonicalRevision::initial(),
            {}, std::span(&actor, 1), std::span(&motion, 1)));
        receive(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsEquipmentSnapshot,
            encodeLatestWinsEquipmentSnapshot(equipment), TransportChannel::LatestWins);
        const auto early = client->advance();
        require(early.result == ClientRuntimeResult::Accepted && !early.equipmentSnapshotApplied
            && client->session().stateMachine().state() == ClientSessionState::AwaitingAuthenticationResult
            && !client->session().stateMachine().sessionId());
        const auto accepted = AuthenticationAcceptedMessage::create(resumeToken(std::byte{8}), 5000).value();
        receive(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
            encodeAuthenticationAccepted(accepted), TransportChannel::ReliableOrdered);
        require(client->advance().result == ClientRuntimeResult::Accepted);
        const auto established = client->advance();
        require(established.result == ClientRuntimeResult::Accepted && established.equipmentSnapshotApplied
            && client->session().stateMachine().confirmedEquipmentSnapshot() == equipment);
        std::puts("PASS early-native-snapshot: no pre-authentication mutation, accepted after authentication");
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "early-native-snapshot")
    {
        try { earlyNativeSnapshot(); return 0; }
        catch (const std::exception& error) { std::fprintf(stderr, "early native snapshot: %s\n", error.what()); return 1; }
    }
    require(argc == 1);
    using namespace TES3MP;
    FakeRuntime runtime;
    TestSupport::ManualClock clock(MonotonicInstant::fromNanoseconds(0));
    const auto policy = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
    auto created = HeadlessClientSession::create(runtime, clock, policy, SessionGeneration::initial());
    require(std::holds_alternative<std::unique_ptr<HeadlessClientSession>>(created));
    auto session = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(created));
    const auto endpoint = *ConnectionEndpoint::create("127.0.0.1", 25565);
    require(session->connect(endpoint) == HeadlessClientResult::Accepted);
    require(session->connect(endpoint) == HeadlessClientResult::AlreadyStarted);
    require(session->pump().action == ClientSessionAction::SendClientHello);
    require(session->connection().has_value());

    auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    auto clientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, {}, {}));
    auto serverOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(versions), {}, {}));
    auto hello = negotiateClientHello(ClientHello::fromOffer(std::move(clientOffer)), serverOffer);
    require(session->handle(ClientServerHelloReceived{ std::get<ServerHello>(std::move(hello)) }).action
        == ClientSessionAction::AuthenticationInputReady);
    require(session->handle(ClientAuthenticationSubmitted{}).accepted());
    require(session->handle(ClientAuthenticationAccepted{}).action == ClientSessionAction::SessionEstablished);
    const auto sessionId = value<SessionId>(1);
    const auto playerId = value<PlayerId>(1);
    const auto entityId = value<EntityId>(1);
    require(session->bindEstablishedSession(sessionId) == ClientSessionBindingResult::Bound);

    const std::array enterChanges{ ObservationChange{ playerId, entityId, ObservationChangeKind::Enter } };
    auto entered = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(
        sessionId, SessionGeneration::initial(), CanonicalRevision::initial(), enterChanges));
    require(session->receiveReliableObservationBatch(entered) == ReliableObservationReceiveResult::BaselineMissing);
    const std::array<InterestMember, 0> emptyMembers{};
    auto baseline
        = std::get<ReliableInterestBaseline>(ReliableInterestBaseline::create(sessionId, SessionGeneration::initial(),
            CanonicalRevision::initial(), CanonicalStateVersion::initial(), ServerTick::initial(), emptyMembers));
    require(session->receiveReliableInterestBaseline(baseline) == ReliableInterestBaselineReceiveResult::Applied);
    require(session->receiveReliableInterestBaseline(baseline)
        == ReliableInterestBaselineReceiveResult::IdenticalDuplicate);
    require(session->receiveReliableObservationBatch(entered) == ReliableObservationReceiveResult::Applied);
    require(session->observedPlayers().size() == 1);
    require(session->receiveReliableObservationBatch(entered) == ReliableObservationReceiveResult::IdenticalDuplicate);
    const auto revisionOne = *CanonicalRevision::initial().next();
    const std::array secondEnter{ ObservationChange{
        value<PlayerId>(2), value<EntityId>(2), ObservationChangeKind::Enter } };
    auto wrongSession = std::get<ReliableObservationBatch>(
        ReliableObservationBatch::create(value<SessionId>(2), SessionGeneration::initial(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(wrongSession))
        == ReliableObservationReceiveResult::SessionMismatch);
    auto wrongGeneration = std::get<ReliableObservationBatch>(
        ReliableObservationBatch::create(sessionId, *SessionGeneration::initial().next(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(wrongGeneration))
        == ReliableObservationReceiveResult::GenerationMismatch);
    const std::array badLeave{ ObservationChange{ playerId, value<EntityId>(2), ObservationChangeKind::Leave } };
    auto contradictory = std::get<ReliableObservationBatch>(
        ReliableObservationBatch::create(sessionId, SessionGeneration::initial(), revisionOne, badLeave));
    require(session->receiveReliableObservationBatch(std::move(contradictory))
        == ReliableObservationReceiveResult::ContradictoryChange);
    require(session->observedPlayers().size() == 1);
    auto second = std::get<ReliableObservationBatch>(
        ReliableObservationBatch::create(sessionId, SessionGeneration::initial(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(second)) == ReliableObservationReceiveResult::Applied);
    require(session->observedPlayers().size() == 2);
    auto stale = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(
        sessionId, SessionGeneration::initial(), CanonicalRevision::initial(), enterChanges));
    require(session->receiveReliableObservationBatch(std::move(stale)) == ReliableObservationReceiveResult::StaleTick);
    const std::array sameTickLeave{ ObservationChange{ playerId, entityId, ObservationChangeKind::Leave } };
    auto sameTick = std::get<ReliableObservationBatch>(
        ReliableObservationBatch::create(sessionId, SessionGeneration::initial(), revisionOne, sameTickLeave));
    require(session->receiveReliableObservationBatch(std::move(sameTick))
        == ReliableObservationReceiveResult::ContradictorySameTick);
    require(session->observedPlayers().size() == 2);

    TestSupport::FakeClientScript script;
    require(script.addPump() && script.addClose());
    TestSupport::ScriptedFakeClient driver(*session);
    require(driver.execute(script, ServerTick::initial()));
    const auto first = driver.timelineNdjson();
    require(first && first->find("secret") == std::string::npos);
    require(driver.timeline().size() == 2);

    FakeRuntime replayRuntime;
    replayRuntime.emit = false;
    auto replayCreated = HeadlessClientSession::create(replayRuntime, clock, policy, SessionGeneration::initial());
    auto replaySession = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(replayCreated));
    TestSupport::ScriptedFakeClient replayDriver(*replaySession);
    require(replayDriver.execute(script, ServerTick::initial()));
    require(replayDriver.timeline().size() == driver.timeline().size());
    for (std::size_t index = 0; index < driver.timeline().size(); ++index)
        require(replayDriver.timeline()[index] == driver.timeline()[index]);

    TestSupport::FakeClientScript bounded;
    for (std::size_t i = 0; i < TestSupport::FakeClientScript::MaximumSteps; ++i)
        require(bounded.addPump());
    require(!bounded.addPump());

    FakeRuntime failedRuntime;
    failedRuntime.fail = true;
    auto failedCreated = HeadlessClientSession::create(failedRuntime, clock, policy, SessionGeneration::initial());
    auto failed = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(failedCreated));
    require(failed->pump().result == HeadlessClientResult::TransportFailed);
    require(failed->stateMachine().state() == ClientSessionState::Closed);

    FakeRuntime timeoutRuntime;
    timeoutRuntime.emit = false;
    TestSupport::ManualClock timeoutClock(MonotonicInstant::fromNanoseconds(0));
    auto timeoutCreated
        = HeadlessClientSession::create(timeoutRuntime, timeoutClock, policy, SessionGeneration::initial());
    auto timeoutSession = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(timeoutCreated));
    require(timeoutSession->connect(endpoint) == HeadlessClientResult::Accepted);
    require(timeoutClock.advance(1'000'000));
    const auto timedOut = timeoutSession->pump();
    require(timedOut.result == HeadlessClientResult::TransportFailed
        && timedOut.action == ClientSessionAction::SessionTimedOut && !timeoutSession->attempt()
        && !timeoutSession->connection() && timeoutSession->stateMachine().state() == ClientSessionState::TimedOut);

    FakeRuntime runtimeTimeoutTransport;
    runtimeTimeoutTransport.emit = false;
    TestSupport::ManualClock runtimeTimeoutClock(MonotonicInstant::fromNanoseconds(0));
    const auto timeoutQueuePolicy = OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    require(timeoutQueuePolicy.has_value());
    auto runtimeTimeoutCreated = ClientSessionRuntime::create(
        runtimeTimeoutTransport, runtimeTimeoutClock, policy, SessionGeneration::initial(), *timeoutQueuePolicy);
    auto runtimeTimeout = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(runtimeTimeoutCreated));
    auto timeoutVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    auto timeoutOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(timeoutVersions), {}, {}));
    auto timeoutMaterial = AuthenticationMaterial::create({});
    require(runtimeTimeout->start(endpoint, ClientHello::fromOffer(std::move(timeoutOffer)),
                AuthenticationRequest::join(std::move(*timeoutMaterial)))
        == HeadlessClientResult::Accepted);
    require(runtimeTimeoutClock.advance(1'000'000));
    const auto runtimeTimedOut = runtimeTimeout->advance();
    require(runtimeTimedOut.result == ClientRuntimeResult::TransportFailed
        && runtimeTimedOut.action == ClientSessionAction::SessionTimedOut);

    FakeRuntime runtimeTransport;
    const auto runtimeQueuePolicy = OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    require(runtimeQueuePolicy.has_value());
    auto runtimeCreated = ClientSessionRuntime::create(
        runtimeTransport, clock, policy, SessionGeneration::initial(), *runtimeQueuePolicy);
    require(std::holds_alternative<std::unique_ptr<ClientSessionRuntime>>(runtimeCreated));
    auto clientRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(runtimeCreated));
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::Accepted);
    auto runtimeVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    auto runtimeServerOffer = std::get<CapabilityOffer>(CapabilityOffer::create(runtimeVersions, {}, {}));
    auto runtimeClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(runtimeVersions), {}, {}));
    const std::array credentialBytes{ std::byte{ 7 } };
    auto credential = AuthenticationMaterial::create(credentialBytes);
    require(credential.has_value());
    require(clientRuntime->start(endpoint, ClientHello::fromOffer(std::move(runtimeClientOffer)),
                AuthenticationRequest::join(std::move(*credential)))
        == HeadlessClientResult::Accepted);
    require(clientRuntime->advance().action == ClientSessionAction::SendClientHello);
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::Accepted);
    auto sentHello = decodeProtocolFrame(runtimeTransport.sent);
    require(std::holds_alternative<DecodedFrame>(sentHello)
        && std::get<DecodedFrame>(sentHello).messageKind() == MessageKind::ClientHello);

    runtimeClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(runtimeVersions, {}, {}));
    auto negotiated = negotiateClientHello(ClientHello::fromOffer(std::move(runtimeClientOffer)), runtimeServerOffer);
    auto serverHelloPayload = encodeServerHello(std::get<ServerHello>(std::move(negotiated)));
    auto serverHelloFrame
        = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ServerHello, serverHelloPayload);
    runtimeTransport.inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(serverHelloFrame)) });
    auto advanced = clientRuntime->advance();
    require(advanced.result == ClientRuntimeResult::Accepted
        && clientRuntime->session().stateMachine().state() == ClientSessionState::AwaitingAuthenticationResult);
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::Accepted);
    auto sentAuthentication = decodeProtocolFrame(runtimeTransport.sent);
    require(std::holds_alternative<DecodedFrame>(sentAuthentication)
        && std::get<DecodedFrame>(sentAuthentication).messageKind() == MessageKind::AuthenticationRequest);

    const std::array helloPayload{ std::byte{ 1 } };
    runtimeTransport.sent.clear();
    require(clientRuntime->queue(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)
        == ClientRuntimeResult::Accepted);
    require(runtimeTransport.sent.empty());
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::Accepted);
    require(runtimeTransport.sentChannel == TransportChannel::ReliableOrdered && !runtimeTransport.sent.empty());

    runtimeTransport.inbound.push_back({ TransportChannel::LatestWins, runtimeTransport.sent });
    require(clientRuntime->drainInbound().result == ClientRuntimeResult::ProtocolRejected);
    require(clientRuntime->session().stateMachine().state() == ClientSessionState::Closed);
    require(clientRuntime->queue(MessageClass::SessionControl, MessageKind::ClientHello, helloPayload)
        == ClientRuntimeResult::Accepted);
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::NotConnected);

    FakeRuntime orderedRuntime;
    auto orderedCreated = ClientSessionRuntime::create(
        orderedRuntime, clock, policy, SessionGeneration::initial(), *runtimeQueuePolicy);
    auto ordered = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(orderedCreated));
    auto orderedVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
    const std::array characterCapabilities{ characterCreationCapability() };
    auto orderedClientOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(orderedVersions, characterCapabilities, {}));
    auto orderedServerOffer
        = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(orderedVersions), characterCapabilities, {}));
    auto orderedMaterial = AuthenticationMaterial::create({});
    require(ordered->start(endpoint, ClientHello::fromOffer(std::move(orderedClientOffer)),
                AuthenticationRequest::join(std::move(*orderedMaterial)))
        == HeadlessClientResult::Accepted);
    require(ordered->advance().action == ClientSessionAction::SendClientHello);
    require(ordered->flushOutbound() == ClientRuntimeResult::Accepted);
    auto orderedNegotiated = negotiateClientHello(
        std::get<ClientHello>(
            decodeClientHello(std::get<DecodedFrame>(decodeProtocolFrame(orderedRuntime.sent)).payload())),
        orderedServerOffer);
    auto orderedHelloFrame = encodeProtocolFrame(MessageClass::SessionControl, MessageKind::ServerHello,
        encodeServerHello(std::get<ServerHello>(std::move(orderedNegotiated))));
    orderedRuntime.inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(orderedHelloFrame)) });
    require(ordered->advance().result == ClientRuntimeResult::Accepted);
    require(ordered->flushOutbound() == ClientRuntimeResult::Accepted);

    auto accepted
        = AuthenticationAcceptedMessage::create(resumeToken(std::byte{ 1 }), MinimumResumeTokenLifetimeMilliseconds,
            std::nullopt, CharacterLifecycle::NewCharacter, CharacterProfileRevision::initial());
    auto acceptedFrame = encodeProtocolFrame(
        MessageClass::SessionControl, MessageKind::AuthenticationAccepted, encodeAuthenticationAccepted(*accepted));
    ReliableCharacterProfile profile{ value<SessionId>(1), SessionGeneration::initial(), value<PlayerId>(1),
        CharacterConfirmationResult::Confirmed, CharacterProfile::fresh() };
    auto profileFrame = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableCharacterProfile,
        encodeReliableCharacterProfile(profile));
    const auto zeroTurn = Turn32::fromValue(0);
    const std::array entries{ SpatialEntitySnapshot(ServerTick::initial(), value<PlayerId>(1), value<EntityId>(1),
        value<AppearanceId>(1), EntityRevision::initial(), AuthorityEpoch::initial(),
        Transform(CellId::interior(value<CellSpaceId>(1)), Position3(61, -135, 24),
            Orientation3(zeroTurn, zeroTurn, zeroTurn)),
        LinearVelocity3(0, 0, 0)) };
    auto world = std::get<SpatialWorldView>(SpatialWorldView::create(entries));
    LatestWinsSnapshot snapshot(LatestWinsSnapshotHeader(value<SessionId>(1), SessionGeneration::initial(),
                                    value<PlayerId>(1), value<EntityId>(1), CanonicalRevision::initial(), std::nullopt),
        std::move(world));
    auto snapshotFrame = encodeProtocolFrame(
        MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot, encodeLatestWinsSnapshot(snapshot));
    orderedRuntime.inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(acceptedFrame)) });
    orderedRuntime.inbound.push_back(
        { TransportChannel::ReliableOrdered, std::get<std::vector<std::byte>>(std::move(profileFrame)) });
    orderedRuntime.inbound.push_back(
        { TransportChannel::LatestWins, std::get<std::vector<std::byte>>(std::move(snapshotFrame)) });
    const auto orderedAdvance = ordered->advance();
    require(orderedAdvance.result == ClientRuntimeResult::Accepted && orderedAdvance.characterProfileApplied
        && ordered->confirmedCharacterProfile()
        && ordered->confirmedCharacterProfile()->playerId == value<PlayerId>(1));

    FakeRuntime retryRuntime;
    retryRuntime.acceptConnect = false;
    auto retryCreated
        = ClientSessionRuntime::create(retryRuntime, clock, policy, SessionGeneration::initial(), *runtimeQueuePolicy);
    auto retry = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(retryCreated));
    std::array<std::byte, ResumeTokenBytes> retryTokenBytes{};
    retryTokenBytes.fill(std::byte{ 9 });
    auto retryToken = ResumeToken::create(retryTokenBytes);
    runtimeVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    runtimeClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(runtimeVersions), {}, {}));
    require(retry->start(endpoint, ClientHello::fromOffer(std::move(runtimeClientOffer)),
                AuthenticationRequest::resume(std::move(*retryToken)))
        != HeadlessClientResult::Accepted);
    require(retry->takeUnsubmittedResumeToken().has_value());
    require(!retry->takeUnsubmittedResumeToken());
}
