#ifndef TES3MP_PROTOCOL_EXCHANGE_HPP
#define TES3MP_PROTOCOL_EXCHANGE_HPP

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
        MissingRequestedCell,
        InvalidStrongValue,
        InvalidAcknowledgementPresence,
        TooManySnapshotEntries,
        SnapshotEntriesNotStrictlySorted,
        InvalidCellKind,
        InvalidInteriorGrid,
        MissingObservationHeader,
        TooManyObservationChanges,
        ObservationChangesNotStrictlySorted,
        InvalidObservationChangeKind,
    };

    class FixtureCellTransition
    {
    public:
        constexpr explicit FixtureCellTransition(CellId requestedCell) noexcept
            : mRequestedCell(requestedCell)
        {
        }

        constexpr const CellId& requestedCell() const noexcept { return mRequestedCell; }

        friend constexpr bool operator==(const FixtureCellTransition&, const FixtureCellTransition&) noexcept = default;

    private:
        CellId mRequestedCell;
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

    using ReliableOperationBody = std::variant<PlayerMotionIntent, FixtureCellTransition>;

    class ReliableOperation
    {
    public:
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, PlayerMotionIntent intent) noexcept;
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, FixtureCellTransition transition) noexcept;

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

    class ReliableObservationBatch
    {
    public:
        static std::variant<ReliableObservationBatch, ExchangeDecodeError> create(SessionId targetSessionId,
            SessionGeneration targetSessionGeneration, ServerTick serverTick, std::span<const ObservationChange> changes);
        SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        ServerTick serverTick() const noexcept { return mServerTick; }
        std::span<const ObservationChange> changes() const noexcept { return mChanges; }
        friend bool operator==(const ReliableObservationBatch&, const ReliableObservationBatch&) noexcept = default;
    private:
        ReliableObservationBatch(SessionId session, SessionGeneration generation, ServerTick tick,
            std::vector<ObservationChange> changes) : mTargetSessionId(session), mTargetSessionGeneration(generation),
            mServerTick(tick), mChanges(std::move(changes)) {}
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        ServerTick mServerTick;
        std::vector<ObservationChange> mChanges;
    };

    using ReliableOperationDecodeResult = std::variant<ReliableOperation, ExchangeDecodeError>;
    using LatestWinsSnapshotDecodeResult = std::variant<LatestWinsSnapshot, ExchangeDecodeError>;
    using ReliableObservationBatchDecodeResult = std::variant<ReliableObservationBatch, ExchangeDecodeError>;

    std::vector<std::byte> encodeReliableOperation(const ReliableOperation& value);
    std::vector<std::byte> encodeLatestWinsSnapshot(const LatestWinsSnapshot& value);
    std::vector<std::byte> encodeReliableObservationBatch(const ReliableObservationBatch& value);

    ReliableOperationDecodeResult decodeReliableOperation(std::span<const std::byte> payload);
    LatestWinsSnapshotDecodeResult decodeLatestWinsSnapshot(std::span<const std::byte> payload);
    ReliableObservationBatchDecodeResult decodeReliableObservationBatch(std::span<const std::byte> payload);
}

#endif
