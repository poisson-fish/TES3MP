#ifndef TES3MP_SERVER_COMMAND_INTAKE_HPP
#define TES3MP_SERVER_COMMAND_INTAKE_HPP

#include "command_primitives.hpp"
#include "fixed_tick_scheduler.hpp"
#include "observability.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumPendingServerCommands = 4096;
    inline constexpr std::size_t MaximumPendingServerCommandsPerSessionGeneration = 128;
    inline constexpr std::size_t MaximumServerCommandsPerTick = 1024;

    class PlayerMotionCommandProposal
    {
    public:
        constexpr explicit PlayerMotionCommandProposal(LinearVelocity3 desiredVelocity) noexcept
            : mDesiredVelocity(desiredVelocity)
        {
        }

        constexpr LinearVelocity3 desiredVelocity() const noexcept { return mDesiredVelocity; }

        friend constexpr bool operator==(PlayerMotionCommandProposal, PlayerMotionCommandProposal) noexcept = default;

    private:
        LinearVelocity3 mDesiredVelocity;
    };

    class FixtureCellTransitionCommandProposal
    {
    public:
        constexpr explicit FixtureCellTransitionCommandProposal(CellId requestedCell) noexcept
            : mRequestedCell(requestedCell)
        {
        }

        constexpr const CellId& requestedCell() const noexcept { return mRequestedCell; }

        friend constexpr bool operator==(const FixtureCellTransitionCommandProposal&,
            const FixtureCellTransitionCommandProposal&) noexcept = default;

    private:
        CellId mRequestedCell;
    };

    using ServerCommandPayload = std::variant<PlayerMotionCommandProposal, FixtureCellTransitionCommandProposal>;

    class ServerCommandProposal
    {
    public:
        constexpr ServerCommandProposal(SessionId sessionId, SessionGeneration sessionGeneration,
            CommandSequence commandSequence, CommandId commandId, CanonicalRevision observedCanonicalRevision,
            EntityPrecondition entityPrecondition, PlayerMotionCommandProposal motion) noexcept
            : mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mObservedCanonicalRevision(observedCanonicalRevision)
            , mEntityPrecondition(entityPrecondition)
            , mPayload(motion)
        {
        }

        constexpr ServerCommandProposal(SessionId sessionId, SessionGeneration sessionGeneration,
            CommandSequence commandSequence, CommandId commandId, CanonicalRevision observedCanonicalRevision,
            EntityPrecondition entityPrecondition, FixtureCellTransitionCommandProposal transition) noexcept
            : mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mObservedCanonicalRevision(observedCanonicalRevision)
            , mEntityPrecondition(entityPrecondition)
            , mPayload(transition)
        {
        }

        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr CommandSequence commandSequence() const noexcept { return mCommandSequence; }
        constexpr CommandId commandId() const noexcept { return mCommandId; }
        constexpr CanonicalRevision observedCanonicalRevision() const noexcept { return mObservedCanonicalRevision; }
        constexpr EntityPrecondition entityPrecondition() const noexcept { return mEntityPrecondition; }
        constexpr const ServerCommandPayload& payload() const noexcept { return mPayload; }

        friend constexpr bool operator==(ServerCommandProposal, ServerCommandProposal) noexcept = default;

    private:
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        CommandSequence mCommandSequence;
        CommandId mCommandId;
        CanonicalRevision mObservedCanonicalRevision;
        EntityPrecondition mEntityPrecondition;
        ServerCommandPayload mPayload;
    };

    enum class CommandSubmissionResult : std::uint8_t
    {
        Accepted,
        PerSessionPendingLimit,
        GlobalPendingLimit,
        CoordinatorTerminated,
    };

    enum class ServerCommandPumpError : std::uint8_t
    {
        None,
        ClockMovedBackwards,
        DeadlineOverflow,
        TickExhausted,
        IngressOrdinalExhausted,
    };

    class StampedServerCommand
    {
    public:
        constexpr WriterAdmissionStamp stamp() const noexcept { return mStamp; }
        constexpr const ServerCommandProposal& proposal() const noexcept { return mProposal; }

        friend constexpr bool operator==(const StampedServerCommand&, const StampedServerCommand&) noexcept = default;

    private:
        friend class ServerCommandIntakeCoordinator;

        constexpr StampedServerCommand(WriterAdmissionStamp stamp, ServerCommandProposal proposal) noexcept
            : mStamp(stamp)
            , mProposal(proposal)
        {
        }

        WriterAdmissionStamp mStamp;
        ServerCommandProposal mProposal;
    };

    class ServerTickCommandBatch
    {
    public:
        constexpr ScheduledTick scheduledTick() const noexcept { return mScheduledTick; }
        std::span<const StampedServerCommand> commands() const noexcept { return mCommands; }

        friend bool operator==(const ServerTickCommandBatch&, const ServerTickCommandBatch&) noexcept = default;

    private:
        friend class ServerCommandIntakeCoordinator;

        ServerTickCommandBatch(ScheduledTick scheduledTick, std::vector<StampedServerCommand> commands) noexcept
            : mScheduledTick(scheduledTick)
            , mCommands(std::move(commands))
        {
        }

        ScheduledTick mScheduledTick;
        std::vector<StampedServerCommand> mCommands;
    };

    class ServerCommandPumpResult
    {
    public:
        constexpr ServerCommandPumpError error() const noexcept { return mError; }
        constexpr std::uint64_t dueTickLag() const noexcept { return mDueTickLag; }
        std::span<const ServerTickCommandBatch> batches() const noexcept { return mBatches; }
        constexpr explicit operator bool() const noexcept { return mError == ServerCommandPumpError::None; }

        friend bool operator==(const ServerCommandPumpResult&, const ServerCommandPumpResult&) noexcept = default;

    private:
        friend class ServerCommandIntakeCoordinator;

        ServerCommandPumpError mError = ServerCommandPumpError::None;
        std::uint64_t mDueTickLag = 0;
        std::vector<ServerTickCommandBatch> mBatches;
    };

    class ServerCommandIntakeCoordinator
    {
    public:
        ServerCommandIntakeCoordinator(const MonotonicClock& clock, Observability& observability,
            MonotonicInstant epoch, ServerTick nextTick, IngressOrdinal nextIngressOrdinal);

        ServerCommandIntakeCoordinator(const ServerCommandIntakeCoordinator&) = delete;
        ServerCommandIntakeCoordinator& operator=(const ServerCommandIntakeCoordinator&) = delete;
        ServerCommandIntakeCoordinator(ServerCommandIntakeCoordinator&&) = delete;
        ServerCommandIntakeCoordinator& operator=(ServerCommandIntakeCoordinator&&) = delete;

        CommandSubmissionResult submit(ServerCommandProposal proposal);
        ServerCommandPumpResult pump();

        std::size_t pendingCount() const noexcept { return mPending.size(); }
        ServerTick nextTick() const noexcept { return mScheduler.nextTick(); }
        std::optional<IngressOrdinal> nextIngressOrdinal() const noexcept { return mNextIngressOrdinal; }
        ServerCommandPumpError terminalError() const noexcept { return mTerminalError; }

    private:
        std::size_t pendingFor(SessionId sessionId, SessionGeneration generation) const noexcept;
        void observeOutcome(CommandIntakeObservationOutcome outcome, std::uint64_t amount = 1) noexcept;
        void observePending() noexcept;
        void observeEvent(CommandIntakeObservationOutcome outcome, EventSeverity severity) noexcept;
        ServerCommandPumpResult terminate(
            ServerCommandPumpError error, CommandIntakeObservationOutcome outcome, std::uint64_t dueTickLag) noexcept;

        Observability& mObservability;
        FixedTickScheduler mScheduler;
        std::vector<ServerCommandProposal> mPending;
        std::optional<IngressOrdinal> mNextIngressOrdinal;
        ServerCommandPumpError mTerminalError = ServerCommandPumpError::None;
    };
}

#endif
