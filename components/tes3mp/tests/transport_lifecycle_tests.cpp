#include <tes3mp/transport.hpp>

#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    bool endpointContract()
    {
        const auto ipv4 = TES3MP::ConnectionEndpoint::create("127.0.0.1", 25565);
        const auto ipv6 = TES3MP::ConnectionEndpoint::create("2001:0DB8::1", 25565);
        const auto dns = TES3MP::ConnectionEndpoint::create("Example.COM.", 25565);
        const auto idna = TES3MP::ConnectionEndpoint::create("xn--bcher-kva.example", 25565);
        return check(ipv4 && ipv4->hostKind() == TES3MP::EndpointHostKind::Ipv4 && !ipv4->requiresDns(),
                   "valid IPv4 endpoint rejected")
            && check(ipv6 && ipv6->host() == "2001:0db8::1" && !ipv6->requiresDns(),
                "valid IPv6 endpoint rejected or not normalized")
            && check(
                dns && dns->host() == "example.com" && dns->requiresDns(), "DNS trailing-root normalization failed")
            && check(idna && idna->requiresDns(), "valid ASCII IDNA A-label rejected")
            && check(!TES3MP::ConnectionEndpoint::create("example.com", 0), "zero connect port accepted")
            && check(!TES3MP::ConnectionEndpoint::create("https://example.com", 1), "URI endpoint accepted")
            && check(!TES3MP::ConnectionEndpoint::create("example.com:1", 1), "embedded port accepted")
            && check(!TES3MP::ConnectionEndpoint::create("bad host", 1), "whitespace accepted")
            && check(!TES3MP::ConnectionEndpoint::create("-bad.example", 1), "invalid label accepted")
            && check(!TES3MP::ConnectionEndpoint::create("b\xC3\xBC"
                                                         "cher.example",
                         1),
                "Unicode host accepted")
            && check(!TES3MP::ConnectionEndpoint::create(std::string(254, 'a'), 1), "oversized host accepted");
    }

    bool listenerContract()
    {
        const auto wildcard = TES3MP::ListenerEndpoint::create("0.0.0.0", 0);
        const auto ipv6 = TES3MP::ListenerEndpoint::create("::", 0);
        return check(wildcard && wildcard->port() == 0, "IPv4 wildcard/ephemeral listener rejected")
            && check(ipv6 && ipv6->hostKind() == TES3MP::EndpointHostKind::Ipv6, "IPv6 wildcard listener rejected")
            && check(!TES3MP::ListenerEndpoint::create("example.com", 0), "DNS listener accepted");
    }

    bool limitAndIdentityContract()
    {
        const auto limits = TES3MP::TransportLimits::create(1, 8, 8, 128);
        const auto listener = TES3MP::ListenerId::initial();
        const auto attempt = TES3MP::ConnectAttemptId::initial();
        const auto connection = TES3MP::TransportConnectionId::initial();
        return check(limits.has_value(), "approved hard-ceiling profile rejected")
            && check(!TES3MP::TransportLimits::create(2, 8, 8, 128), "listener ceiling exceeded")
            && check(!TES3MP::TransportLimits::create(1, 9, 8, 128), "attempt ceiling exceeded")
            && check(!TES3MP::TransportLimits::create(1, 8, 9, 128), "connection ceiling exceeded")
            && check(!TES3MP::TransportLimits::create(1, 8, 8, 129), "event ceiling exceeded")
            && check(listener.value() == 1 && attempt.value() == 1 && connection.value() == 1,
                "owned identities do not start at one")
            && check(listener.next() && listener.next()->value() == 2, "owned identity is not monotonic");
    }

    bool channelContract()
    {
        using TES3MP::MessageClass;
        using TES3MP::TransportChannel;
        const auto invalidClass = static_cast<MessageClass>(255);
        const auto invalidChannel = static_cast<TransportChannel>(255);
        return check(TES3MP::transportChannelFor(MessageClass::SessionControl) == TransportChannel::ReliableOrdered,
                   "session control is not mapped to the reliable channel")
            && check(TES3MP::transportChannelFor(MessageClass::ReliableOperation) == TransportChannel::ReliableOrdered,
                "reliable operation is not mapped to the reliable channel")
            && check(TES3MP::transportChannelFor(MessageClass::LatestWinsSnapshot) == TransportChannel::LatestWins,
                "snapshot is not mapped to the latest-wins channel")
            && check(!TES3MP::transportChannelFor(MessageClass::PresentationSample),
                "presentation samples must remain transport-disabled until Slice 9.5")
            && check(!TES3MP::transportChannelFor(invalidClass), "unknown message class acquired a channel")
            && check(TES3MP::maximumTransportMessageBytes(TransportChannel::ReliableOrdered)
                    == TES3MP::ProtocolFrameHeaderBytes + TES3MP::ReliableOperationMaximumPayloadBytes,
                "reliable channel maximum does not match the protocol frame budget")
            && check(TES3MP::maximumTransportMessageBytes(TransportChannel::LatestWins)
                    == TES3MP::ProtocolFrameHeaderBytes + TES3MP::LatestWinsSnapshotMaximumPayloadBytes,
                "snapshot channel maximum does not match the protocol frame budget")
            && check(!TES3MP::maximumTransportMessageBytes(invalidChannel),
                "unknown transport channel acquired a message budget")
            && check(TES3MP::isMessageClassAllowedOnTransportChannel(
                         MessageClass::ReliableOperation, TransportChannel::ReliableOrdered),
                "reliable operation was rejected from its channel")
            && check(!TES3MP::isMessageClassAllowedOnTransportChannel(
                         MessageClass::ReliableOperation, TransportChannel::LatestWins),
                "reliable operation was accepted on the latest-wins channel")
            && check(!TES3MP::isMessageClassAllowedOnTransportChannel(
                         MessageClass::LatestWinsSnapshot, TransportChannel::ReliableOrdered),
                "snapshot was accepted on the reliable channel")
            && check(TES3MP::TransportRuntime::MaxMessagesPerReceive == 128,
                "approved initial receive-drain ceiling changed");
    }

    bool telemetryAndStableReasonContract()
    {
        static_assert(std::is_abstract_v<TES3MP::TransportTelemetrySink>);
        static_assert(!std::is_constructible_v<TES3MP::TransportTelemetryObservation, std::string_view>);
        TES3MP::NullTransportTelemetrySink sink;
        const TES3MP::TransportTelemetryObservation observation{ TES3MP::TransportTelemetryKind::PendingBytes,
            TES3MP::TransportTelemetryDirection::Outbound, TES3MP::TransportChannel::ReliableOrdered, 17 };
        using F = TES3MP::TransportFailure;
        using R = TES3MP::StableNetworkReason;
        const std::array mappings{ std::pair{ F::None, R::None }, std::pair{ F::PeerClosed, R::PeerClosed },
            std::pair{ F::LocalClose, R::LocalGracefulClose }, std::pair{ F::LocalAbort, R::LocalAbort },
            std::pair{ F::TimedOut, R::TimedOut }, std::pair{ F::AuthenticationDenied, R::AuthenticationDenied },
            std::pair{ F::AuthenticationTemporarilyUnavailable, R::AuthenticationTemporarilyUnavailable },
            std::pair{ F::SlowPeer, R::SlowPeer }, std::pair{ F::CapacityExhausted, R::CapacityExhausted },
            std::pair{ F::ResolutionFailed, R::NameResolutionFailed },
            std::pair{ F::SecuritySetupFailed, R::SecuritySetupFailed },
            std::pair{ F::InvalidMessage, R::ProtocolViolation }, std::pair{ F::Shutdown, R::RuntimeShutdown },
            std::pair{ F::DependencyFailure, R::TransportDependencyFailed } };
        bool mappingsMatch = true;
        for (const auto& [failure, reason] : mappings)
            mappingsMatch = mappingsMatch && TES3MP::stableNetworkReason(failure) == reason;
        return check(sink.tryRecord(observation) == TES3MP::TransportTelemetryResult::Accepted,
                   "null transport telemetry sink rejected observation")
            && check(mappingsMatch, "stable reason mapping changed")
            && check(TES3MP::stableNetworkReason(static_cast<TES3MP::TransportFailure>(255))
                    == TES3MP::StableNetworkReason::TransportDependencyFailed,
                "unknown failure did not fail closed");
    }
}

int main()
{
    return endpointContract() && listenerContract() && limitAndIdentityContract() && channelContract()
            && telemetryAndStableReasonContract()
        ? 0
        : 1;
}
