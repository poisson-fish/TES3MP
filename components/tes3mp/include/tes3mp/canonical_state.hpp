#ifndef TES3MP_CANONICAL_STATE_HPP
#define TES3MP_CANONICAL_STATE_HPP

#include "spatial_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumCanonicalPlayerEntities = 256;
    inline constexpr std::size_t MaximumCanonicalActiveSessions = 256;
    inline constexpr std::size_t MaximumFinalizedCommandHistory = 1024;

    enum class CommandDisposition : std::uint8_t
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
    };

    class FinalizedCommandRecord
    {
    public:
        constexpr FinalizedCommandRecord(
            CommandSequence commandSequence, CommandId commandId, CommandDisposition disposition) noexcept
            : mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mDisposition(disposition)
        {
        }

        constexpr CommandSequence commandSequence() const noexcept { return mCommandSequence; }
        constexpr CommandId commandId() const noexcept { return mCommandId; }
        constexpr CommandDisposition disposition() const noexcept { return mDisposition; }

        friend constexpr bool operator==(FinalizedCommandRecord, FinalizedCommandRecord) noexcept = default;

    private:
        CommandSequence mCommandSequence;
        CommandId mCommandId;
        CommandDisposition mDisposition;
    };

    enum class CanonicalSessionHistoryErrorCode : std::uint8_t
    {
        LimitExceeded,
        WithoutAcknowledgement,
        NotContiguous,
        DoesNotEndAtAcknowledgement,
        ContainsNonFinalDisposition,
    };

    struct CanonicalSessionHistoryError
    {
        CanonicalSessionHistoryErrorCode code;
        std::size_t index = 0;
        std::uint64_t value = 0;
        std::uint64_t relatedValue = 0;

        friend constexpr bool operator==(CanonicalSessionHistoryError, CanonicalSessionHistoryError) noexcept = default;
    };

    class CanonicalPlayerEntityState
    {
    public:
        constexpr CanonicalPlayerEntityState(PlayerId playerId, EntityId entityId, Transform transform,
            LinearVelocity3 linearVelocity, EntityRevision entityRevision, AuthorityEpoch authorityEpoch,
            ServerTick lastSpatialChangeTick) noexcept
            : mPlayerId(playerId)
            , mEntityId(entityId)
            , mTransform(transform)
            , mLinearVelocity(linearVelocity)
            , mEntityRevision(entityRevision)
            , mAuthorityEpoch(authorityEpoch)
            , mLastSpatialChangeTick(lastSpatialChangeTick)
        {
        }

        constexpr PlayerId playerId() const noexcept { return mPlayerId; }
        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr const Transform& transform() const noexcept { return mTransform; }
        constexpr LinearVelocity3 linearVelocity() const noexcept { return mLinearVelocity; }
        constexpr EntityRevision entityRevision() const noexcept { return mEntityRevision; }
        constexpr AuthorityEpoch authorityEpoch() const noexcept { return mAuthorityEpoch; }
        constexpr ServerTick lastSpatialChangeTick() const noexcept { return mLastSpatialChangeTick; }

        friend constexpr bool operator==(const CanonicalPlayerEntityState&, const CanonicalPlayerEntityState&) noexcept
            = default;

    private:
        PlayerId mPlayerId;
        EntityId mEntityId;
        Transform mTransform;
        LinearVelocity3 mLinearVelocity;
        EntityRevision mEntityRevision;
        AuthorityEpoch mAuthorityEpoch;
        ServerTick mLastSpatialChangeTick;
    };

    enum class SpatialAdvanceErrorCode : std::uint8_t
    {
        TickRegression,
        RevisionExhausted,
    };

    struct SpatialAdvanceError
    {
        SpatialAdvanceErrorCode code;
        ServerTick previousTick;
        ServerTick proposedTick;
        EntityRevision previousRevision;

        friend constexpr bool operator==(SpatialAdvanceError, SpatialAdvanceError) noexcept = default;
    };

    using SpatialAdvanceResult = std::variant<CanonicalPlayerEntityState, SpatialAdvanceError>;

    SpatialAdvanceResult advanceCanonicalSpatialState(const CanonicalPlayerEntityState& current, ServerTick commitTick,
        Transform replacementTransform, LinearVelocity3 replacementVelocity) noexcept;

    class CanonicalSessionProgress
    {
    public:
        CanonicalSessionProgress(SessionId sessionId, SessionGeneration sessionGeneration, PlayerId playerId,
            EntityId entityId, std::optional<CommandSequence> highestContiguousFinalizedCommand);

        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr PlayerId playerId() const noexcept { return mPlayerId; }
        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr std::optional<CommandSequence> highestContiguousFinalizedCommand() const noexcept
        {
            return mHighestContiguousFinalizedCommand;
        }
        std::span<const FinalizedCommandRecord> finalizedCommandHistory() const noexcept
        {
            return *mFinalizedCommandHistory;
        }
        bool containsFinalizedCommandId(CommandId commandId) const noexcept;

        friend bool operator==(const CanonicalSessionProgress&, const CanonicalSessionProgress&) noexcept;

    private:
        friend std::variant<CanonicalSessionProgress, CanonicalSessionHistoryError> createCanonicalSessionProgress(
            SessionId, SessionGeneration, PlayerId, EntityId, std::optional<CommandSequence>,
            std::span<const FinalizedCommandRecord>);

        CanonicalSessionProgress(SessionId sessionId, SessionGeneration sessionGeneration, PlayerId playerId,
            EntityId entityId, std::optional<CommandSequence> highestContiguousFinalizedCommand,
            std::vector<FinalizedCommandRecord> finalizedCommandHistory);

        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        PlayerId mPlayerId;
        EntityId mEntityId;
        std::optional<CommandSequence> mHighestContiguousFinalizedCommand;
        std::shared_ptr<const std::vector<FinalizedCommandRecord>> mFinalizedCommandHistory;
    };

    using CanonicalSessionProgressCreationResult = std::variant<CanonicalSessionProgress, CanonicalSessionHistoryError>;

    CanonicalSessionProgressCreationResult createCanonicalSessionProgress(SessionId sessionId,
        SessionGeneration sessionGeneration, PlayerId playerId, EntityId entityId,
        std::optional<CommandSequence> highestContiguousFinalizedCommand,
        std::span<const FinalizedCommandRecord> finalizedCommandHistory);

    enum class CanonicalStateErrorCode : std::uint8_t
    {
        PlayerLimitExceeded,
        ActiveSessionLimitExceeded,
        PlayerIdsNotStrictlyOrdered,
        SessionIdsNotStrictlyOrdered,
        DuplicateEntityId,
        SessionPlayerMissing,
        SessionEntityMismatch,
        DuplicateActiveBinding,
        FinalizedHistoryLimitExceeded,
        FinalizedHistoryWithoutAcknowledgement,
        FinalizedHistoryNotStrictlyOrdered,
        FinalizedHistoryDoesNotEndAtAcknowledgement,
        FinalizedHistoryContainsNonFinalDisposition,
    };

    struct CanonicalStateError
    {
        CanonicalStateErrorCode code;
        std::size_t index = 0;
        std::uint64_t value = 0;
        std::uint64_t relatedValue = 0;

        friend constexpr bool operator==(CanonicalStateError, CanonicalStateError) noexcept = default;
    };

    class CanonicalServerState
    {
    public:
        std::span<const CanonicalPlayerEntityState> players() const noexcept { return mPlayers; }
        std::span<const CanonicalSessionProgress> activeSessions() const noexcept { return mActiveSessions; }

        const CanonicalPlayerEntityState* findPlayer(PlayerId playerId) const noexcept;
        const CanonicalSessionProgress* findActiveSession(SessionId sessionId) const noexcept;

        friend bool operator==(const CanonicalServerState&, const CanonicalServerState&) noexcept = default;

    private:
        friend std::variant<CanonicalServerState, CanonicalStateError> createCanonicalServerState(
            std::span<const CanonicalPlayerEntityState>, std::span<const CanonicalSessionProgress>);

        CanonicalServerState(std::vector<CanonicalPlayerEntityState> players,
            std::vector<CanonicalSessionProgress> activeSessions) noexcept;

        std::vector<CanonicalPlayerEntityState> mPlayers;
        std::vector<CanonicalSessionProgress> mActiveSessions;
    };

    using CanonicalStateCreationResult = std::variant<CanonicalServerState, CanonicalStateError>;

    CanonicalStateCreationResult createCanonicalServerState(
        std::span<const CanonicalPlayerEntityState> players, std::span<const CanonicalSessionProgress> activeSessions);
}

#endif
