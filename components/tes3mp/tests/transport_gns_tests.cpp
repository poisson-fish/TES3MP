#include <tes3mp/transport_gns.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using namespace std::chrono_literals;

    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    struct ConnectionResult
    {
        TES3MP::ListenerId listener = TES3MP::ListenerId::initial();
        TES3MP::ConnectAttemptId attempt = TES3MP::ConnectAttemptId::initial();
        std::vector<TES3MP::TransportConnectionId> connections;
    };

    bool pollUntil(TES3MP::TransportRuntime& runtime, std::chrono::steady_clock::time_point deadline,
        const auto& predicate, std::vector<TES3MP::TransportEvent>& observed)
    {
        std::array<TES3MP::TransportEvent, 16> events;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto result = runtime.poll(events);
            if (result.result == TES3MP::TransportResult::RuntimeFailed)
                return false;
            observed.insert(observed.end(), events.begin(), events.begin() + result.events);
            if (predicate(observed))
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }

    bool establish(std::string_view host)
    {
        const auto limits = TES3MP::TransportLimits::create(1, 8, 8, 128);
        if (!check(limits.has_value(), "approved transport limits rejected"))
            return false;
        auto factory = TES3MP::makeGameNetworkingSocketsTransport(*limits);
        if (!check(static_cast<bool>(factory), "verified transport factory failed"))
            return false;

        auto listenerEndpoint = TES3MP::ListenerEndpoint::create("127.0.0.1", 0);
        auto listener = factory.runtime->startListener(*listenerEndpoint);
        if (listener.result != TES3MP::TransportResult::Accepted)
            std::cerr << "listener admission result=" << static_cast<int>(listener.result) << '\n';
        if (!check(listener.result == TES3MP::TransportResult::Accepted && listener.id.has_value(),
                "loopback listener was not admitted"))
            return false;

        std::vector<TES3MP::TransportEvent> observed;
        const bool bound = pollUntil(*factory.runtime, std::chrono::steady_clock::now() + 3s,
            [](const auto& values) {
                return std::ranges::any_of(values, [](const auto& event) {
                    return event.kind == TES3MP::TransportEventKind::ListenerStarted
                        && event.boundEndpoint.has_value() && event.boundEndpoint->port() != 0;
                });
            },
            observed);
        if (!check(bound, "listener did not report its numeric ephemeral endpoint"))
            return false;
        const auto started = std::ranges::find_if(observed, [](const auto& event) {
            return event.kind == TES3MP::TransportEventKind::ListenerStarted;
        });
        const auto endpoint = TES3MP::ConnectionEndpoint::create(host, started->boundEndpoint->port());
        if (!check(endpoint.has_value(), "loopback connection endpoint rejected"))
            return false;
        auto attempt = factory.runtime->connect(*endpoint);
        if (!check(attempt.result == TES3MP::TransportResult::Accepted && attempt.id.has_value(),
                "loopback connection was not admitted"))
            return false;

        const bool connected = pollUntil(*factory.runtime, std::chrono::steady_clock::now() + 8s,
            [](const auto& values) {
                bool client = false;
                bool server = false;
                for (const auto& event : values)
                {
                    client |= event.kind == TES3MP::TransportEventKind::ConnectSucceeded;
                    server |= event.kind == TES3MP::TransportEventKind::ConnectionAccepted;
                }
                return client && server;
            },
            observed);
        if (!check(connected, "encrypted loopback connection did not establish"))
            return false;

        std::vector<TES3MP::TransportConnectionId> connections;
        for (const auto& event : observed)
        {
            if ((event.kind == TES3MP::TransportEventKind::ConnectSucceeded
                    || event.kind == TES3MP::TransportEventKind::ConnectionAccepted)
                && event.connection)
            {
                if (!check(event.security == TES3MP::TransportSecurity::EncryptedUnauthenticated,
                        "connection event made an invalid security claim"))
                    return false;
                connections.push_back(*event.connection);
            }
        }
        if (!check(connections.size() == 2 && connections[0] != connections[1],
                "client/server library handles did not receive distinct owned identities"))
            return false;

        if (!check(factory.runtime->stopListener(*listener.id) == TES3MP::TransportResult::Accepted,
                "listener stop failed"))
            return false;
        if (!check(factory.runtime->stopListener(*listener.id) == TES3MP::TransportResult::AlreadyFinalized,
                "repeated listener stop was not idempotent"))
            return false;

        for (const auto connection : connections)
        {
            if (!check(factory.runtime->close(connection, TES3MP::TransportCloseMode::Abort)
                        == TES3MP::TransportResult::Accepted,
                    "connection abort failed"))
                return false;
            if (!check(factory.runtime->close(connection, TES3MP::TransportCloseMode::Abort)
                        == TES3MP::TransportResult::AlreadyFinalized,
                    "repeated connection abort was not idempotent"))
                return false;
        }
        if (!check(factory.runtime->shutdown() == TES3MP::TransportResult::Accepted,
                "runtime shutdown failed"))
            return false;
        return check(factory.runtime->shutdown() == TES3MP::TransportResult::AlreadyFinalized,
            "repeated runtime shutdown was not idempotent");
    }

    bool cancellationAndAttemptCapacity()
    {
        auto factory = TES3MP::makeGameNetworkingSocketsTransport(*TES3MP::TransportLimits::create(1, 8, 8, 128));
        if (!check(static_cast<bool>(factory), "cancellation runtime factory failed"))
            return false;
        const auto concurrent
            = TES3MP::makeGameNetworkingSocketsTransport(*TES3MP::TransportLimits::create(1, 8, 8, 128));
        if (!check(!concurrent
                    && concurrent.failure == TES3MP::TransportFactoryFailure::RuntimeAlreadyActive,
                "concurrent runtime did not return its stable factory failure"))
            return false;
        std::vector<TES3MP::ConnectAttemptId> attempts;
        for (std::size_t index = 0; index < TES3MP::TransportLimits::MaxPendingAttempts; ++index)
        {
            const auto endpoint = TES3MP::ConnectionEndpoint::create("127.0.0.1", static_cast<std::uint16_t>(9 + index));
            const auto attempt = factory.runtime->connect(*endpoint);
            if (!check(attempt.result == TES3MP::TransportResult::Accepted && attempt.id,
                    "bounded numeric attempt was not admitted"))
                return false;
            attempts.push_back(*attempt.id);
        }
        const auto excess = factory.runtime->connect(*TES3MP::ConnectionEndpoint::create("127.0.0.1", 99));
        if (!check(excess.result == TES3MP::TransportResult::AtCapacity && !excess.id,
                "ninth logical attempt bypassed the hard ceiling"))
            return false;
        for (const auto attempt : attempts)
        {
            if (!check(factory.runtime->cancelConnect(attempt) == TES3MP::TransportResult::Accepted,
                    "numeric attempt cancellation failed"))
                return false;
            if (!check(factory.runtime->cancelConnect(attempt) == TES3MP::TransportResult::AlreadyFinalized,
                    "repeated numeric attempt cancellation was not idempotent"))
                return false;
        }
        std::array<TES3MP::TransportEvent, 16> events;
        const auto polled = factory.runtime->poll(events);
        if (!check(polled.events == attempts.size(), "numeric cancellation did not emit exactly one event per attempt"))
            return false;
        for (std::size_t index = 0; index < polled.events; ++index)
        {
            if (!check(events[index].kind == TES3MP::TransportEventKind::ConnectCancelled,
                    "numeric cancellation emitted the wrong terminal event"))
                return false;
        }

        const auto dns = factory.runtime->connect(*TES3MP::ConnectionEndpoint::create("cancelled.invalid", 25565));
        if (!check(dns.result == TES3MP::TransportResult::Accepted && dns.id,
                "DNS attempt was not admitted"))
            return false;
        if (!check(factory.runtime->cancelConnect(*dns.id) == TES3MP::TransportResult::Accepted,
                "DNS attempt cancellation failed"))
            return false;
        const auto dnsPoll = factory.runtime->poll(events);
        return check(dnsPoll.events == 1 && events[0].kind == TES3MP::TransportEventKind::ConnectCancelled,
            "DNS cancellation did not emit exactly one terminal event");
    }

    bool stoppedListenerRejectsDelayedAccept()
    {
        auto factory = TES3MP::makeGameNetworkingSocketsTransport(*TES3MP::TransportLimits::create(1, 8, 8, 128));
        if (!check(static_cast<bool>(factory), "listener-stop runtime factory failed"))
            return false;
        const auto listener
            = factory.runtime->startListener(*TES3MP::ListenerEndpoint::create("127.0.0.1", 0));
        if (!check(listener.result == TES3MP::TransportResult::Accepted && listener.id,
                "listener-stop fixture was not admitted"))
            return false;

        std::array<TES3MP::TransportEvent, 16> events;
        const auto initial = factory.runtime->poll(events);
        if (!check(initial.events == 1 && events[0].boundEndpoint,
                "listener-stop fixture did not report its bound endpoint"))
            return false;
        const auto endpoint
            = TES3MP::ConnectionEndpoint::create("127.0.0.1", events[0].boundEndpoint->port());
        const auto attempt = factory.runtime->connect(*endpoint);
        if (!check(attempt.result == TES3MP::TransportResult::Accepted && attempt.id,
                "listener-stop fixture connection was not admitted"))
            return false;
        if (!check(factory.runtime->stopListener(*listener.id) == TES3MP::TransportResult::Accepted,
                "listener stop during pending handshake failed"))
            return false;

        const auto deadline = std::chrono::steady_clock::now() + 250ms;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto polled = factory.runtime->poll(events);
            if (!check(polled.result != TES3MP::TransportResult::RuntimeFailed,
                    "listener stop caused runtime failure"))
                return false;
            for (std::size_t index = 0; index < polled.events; ++index)
            {
                if (!check(events[index].kind != TES3MP::TransportEventKind::ConnectionAccepted,
                        "delayed accept targeted a stopped listener"))
                    return false;
            }
            std::this_thread::sleep_for(1ms);
        }
        const auto cancelled = factory.runtime->cancelConnect(*attempt.id);
        return check(cancelled == TES3MP::TransportResult::Accepted
                || cancelled == TES3MP::TransportResult::AlreadyFinalized,
            "listener-stop fixture attempt did not reach a bounded terminal state");
    }

    bool eventOverflowFailsClosed()
    {
        auto factory = TES3MP::makeGameNetworkingSocketsTransport(*TES3MP::TransportLimits::create(1, 8, 8, 1));
        if (!check(static_cast<bool>(factory), "overflow runtime factory failed"))
            return false;
        const auto listener = factory.runtime->startListener(*TES3MP::ListenerEndpoint::create("127.0.0.1", 0));
        if (!check(listener.result == TES3MP::TransportResult::Accepted, "overflow listener was not admitted"))
            return false;
        if (!check(factory.runtime->shutdown() == TES3MP::TransportResult::RuntimeFailed,
                "retained-event overflow did not terminally fail shutdown"))
            return false;
        std::array<TES3MP::TransportEvent, 2> events;
        const auto polled = factory.runtime->poll(events);
        return check(polled.result == TES3MP::TransportResult::RuntimeFailed && polled.events == 1
                && events[0].kind == TES3MP::TransportEventKind::RuntimeFailed
                && events[0].failure == TES3MP::TransportFailure::EventCapacityExceeded,
            "overflow did not retain one closed runtime-failure result");
    }
}

int main()
{
    return establish("127.0.0.1") && establish("localhost") && cancellationAndAttemptCapacity()
            && stoppedListenerRejectsDelayedAccept() && eventOverflowFailsClosed()
        ? 0
        : 1;
}
