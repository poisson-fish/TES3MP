#ifndef TES3MP_TRANSPORT_HPP
#define TES3MP_TRANSPORT_HPP

#include "admission_scope.hpp"
#include "protocol_frame.hpp"
#include "strong_value.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
        MessageTooLarge,
        WouldBlock,
        NotReady,
        ProtocolViolation,
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
        LocalAbort,
        TimedOut,
        AuthenticationDenied,
        AuthenticationTemporarilyUnavailable,
        SlowPeer,
        CapacityExhausted,
        SecuritySetupFailed,
        Shutdown,
        EventCapacityExceeded,
        CounterExhausted,
        DependencyFailure,
        InvalidMessage,
    };

    enum class TransportChannel : std::uint8_t;

    enum class StableNetworkReason : std::uint8_t
    {
        None,
        LocalGracefulClose,
        LocalAbort,
        PeerClosed,
        TimedOut,
        AuthenticationDenied,
        AuthenticationTemporarilyUnavailable,
        ProtocolViolation,
        SlowPeer,
        CapacityExhausted,
        NameResolutionFailed,
        SecuritySetupFailed,
        TransportDependencyFailed,
        RuntimeShutdown,
    };

    StableNetworkReason stableNetworkReason(TransportFailure failure) noexcept;
    std::uint64_t saturatingTelemetryAdd(std::uint64_t value, std::uint64_t amount) noexcept;

    enum class TransportTelemetryKind : std::uint8_t
    {
        Submitted,
        Admitted,
        Received,
        Coalesced,
        Rejected,
        WouldBlock,
        Evicted,
        QueuedMessages,
        QueuedBytes,
        PendingBytes,
        UnacknowledgedBytes,
    };

    enum class TransportTelemetryDirection : std::uint8_t
    {
        Outbound,
        Inbound,
    };

    struct TransportTelemetryObservation
    {
        TransportTelemetryKind kind;
        TransportTelemetryDirection direction;
        TransportChannel channel;
        std::uint64_t value;
        StableNetworkReason reason = StableNetworkReason::None;

        friend constexpr bool operator==(TransportTelemetryObservation, TransportTelemetryObservation) noexcept
            = default;
    };

    enum class TransportTelemetryResult : std::uint8_t
    {
        Accepted,
        Dropped,
    };

    class TransportTelemetrySink
    {
    public:
        virtual ~TransportTelemetrySink() = default;
        virtual TransportTelemetryResult tryRecord(const TransportTelemetryObservation& observation) noexcept = 0;
    };

    class NullTransportTelemetrySink final : public TransportTelemetrySink
    {
    public:
        TransportTelemetryResult tryRecord(const TransportTelemetryObservation&) noexcept override
        {
            return TransportTelemetryResult::Accepted;
        }
    };

    enum class TransportChannel : std::uint8_t
    {
        ReliableOrdered = 1,
        LatestWins = 2,
    };

    inline constexpr std::size_t ReliableOrderedMaximumMessageBytes
        = ProtocolFrameHeaderBytes + ReliableOperationMaximumPayloadBytes;
    inline constexpr std::size_t LatestWinsMaximumMessageBytes
        = ProtocolFrameHeaderBytes + LatestWinsSnapshotMaximumPayloadBytes;

    std::optional<TransportChannel> transportChannelFor(MessageClass messageClass) noexcept;
    std::optional<std::size_t> maximumTransportMessageBytes(TransportChannel channel) noexcept;
    bool isMessageClassAllowedOnTransportChannel(MessageClass messageClass, TransportChannel channel) noexcept;

    struct TransportMessage
    {
        TransportChannel channel = TransportChannel::ReliableOrdered;
        std::vector<std::byte> bytes;
    };

    struct TransportReceiveResult
    {
        TransportResult result = TransportResult::Accepted;
        std::size_t messages = 0;
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
        std::optional<AdmissionScopeId> admissionScope;
    };

    struct TransportPollResult
    {
        TransportResult result = TransportResult::Accepted;
        std::size_t events = 0;
    };

    class TransportRuntime
    {
    public:
        static constexpr std::size_t MaxMessagesPerReceive = 128;

        virtual ~TransportRuntime() = default;

        virtual TransportAdmission<ListenerId> startListener(const ListenerEndpoint& endpoint) = 0;
        virtual TransportResult stopListener(ListenerId listener) = 0;
        virtual TransportAdmission<ConnectAttemptId> connect(const ConnectionEndpoint& endpoint) = 0;
        virtual TransportResult cancelConnect(ConnectAttemptId attempt) = 0;
        virtual TransportResult send(
            TransportConnectionId connection, TransportChannel channel, std::span<const std::byte> message)
            = 0;
        virtual TransportReceiveResult receive(TransportConnectionId connection, std::span<TransportMessage> output)
            = 0;
        virtual TransportResult close(TransportConnectionId connection, TransportCloseMode mode) = 0;
        virtual TransportPollResult poll(std::span<TransportEvent> output) = 0;
        virtual TransportResult shutdown() = 0;
    };

    struct OutboundQueuePolicy
    {
        static constexpr std::size_t MaxConnections = 256;
        static constexpr std::size_t MaxReliableMessages = 256;
        static constexpr std::size_t MaxReliableBytes = 4 * 1024 * 1024;
        static constexpr std::size_t MaxSendAttemptsPerPump = 32;

        static std::optional<OutboundQueuePolicy> create(std::size_t reliableMessages, std::size_t reliableBytes,
            std::size_t sendAttemptsPerPump, std::size_t reliableBurstBeforeLatest, std::size_t reliableRateBurst,
            std::uint64_t reliableRefillMilliseconds, std::size_t latestRateBurst,
            std::uint64_t latestRefillMilliseconds, std::size_t consecutiveBlockLimit,
            std::uint64_t blockedDeadlineMilliseconds) noexcept;

        std::size_t reliableMessages = MaxReliableMessages;
        std::size_t reliableBytes = MaxReliableBytes;
        std::size_t sendAttemptsPerPump = MaxSendAttemptsPerPump;
        std::size_t reliableBurstBeforeLatest = 4;
        std::size_t reliableRateBurst = MaxSendAttemptsPerPump;
        std::uint64_t reliableRefillMilliseconds = 1;
        std::size_t latestRateBurst = 1;
        std::uint64_t latestRefillMilliseconds = 1;
        std::size_t consecutiveBlockLimit = 8;
        std::uint64_t blockedDeadlineMilliseconds = 1000;
    };

    enum class OutboundPumpResult
    {
        Progress,
        Idle,
        Blocked,
        SlowPeerEvicted,
        TransportFailed,
        InvalidTime,
    };

    class OutboundTransportQueue
    {
    public:
        explicit OutboundTransportQueue(OutboundQueuePolicy policy);
        OutboundTransportQueue(OutboundQueuePolicy policy, TransportTelemetrySink& telemetry);

        TransportResult enqueue(TransportChannel channel, std::span<const std::byte> message);
        TransportResult enqueuePair(TransportChannel firstChannel, std::span<const std::byte> first,
            TransportChannel secondChannel, std::span<const std::byte> second);
        OutboundPumpResult pump(
            TransportRuntime& runtime, TransportConnectionId connection, std::uint64_t nowMilliseconds);
        void clear() noexcept;

        std::size_t reliableMessages() const noexcept { return mReliable.size(); }
        std::size_t reliableBytes() const noexcept { return mReliableBytes; }
        bool hasLatest() const noexcept { return mLatest.has_value(); }

    private:
        struct RateBucket
        {
            std::size_t tokens = 0;
            std::uint64_t lastRefill = 0;
            bool initialized = false;
        };

        void refill(RateBucket& bucket, std::size_t burst, std::uint64_t interval, std::uint64_t now) noexcept;
        bool shouldEvict(std::uint64_t now) const noexcept;
        void count(TransportTelemetryKind kind, TransportChannel channel, std::uint64_t amount = 1,
            StableNetworkReason reason = StableNetworkReason::None) noexcept;
        void gauge(TransportTelemetryKind kind, TransportChannel channel, std::uint64_t value) noexcept;
        void queueGauges() noexcept;

        OutboundQueuePolicy mPolicy;
        std::deque<std::vector<std::byte>> mReliable;
        std::optional<std::vector<std::byte>> mLatest;
        std::size_t mReliableBytes = 0;
        RateBucket mReliableRate;
        RateBucket mLatestRate;
        std::optional<std::uint64_t> mFirstReliableBlock;
        std::size_t mConsecutiveReliableBlocks = 0;
        std::optional<std::uint64_t> mLastPumpTime;
        TransportTelemetrySink* mTelemetry = nullptr;
        std::array<std::array<std::uint64_t, 2>, 7> mCounters{};
    };

    class OutboundQueueSet
    {
    public:
        static std::optional<OutboundQueueSet> create(OutboundQueuePolicy policy, std::size_t connections);
        static std::optional<OutboundQueueSet> create(
            OutboundQueuePolicy policy, std::size_t connections, TransportTelemetrySink& telemetry);
        TransportResult attach(TransportConnectionId connection);
        TransportResult detach(TransportConnectionId connection) noexcept;
        TransportResult enqueue(
            TransportConnectionId connection, TransportChannel channel, std::span<const std::byte> message);
        TransportResult enqueuePair(TransportConnectionId connection, TransportChannel firstChannel,
            std::span<const std::byte> first, TransportChannel secondChannel, std::span<const std::byte> second);
        std::optional<OutboundPumpResult> pump(
            TransportRuntime& runtime, TransportConnectionId connection, std::uint64_t nowMilliseconds);
        std::size_t connections() const noexcept { return mQueues.size(); }

    private:
        OutboundQueueSet(OutboundQueuePolicy policy, std::size_t connections, TransportTelemetrySink* telemetry)
            : mPolicy(policy)
            , mConnectionLimit(connections)
            , mTelemetry(telemetry)
        {
        }

        OutboundQueuePolicy mPolicy;
        std::size_t mConnectionLimit;
        TransportTelemetrySink* mTelemetry;
        std::map<TransportConnectionId, OutboundTransportQueue> mQueues;
    };
}

#endif
