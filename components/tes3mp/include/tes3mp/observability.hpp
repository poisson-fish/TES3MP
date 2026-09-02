#ifndef TES3MP_OBSERVABILITY_HPP
#define TES3MP_OBSERVABILITY_HPP

#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace TES3MP
{
    inline constexpr std::size_t MaximumMetricDimensions = 4;

    enum class ObservationResult : std::uint8_t
    {
        Accepted,
        Dropped,
    };

    enum class MetricOperation : std::uint8_t
    {
        CounterAdd,
        GaugeSet,
        DistributionObserve,
    };

    enum class MetricUnit : std::uint8_t
    {
        Count,
        Nanoseconds,
    };

    enum class MetricKey : std::uint16_t
    {
        ContractCounter = 1,
        ContractGauge = 2,
        ContractDistribution = 3,
        SessionTransitions = 0x100,
        SessionAuthenticationOutcomes = 0x101,
        SessionTimeouts = 0x102,
        SessionStaleCompletions = 0x103,
        SessionCancellations = 0x104,
        CommandIntakeOutcomes = 0x200,
        CommandIntakePending = 0x201,
        CommandReductionOutcomes = 0x202,
        CanonicalSinkDeliveries = 0x203,
    };

    struct MetricDefinition
    {
        MetricOperation operation;
        MetricUnit unit;

        friend constexpr bool operator==(MetricDefinition, MetricDefinition) noexcept = default;
    };

    constexpr std::optional<MetricDefinition> metricDefinition(MetricKey key) noexcept
    {
        switch (key)
        {
            case MetricKey::ContractCounter:
                return MetricDefinition{ MetricOperation::CounterAdd, MetricUnit::Count };
            case MetricKey::ContractGauge:
                return MetricDefinition{ MetricOperation::GaugeSet, MetricUnit::Count };
            case MetricKey::ContractDistribution:
                return MetricDefinition{ MetricOperation::DistributionObserve, MetricUnit::Nanoseconds };
            case MetricKey::SessionTransitions:
            case MetricKey::SessionAuthenticationOutcomes:
            case MetricKey::SessionTimeouts:
            case MetricKey::SessionStaleCompletions:
            case MetricKey::SessionCancellations:
                return MetricDefinition{ MetricOperation::CounterAdd, MetricUnit::Count };
            case MetricKey::CommandIntakeOutcomes:
                return MetricDefinition{ MetricOperation::CounterAdd, MetricUnit::Count };
            case MetricKey::CommandIntakePending:
                return MetricDefinition{ MetricOperation::GaugeSet, MetricUnit::Count };
            case MetricKey::CommandReductionOutcomes:
            case MetricKey::CanonicalSinkDeliveries:
                return MetricDefinition{ MetricOperation::CounterAdd, MetricUnit::Count };
        }
        return std::nullopt;
    }

    enum class MetricDimensionKey : std::uint8_t
    {
        ContractOutcome = 1,
        SessionOutcome = 2,
        CommandIntakeOutcome = 3,
        CommandReductionOutcome = 4,
        CanonicalSinkRole = 5,
        CanonicalSinkDeliveryOutcome = 6,
    };

    enum class MetricDimensionValue : std::uint8_t
    {
        Accepted = 1,
        Dropped = 2,
        TransitionAccepted = 10,
        IllegalTransition = 11,
        AuthenticationSucceeded = 12,
        AuthenticationRejected = 13,
        TimedOut = 14,
        Cancelled = 15,
        StaleCompletion = 16,
        CommandAccepted = 20,
        CommandPerSessionPendingLimit = 21,
        CommandGlobalPendingLimit = 22,
        CommandCoordinatorTerminated = 23,
        CommandDeferredByTickBudget = 24,
        CommandClockMovedBackwards = 25,
        CommandDeadlineOverflow = 26,
        CommandTickExhausted = 27,
        CommandIngressOrdinalExhausted = 28,
        CommandReductionApplied = 40,
        CommandReductionUnknownSession = 41,
        CommandReductionSessionGenerationMismatch = 42,
        CommandReductionAlreadyFinalized = 43,
        CommandReductionSequenceGap = 44,
        CommandReductionDuplicateCommandId = 45,
        CommandReductionEntityBindingMismatch = 46,
        CommandReductionEntityRevisionMismatch = 47,
        CommandReductionAuthorityEpochMismatch = 48,
        CommandReductionSpatialTickRegression = 49,
        CommandReductionEntityRevisionExhausted = 50,
        CommandReductionCommandLimitExceeded = 51,
        CommandReductionEligibleTickMismatch = 52,
        CommandReductionIngressOrdinalNotStrictlyIncreasing = 53,
        CommandReductionCandidateStateInvalid = 54,
        CommandReductionStateVersionCapacityExceeded = 55,
        CommandReductionUnknownFixtureCell = 56,
        CanonicalSinkPersistence = 60,
        CanonicalSinkReplay = 61,
        CanonicalSinkScript = 62,
        CanonicalSinkMetrics = 63,
        CanonicalSinkAccepted = 64,
        CanonicalSinkBackpressured = 65,
        CanonicalSinkFailed = 66,
    };

    struct MetricDimension
    {
        MetricDimensionKey key = MetricDimensionKey::ContractOutcome;
        MetricDimensionValue value = MetricDimensionValue::Accepted;

        friend constexpr bool operator==(MetricDimension, MetricDimension) noexcept = default;
    };

    struct CounterAddition
    {
        std::uint64_t amount = 0;

        friend constexpr bool operator==(CounterAddition, CounterAddition) noexcept = default;
    };

    struct GaugeValue
    {
        std::int64_t value = 0;

        friend constexpr bool operator==(GaugeValue, GaugeValue) noexcept = default;
    };

    struct DistributionSample
    {
        std::uint64_t value = 0;

        friend constexpr bool operator==(DistributionSample, DistributionSample) noexcept = default;
    };

    using MetricValue = std::variant<CounterAddition, GaugeValue, DistributionSample>;

    class MetricObservation
    {
    public:
        static std::optional<MetricObservation> create(
            MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions = {}) noexcept;

        constexpr MetricKey key() const noexcept { return mKey; }
        constexpr MetricOperation operation() const noexcept { return metricDefinition(mKey)->operation; }
        constexpr MetricUnit unit() const noexcept { return metricDefinition(mKey)->unit; }
        constexpr const MetricValue& value() const noexcept { return mValue; }
        constexpr std::span<const MetricDimension> dimensions() const noexcept
        {
            return { mDimensions.data(), mDimensionCount };
        }

        friend constexpr bool operator==(const MetricObservation&, const MetricObservation&) noexcept = default;

    private:
        MetricObservation(MetricKey key, MetricValue value, std::span<const MetricDimension> dimensions) noexcept;

        MetricKey mKey;
        MetricValue mValue;
        std::array<MetricDimension, MaximumMetricDimensions> mDimensions{};
        std::size_t mDimensionCount = 0;
    };

    enum class EventSeverity : std::uint8_t
    {
        Debug,
        Info,
        Warning,
        Error,
    };

    enum class EventKind : std::uint16_t
    {
        ContractObservation = 1,
        SessionLifecycle = 2,
        CommandIntake = 3,
        CommandReduction = 4,
        CanonicalSinkDelivery = 5,
    };

    struct ContractObservationEvent
    {
        ObservationResult result = ObservationResult::Accepted;

        friend constexpr bool operator==(ContractObservationEvent, ContractObservationEvent) noexcept = default;
    };

    enum class SessionObservationRole : std::uint8_t
    {
        Client,
        Server,
    };

    enum class SessionObservationOutcome : std::uint8_t
    {
        TransitionAccepted,
        IllegalTransition,
        AuthenticationSucceeded,
        AuthenticationRejected,
        TimedOut,
        Cancelled,
        StaleCompletion,
    };

    enum class SessionObservationStage : std::uint8_t
    {
        TransportAndNegotiation,
        AuthenticationInput,
        AuthenticationProvider,
        Terminal,
    };

    struct SessionLifecycleEvent
    {
        SessionObservationRole role;
        SessionObservationOutcome outcome;
        SessionObservationStage stage;

        friend constexpr bool operator==(SessionLifecycleEvent, SessionLifecycleEvent) noexcept = default;
    };

    enum class CommandIntakeObservationOutcome : std::uint8_t
    {
        Accepted,
        PerSessionPendingLimit,
        GlobalPendingLimit,
        CoordinatorTerminated,
        DeferredByTickBudget,
        ClockMovedBackwards,
        DeadlineOverflow,
        TickExhausted,
        IngressOrdinalExhausted,
    };

    struct CommandIntakeEvent
    {
        CommandIntakeObservationOutcome outcome;
        std::uint64_t pendingCommands;

        friend constexpr bool operator==(CommandIntakeEvent, CommandIntakeEvent) noexcept = default;
    };

    enum class CommandReductionObservationOutcome : std::uint8_t
    {
        Applied,
        UnknownSession,
        SessionGenerationMismatch,
        AlreadyFinalized,
        SequenceGap,
        DuplicateCommandId,
        EntityBindingMismatch,
        EntityRevisionMismatch,
        AuthorityEpochMismatch,
        SpatialTickRegression,
        EntityRevisionExhausted,
        CommandLimitExceeded,
        EligibleTickMismatch,
        IngressOrdinalNotStrictlyIncreasing,
        CandidateStateInvalid,
        StateVersionCapacityExceeded,
        UnknownFixtureCell,
    };

    struct CommandReductionEvent
    {
        CommandReductionObservationOutcome outcome;
        std::uint64_t processedCommands;

        friend constexpr bool operator==(CommandReductionEvent, CommandReductionEvent) noexcept = default;
    };

    enum class CanonicalSinkObservationRole : std::uint8_t
    {
        Persistence,
        Replay,
        Script,
        Metrics,
    };

    enum class CanonicalSinkObservationOutcome : std::uint8_t
    {
        Accepted,
        Backpressured,
        Failed,
    };

    struct CanonicalSinkDeliveryEvent
    {
        CanonicalSinkObservationRole role;
        CanonicalSinkObservationOutcome outcome;

        friend constexpr bool operator==(CanonicalSinkDeliveryEvent, CanonicalSinkDeliveryEvent) noexcept = default;
    };

    using StructuredEventPayload = std::variant<ContractObservationEvent, SessionLifecycleEvent, CommandIntakeEvent,
        CommandReductionEvent, CanonicalSinkDeliveryEvent>;

    class StructuredEvent
    {
    public:
        static std::optional<StructuredEvent> create(
            EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept;

        constexpr EventKind kind() const noexcept
        {
            if (std::holds_alternative<ContractObservationEvent>(mPayload))
                return EventKind::ContractObservation;
            if (std::holds_alternative<SessionLifecycleEvent>(mPayload))
                return EventKind::SessionLifecycle;
            if (std::holds_alternative<CommandIntakeEvent>(mPayload))
                return EventKind::CommandIntake;
            if (std::holds_alternative<CommandReductionEvent>(mPayload))
                return EventKind::CommandReduction;
            return EventKind::CanonicalSinkDelivery;
        }
        constexpr EventSeverity severity() const noexcept { return mSeverity; }
        constexpr std::optional<ServerTick> tick() const noexcept { return mTick; }
        constexpr const StructuredEventPayload& payload() const noexcept { return mPayload; }

        friend constexpr bool operator==(const StructuredEvent&, const StructuredEvent&) noexcept = default;

    private:
        StructuredEvent(
            EventSeverity severity, std::optional<ServerTick> tick, StructuredEventPayload payload) noexcept;

        EventSeverity mSeverity;
        std::optional<ServerTick> mTick;
        StructuredEventPayload mPayload;
    };

    class MetricSink
    {
    public:
        virtual ~MetricSink() = default;
        virtual ObservationResult tryRecord(const MetricObservation& observation) noexcept = 0;
    };

    class StructuredEventSink
    {
    public:
        virtual ~StructuredEventSink() = default;
        virtual ObservationResult tryRecord(const StructuredEvent& event) noexcept = 0;
    };

    class NullMetricSink final : public MetricSink
    {
    public:
        ObservationResult tryRecord(const MetricObservation&) noexcept override { return ObservationResult::Accepted; }
    };

    class NullStructuredEventSink final : public StructuredEventSink
    {
    public:
        ObservationResult tryRecord(const StructuredEvent&) noexcept override { return ObservationResult::Accepted; }
    };

    class Observability
    {
    public:
        constexpr Observability(MetricSink& metrics, StructuredEventSink& events) noexcept
            : mMetrics(metrics)
            , mEvents(events)
        {
        }

        constexpr MetricSink& metrics() const noexcept { return mMetrics; }
        constexpr StructuredEventSink& events() const noexcept { return mEvents; }

    private:
        MetricSink& mMetrics;
        StructuredEventSink& mEvents;
    };
}

#endif
