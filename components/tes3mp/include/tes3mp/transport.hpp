#ifndef TES3MP_TRANSPORT_HPP
#define TES3MP_TRANSPORT_HPP

#include "strong_value.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace TES3MP::Detail
{
    struct ListenerIdTag
    {
        static constexpr std::string_view name = "ListenerId";
    };

    struct ConnectAttemptIdTag
    {
        static constexpr std::string_view name = "ConnectAttemptId";
    };

    struct TransportConnectionIdTag
    {
        static constexpr std::string_view name = "TransportConnectionId";
    };
}

namespace TES3MP
{
    using ListenerId = Detail::StrongValue<Detail::ListenerIdTag, Detail::StrongValuePolicy::CounterFromOne>;
    using ConnectAttemptId
        = Detail::StrongValue<Detail::ConnectAttemptIdTag, Detail::StrongValuePolicy::CounterFromOne>;
    using TransportConnectionId
        = Detail::StrongValue<Detail::TransportConnectionIdTag, Detail::StrongValuePolicy::CounterFromOne>;

    enum class EndpointHostKind
    {
        Ipv4,
        Ipv6,
        DnsName,
    };

    class ConnectionEndpoint
    {
    public:
        static constexpr std::size_t MaxHostBytes = 253;
        static constexpr std::size_t MaxLabelBytes = 63;

        static std::optional<ConnectionEndpoint> create(std::string_view host, std::uint16_t port);

        const std::string& host() const noexcept { return mHost; }
        std::uint16_t port() const noexcept { return mPort; }
        EndpointHostKind hostKind() const noexcept { return mHostKind; }
        bool requiresDns() const noexcept { return mHostKind == EndpointHostKind::DnsName; }

        friend bool operator==(const ConnectionEndpoint&, const ConnectionEndpoint&) = default;

    private:
        ConnectionEndpoint(std::string host, std::uint16_t port, EndpointHostKind hostKind)
            : mHost(std::move(host))
            , mPort(port)
            , mHostKind(hostKind)
        {
        }

        std::string mHost;
        std::uint16_t mPort;
        EndpointHostKind mHostKind;
    };

    class ListenerEndpoint
    {
    public:
        static std::optional<ListenerEndpoint> create(std::string_view numericAddress, std::uint16_t port);

        const std::string& address() const noexcept { return mAddress; }
        std::uint16_t port() const noexcept { return mPort; }
        EndpointHostKind hostKind() const noexcept { return mHostKind; }

        friend bool operator==(const ListenerEndpoint&, const ListenerEndpoint&) = default;

    private:
        ListenerEndpoint(std::string address, std::uint16_t port, EndpointHostKind hostKind)
            : mAddress(std::move(address))
            , mPort(port)
            , mHostKind(hostKind)
        {
        }

        std::string mAddress;
        std::uint16_t mPort;
        EndpointHostKind mHostKind;
    };

    struct TransportLimits
    {
        static constexpr std::size_t MaxListeners = 1;
        static constexpr std::size_t MaxPendingAttempts = 8;
        static constexpr std::size_t MaxConnections = 8;
        static constexpr std::size_t MaxRetainedEvents = 128;
        static constexpr std::size_t MaxCandidateHandlesPerAttempt = 2;
        static constexpr std::size_t MaxResolvedAddresses = 8;

        static std::optional<TransportLimits> create(std::size_t listeners, std::size_t pendingAttempts,
            std::size_t connections, std::size_t retainedEvents) noexcept;

        std::size_t listeners = MaxListeners;
        std::size_t pendingAttempts = MaxPendingAttempts;
        std::size_t connections = MaxConnections;
        std::size_t retainedEvents = MaxRetainedEvents;
    };

    enum class TransportResult
    {
        Accepted,
        InvalidInput,
        AtCapacity,
        AlreadyFinalized,
        UnknownId,
        RuntimeFailed,
        CounterExhausted,
    };

    template <class Id>
    struct TransportAdmission
    {
        TransportResult result = TransportResult::RuntimeFailed;
        std::optional<Id> id;
    };

    enum class TransportSecurity
    {
        EncryptedUnauthenticated,
    };

    enum class TransportCloseMode
    {
        Graceful,
        Abort,
    };

    enum class TransportEventKind
    {
        ListenerStarted,
        ListenerStopped,
        ConnectSucceeded,
        ConnectFailed,
        ConnectCancelled,
        ConnectionAccepted,
        ConnectionClosed,
        RuntimeFailed,
    };

    enum class TransportFailure
    {
        None,
        InvalidEndpoint,
        ResolutionNoData,
        ResolutionFailed,
        ConnectionFailed,
        PeerClosed,
        LocalClose,
        Shutdown,
        EventCapacityExceeded,
        CounterExhausted,
        DependencyFailure,
    };

    struct TransportEvent
    {
        TransportEventKind kind = TransportEventKind::RuntimeFailed;
        TransportFailure failure = TransportFailure::None;
        std::optional<ListenerId> listener;
        std::optional<ConnectAttemptId> attempt;
        std::optional<TransportConnectionId> connection;
        std::optional<ListenerEndpoint> boundEndpoint;
        TransportSecurity security = TransportSecurity::EncryptedUnauthenticated;
    };

    struct TransportPollResult
    {
        TransportResult result = TransportResult::Accepted;
        std::size_t events = 0;
    };

    class TransportRuntime
    {
    public:
        virtual ~TransportRuntime() = default;

        virtual TransportAdmission<ListenerId> startListener(const ListenerEndpoint& endpoint) = 0;
        virtual TransportResult stopListener(ListenerId listener) = 0;
        virtual TransportAdmission<ConnectAttemptId> connect(const ConnectionEndpoint& endpoint) = 0;
        virtual TransportResult cancelConnect(ConnectAttemptId attempt) = 0;
        virtual TransportResult close(TransportConnectionId connection, TransportCloseMode mode) = 0;
        virtual TransportPollResult poll(std::span<TransportEvent> output) = 0;
        virtual TransportResult shutdown() = 0;
    };
}

#endif
