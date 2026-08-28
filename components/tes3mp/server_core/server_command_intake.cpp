#include <tes3mp/server_command_intake.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace
{
    TES3MP::MetricDimensionValue metricValue(TES3MP::CommandIntakeObservationOutcome outcome) noexcept
    {
        using TES3MP::CommandIntakeObservationOutcome;
        using TES3MP::MetricDimensionValue;
        switch (outcome)
        {
            case CommandIntakeObservationOutcome::Accepted:
                return MetricDimensionValue::CommandAccepted;
            case CommandIntakeObservationOutcome::PerSessionPendingLimit:
                return MetricDimensionValue::CommandPerSessionPendingLimit;
            case CommandIntakeObservationOutcome::GlobalPendingLimit:
                return MetricDimensionValue::CommandGlobalPendingLimit;
            case CommandIntakeObservationOutcome::CoordinatorTerminated:
                return MetricDimensionValue::CommandCoordinatorTerminated;
            case CommandIntakeObservationOutcome::DeferredByTickBudget:
                return MetricDimensionValue::CommandDeferredByTickBudget;
            case CommandIntakeObservationOutcome::ClockMovedBackwards:
                return MetricDimensionValue::CommandClockMovedBackwards;
            case CommandIntakeObservationOutcome::DeadlineOverflow:
                return MetricDimensionValue::CommandDeadlineOverflow;
            case CommandIntakeObservationOutcome::TickExhausted:
                return MetricDimensionValue::CommandTickExhausted;
            case CommandIntakeObservationOutcome::IngressOrdinalExhausted:
                return MetricDimensionValue::CommandIngressOrdinalExhausted;
        }
        return MetricDimensionValue::CommandCoordinatorTerminated;
    }

    TES3MP::ServerCommandPumpError pumpError(TES3MP::SchedulerError error) noexcept
    {
        using TES3MP::SchedulerError;
        using TES3MP::ServerCommandPumpError;
        switch (error)
        {
            case SchedulerError::None:
                return ServerCommandPumpError::None;
            case SchedulerError::ClockMovedBackwards:
                return ServerCommandPumpError::ClockMovedBackwards;
            case SchedulerError::DeadlineOverflow:
                return ServerCommandPumpError::DeadlineOverflow;
            case SchedulerError::TickExhausted:
                return ServerCommandPumpError::TickExhausted;
        }
        return ServerCommandPumpError::TickExhausted;
    }

    TES3MP::CommandIntakeObservationOutcome observationOutcome(TES3MP::SchedulerError error) noexcept
    {
        using TES3MP::CommandIntakeObservationOutcome;
        using TES3MP::SchedulerError;
        switch (error)
        {
            case SchedulerError::ClockMovedBackwards:
                return CommandIntakeObservationOutcome::ClockMovedBackwards;
            case SchedulerError::DeadlineOverflow:
                return CommandIntakeObservationOutcome::DeadlineOverflow;
            case SchedulerError::TickExhausted:
                return CommandIntakeObservationOutcome::TickExhausted;
            case SchedulerError::None:
                return CommandIntakeObservationOutcome::CoordinatorTerminated;
        }
        return CommandIntakeObservationOutcome::CoordinatorTerminated;
    }
}

namespace TES3MP
{
    ServerCommandIntakeCoordinator::ServerCommandIntakeCoordinator(const MonotonicClock& clock,
        Observability& observability, MonotonicInstant epoch, ServerTick nextTick, IngressOrdinal nextIngressOrdinal)
        : mObservability(observability)
        , mScheduler(clock, epoch, nextTick)
        , mNextIngressOrdinal(nextIngressOrdinal)
    {
        mPending.reserve(MaximumPendingServerCommands);
    }

    std::size_t ServerCommandIntakeCoordinator::pendingFor(
        SessionId sessionId, SessionGeneration generation) const noexcept
    {
        return static_cast<std::size_t>(std::count_if(
            mPending.begin(), mPending.end(), [sessionId, generation](const ServerCommandProposal& proposal) {
                return proposal.sessionId() == sessionId && proposal.sessionGeneration() == generation;
            }));
    }

    void ServerCommandIntakeCoordinator::observeOutcome(
        CommandIntakeObservationOutcome outcome, std::uint64_t amount) noexcept
    {
        const std::array dimensions{
            MetricDimension{ MetricDimensionKey::CommandIntakeOutcome, metricValue(outcome) },
        };
        if (const auto metric
            = MetricObservation::create(MetricKey::CommandIntakeOutcomes, CounterAddition{ amount }, dimensions))
            (void)mObservability.metrics().tryRecord(*metric);
    }

    void ServerCommandIntakeCoordinator::observePending() noexcept
    {
        if (const auto metric = MetricObservation::create(
                MetricKey::CommandIntakePending, GaugeValue{ static_cast<std::int64_t>(mPending.size()) }))
            (void)mObservability.metrics().tryRecord(*metric);
    }

    void ServerCommandIntakeCoordinator::observeEvent(
        CommandIntakeObservationOutcome outcome, EventSeverity severity) noexcept
    {
        if (const auto event = StructuredEvent::create(
                severity, std::nullopt, CommandIntakeEvent{ outcome, static_cast<std::uint64_t>(mPending.size()) }))
            (void)mObservability.events().tryRecord(*event);
    }

    CommandSubmissionResult ServerCommandIntakeCoordinator::submit(ServerCommandProposal proposal)
    {
        if (mTerminalError != ServerCommandPumpError::None || !mNextIngressOrdinal)
        {
            if (mTerminalError == ServerCommandPumpError::None)
            {
                mTerminalError = ServerCommandPumpError::IngressOrdinalExhausted;
                observeOutcome(CommandIntakeObservationOutcome::IngressOrdinalExhausted);
                observeEvent(CommandIntakeObservationOutcome::IngressOrdinalExhausted, EventSeverity::Error);
            }
            observeOutcome(CommandIntakeObservationOutcome::CoordinatorTerminated);
            return CommandSubmissionResult::CoordinatorTerminated;
        }

        if (pendingFor(proposal.sessionId(), proposal.sessionGeneration())
            >= MaximumPendingServerCommandsPerSessionGeneration)
        {
            observeOutcome(CommandIntakeObservationOutcome::PerSessionPendingLimit);
            observeEvent(CommandIntakeObservationOutcome::PerSessionPendingLimit, EventSeverity::Warning);
            return CommandSubmissionResult::PerSessionPendingLimit;
        }

        if (mPending.size() >= MaximumPendingServerCommands)
        {
            observeOutcome(CommandIntakeObservationOutcome::GlobalPendingLimit);
            observeEvent(CommandIntakeObservationOutcome::GlobalPendingLimit, EventSeverity::Warning);
            return CommandSubmissionResult::GlobalPendingLimit;
        }

        mPending.push_back(proposal);
        observeOutcome(CommandIntakeObservationOutcome::Accepted);
        observePending();
        return CommandSubmissionResult::Accepted;
    }

    ServerCommandPumpResult ServerCommandIntakeCoordinator::terminate(
        ServerCommandPumpError error, CommandIntakeObservationOutcome outcome, std::uint64_t dueTickLag) noexcept
    {
        mTerminalError = error;
        observeOutcome(outcome);
        observeEvent(outcome, EventSeverity::Error);
        ServerCommandPumpResult result;
        result.mError = error;
        result.mDueTickLag = dueTickLag;
        return result;
    }

    ServerCommandPumpResult ServerCommandIntakeCoordinator::pump()
    {
        if (mTerminalError != ServerCommandPumpError::None)
        {
            ServerCommandPumpResult result;
            result.mError = mTerminalError;
            return result;
        }

        const auto scheduled = mScheduler.pump();
        if (!scheduled)
            return terminate(
                pumpError(scheduled.error()), observationOutcome(scheduled.error()), scheduled.dueTickLag());

        ServerCommandPumpResult result;
        result.mDueTickLag = scheduled.dueTickLag();
        if (scheduled.ticks().empty())
            return result;

        const std::size_t maximumAdmitted = scheduled.ticks().size() * MaximumServerCommandsPerTick;
        const std::size_t admittedCount = std::min(mPending.size(), maximumAdmitted);
        if (admittedCount != 0)
        {
            if (!mNextIngressOrdinal)
                return terminate(ServerCommandPumpError::IngressOrdinalExhausted,
                    CommandIntakeObservationOutcome::IngressOrdinalExhausted, scheduled.dueTickLag());
            const std::uint64_t available
                = std::numeric_limits<std::uint64_t>::max() - mNextIngressOrdinal->value() + 1;
            if (admittedCount > available)
                return terminate(ServerCommandPumpError::IngressOrdinalExhausted,
                    CommandIntakeObservationOutcome::IngressOrdinalExhausted, scheduled.dueTickLag());
        }

        result.mBatches.reserve(scheduled.ticks().size());
        std::size_t sourceIndex = 0;
        std::optional<IngressOrdinal> nextOrdinal = mNextIngressOrdinal;
        for (const auto tick : scheduled.ticks())
        {
            const std::size_t remaining = admittedCount - sourceIndex;
            const std::size_t batchSize = std::min(remaining, MaximumServerCommandsPerTick);
            std::vector<StampedServerCommand> commands;
            commands.reserve(batchSize);
            for (std::size_t index = 0; index < batchSize; ++index)
            {
                const auto ordinal = *nextOrdinal;
                commands.push_back(
                    StampedServerCommand(WriterAdmissionStamp(tick.value(), ordinal), mPending[sourceIndex++]));
                nextOrdinal = ordinal.next();
            }
            if (remaining > MaximumServerCommandsPerTick)
                observeOutcome(CommandIntakeObservationOutcome::DeferredByTickBudget,
                    static_cast<std::uint64_t>(remaining - MaximumServerCommandsPerTick));
            result.mBatches.push_back(ServerTickCommandBatch(tick, std::move(commands)));
        }

        mNextIngressOrdinal = nextOrdinal;
        mPending.erase(mPending.begin(), mPending.begin() + static_cast<std::ptrdiff_t>(admittedCount));
        observePending();
        return result;
    }
}
