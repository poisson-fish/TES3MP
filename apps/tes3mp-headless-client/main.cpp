#include <tes3mp/authentication.hpp>
#include <tes3mp/client_session_runtime.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/protocol_handshake.hpp>
#include <tes3mp/transport_gns.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <thread>
#include <variant>

namespace
{
    constexpr std::size_t Phase7ReconnectCycles = 32;
    constexpr auto Phase7SoakDuration = std::chrono::seconds(60);

    class SteadyClock final : public TES3MP::MonotonicClock
    {
    public:
        TES3MP::MonotonicInstant now() const noexcept override
        {
            const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
            return TES3MP::MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(value));
        }
    };

    std::optional<TES3MP::OutboundQueuePolicy> outboundPolicy()
    {
        return TES3MP::OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
    }

    std::optional<std::uint64_t> number(std::string_view text)
    {
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    struct LifecycleAttempt
    {
        bool accepted = false;
        std::optional<TES3MP::ResumeToken> token;
        std::uint64_t lifetimeMilliseconds = 0;
        std::optional<TES3MP::SessionId> session;
        std::optional<TES3MP::PlayerId> player;
        std::optional<TES3MP::EntityId> entity;
        std::optional<TES3MP::SessionGeneration> generation;
        std::optional<TES3MP::EntityRevision> revision;
        std::optional<TES3MP::CommandSequence> acknowledged;
    };

    LifecycleAttempt runLifecycleAttempt(TES3MP::TransportRuntime& runtime, TES3MP::MonotonicClock& clock,
        const TES3MP::SessionTimeoutPolicy& timeouts, const TES3MP::ConnectionEndpoint& endpoint,
        TES3MP::AuthenticationRequest request, std::uint64_t timeoutMilliseconds, bool advanceProgress = false,
        TES3MP::SessionGeneration generation = TES3MP::SessionGeneration::initial())
    {
        LifecycleAttempt result;
        const auto policy = outboundPolicy();
        auto created = policy ? TES3MP::ClientSessionRuntime::create(runtime, clock, timeouts, generation, *policy)
                              : TES3MP::ClientRuntimeCreateResult{ TES3MP::SessionTransitionError{} };
        auto* value = std::get_if<std::unique_ptr<TES3MP::ClientSessionRuntime>>(&created);
        auto range = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 0));
        auto offer = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(std::move(range), {}, {}));
        if (!value || !*value
            || (*value)->start(endpoint, TES3MP::ClientHello::fromOffer(std::move(offer)), std::move(request))
                != TES3MP::HeadlessClientResult::Accepted)
            return result;
        auto& clientRuntime = **value;
        auto& client = clientRuntime.session();
        bool bound = false;
        std::uint64_t motionCommandsSent = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto advanced = clientRuntime.advance();
            if (advanced.result != TES3MP::ClientRuntimeResult::Accepted)
                break;
            if (advanced.authenticationAccepted)
            {
                result.lifetimeMilliseconds = clientRuntime.resumeLifetimeMilliseconds();
                result.token = clientRuntime.takeResumeToken();
            }
            if (advanced.snapshotApplied && result.token)
            {
                const auto sessionId = *client.stateMachine().sessionId();
                bound = true;
                const auto& confirmed = *client.stateMachine().confirmedSnapshot();
                const auto self = std::ranges::find_if(confirmed.view().entries(),
                    [&](const auto& entry) { return entry.playerId().value() == sessionId.value(); });
                if (self == confirmed.view().entries().end())
                    break;
                const auto acknowledged = confirmed.header().acknowledgedCommandSequence();
                if (advanceProgress && (!acknowledged || acknowledged->value() < 2))
                {
                    const auto next = acknowledged ? 2u : 1u;
                    if (motionCommandsSent < next)
                    {
                        if (clientRuntime.queueMotionIntent(
                                TES3MP::PlayerMotionIntent(TES3MP::LinearVelocity3(next == 1 ? 3 : 0, 0, 0)))
                            != TES3MP::ClientRuntimeResult::Accepted)
                            break;
                        motionCommandsSent = next;
                    }
                    continue;
                }
                result.accepted = true;
                result.session = sessionId;
                result.player = self->playerId();
                result.entity = self->entityId();
                result.generation = confirmed.header().targetSessionGeneration();
                result.revision = self->entityRevision();
                result.acknowledged = confirmed.header().acknowledgedCommandSequence();
                client.close();
                return result;
            }
            if (clientRuntime.flushOutbound() != TES3MP::ClientRuntimeResult::Accepted)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.close();
        return result;
    }
}

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 6)
    {
        std::cerr << "usage: tes3mp_headless_client <host> <port> <password-file> <timeout-ms> "
                     "[mover|observer|motion-one|motion-two|lifecycle|reconnect|soak-one|soak-two]\n";
        return 2;
    }
    const auto port = number(argv[2]);
    const auto timeout = number(argv[4]);
    auto endpoint = port && *port <= 65535
        ? TES3MP::ConnectionEndpoint::create(argv[1], static_cast<std::uint16_t>(*port))
        : std::nullopt;
    if (!endpoint || !timeout || *timeout == 0 || *timeout > 60'000)
        return 2;

    std::ifstream passwordStream(argv[3], std::ios::binary);
    std::vector<std::byte> passwordBytes;
    char byte = 0;
    while (passwordStream.get(byte) && passwordBytes.size() <= TES3MP::MaximumAuthenticationMaterialBytes)
        passwordBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    if (!passwordBytes.empty() && passwordBytes.back() == std::byte{ '\n' })
        passwordBytes.pop_back();
    if (!passwordBytes.empty() && passwordBytes.back() == std::byte{ '\r' })
        passwordBytes.pop_back();
    auto password = TES3MP::AuthenticationMaterial::create(passwordBytes);
    std::fill(passwordBytes.begin(), passwordBytes.end(), std::byte{});
    if (!passwordStream.eof() || !password)
        return 2;

    auto limits = TES3MP::TransportLimits::create(1, 1, 1, 32);
    auto factory = limits ? TES3MP::makeGameNetworkingSocketsTransport(*limits) : TES3MP::TransportFactoryResult{};
    SteadyClock clock;
    auto timeouts
        = TES3MP::SessionTimeoutPolicy::create(*timeout * 1'000'000, *timeout * 1'000'000, *timeout * 1'000'000);
    const auto queuePolicy = outboundPolicy();
    auto created = factory && timeouts && queuePolicy
        ? TES3MP::ClientSessionRuntime::create(
              *factory.runtime, clock, *timeouts, TES3MP::SessionGeneration::initial(), *queuePolicy)
        : TES3MP::ClientRuntimeCreateResult{ TES3MP::SessionTransitionError{} };
    auto* sessionValue = std::get_if<std::unique_ptr<TES3MP::ClientSessionRuntime>>(&created);
    if (!sessionValue || !*sessionValue)
        return 3;
    auto& clientRuntime = **sessionValue;
    auto& session = clientRuntime.session();
    bool authenticationAccepted = false;
    const std::string_view mode = argc == 6 ? argv[5] : "join";
    if (mode != "join" && mode != "mover" && mode != "observer" && mode != "motion-one" && mode != "motion-two"
        && mode != "lifecycle" && mode != "reconnect" && mode != "soak-one" && mode != "soak-two")
        return 2;
    const bool soak = mode == "soak-one" || mode == "soak-two";
    if (mode == "lifecycle" || mode == "reconnect")
    {
        auto first = runLifecycleAttempt(*factory.runtime, clock, *timeouts, *endpoint,
            TES3MP::AuthenticationRequest::join(std::move(*password)), *timeout, true);
        if (!first.accepted || !first.token || !first.session || !first.player || !first.entity || !first.generation
            || !first.revision || !first.acknowledged)
        {
            std::cerr << "lifecycle initial join/progress failed\n";
            return 3;
        }
        const auto initialSession = first.session;
        const auto initialPlayer = first.player;
        const auto initialEntity = first.entity;
        const auto initialRevision = first.revision;
        const auto initialAcknowledged = first.acknowledged;
        auto resumed = std::move(first);
        const auto reconnectCycles = mode == "reconnect" ? Phase7ReconnectCycles : 1;
        for (std::size_t cycle = 1; cycle <= reconnectCycles; ++cycle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(mode == "reconnect" ? 1'050 : 1'000));
            const auto expectedGeneration = resumed.generation->next();
            auto next = runLifecycleAttempt(*factory.runtime, clock, *timeouts, *endpoint,
                TES3MP::AuthenticationRequest::resume(std::move(*resumed.token)), *timeout, false, *expectedGeneration);
            if (!next.accepted || !next.token || next.session != initialSession || next.player != initialPlayer
                || next.entity != initialEntity || next.generation != expectedGeneration
                || next.revision != initialRevision || next.acknowledged != initialAcknowledged)
            {
                std::cerr << "lifecycle resume preservation failed cycle=" << cycle << " accepted=" << next.accepted
                          << " generation=" << (next.generation ? next.generation->value() : 0)
                          << " expected_generation=" << expectedGeneration->value() << '\n';
                return 3;
            }
            resumed = std::move(next);
        }
        if (mode == "reconnect")
        {
            std::cout << "{\"event\":\"reconnect_flow_complete\",\"reconnect_cycles\":" << reconnectCycles
                      << ",\"resumed_session_id\":" << resumed.session->value()
                      << ",\"identity_preserved\":true,\"progress_preserved\":true}\n";
            factory.runtime->shutdown();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(resumed.lifetimeMilliseconds + 100));
        auto expired = runLifecycleAttempt(*factory.runtime, clock, *timeouts, *endpoint,
            TES3MP::AuthenticationRequest::resume(std::move(*resumed.token)), *timeout);
        if (expired.accepted)
        {
            std::cerr << "expired resume accepted\n";
            return 3;
        }
        std::ifstream freshPasswordStream(argv[3], std::ios::binary);
        std::vector<std::byte> freshBytes;
        while (freshPasswordStream.get(byte) && freshBytes.size() <= TES3MP::MaximumAuthenticationMaterialBytes)
            freshBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        if (!freshBytes.empty() && freshBytes.back() == std::byte{ '\n' })
            freshBytes.pop_back();
        if (!freshBytes.empty() && freshBytes.back() == std::byte{ '\r' })
            freshBytes.pop_back();
        auto freshPassword = TES3MP::AuthenticationMaterial::create(freshBytes);
        std::fill(freshBytes.begin(), freshBytes.end(), std::byte{});
        if (!freshPasswordStream.eof() || !freshPassword)
        {
            std::cerr << "fresh credential reload failed\n";
            return 3;
        }
        auto fresh = runLifecycleAttempt(*factory.runtime, clock, *timeouts, *endpoint,
            TES3MP::AuthenticationRequest::join(std::move(*freshPassword)), *timeout);
        if (!fresh.accepted || !fresh.session || !fresh.player || !fresh.entity || fresh.session == initialSession
            || fresh.player == initialPlayer || fresh.entity == initialEntity)
        {
            std::cerr << "fresh identity creation failed\n";
            return 3;
        }
        std::cout << "{\"event\":\"lifecycle_flow_complete\",\"resumed_session_id\":" << resumed.session->value()
                  << ",\"fresh_session_id\":" << fresh.session->value()
                  << ",\"identity_preserved\":true,\"progress_preserved\":true,"
                     "\"expired_resume_rejected\":true,\"fresh_identity_created\":true}\n";
        factory.runtime->shutdown();
        return 0;
    }
    auto versions = std::get<TES3MP::ProtocolVersionRange>(TES3MP::ProtocolVersionRange::create(1, 0, 0));
    auto offer = std::get<TES3MP::CapabilityOffer>(TES3MP::CapabilityOffer::create(std::move(versions), {}, {}));
    if (clientRuntime.start(*endpoint, TES3MP::ClientHello::fromOffer(std::move(offer)),
            TES3MP::AuthenticationRequest::join(std::move(*password)))
        != TES3MP::HeadlessClientResult::Accepted)
        return 3;
    bool bound = false;
    bool sentExterior = false;
    bool sentInterior = false;
    bool sawLeave = false;
    bool sawEnter = false;
    bool sentMotion = false;
    bool sawConvergedMovement = false;
    bool rejectedStaleView = false;
    std::optional<TES3MP::LatestWinsSnapshot> staleSnapshot;
    std::optional<std::chrono::steady_clock::time_point> motionNotBefore;
    std::optional<std::chrono::steady_clock::time_point> soakStarted;
    const auto deadline = std::chrono::steady_clock::now()
        + (soak ? Phase7SoakDuration + std::chrono::seconds(10) : std::chrono::milliseconds(*timeout));

    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto previousSnapshot = session.stateMachine().confirmedSnapshot();
        auto pumped = clientRuntime.advance();
        if (pumped.result != TES3MP::ClientRuntimeResult::Accepted)
        {
            std::cerr << "inbound drain failed " << static_cast<int>(pumped.result) << '\n';
            break;
        }
        authenticationAccepted = authenticationAccepted || pumped.authenticationAccepted;
        if (pumped.observationApplied)
        {
            if (mode != "motion-one" && mode != "motion-two" && bound)
            {
                for (const auto& change : session.stateMachine().confirmedObservationBatch()->changes())
                {
                    sawLeave = sawLeave || change.kind == TES3MP::ObservationChangeKind::Leave;
                    sawEnter = sawEnter || (sawLeave && change.kind == TES3MP::ObservationChangeKind::Enter);
                }
            }
        }
        if (authenticationAccepted && pumped.snapshotApplied && !bound)
        {
            const auto sessionId = *session.stateMachine().sessionId();
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            const auto foundSelf = std::ranges::find_if(confirmed.view().entries(),
                [&](const auto& entry) { return entry.playerId().value() == sessionId.value(); });
            if (foundSelf == confirmed.view().entries().end())
                return 3;
            const auto& entry = *foundSelf;
            std::cout << "{\"event\":\"joined\",\"session_id\":" << sessionId.value()
                      << ",\"player_id\":" << entry.playerId().value() << ",\"entity_id\":" << entry.entityId().value()
                      << "}" << std::endl;
            bound = true;
            motionNotBefore = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            if (soak)
                soakStarted = std::chrono::steady_clock::now();
            if (mode == "join")
            {
                session.close();
                factory.runtime->shutdown();
                return 0;
            }
        }
        else if (bound && pumped.snapshotApplied)
        {
            if (!staleSnapshot)
                staleSnapshot = previousSnapshot;
        }
        if (bound && mode == "mover" && session.connection())
        {
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            const auto self = std::ranges::find_if(confirmed.view().entries(), [&](const auto& entry) {
                return entry.playerId().value() == confirmed.header().targetSessionId().value();
            });
            if (self != confirmed.view().entries().end())
            {
                const bool exterior = self->transform().cell().kind() == TES3MP::CellId::Kind::Exterior;
                if (!sentExterior || (exterior && !sentInterior))
                {
                    const auto cell = sentExterior
                        ? TES3MP::CellId::interior(TES3MP::CellSpaceId::fromValue(7).value())
                        : TES3MP::CellId::exterior(TES3MP::CellSpaceId::fromValue(8).value(), 0, 0);
                    if (clientRuntime.queueCellTransition(TES3MP::FixtureCellTransition(cell))
                        != TES3MP::ClientRuntimeResult::Accepted)
                        return 3;
                    if (!sentExterior)
                        sentExterior = true;
                    else
                        sentInterior = true;
                }
            }
        }
        if (bound && (mode == "motion-one" || mode == "motion-two") && !sentMotion && session.connection()
            && motionNotBefore && std::chrono::steady_clock::now() >= *motionNotBefore)
        {
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            if (confirmed.view().entries().size() != 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if ((mode == "motion-two" || mode == "soak-two")
                && std::ranges::none_of(confirmed.view().entries(),
                    [](const auto& entry) { return entry.playerId().value() == 1 && entry.linearVelocity().x() == 1; }))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const auto self = std::ranges::find_if(confirmed.view().entries(), [&](const auto& entry) {
                return entry.playerId().value() == confirmed.header().targetSessionId().value();
            });
            if (self == confirmed.view().entries().end())
                return 3;
            if (clientRuntime.queueMotionIntent(TES3MP::PlayerMotionIntent(
                    TES3MP::LinearVelocity3(mode == "motion-two" || mode == "soak-two" ? 2 : 1, 0, 0)))
                != TES3MP::ClientRuntimeResult::Accepted)
                return 3;
            sentMotion = true;
        }
        if (sentMotion)
        {
            const auto& entries = session.stateMachine().confirmedSnapshot()->view().entries();
            bool sawOne = false;
            bool sawTwo = false;
            for (const auto& entry : entries)
            {
                sawOne = sawOne
                    || (entry.playerId().value() == 1 && entry.linearVelocity().x() == 1
                        && entry.transform().position().x() > 10);
                sawTwo = sawTwo
                    || (entry.playerId().value() == 2 && entry.linearVelocity().x() == 2
                        && entry.transform().position().x() > 10);
            }
            sawConvergedMovement = sawOne && sawTwo;
            if (sawConvergedMovement && staleSnapshot)
            {
                rejectedStaleView = session.receiveLatestWinsSnapshot(std::move(*staleSnapshot))
                    == TES3MP::LatestWinsSnapshotReceiveResult::StaleTick;
                staleSnapshot.reset();
            }
        }
        if (!soak && sawConvergedMovement && rejectedStaleView)
        {
            std::cout << "{\"event\":\"movement_flow_complete\",\"role\":\"" << mode
                      << "\",\"stale_view_rejected\":true}\n";
            session.close();
            factory.runtime->shutdown();
            return 0;
        }
        if (soak && soakStarted && std::chrono::steady_clock::now() >= *soakStarted + Phase7SoakDuration)
        {
            const auto& confirmed = *session.stateMachine().confirmedSnapshot();
            if (confirmed.view().entries().size() != 2
                || std::ranges::any_of(confirmed.view().entries(), [](const auto& entry) {
                       return entry.linearVelocity().x() != 0 || entry.linearVelocity().y() != 0
                           || entry.linearVelocity().z() != 0;
                   }))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            std::cout << "{\"event\":\"soak_flow_complete\",\"duration_seconds\":60,\"entries\":[";
            bool firstEntry = true;
            for (const auto& entry : confirmed.view().entries())
            {
                if (!firstEntry)
                    std::cout << ',';
                firstEntry = false;
                std::cout << "{\"player_id\":" << entry.playerId().value()
                          << ",\"entity_id\":" << entry.entityId().value()
                          << ",\"revision\":" << entry.entityRevision().value()
                          << ",\"x\":" << entry.transform().position().x()
                          << ",\"y\":" << entry.transform().position().y()
                          << ",\"z\":" << entry.transform().position().z() << '}';
            }
            std::cout << "]}" << std::endl;
            session.close();
            factory.runtime->shutdown();
            return 0;
        }
        if (bound && sawLeave && sawEnter && (mode == "observer" || sentInterior))
        {
            std::cout << "{\"event\":\"fixture_flow_complete\",\"role\":\"" << mode << "\"}\n";
            session.close();
            factory.runtime->shutdown();
            return 0;
        }
        const auto flushed = clientRuntime.flushOutbound();
        if (flushed != TES3MP::ClientRuntimeResult::Accepted)
        {
            std::cerr << "outbound flush failed " << static_cast<int>(flushed) << '\n';
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.close();
    factory.runtime->shutdown();
    if (bound && session.stateMachine().confirmedSnapshot())
    {
        for (const auto& entry : session.stateMachine().confirmedSnapshot()->view().entries())
            std::cerr << "player=" << entry.playerId().value() << " x=" << entry.transform().position().x()
                      << " vx=" << entry.linearVelocity().x() << '\n';
    }
    std::cerr << "join failed\n";
    return 3;
}
