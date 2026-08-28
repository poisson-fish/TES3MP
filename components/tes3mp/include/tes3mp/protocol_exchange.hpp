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
        InvalidStrongValue,
        InvalidAcknowledgementPresence,
        TooManySnapshotEntries,
        SnapshotEntriesNotStrictlySorted,
        InvalidCellKind,
        InvalidInteriorGrid,
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

    class ReliableOperation
    {
    public:
        static std::variant<ReliableOperation, ExchangeDecodeError> create(
            ReliableOperationHeader header, PlayerMotionIntent intent) noexcept;

        constexpr const ReliableOperationHeader& header() const noexcept { return mHeader; }
        constexpr PlayerMotionIntent intent() const noexcept { return mIntent; }

        friend constexpr bool operator==(const ReliableOperation&, const ReliableOperation&) noexcept = default;

    private:
        ReliableOperation(ReliableOperationHeader header, PlayerMotionIntent intent) noexcept
            : mHeader(header)
            , mIntent(intent)
        {
        }

        ReliableOperationHeader mHeader;
        PlayerMotionIntent mIntent;
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

    using ReliableOperationDecodeResult = std::variant<ReliableOperation, ExchangeDecodeError>;
    using LatestWinsSnapshotDecodeResult = std::variant<LatestWinsSnapshot, ExchangeDecodeError>;

    std::vector<std::byte> encodeReliableOperation(const ReliableOperation& value);
    std::vector<std::byte> encodeLatestWinsSnapshot(const LatestWinsSnapshot& value);

    ReliableOperationDecodeResult decodeReliableOperation(std::span<const std::byte> payload);
    LatestWinsSnapshotDecodeResult decodeLatestWinsSnapshot(std::span<const std::byte> payload);
}

#endif
