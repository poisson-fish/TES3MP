#include "adapter.hpp"
#include "client_connection.hpp"
#include "movement_mapping.hpp"
#include "providers.hpp"
#include "remote_motion.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

namespace
{
    void require(bool value)
    {
        if (!value)
            std::abort();
    }

    template <class T>
    T value(std::uint64_t raw)
    {
        return T::fromValue(raw).value();
    }

    TES3MP::SpatialEntitySnapshot remoteSample(std::uint64_t tick, std::uint64_t revision, std::int64_t x,
        std::int64_t velocity = 4096, std::uint64_t epoch = 1)
    {
        const auto zero = TES3MP::Turn32::fromValue(0);
        return TES3MP::SpatialEntitySnapshot(value<TES3MP::ServerTick>(tick), value<TES3MP::PlayerId>(1),
            value<TES3MP::EntityId>(2), value<TES3MP::EntityRevision>(revision), value<TES3MP::AuthorityEpoch>(epoch),
            TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), TES3MP::Position3(x, 0, 0),
                TES3MP::Orientation3(zero, zero, zero)),
            TES3MP::LinearVelocity3(velocity, 0, 0));
    }

    TES3MP::ServerHello serverHello()
    {
        auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 0));
        auto client = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(versions, {}, {}));
        auto server = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(std::move(versions), {}, {}));
        auto negotiated = TES3MP::negotiateClientHello(TES3MP::ClientHello::fromOffer(std::move(client)), server);
        return std::get<TES3MP::ServerHello>(std::move(negotiated));
    }

    std::vector<std::byte> frame(
        TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind, std::span<const std::byte> payload)
    {
        return std::get<std::vector<std::byte>>(TES3MP::encodeProtocolFrame(messageClass, messageKind, payload));
    }

    TES3MP::AuthenticationAcceptedMessage accepted(
        std::byte marker, std::uint64_t lifetime = TES3MP::MinimumResumeTokenLifetimeMilliseconds)
    {
        std::array<std::byte, TES3MP::ResumeTokenBytes> bytes{};
        bytes.fill(marker);
        auto token = TES3MP::ResumeToken::create(bytes);
        return std::move(*TES3MP::AuthenticationAcceptedMessage::create(std::move(*token), lifetime));
    }

    TES3MP::LatestWinsSnapshot selfSnapshot(TES3MP::SessionGeneration generation)
    {
        const auto session = value<TES3MP::SessionId>(1);
        const auto player = value<TES3MP::PlayerId>(1);
        const auto entity = value<TES3MP::EntityId>(1);
        const auto zero = TES3MP::Turn32::fromValue(0);
        const std::array entries{ TES3MP::SpatialEntitySnapshot(TES3MP::ServerTick::initial(), player, entity,
            TES3MP::EntityRevision::initial(), TES3MP::AuthorityEpoch::initial(),
            TES3MP::Transform(TES3MP::CellId::interior(value<TES3MP::CellSpaceId>(7)), TES3MP::Position3(0, 0, 0),
                TES3MP::Orientation3(zero, zero, zero)),
            TES3MP::LinearVelocity3(0, 0, 0)) };
        auto view = std::get<TES3MP::SpatialWorldView>(TES3MP::SpatialWorldView::create(entries));
        return TES3MP::LatestWinsSnapshot(TES3MP::LatestWinsSnapshotHeader(session, generation, player, entity,
                                              TES3MP::CanonicalRevision::initial(), std::nullopt),
            std::move(view));
    }

    class MotionMetrics final : public TES3MP::OpenMWAdapter::RemoteMotionMetricSink
    {
    public:
        TES3MP::ObservationResult tryRecord(TES3MP::OpenMWAdapter::RemoteMotionMetric metric) noexcept override
        {
            if (size == values.size())
            {
                ++dropped;
                return TES3MP::ObservationResult::Dropped;
            }
            values[size++] = metric;
            return TES3MP::ObservationResult::Accepted;
        }

        bool has(TES3MP::OpenMWAdapter::RemoteMotionMetricKey key) const
        {
            for (std::size_t index = 0; index < size; ++index)
                if (values[index]->key == key)
                    return true;
            return false;
        }

        std::size_t count(TES3MP::OpenMWAdapter::RemoteMotionMetricKey key) const
        {
            std::size_t result = 0;
            for (std::size_t index = 0; index < size; ++index)
                if (values[index]->key == key)
                    ++result;
            return result;
        }

        std::array<std::optional<TES3MP::OpenMWAdapter::RemoteMotionMetric>, 128> values{};
        std::size_t size = 0;
        std::size_t dropped = 0;
    };

    class DroppingMotionMetrics final : public TES3MP::OpenMWAdapter::RemoteMotionMetricSink
    {
    public:
        TES3MP::ObservationResult tryRecord(TES3MP::OpenMWAdapter::RemoteMotionMetric) noexcept override
        {
            return TES3MP::ObservationResult::Dropped;
        }
    };

    class Clock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            return TES3MP::MonotonicInstant::fromNanoseconds(nanoseconds);
        }

        std::uint64_t nanoseconds = 0;
    };

    class IdleTransport final : public TES3MP::TransportRuntime
    {
    public:
        TES3MP::TransportAdmission<TES3MP::ListenerId> startListener(const TES3MP::ListenerEndpoint&) override
        {
            return { TES3MP::TransportResult::NotReady, std::nullopt };
        }
        TES3MP::TransportResult stopListener(TES3MP::ListenerId) override { return TES3MP::TransportResult::NotReady; }
        TES3MP::TransportAdmission<TES3MP::ConnectAttemptId> connect(const TES3MP::ConnectionEndpoint&) override
        {
            if (acceptConnections)
            {
                pendingAttempt = value<TES3MP::ConnectAttemptId>(nextConnection);
                return { TES3MP::TransportResult::Accepted, pendingAttempt };
            }
            return { TES3MP::TransportResult::NotReady, std::nullopt };
        }
        TES3MP::TransportResult cancelConnect(TES3MP::ConnectAttemptId) override
        {
            return TES3MP::TransportResult::NotReady;
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
            const auto count = std::min(output.size(), inbound.size());
            for (std::size_t index = 0; index < count; ++index)
                output[index] = std::move(inbound[index]);
            inbound.erase(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(count));
            return { acceptConnections ? TES3MP::TransportResult::Accepted : TES3MP::TransportResult::NotReady, count };
        }
        TES3MP::TransportResult close(TES3MP::TransportConnectionId, TES3MP::TransportCloseMode) override
        {
            return TES3MP::TransportResult::Accepted;
        }
        TES3MP::TransportPollResult poll(std::span<TES3MP::TransportEvent> output) override
        {
            if (failPoll)
                return { TES3MP::TransportResult::RuntimeFailed, 0 };
            if (pendingAttempt && !output.empty())
            {
                const auto connection = value<TES3MP::TransportConnectionId>(nextConnection++);
                output[0] = { TES3MP::TransportEventKind::ConnectSucceeded, TES3MP::TransportFailure::None,
                    std::nullopt, pendingAttempt, connection };
                pendingAttempt.reset();
                return { TES3MP::TransportResult::Accepted, 1 };
            }
            return { TES3MP::TransportResult::Accepted, 0 };
        }
        TES3MP::TransportResult shutdown() override { return TES3MP::TransportResult::Accepted; }

        bool failPoll = false;
        bool acceptConnections = false;
        std::uint64_t nextConnection = 1;
        std::optional<TES3MP::ConnectAttemptId> pendingAttempt;
        std::vector<TES3MP::TransportMessage> inbound;
        std::vector<std::byte> sent;
        std::optional<TES3MP::TransportChannel> sentChannel;

        void enqueue(TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind,
            std::span<const std::byte> payload, TES3MP::TransportChannel channel)
        {
            inbound.push_back({ channel, frame(messageClass, messageKind, payload) });
        }
    };

    class Input final : public TES3MP::OpenMWAdapter::SemanticInputProvider
    {
    public:
        TES3MP::OpenMWAdapter::CellTransitionCapture captureCellTransition() noexcept override { return {}; }
        std::optional<TES3MP::PlayerMotionIntent> sampleCurrentIntent() noexcept override
        {
            ++calls;
            return TES3MP::PlayerMotionIntent(TES3MP::LinearVelocity3(1, 2, 3));
        }
        unsigned calls = 0;
    };

    class Presentation final : public TES3MP::OpenMWAdapter::PresentationProvider
    {
    public:
        TES3MP::OpenMWAdapter::ProviderResult applyAuthoritative(const TES3MP::LatestWinsSnapshot&,
            std::span<const TES3MP::ObservedPlayer>, bool, TES3MP::MonotonicInstant) noexcept override
        {
            ++calls;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        TES3MP::OpenMWAdapter::ProviderResult advance(TES3MP::MonotonicInstant) noexcept override
        {
            ++advances;
            return TES3MP::OpenMWAdapter::ProviderResult::Accepted;
        }
        void clear() noexcept override { ++clears; }
        unsigned calls = 0;
        unsigned advances = 0;
        unsigned clears = 0;
    };

    class Status final : public TES3MP::OpenMWAdapter::ConnectionStatusProvider
    {
    public:
        void report(TES3MP::OpenMWAdapter::ConnectionStatus value) noexcept override
        {
            last = value;
            values.push_back(value);
        }
        std::optional<TES3MP::OpenMWAdapter::ConnectionStatus> last;
        std::vector<TES3MP::OpenMWAdapter::ConnectionStatus> values;
    };

    class DisconnectOnce final : public TES3MP::OpenMWAdapter::ConnectionControlProvider
    {
    public:
        bool disconnectRequested() noexcept override
        {
            if (!pending)
                return false;
            pending = false;
            return true;
        }
        bool pending = true;
    };

    class Coordinator final : public TES3MP::OpenMWAdapter::EngineCoordinator
    {
    public:
        void frame(float duration) noexcept override
        {
            ++calls;
            lastDuration = duration;
        }
        unsigned calls = 0;
        float lastDuration = 0;
    };
}

int main()
{
    using namespace TES3MP;
    using namespace TES3MP::OpenMWAdapter;

    const auto endpoint = *ConnectionEndpoint::create("127.0.0.1", 25560);
    const auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
    const auto outbound = *OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    const ReconnectConfiguration reconnect{ endpoint, timeouts, outbound };

    require(mapPlanarMovement(0, 0, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(mapPlanarMovement(1, 0, 0).desiredVelocity() == LinearVelocity3(DesktopFixtureSpeedQuantaPerTick, 0, 0));
    require(mapPlanarMovement(0, 1, 0).desiredVelocity() == LinearVelocity3(0, DesktopFixtureSpeedQuantaPerTick, 0));
    require(mapPlanarMovement(1, 1, 0).desiredVelocity() == LinearVelocity3(2896, 2896, 0));
    require(mapPlanarMovement(0, 1, std::numbers::pi / 2).desiredVelocity()
        == LinearVelocity3(DesktopFixtureSpeedQuantaPerTick, 0, 0));
    require(
        mapPlanarMovement(0.5 / DesktopFixtureSpeedQuantaPerTick, 0, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(
        mapPlanarMovement(1.5 / DesktopFixtureSpeedQuantaPerTick, 0, 0).desiredVelocity() == LinearVelocity3(2, 0, 0));
    require(
        mapPlanarMovement(std::numeric_limits<double>::infinity(), 1, 0).desiredVelocity() == LinearVelocity3(0, 0, 0));

    MotionMetrics motionMetrics;
    RemoteMotionBuffer remote(motionMetrics);
    require(remote.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(remote.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(33'333'333)));
    require(remote.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(66'666'667)));
    auto pose = remote.advance(MonotonicInstant::fromNanoseconds(83'333'334));
    require(pose && std::abs(pose->x - 2048.0) < 1.0);
    pose = remote.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(pose && std::abs(pose->x - 4096.0) < 1.0);
    require(remote.observe(remoteSample(3, 4, 12'288), MonotonicInstant::fromNanoseconds(100'000'000)));
    require(remote.observe(remoteSample(4, 5, 16'384), MonotonicInstant::fromNanoseconds(133'333'334)));
    require(remote.sampleCount() == MaximumRemoteMotionSamples);
    require(remote.observe(remoteSample(5, 6, 20'480), MonotonicInstant::fromNanoseconds(166'666'667)));
    require(remote.sampleCount() == MaximumRemoteMotionSamples);

    MotionMetrics extrapolationMetrics;
    RemoteMotionBuffer extrapolation(extrapolationMetrics);
    require(extrapolation.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(extrapolation.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(extrapolation.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    pose = extrapolation.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(pose && std::abs(pose->x - 20'480.0) < 1.0);
    const auto held = extrapolation.advance(MonotonicInstant::fromNanoseconds(500'000'000));
    require(held && std::abs(held->x - pose->x) < 1.0);
    require(extrapolationMetrics.has(RemoteMotionMetricKey::SnapshotAgeNanoseconds));
    require(extrapolationMetrics.has(RemoteMotionMetricKey::BufferDepth));
    require(extrapolationMetrics.has(RemoteMotionMetricKey::ExtrapolationNanoseconds));

    MotionMetrics correctionMetrics;
    RemoteMotionBuffer correction(correctionMetrics);
    require(correction.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(correction.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(correction.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    pose = correction.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(pose && std::abs(pose->x - 12'288.0) < 1.0);
    require(correction.observe(remoteSample(2, 4, 9216), MonotonicInstant::fromNanoseconds(100'000'000)));
    const auto continuous = correction.advance(MonotonicInstant::fromNanoseconds(100'000'000));
    require(continuous && std::abs(continuous->x - pose->x) < 1.0);
    const auto corrected = correction.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(corrected && std::abs(corrected->x - 21'504.0) < 1.0);
    require(correctionMetrics.has(RemoteMotionMetricKey::CorrectionDistanceQuanta));

    const auto snapsBefore = correctionMetrics.count(RemoteMotionMetricKey::HardSnaps);
    require(correction.observe(remoteSample(2, 5, 40'000), MonotonicInstant::fromNanoseconds(166'666'667)));
    const auto snapped = correction.advance(MonotonicInstant::fromNanoseconds(166'666'667));
    require(snapped && snapped->x > corrected->x + RemoteHardSnapDistanceQuanta);
    require(correctionMetrics.count(RemoteMotionMetricKey::HardSnaps) == snapsBefore + 1);
    require(correction.observe(remoteSample(3, 6, 50'000, 0, 2), MonotonicInstant::fromNanoseconds(200'000'000)));
    require(correction.sampleCount() == 1);
    const auto discontinuity = correction.advance(MonotonicInstant::fromNanoseconds(200'000'000));
    require(discontinuity && discontinuity->x == 50'000.0);
    correction.clear();
    require(correction.sampleCount() == 0 && !correction.advance(MonotonicInstant::fromNanoseconds(300'000'000)));

    MotionMetrics clockMetrics;
    RemoteMotionBuffer clockRegression(clockMetrics);
    require(clockRegression.observe(remoteSample(0, 1, 0), MonotonicInstant::fromNanoseconds(0)));
    require(clockRegression.observe(remoteSample(1, 2, 4096), MonotonicInstant::fromNanoseconds(0)));
    require(clockRegression.observe(remoteSample(2, 3, 8192), MonotonicInstant::fromNanoseconds(0)));
    const auto beforeRegression = clockRegression.advance(MonotonicInstant::fromNanoseconds(33'333'334));
    const auto afterRegression = clockRegression.advance(MonotonicInstant::fromNanoseconds(20'000'000));
    require(beforeRegression && afterRegression && std::abs(beforeRegression->x - afterRegression->x) < 1.0);

    DroppingMotionMetrics droppingMetrics;
    RemoteMotionBuffer dropsDoNotChangePresentation(droppingMetrics);
    require(dropsDoNotChangePresentation.observe(remoteSample(0, 1, 7), MonotonicInstant::fromNanoseconds(0)));
    require(dropsDoNotChangePresentation.advance(MonotonicInstant::fromNanoseconds(0))->x == 7.0);

    MotionIntentTracker motion;
    motion.sample(PlayerMotionIntent(LinearVelocity3(10, 0, 0)));
    require(motion.next(LinearVelocity3(0, 0, 0)).has_value());
    require(motion.markQueued(CommandSequence::initial()) && motion.pending());
    motion.sample(PlayerMotionIntent(LinearVelocity3(0, 0, 0)));
    require(!motion.next(LinearVelocity3(0, 0, 0)));
    motion.observeAcknowledgement(CommandSequence::initial());
    require(!motion.pending());
    require(motion.next(LinearVelocity3(10, 0, 0))->desiredVelocity() == LinearVelocity3(0, 0, 0));
    require(motion.markQueued(*CommandSequence::initial().next()));
    motion.observeAcknowledgement(*CommandSequence::initial().next());
    require(!motion.next(LinearVelocity3(0, 0, 0)));

    Input input;
    auto intent = input.sampleCurrentIntent();
    require(input.calls == 1 && intent && intent->desiredVelocity() == TES3MP::LinearVelocity3(1, 2, 3));
    Presentation presentation;
    Status status;
    Coordinator coordinator;
    coordinator.frame(0.25f);
    require(coordinator.calls == 1 && coordinator.lastDuration == 0.25f);
    require(!TES3MP::OpenMWAdapter::makeCoordinator({}, {}, {}, reconnect, input, presentation, status));

    auto transport = std::make_unique<IdleTransport>();
    auto* transportObserver = transport.get();
    auto clock = std::make_unique<Clock>();
    auto created = ClientSessionRuntime::create(*transport, *clock, timeouts, SessionGeneration::initial(), outbound);
    auto runtime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(created));
    auto liveCoordinator = makeCoordinator(
        std::move(transport), std::move(clock), std::move(runtime), reconnect, input, presentation, status);
    require(static_cast<bool>(liveCoordinator));
    liveCoordinator->frame(0.01f);
    require(presentation.advances == 1);
    transportObserver->failPoll = true;
    liveCoordinator->frame(0.01f);
    require(presentation.clears == 1 && status.last == ConnectionStatus::Disconnected);
    liveCoordinator.reset();
    require(presentation.clears == 2);

    Input reconnectInput;
    Presentation reconnectPresentation;
    Status reconnectStatus;
    DisconnectOnce disconnect;
    auto reconnectTransport = std::make_unique<IdleTransport>();
    auto* reconnectTransportObserver = reconnectTransport.get();
    reconnectTransportObserver->acceptConnections = true;
    auto reconnectClock = std::make_unique<Clock>();
    auto* reconnectClockObserver = reconnectClock.get();
    auto reconnectCreated = ClientSessionRuntime::create(
        *reconnectTransport, *reconnectClock, timeouts, SessionGeneration::initial(), outbound);
    auto reconnectRuntime = std::get<std::unique_ptr<ClientSessionRuntime>>(std::move(reconnectCreated));
    auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
    auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(versions), {}, {}));
    const std::array passwordBytes{ std::byte{ 1 } };
    auto password = AuthenticationMaterial::create(passwordBytes);
    require(password
        && reconnectRuntime->start(
               endpoint, ClientHello::fromOffer(std::move(offer)), AuthenticationRequest::join(std::move(*password)))
            == HeadlessClientResult::Accepted);
    auto reconnectCoordinator = makeCoordinator(std::move(reconnectTransport), std::move(reconnectClock),
        std::move(reconnectRuntime), reconnect, reconnectInput, reconnectPresentation, reconnectStatus, &disconnect);
    reconnectCoordinator->frame(0.01f);
    auto helloPayload = encodeServerHello(serverHello());
    reconnectTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, helloPayload, TransportChannel::ReliableOrdered);
    reconnectCoordinator->frame(0.01f);
    auto initialAccepted = accepted(std::byte{ 2 }, 2 * MinimumResumeTokenLifetimeMilliseconds);
    auto initialAcceptedPayload = encodeAuthenticationAccepted(initialAccepted);
    reconnectTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        initialAcceptedPayload, TransportChannel::ReliableOrdered);
    auto initialSnapshot = selfSnapshot(SessionGeneration::initial());
    auto initialSnapshotPayload = encodeLatestWinsSnapshot(initialSnapshot);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        initialSnapshotPayload, TransportChannel::LatestWins);
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.calls == 1);

    reconnectTransportObserver->acceptConnections = false;
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.clears == 1 && reconnectStatus.last == ConnectionStatus::Reconnecting);
    reconnectTransportObserver->acceptConnections = true;
    reconnectClockObserver->nanoseconds = 999'999'999;
    reconnectCoordinator->frame(0.01f);
    require(!reconnectTransportObserver->pendingAttempt && reconnectTransportObserver->nextConnection == 2);
    reconnectClockObserver->nanoseconds = 1'000'000'000;
    reconnectCoordinator->frame(0.01f);
    require(reconnectTransportObserver->pendingAttempt && reconnectTransportObserver->nextConnection == 2);
    reconnectCoordinator->frame(0.01f);
    reconnectTransportObserver->enqueue(
        MessageClass::SessionControl, MessageKind::ServerHello, helloPayload, TransportChannel::ReliableOrdered);
    reconnectCoordinator->frame(0.01f);
    auto sentResumeFrame = decodeProtocolFrame(reconnectTransportObserver->sent);
    require(std::holds_alternative<DecodedFrame>(sentResumeFrame));
    auto sentResume = decodeAuthenticationRequest(std::get<DecodedFrame>(sentResumeFrame).payload());
    require(std::holds_alternative<AuthenticationRequest>(sentResume)
        && std::get<AuthenticationRequest>(std::move(sentResume)).kind() == AuthenticationCredentialKind::ResumeToken);
    auto rotatedAccepted = accepted(std::byte{ 3 });
    auto rotatedAcceptedPayload = encodeAuthenticationAccepted(rotatedAccepted);
    reconnectTransportObserver->enqueue(MessageClass::SessionControl, MessageKind::AuthenticationAccepted,
        rotatedAcceptedPayload, TransportChannel::ReliableOrdered);
    auto resumedSnapshot = selfSnapshot(*SessionGeneration::initial().next());
    auto resumedSnapshotPayload = encodeLatestWinsSnapshot(resumedSnapshot);
    reconnectTransportObserver->enqueue(MessageClass::LatestWinsSnapshot, MessageKind::LatestWinsSnapshot,
        resumedSnapshotPayload, TransportChannel::LatestWins);
    reconnectCoordinator->frame(0.01f);
    require(reconnectPresentation.calls == 2 && reconnectStatus.last == ConnectionStatus::Resumed);

    require(std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(
                TES3MP::OpenMWAdapter::makeClientCoordinator(
                    "", 0, 0, "unreachable/credential", TES3MP::OpenMWAdapter::ClientProviders{}))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::ProvidersUnavailable);
    const TES3MP::OpenMWAdapter::ClientProviders providers{ &input, &presentation, &status, nullptr };
    require(std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(
                TES3MP::OpenMWAdapter::makeClientCoordinator("", 25560, 1000, {}, providers))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::InvalidEndpoint);
    require(std::get<TES3MP::OpenMWAdapter::ClientCompositionFailure>(
                TES3MP::OpenMWAdapter::makeClientCoordinator("127.0.0.1", 25560, 0, {}, providers))
        == TES3MP::OpenMWAdapter::ClientCompositionFailure::InvalidTimeout);
}
