#include "server_application.hpp"
#include "server_config.hpp"
#include "connection_session_coordinator.hpp"
#include "phase7_proof_profile.hpp"

#include <tes3mp/observability.hpp>
#include <tes3mp/server_authentication.hpp>
#include <tes3mp/transport_gns.hpp>

#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <variant>

namespace
{
    volatile std::sig_atomic_t stopRequested = 0;
    void requestStop(int) { stopRequested = 1; }

    class SteadyMonotonicClock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
            return TES3MP::MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(nanoseconds));
        }
    };
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: tes3mp_server <config-file>\n";
        return 2;
    }
    std::ifstream configStream(argv[1], std::ios::binary);
    if (!configStream)
    {
        std::cerr << "configuration unavailable\n";
        return 2;
    }
    std::string text;
    text.reserve(TES3MP::ServerApp::MaximumConfigBytes + 1);
    char byte = 0;
    while (configStream.get(byte) && text.size() <= TES3MP::ServerApp::MaximumConfigBytes)
        text.push_back(byte);
    auto parsed = TES3MP::ServerApp::parseServerConfig(text);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&parsed))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto config = std::get<TES3MP::ServerApp::ServerConfig>(std::move(parsed));
    auto password = TES3MP::ServerApp::loadJoinPassword(config.joinPasswordFile);
    if (const auto* error = std::get_if<TES3MP::ServerApp::ConfigError>(&password))
    {
        std::cerr << TES3MP::ServerApp::describeConfigError(*error) << '\n';
        return 2;
    }
    auto limits = TES3MP::TransportLimits::create(1, 8, 8, 128);
    if (!limits)
    {
        std::cerr << "invalid compiled transport limits\n";
        return 3;
    }
    auto factory = TES3MP::makeGameNetworkingSocketsTransport(*limits);
    if (!factory)
    {
        std::cerr << "transport initialization failed\n";
        return 3;
    }
    if (!TES3MP::ServerApp::phase7ProofDisconnectGraceAccepted(config.disconnectGraceMilliseconds))
    {
        std::cerr << "disconnect grace is outside Phase 7 proof bounds\n";
        return 2;
    }

    SteadyMonotonicClock clock;
    auto crypto = TES3MP::makeProductionCredentialCrypto();
    auto ratePolicy = TES3MP::AuthenticationRateLimitPolicy::create(
        TES3MP::ServerApp::Phase7SourceAuthenticationBurst,
        TES3MP::ServerApp::Phase7GlobalAuthenticationBurst,
        TES3MP::ServerApp::Phase7AuthenticationRefillMilliseconds,
        TES3MP::ServerApp::Phase7AuthenticationRefillMilliseconds);
    auto limiter = ratePolicy ? TES3MP::AuthenticationRateLimiter::create(*ratePolicy, clock.now()) : nullptr;
    auto joinProvider = crypto ? TES3MP::JoinPasswordAuthenticationProvider::create(
                                    *crypto, std::get<TES3MP::AuthenticationMaterial>(std::move(password)))
                               : nullptr;
    auto resumeStore = crypto ? TES3MP::ResumeTokenStore::create(*crypto, config.disconnectGraceMilliseconds)
                              : nullptr;
    auto queues = TES3MP::OutboundQueueSet::create(
        TES3MP::OutboundQueuePolicy{}, TES3MP::ServerApp::Phase7ConnectionCapacity);
    const auto timeoutNanoseconds = config.disconnectGraceMilliseconds * 1'000'000;
    auto timeouts = TES3MP::SessionTimeoutPolicy::create(
        timeoutNanoseconds, timeoutNanoseconds, timeoutNanoseconds);
    auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(
        TES3MP::ServerApp::Phase7ProtocolMajor, TES3MP::ServerApp::Phase7ProtocolMinor,
        TES3MP::ServerApp::Phase7ProtocolPatch));
    auto offer = TES3MP::CapabilityOffer::create(std::move(versions), {}, {});
    const auto zero = TES3MP::Turn32::fromValue(0);
    auto fixtureSpawn = TES3MP::Transform(TES3MP::CellId::interior(*TES3MP::CellSpaceId::fromValue(7)),
        TES3MP::Position3(10, 20, 30), TES3MP::Orientation3(zero, zero, zero));
    TES3MP::NullMetricSink metrics;
    TES3MP::NullStructuredEventSink events;
    TES3MP::Observability observability(metrics, events);
    auto emptyState = std::get<TES3MP::CanonicalServerState>(TES3MP::createCanonicalServerState({}, {}));
    TES3MP::CanonicalCommandReducer reducer(std::move(emptyState), observability);
    TES3MP::ServerCommandIntakeCoordinator intake(
        clock, observability, clock.now(), TES3MP::ServerTick::initial(), TES3MP::IngressOrdinal::initial());
    auto joins = TES3MP::AuthenticatedJoinCoordinator::create(fixtureSpawn,
        { *TES3MP::SessionId::fromValue(1), *TES3MP::PlayerId::fromValue(1),
            *TES3MP::EntityId::fromValue(1) }, reducer);
    auto lifecycle = TES3MP::ServerLifecycleCoordinator::create(
        config.disconnectGraceMilliseconds * 1'000'000, reducer);
    if (!crypto || !limiter || !joinProvider || !resumeStore || !queues || !timeouts || !joins || !lifecycle
        || !std::holds_alternative<TES3MP::CapabilityOffer>(offer))
    {
        std::cerr << "server composition failed\n";
        return 3;
    }
    TES3MP::SharedServerAuthenticationService authentication(
        *limiter, *joinProvider, *resumeStore, clock);
    TES3MP::ServerApp::ConnectionSessionCoordinator sessions(clock, observability, *timeouts,
        std::get<TES3MP::CapabilityOffer>(std::move(offer)), authentication, *queues,
        TES3MP::ServerApp::Phase7ConnectionCapacity);
    TES3MP::ServerApp::ServerApplication application(*factory.runtime, config,
        { sessions, *joins, *crypto, *queues, clock, intake, reducer, *lifecycle });
    if (!application.start())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::cout << "server started\n";
    while (stopRequested == 0)
    {
        if (!application.pump(intake.nextTick()))
        {
            std::cerr << application.failure() << '\n';
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config.tickIntervalMilliseconds));
    }
    if (!application.stop())
    {
        std::cerr << application.failure() << '\n';
        return 3;
    }
    std::cout << "server stopped\n";
    return 0;
}
