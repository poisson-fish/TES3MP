#ifndef TES3MP_SERVER_COMMAND_REDUCER_HPP
#define TES3MP_SERVER_COMMAND_REDUCER_HPP

#include "canonical_publication.hpp"
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
        constexpr explicit operator bool() const noexcept { return mError == CommandBatchReductionError::None; }

        friend bool operator==(const CommandBatchReductionResult&, const CommandBatchReductionResult&) noexcept
            = default;

    private:
        friend class CanonicalCommandReducer;

        CommandBatchReductionError mError = CommandBatchReductionError::None;
        std::vector<CommandDispositionRecord> mDispositions;
    };

    class CanonicalCommandReducer
    {
    public:
        // Eligible for later reviewed composition; this type owns no connection,
        // protocol request, target projection, delivery, or runtime loop.
        CanonicalCommandReducer(CanonicalServerState initialState, Observability& observability);

        CanonicalCommandReducer(const CanonicalCommandReducer&) = delete;
        CanonicalCommandReducer& operator=(const CanonicalCommandReducer&) = delete;
        CanonicalCommandReducer(CanonicalCommandReducer&&) = delete;
        CanonicalCommandReducer& operator=(CanonicalCommandReducer&&) = delete;

        // Writer-context inspection only. Cross-thread readers use the immutable
        // latest publication handle.
        const CanonicalServerState& state() const noexcept { return *mState; }
        CanonicalStateVersion stateVersion() const noexcept { return mStateVersion; }
        std::shared_ptr<const CanonicalStatePublication> latestPublication() const noexcept;
        CommandBatchReductionResult apply(const ServerTickCommandBatch& batch);

    private:
        void publish(std::shared_ptr<CanonicalStatePublication> publication) noexcept;
        void observe(CommandDisposition disposition, ServerTick tick) noexcept;
        void observe(CommandBatchReductionError error, ServerTick tick, std::uint64_t processedCommands) noexcept;

        std::shared_ptr<const CanonicalServerState> mState;
        CanonicalStateVersion mStateVersion = CanonicalStateVersion::initial();
        ServerTick mCheckpointTick = ServerTick::initial();
        std::atomic<std::shared_ptr<const CanonicalStatePublication>> mLatestPublication;
        Observability& mObservability;
    };
}

#endif
