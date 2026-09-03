#ifndef TES3MP_SERVER_COMMAND_REDUCER_HPP
#define TES3MP_SERVER_COMMAND_REDUCER_HPP

#include "canonical_publication.hpp"
#include "canonical_sinks.hpp"
#include "observability.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace TES3MP
{
    enum class CommandBatchReductionError : std::uint8_t
    {
        None,
        CommandLimitExceeded,
        EligibleTickMismatch,
        IngressOrdinalNotStrictlyIncreasing,
        StateVersionCapacityExceeded,
        CandidateStateInvalid,
        SpatialIntegrationOverflow,
        SpatialRevisionExhausted,
    };

    class CommandDispositionRecord
    {
    public:
        constexpr CommandDispositionRecord(WriterAdmissionStamp stamp, SessionId sessionId,
            SessionGeneration sessionGeneration, CommandSequence commandSequence, CommandId commandId,
            CommandDisposition disposition, bool acknowledgementAdvanced, bool playerStateChanged) noexcept
            : mStamp(stamp)
            , mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mDisposition(disposition)
            , mAcknowledgementAdvanced(acknowledgementAdvanced)
            , mPlayerStateChanged(playerStateChanged)
        {
        }

        constexpr WriterAdmissionStamp stamp() const noexcept { return mStamp; }
        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr CommandSequence commandSequence() const noexcept { return mCommandSequence; }
        constexpr CommandId commandId() const noexcept { return mCommandId; }
        constexpr CommandDisposition disposition() const noexcept { return mDisposition; }
        constexpr bool acknowledgementAdvanced() const noexcept { return mAcknowledgementAdvanced; }
        constexpr bool playerStateChanged() const noexcept { return mPlayerStateChanged; }

        friend constexpr bool operator==(CommandDispositionRecord, CommandDispositionRecord) noexcept = default;

    private:
        WriterAdmissionStamp mStamp;
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        CommandSequence mCommandSequence;
        CommandId mCommandId;
        CommandDisposition mDisposition;
        bool mAcknowledgementAdvanced;
        bool mPlayerStateChanged;
    };

    class CommandBatchReductionResult
    {
    public:
        constexpr CommandBatchReductionError error() const noexcept { return mError; }
        std::span<const CommandDispositionRecord> dispositions() const noexcept { return mDispositions; }
        constexpr CanonicalSinkDeliveryReport sinkDeliveryReport() const noexcept { return mSinkDeliveryReport; }
        constexpr explicit operator bool() const noexcept { return mError == CommandBatchReductionError::None; }

        friend bool operator==(const CommandBatchReductionResult&, const CommandBatchReductionResult&) noexcept
            = default;

    private:
        friend class CanonicalCommandReducer;

        CommandBatchReductionError mError = CommandBatchReductionError::None;
        std::vector<CommandDispositionRecord> mDispositions;
        CanonicalSinkDeliveryReport mSinkDeliveryReport;
    };

    class CanonicalCommandReducer
    {
    public:
        class PreparedBatch
        {
        public:
            PreparedBatch(PreparedBatch&&) noexcept = default;
            PreparedBatch& operator=(PreparedBatch&&) noexcept = default;
            PreparedBatch(const PreparedBatch&) = delete;
            PreparedBatch& operator=(const PreparedBatch&) = delete;
            const CanonicalServerState& candidateState() const noexcept { return *mState; }
            CanonicalRevision candidateRevision() const noexcept { return mCanonicalRevision; }
            const CommandBatchReductionResult& result() const noexcept { return mResult; }
        private:
            friend class CanonicalCommandReducer;
            PreparedBatch() = default;
            CanonicalStateVersion mBaseVersion = CanonicalStateVersion::initial();
            CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
            CanonicalRevision mBaseCanonicalRevision = CanonicalRevision::initial();
            CanonicalRevision mCanonicalRevision = CanonicalRevision::initial();
            ServerTick mCheckpointTick = ServerTick::initial();
            std::shared_ptr<const CanonicalServerState> mState;
            std::shared_ptr<CanonicalStatePublication> mPublication;
            CommandBatchReductionResult mResult;
        };

        class PreparedJoin
        {
        public:
            PreparedJoin(PreparedJoin&&) noexcept = default;
            PreparedJoin& operator=(PreparedJoin&&) noexcept = default;
            PreparedJoin(const PreparedJoin&) = delete;
            PreparedJoin& operator=(const PreparedJoin&) = delete;
            const CanonicalServerState& candidateState() const noexcept { return *mState; }
            CanonicalRevision candidateRevision() const noexcept { return mCanonicalRevision; }
        private:
            friend class CanonicalCommandReducer;
            PreparedJoin() = default;
            CanonicalStateVersion mBaseVersion = CanonicalStateVersion::initial();
            CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
            CanonicalRevision mBaseCanonicalRevision = CanonicalRevision::initial();
            CanonicalRevision mCanonicalRevision = CanonicalRevision::initial();
            ServerTick mCheckpointTick = ServerTick::initial();
            std::shared_ptr<const CanonicalServerState> mState;
            std::shared_ptr<CanonicalStatePublication> mPublication;
        };

        class PreparedLifecycle
        {
        public:
            PreparedLifecycle(PreparedLifecycle&&) noexcept = default;
            PreparedLifecycle& operator=(PreparedLifecycle&&) noexcept = default;
            PreparedLifecycle(const PreparedLifecycle&) = delete;
            PreparedLifecycle& operator=(const PreparedLifecycle&) = delete;
            const CanonicalServerState& candidateState() const noexcept { return *mState; }
            CanonicalRevision candidateRevision() const noexcept { return mCanonicalRevision; }
        private:
            friend class CanonicalCommandReducer;
            PreparedLifecycle() = default;
            CanonicalStateVersion mBaseVersion = CanonicalStateVersion::initial();
            CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
            CanonicalRevision mBaseCanonicalRevision = CanonicalRevision::initial();
            CanonicalRevision mCanonicalRevision = CanonicalRevision::initial();
            ServerTick mCheckpointTick = ServerTick::initial();
            std::shared_ptr<const CanonicalServerState> mState;
            std::shared_ptr<CanonicalStatePublication> mPublication;
        };

        // Eligible for later reviewed composition; this type owns no connection,
        // protocol request, target projection, delivery, or runtime loop.
        CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability);
        CanonicalCommandReducer(
            CanonicalServerState initialState, Observability& observability, CanonicalSinkBundle sinks);

        CanonicalCommandReducer(const CanonicalCommandReducer&) = delete;
        CanonicalCommandReducer& operator=(const CanonicalCommandReducer&) = delete;
        CanonicalCommandReducer(CanonicalCommandReducer&&) = delete;
        CanonicalCommandReducer& operator=(CanonicalCommandReducer&&) = delete;

        // Writer-context inspection only. Cross-thread readers use the immutable
        // latest publication handle.
        const CanonicalServerState& state() const noexcept { return *mState; }
        CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::shared_ptr<const CanonicalStatePublication> latestPublication() const noexcept;
        PreparedBatch prepare(const ServerTickCommandBatch& batch);
        PreparedBatch prepareTick(const ServerTickCommandBatch& batch);
        bool commit(PreparedBatch&& prepared);
        std::optional<PreparedJoin> prepareJoin(CanonicalPlayerEntityState player,
            CanonicalSessionProgress session, ServerTick tick);
        bool commit(PreparedJoin&& prepared);
        std::optional<PreparedLifecycle> prepareDisconnect(SessionId session, ServerTick tick);
        std::optional<PreparedLifecycle> prepareDisconnectBatch(
            std::span<const SessionId> sessions, ServerTick tick);
        std::optional<PreparedLifecycle> prepareResume(
            CanonicalSessionProgress session, ServerTick tick);
        std::optional<PreparedLifecycle> prepareExpiration(PlayerId player, SessionId session,
            SessionGeneration generation, ServerTick tick);
        bool commit(PreparedLifecycle&& prepared);
        CommandBatchReductionResult apply(const ServerTickCommandBatch& batch);

    private:
        std::optional<PreparedLifecycle> prepareLifecycleState(
            std::vector<CanonicalPlayerEntityState> players,
            std::vector<CanonicalSessionProgress> sessions, CanonicalSessionLifecycleKind kind,
            SessionId session, PlayerId player, SessionGeneration generation, ServerTick tick);
        CanonicalSinkDeliveryReport publish(std::shared_ptr<CanonicalStatePublication> publication) noexcept;
        CanonicalSinkDeliveryReport deliver(
            const std::shared_ptr<const CanonicalStatePublication>& publication) noexcept;
        void observe(CommandDisposition disposition, ServerTick tick) noexcept;
        void observe(CommandBatchReductionError error, ServerTick tick, std::uint64_t processedCommands) noexcept;
        void observe(CanonicalSinkRole role, CanonicalSinkDeliveryResult result, ServerTick tick) noexcept;

        std::shared_ptr<const CanonicalServerState> mState;
        CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
        CanonicalRevision mCanonicalRevision = CanonicalRevision::initial();
        ServerTick mCheckpointTick = ServerTick::initial();
        std::shared_ptr<const CanonicalStatePublication> mLatestPublication;
        Observability& mObservability;
        CanonicalSinkBundle mSinks;
    };
}

#endif
