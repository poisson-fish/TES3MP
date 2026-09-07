#ifndef TES3MP_PROTOCOL_EXCHANGE_HPP
#define TES3MP_PROTOCOL_EXCHANGE_HPP

#include "movement_policy.hpp"
#include "protocol_envelope.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumSpatialWorldViewEntries = 256;
    inline constexpr std::size_t MaximumObservationChanges = 256;
    inline constexpr std::size_t MaximumInterestMembers = 256;

    enum class ExchangeDecodeErrorStage : std::uint8_t
    {
        SizePrefix,
        Identifier,
        Verification,
        SemanticValidation,
    };

    enum class ExchangeDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        MissingCommandHeader,
        MissingSnapshotHeader,
        MissingEntityPrecondition,
        MissingBody,
        UnknownBody,
        MissingDesiredVelocity,
        InvalidLocomotionInputTick,
        InvalidLocomotionInputSequence,
        InvalidLocomotionMode,
        MissingRequestedCell,
        InvalidStrongValue,
        InvalidAcknowledgementPresence,
        TooManySnapshotEntries,
        SnapshotEntriesNotStrictlySorted,
        SnapshotLocomotionModesSizeMismatch,
        InvalidCellKind,
        InvalidInteriorGrid,
        MissingObservationHeader,
        TooManyObservationChanges,
        ObservationChangesNotStrictlySorted,
        InvalidObservationChangeKind,
        MissingInterestBaselineHeader,
        TooManyInterestMembers,
        InterestMembersNotStrictlySorted,
        InvalidResyncReason,
    };

    class CellTransition
    {
    public:
        constexpr explicit CellTransition(CellId requestedCell) noexcept
            : mRequestedCell(requestedCell)
        {
        }

        constexpr const CellId& requestedCell() const noexcept { return mRequestedCell; }

        friend constexpr bool operator==(const CellTransition&, const CellTransition&) noexcept = default;

    private:
        CellId mRequestedCell;
    };

    enum class ResyncReason : std::uint8_t
    {
        LocalFeedGap = 1,
        EntityRevisionMismatch = 2,
        ChecksumMismatch = 3,
    };

    struct ExchangeDecodeError
    {
        ExchangeDecodeErrorStage stage;
        ExchangeDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;

        friend constexpr bool operator==(ExchangeDecodeError, ExchangeDecodeError) noexcept = default;
    };

    class SessionResyncRequest
    {
    public:
        static std::variant<SessionResyncRequest, ExchangeDecodeError> create(SessionId sessionId,
            SessionGeneration sessionGeneration, ResyncReason reason,
            CanonicalStateVersion lastObservedStateVersion) noexcept;

        constexpr SessionResyncRequest(SessionId sessionId, SessionGeneration sessionGeneration,
            ResyncReason reason, CanonicalStateVersion lastObservedStateVersion) noexcept
            : mSessionId(sessionId), mSessionGeneration(sessionGeneration), mReason(reason),
              mLastObservedStateVersion(lastObservedStateVersion) {}
        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr ResyncReason reason() const noexcept { return mReason; }
        constexpr CanonicalStateVersion lastObservedStateVersion() const noexcept
        { return mLastObservedStateVersion; }

        friend constexpr bool operator==(SessionResyncRequest, SessionResyncRequest) noexcept = default;

    private:
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        ResyncReason mReason;
        CanonicalStateVersion mLastObservedStateVersion;
    };

    class PlayerMotionIntent
    {
    public:
        constexpr explicit PlayerMotionIntent(LinearVelocity3 desiredVelocity) noexcept
            : mDesiredVelocity(desiredVelocity)
        {
        }

        constexpr LinearVelocity3 desiredVelocity() const noexcept { return mDesiredVelocity; }

        friend constexpr bool operator==(PlayerMotionIntent, PlayerMotionIntent) noexcept = default;
        friend constexpr auto operator<=>(PlayerMotionIntent, PlayerMotionIntent) noexcept = default;

    private:
        LinearVelocity3 mDesiredVelocity;
    };

    class PlayerLocomotionInput
    {
    public:
        constexpr PlayerLocomotionInput(LocomotionInputTick inputTick, LocomotionInputSequence inputSequence,
            LocomotionIntent intent) noexcept
            : mInputTick(inputTick), mInputSequence(inputSequence), mIntent(intent) {}

        constexpr LocomotionInputTick inputTick() const noexcept { return mInputTick; }
        constexpr LocomotionInputSequence inputSequence() const noexcept { return mInputSequence; }
        constexpr const LocomotionIntent& intent() const noexcept { return mIntent; }
        friend constexpr bool operator==(PlayerLocomotionInput, PlayerLocomotionInput) noexcept = default;

    private:
        LocomotionInputTick mInputTick;
        LocomotionInputSequence mInputSequence;
        LocomotionIntent mIntent;
    };

    using ReliableOperationBody = std::variant<PlayerMotionIntent, CellTransition, PlayerLocomotionInput>;

    class ReliableOperation
    {
    public:
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, PlayerMotionIntent intent) noexcept;
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, CellTransition transition) noexcept;
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, PlayerLocomotionInput input) noexcept;

        constexpr const ReliableOperationHeader& header() const noexcept { return mHeader; }
        constexpr const ReliableOperationBody& body() const noexcept { return mBody; }

        friend constexpr bool operator==(const ReliableOperation&, const ReliableOperation&) noexcept = default;

    private:
        ReliableOperation(ReliableOperationHeader header, ReliableOperationBody body) noexcept
            : mHeader(header)
            , mBody(std::move(body))
        {
        }

        ReliableOperationHeader mHeader;
        ReliableOperationBody mBody;
    };

    class SpatialWorldView
    {
    public:
        static std::variant<SpatialWorldView, ExchangeDecodeError> create(
            std::span<const SpatialEntitySnapshot> entries);

        std::span<const SpatialEntitySnapshot> entries() const noexcept { return mEntries; }

        friend bool operator==(const SpatialWorldView&, const SpatialWorldView&) noexcept = default;

    private:
        explicit SpatialWorldView(std::vector<SpatialEntitySnapshot> entries)
            : mEntries(std::move(entries))
        {
        }

        std::vector<SpatialEntitySnapshot> mEntries;
    };

    class LatestWinsSnapshot
    {
    public:
        LatestWinsSnapshot(LatestWinsSnapshotHeader header, SpatialWorldView view)
            : mHeader(header)
            , mView(std::move(view))
        {
        }

        constexpr const LatestWinsSnapshotHeader& header() const noexcept { return mHeader; }
        const SpatialWorldView& view() const noexcept { return mView; }

        friend bool operator==(const LatestWinsSnapshot&, const LatestWinsSnapshot&) noexcept = default;

    private:
        LatestWinsSnapshotHeader mHeader;
        SpatialWorldView mView;
    };

    enum class ObservationChangeKind : std::uint8_t { Enter = 1, Leave = 2 };

    struct ObservationChange
    {
        PlayerId playerId;
        EntityId entityId;
        ObservationChangeKind kind;
        friend constexpr bool operator==(ObservationChange, ObservationChange) noexcept = default;
    };

    struct InterestMember
    {
        PlayerId playerId;
        EntityId entityId;
        friend constexpr bool operator==(InterestMember, InterestMember) noexcept = default;
        friend constexpr auto operator<=>(InterestMember, InterestMember) noexcept = default;
    };

    class ReliableInterestBaseline
    {
    public:
        static std::variant<ReliableInterestBaseline, ExchangeDecodeError> create(SessionId targetSessionId,
            SessionGeneration targetSessionGeneration, CanonicalRevision canonicalRevision,
            CanonicalStateVersion canonicalStateVersion, ServerTick serverTick,
            std::span<const InterestMember> members);

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        constexpr CanonicalStateVersion canonicalStateVersion() const noexcept { return mCanonicalStateVersion; }
        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        std::span<const InterestMember> members() const noexcept { return mMembers; }

        friend bool operator==(const ReliableInterestBaseline&, const ReliableInterestBaseline&) noexcept = default;

    private:
        ReliableInterestBaseline(SessionId session, SessionGeneration generation, CanonicalRevision revision,
            CanonicalStateVersion stateVersion, ServerTick tick, std::vector<InterestMember> members)
            : mTargetSessionId(session), mTargetSessionGeneration(generation), mCanonicalRevision(revision),
              mCanonicalStateVersion(stateVersion), mServerTick(tick), mMembers(std::move(members)) {}

        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        CanonicalRevision mCanonicalRevision;
        CanonicalStateVersion mCanonicalStateVersion;
        ServerTick mServerTick;
        std::vector<InterestMember> mMembers;
    };

    class ReliableObservationBatch
    {
    public:
        static std::variant<ReliableObservationBatch, ExchangeDecodeError> create(SessionId targetSessionId,
            SessionGeneration targetSessionGeneration, CanonicalRevision canonicalRevision,
            std::span<const ObservationChange> changes);
        SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const ObservationChange> changes() const noexcept { return mChanges; }
        friend bool operator==(const ReliableObservationBatch&, const ReliableObservationBatch&) noexcept = default;
    private:
        ReliableObservationBatch(SessionId session, SessionGeneration generation, CanonicalRevision revision,
            std::vector<ObservationChange> changes) : mTargetSessionId(session), mTargetSessionGeneration(generation),
            mCanonicalRevision(revision), mChanges(std::move(changes)) {}
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        CanonicalRevision mCanonicalRevision;
        std::vector<ObservationChange> mChanges;
    };

    using ReliableOperationDecodeResult = std::variant<ReliableOperation, ExchangeDecodeError>;
    using LatestWinsSnapshotDecodeResult = std::variant<LatestWinsSnapshot, ExchangeDecodeError>;
    using ReliableObservationBatchDecodeResult = std::variant<ReliableObservationBatch, ExchangeDecodeError>;
    using ReliableInterestBaselineDecodeResult = std::variant<ReliableInterestBaseline, ExchangeDecodeError>;
    using SessionResyncRequestDecodeResult = std::variant<SessionResyncRequest, ExchangeDecodeError>;

    std::vector<std::byte> encodeReliableOperation(const ReliableOperation& value);
    std::vector<std::byte> encodeLatestWinsSnapshot(const LatestWinsSnapshot& value);
    std::vector<std::byte> encodeReliableObservationBatch(const ReliableObservationBatch& value);
    std::vector<std::byte> encodeReliableInterestBaseline(const ReliableInterestBaseline& value);
    std::vector<std::byte> encodeSessionResyncRequest(const SessionResyncRequest& value);

    ReliableOperationDecodeResult decodeReliableOperation(std::span<const std::byte> payload);
    LatestWinsSnapshotDecodeResult decodeLatestWinsSnapshot(std::span<const std::byte> payload);
    ReliableObservationBatchDecodeResult decodeReliableObservationBatch(std::span<const std::byte> payload);
    ReliableInterestBaselineDecodeResult decodeReliableInterestBaseline(std::span<const std::byte> payload);
    SessionResyncRequestDecodeResult decodeSessionResyncRequest(std::span<const std::byte> payload);
}

#endif
