#include <tes3mp/headless_client_session.hpp>
#include <tes3mp/test_support/manual_clock.hpp>
#include <tes3mp/test_support/scripted_fake_client.hpp>

#include <array>
#include <cstdlib>

namespace
{
    void require(bool value) { if (!value) std::abort(); }
    class FakeRuntime final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override { return {}; }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::UnknownId; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        { return { TES3MP::TransportResult::Accepted, TES3MP::ConnectAttemptId::initial() }; }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override { return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportResult send(TES3MP::TransportConnectionId, TES3MP::TransportChannel, std::span<const std::byte>) override { return TES3MP::TransportResult::Accepted; }
        TES3MP::TransportReceiveResult receive(TES3MP::TransportConnectionId, std::span<TES3MP::TransportMessage>) override { return {}; }
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
    };
}

int main()
{
    using namespace TES3MP;
    FakeRuntime runtime;
    TestSupport::ManualClock clock(MonotonicInstant::fromNanoseconds(0));
    const auto policy = *SessionTimeoutPolicy::create(100, 100, 100);
    auto created = HeadlessClientSession::create(runtime, clock, policy, SessionGeneration::initial());
    require(std::holds_alternative<std::unique_ptr<HeadlessClientSession>>(created));
    auto session = std::get<std::unique_ptr<HeadlessClientSession>>(std::move(created));
    const auto endpoint = *ConnectionEndpoint::create("127.0.0.1", 25565);
    require(session->connect(endpoint) == HeadlessClientResult::Accepted);
    require(session->connect(endpoint) == HeadlessClientResult::AlreadyStarted);
    require(session->pump().action == ClientSessionAction::SendClientHello);
    require(session->connection().has_value());

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
}
