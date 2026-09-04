#ifndef TES3MP_PROTOCOL_FRAME_HPP
#define TES3MP_PROTOCOL_FRAME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::array<std::byte, 4> ProtocolFrameMagic{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'M' },
        std::byte{ 'P' } };
    inline constexpr std::uint8_t ProtocolFrameFormatVersion = 1;
    inline constexpr std::size_t ProtocolFrameHeaderBytes = 12;

    inline constexpr std::size_t SessionControlMaximumPayloadBytes = 4 * 1024;
    inline constexpr std::size_t ReliableOperationMaximumPayloadBytes = 16 * 1024;
    inline constexpr std::size_t LatestWinsSnapshotMaximumPayloadBytes = 64 * 1024;
    inline constexpr std::size_t PresentationSampleMaximumPayloadBytes = 1024;

    enum class MessageClass : std::uint8_t
    {
        SessionControl = 1,
        ReliableOperation = 2,
        LatestWinsSnapshot = 3,
        PresentationSample = 4,
    };

    enum class MessageKind : std::uint16_t
    {
        ClientHello = 0x0001,
        ServerHello = 0x0002,
        SessionRejected = 0x0003,
        AuthenticationRequest = 0x0004,
        AuthenticationAccepted = 0x0005,
        AuthenticationRejected = 0x0006,
        ReliableOperation = 0x0100,
        ReliableObservationBatch = 0x0101,
        LatestWinsSnapshot = 0x0200,
        ClientVrPoseSample = 0x0300,
        ServerVrPoseSnapshot = 0x0301,
    };

    struct MessageDescriptor
    {
        MessageKind kind;
        MessageClass messageClass;
        std::size_t maximumPayloadBytes;

        friend constexpr bool operator==(MessageDescriptor, MessageDescriptor) noexcept = default;
    };

    constexpr std::optional<MessageDescriptor> messageDescriptor(MessageKind kind) noexcept
    {
        switch (kind)
        {
            case MessageKind::ClientHello:
            case MessageKind::ServerHello:
            case MessageKind::SessionRejected:
            case MessageKind::AuthenticationRequest:
            case MessageKind::AuthenticationAccepted:
            case MessageKind::AuthenticationRejected:
                return MessageDescriptor{ kind, MessageClass::SessionControl, SessionControlMaximumPayloadBytes };
            case MessageKind::ReliableOperation:
            case MessageKind::ReliableObservationBatch:
                return MessageDescriptor{ kind, MessageClass::ReliableOperation, ReliableOperationMaximumPayloadBytes };
            case MessageKind::LatestWinsSnapshot:
                return MessageDescriptor{ kind, MessageClass::LatestWinsSnapshot,
                    LatestWinsSnapshotMaximumPayloadBytes };
            case MessageKind::ClientVrPoseSample:
            case MessageKind::ServerVrPoseSnapshot:
                return MessageDescriptor{ kind, MessageClass::PresentationSample,
                    PresentationSampleMaximumPayloadBytes };
        }
        return std::nullopt;
    }

    constexpr std::optional<std::size_t> maximumPayloadBytes(MessageClass messageClass) noexcept
    {
        switch (messageClass)
        {
            case MessageClass::SessionControl:
                return SessionControlMaximumPayloadBytes;
            case MessageClass::ReliableOperation:
                return ReliableOperationMaximumPayloadBytes;
            case MessageClass::LatestWinsSnapshot:
                return LatestWinsSnapshotMaximumPayloadBytes;
            case MessageClass::PresentationSample:
                return PresentationSampleMaximumPayloadBytes;
        }
        return std::nullopt;
    }

    enum class FrameErrorStage : std::uint8_t
    {
        Header,
        Classification,
        PayloadBudget,
        FrameLength,
    };

    enum class FrameErrorCode : std::uint8_t
    {
        TruncatedHeader,
        InvalidMagic,
        UnsupportedFormatVersion,
        UnknownMessageClass,
        UnknownMessageKind,
        ClassKindMismatch,
        EmptyPayload,
        PayloadTooLarge,
        FrameLengthMismatch,
    };

    struct FrameError
    {
        FrameErrorStage stage;
        FrameErrorCode code;
        std::uint8_t observedFormatVersion = 0;
        std::uint8_t observedMessageClass = 0;
        std::uint16_t observedMessageKind = 0;
        std::size_t observedBytes = 0;
        std::size_t limitBytes = 0;

        friend constexpr bool operator==(FrameError, FrameError) noexcept = default;
    };

    class DecodedFrame
    {
    public:
        MessageClass messageClass() const noexcept { return mMessageClass; }
        MessageKind messageKind() const noexcept { return mMessageKind; }
        std::span<const std::byte> payload() const noexcept { return mPayload; }

    private:
        friend std::variant<DecodedFrame, FrameError> decodeProtocolFrame(std::span<const std::byte> frame);

        DecodedFrame(MessageClass messageClass, MessageKind messageKind, std::vector<std::byte> payload)
            : mMessageClass(messageClass)
            , mMessageKind(messageKind)
            , mPayload(std::move(payload))
        {
        }

        MessageClass mMessageClass;
        MessageKind mMessageKind;
        std::vector<std::byte> mPayload;
    };

    using FrameDecodeResult = std::variant<DecodedFrame, FrameError>;
    using FrameEncodeResult = std::variant<std::vector<std::byte>, FrameError>;

    FrameDecodeResult decodeProtocolFrame(std::span<const std::byte> frame);
    FrameEncodeResult encodeProtocolFrame(
        MessageClass messageClass, MessageKind messageKind, std::span<const std::byte> payload);
}

#endif
