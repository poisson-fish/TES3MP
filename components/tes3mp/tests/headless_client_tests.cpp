#include <tes3mp/headless_client_session.hpp>
#include <tes3mp/client_session_runtime.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/scripted_fake_client.hpp>

#include <array>
#include <cstdlib>

namespace
{
    void require(bool value) { if (!value) std::abort(); }
    template <class Value>
    Value value(std::uint64_t raw) { return *Value::fromValue(raw); }
    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override { return {}; }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        { return { TES3MP::TransportResult::Accepted, TES3MP::ConnectAttemptId::initial() }; }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override { return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportResult send(TES3MP::TransportConnectionId, TES3MP::TransportChannel channel,
            std::span<const std::byte> bytes) override
        {
            sentChannel = channel;
            sent.assign(bytes.begin(), bytes.end());
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportReceiveResult receive(TES3MP::TransportConnectionId,
            std::span<TES3MP::TransportMessage> output) override
        {
            const auto count = inbound.size() < output.size() ? inbound.size() : output.size();
            for (std::size_t index = 0; index < count; ++index) output[index] = std::move(inbound[index]);
            inbound.clear();
            return { TES3MP::TransportResult::Accepted, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override { return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            if (fail) return { TES3MP::TransportResult::RuntimeFailed, 0 };
            if (!emit) return { TES3MP::TransportResult::Accepted, 0 };
            emit = false;
            output[0] = { TES3MP::TransportEventKind::ConnectSucceeded, TES3MP::TransportFailure::None,
                std::nullopt, TES3MP::ConnectAttemptId::initial(), TES3MP::TransportConnectionId::initial() };
            return { TES3MP::TransportResult::Accepted, 1 };
        }
        TES3MP::TransportResult shutdown() override { return TES3MP::TransportResult::Accepted; }
        bool emit = true;
        bool fail = false;
        std::vector<TES3MP::TransportMessage> inbound;
        std::vector<std::byte> sent;
        std::optional<TES3MP::TransportChannel> sentChannel;
    };
}

int main()
{
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
    require(session->handle(ClientServerHelloReceived{std::get<ServerHello>(std::move(hello))})
            .action == ClientSessionAction::AuthenticationInputReady);
    require(session->handle(ClientAuthenticationSubmitted{}).accepted());
    require(session->handle(ClientAuthenticationAccepted{}).action == ClientSessionAction::SessionEstablished);
    const auto sessionId = value<SessionId>(1);
    const auto playerId = value<PlayerId>(1);
    const auto entityId = value<EntityId>(1);
    require(session->bindEstablishedSession(sessionId) == ClientSessionBindingResult::Bound);

    const std::array enterChanges{ObservationChange{playerId, entityId,
        ObservationChangeKind::Enter}};
    auto entered = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        SessionGeneration::initial(), CanonicalRevision::initial(), enterChanges));
    require(session->receiveReliableObservationBatch(entered) == ReliableObservationReceiveResult::Applied);
    require(session->observedPlayers().size() == 1);
    require(session->receiveReliableObservationBatch(entered)
        == ReliableObservationReceiveResult::IdenticalDuplicate);
    const auto revisionOne = *CanonicalRevision::initial().next();
    const std::array secondEnter{ObservationChange{value<PlayerId>(2), value<EntityId>(2),
        ObservationChangeKind::Enter}};
    auto wrongSession = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(value<SessionId>(2),
        SessionGeneration::initial(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(wrongSession))
        == ReliableObservationReceiveResult::SessionMismatch);
    auto wrongGeneration = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        *SessionGeneration::initial().next(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(wrongGeneration))
        == ReliableObservationReceiveResult::GenerationMismatch);
    const std::array badLeave{ObservationChange{playerId, value<EntityId>(2),
        ObservationChangeKind::Leave}};
    auto contradictory = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        SessionGeneration::initial(), revisionOne, badLeave));
    require(session->receiveReliableObservationBatch(std::move(contradictory))
        == ReliableObservationReceiveResult::ContradictoryChange);
    require(session->observedPlayers().size() == 1);
    auto second = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        SessionGeneration::initial(), revisionOne, secondEnter));
    require(session->receiveReliableObservationBatch(std::move(second)) == ReliableObservationReceiveResult::Applied);
    require(session->observedPlayers().size() == 2);
    auto stale = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        SessionGeneration::initial(), CanonicalRevision::initial(), enterChanges));
    require(session->receiveReliableObservationBatch(std::move(stale))
        == ReliableObservationReceiveResult::StaleTick);
    const std::array sameTickLeave{ObservationChange{playerId, entityId, ObservationChangeKind::Leave}};
    auto sameTick = std::get<ReliableObservationBatch>(ReliableObservationBatch::create(sessionId,
        SessionGeneration::initial(), revisionOne, sameTickLeave));
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
    auto replayCreated = HeadlessClientSession::create(
        replayRuntime, clock, policy, SessionGeneration::initial());
    auto replaySession = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(replayCreated));
    TestSupport::ScriptedFakeClient replayDriver(*replaySession);
    require(replayDriver.execute(script, ServerTick::initial()));
    require(replayDriver.timeline().size() == driver.timeline().size());
    for (std::size_t index = 0; index < driver.timeline().size(); ++index)
        require(replayDriver.timeline()[index] == driver.timeline()[index]);

    TestSupport::FakeClientScript bounded;
    for (std::size_t i = 0; i < TestSupport::FakeClientScript::MaximumSteps; ++i) require(bounded.addPump());
    require(!bounded.addPump());

    FakeRuntime failedRuntime;
    failedRuntime.fail = true;
    auto failedCreated = HeadlessClientSession::create(failedRuntime, clock, policy, SessionGeneration::initial());
    auto failed = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(failedCreated));
    require(failed->pump().result == HeadlessClientResult::TransportFailed);
    require(failed->stateMachine().state() == ClientSessionState::Closed);

    FakeRuntime runtimeTransport;
    const auto runtimeQueuePolicy = OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    require(runtimeQueuePolicy.has_value());
    auto runtimeCreated = ClientSessionRuntime::create(runtimeTransport, clock, policy,
        SessionGeneration::initial(), *runtimeQueuePolicy);
    require(std::holds_alternative<std::unique_ptr<ClientSessionRuntime>>(runtimeCreated));
    auto clientRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(runtimeCreated));
    require(clientRuntime->flushOutbound() == ClientRuntimeResult::Accepted);
    require(clientRuntime->connect(endpoint) == HeadlessClientResult::Accepted);
    require(clientRuntime->drainInbound().action == ClientSessionAction::SendClientHello);

    auto runtimeVersions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    auto runtimeServerOffer = std::get<CapabilityOffer>(CapabilityOffer::create(runtimeVersions, {}, {}));
    auto runtimeClientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(runtimeVersions), {}, {}));
    auto negotiated = negotiateClientHello(ClientHello::fromOffer(std::move(runtimeClientOffer)), runtimeServerOffer);
    auto serverHelloPayload = encodeServerHello(std::get<ServerHello>(std::move(negotiated)));
    auto serverHelloFrame = encodeProtocolFrame(
        MessageClass::SessionControl, MessageKind::ServerHello, serverHelloPayload);
    runtimeTransport.inbound.push_back({ TransportChannel::ReliableOrdered,
        std::get<std::vector<std::byte>>(std::move(serverHelloFrame)) });
    auto drained = clientRuntime->drainInbound();
    require(drained.result == ClientRuntimeResult::Accepted && drained.messages.size() == 1
        && std::holds_alternative<ServerHello>(drained.messages.front()));

    const std::array helloPayload{ std::byte{ 1 } };
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
}
