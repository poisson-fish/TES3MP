#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <limits>

namespace
{
    constexpr std::uint16_t readLittleEndian16(std::span<const std::byte> bytes) noexcept
    {
        return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0]))
            | (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8);
    }

    constexpr std::uint32_t readLittleEndian32(std::span<const std::byte> bytes) noexcept
    {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0]))
            | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8)
            | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16)
            | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24);
    }

    constexpr void writeLittleEndian16(std::span<std::byte> bytes, std::uint16_t value) noexcept
    {
        bytes[0] = static_cast<std::byte>(value & 0xff);
        bytes[1] = static_cast<std::byte>((value >> 8) & 0xff);
    }

    constexpr void writeLittleEndian32(std::span<std::byte> bytes, std::uint32_t value) noexcept
    {
        bytes[0] = static_cast<std::byte>(value & 0xff);
        bytes[1] = static_cast<std::byte>((value >> 8) & 0xff);
        bytes[2] = static_cast<std::byte>((value >> 16) & 0xff);
        bytes[3] = static_cast<std::byte>((value >> 24) & 0xff);
    }

    constexpr TES3MP::FrameError error(TES3MP::FrameErrorStage stage, TES3MP::FrameErrorCode code,
        std::uint8_t formatVersion = 0, std::uint8_t messageClass = 0, std::uint16_t messageKind = 0,
        std::size_t observedBytes = 0, std::size_t limitBytes = 0) noexcept
    {
        return TES3MP::FrameError{ stage, code, formatVersion, messageClass, messageKind, observedBytes, limitBytes };
    }

    struct ValidatedDescriptor
    {
        TES3MP::MessageClass messageClass;
        TES3MP::MessageKind messageKind;
        TES3MP::MessageDescriptor descriptor;
    };

    std::variant<ValidatedDescriptor, TES3MP::FrameError> validateDescriptor(
        std::uint8_t rawClass, std::uint16_t rawKind, std::uint8_t formatVersion) noexcept
    {
        const auto messageClass = static_cast<TES3MP::MessageClass>(rawClass);
        if (!TES3MP::maximumPayloadBytes(messageClass))
        {
            return error(TES3MP::FrameErrorStage::Classification, TES3MP::FrameErrorCode::UnknownMessageClass,
                formatVersion, rawClass, rawKind);
        }

        const auto messageKind = static_cast<TES3MP::MessageKind>(rawKind);
        const auto descriptor = TES3MP::messageDescriptor(messageKind);
        if (!descriptor)
        {
            return error(TES3MP::FrameErrorStage::Classification, TES3MP::FrameErrorCode::UnknownMessageKind,
                formatVersion, rawClass, rawKind);
        }
        if (descriptor->messageClass != messageClass)
        {
            return error(TES3MP::FrameErrorStage::Classification, TES3MP::FrameErrorCode::ClassKindMismatch,
                formatVersion, rawClass, rawKind);
        }
        return ValidatedDescriptor{ messageClass, messageKind, *descriptor };
    }
}

namespace TES3MP
{
    FrameDecodeResult decodeProtocolFrame(std::span<const std::byte> frame)
    {
        if (frame.size() < ProtocolFrameHeaderBytes)
        {
            return error(FrameErrorStage::Header, FrameErrorCode::TruncatedHeader, 0, 0, 0, frame.size(),
                ProtocolFrameHeaderBytes);
        }
        if (!std::equal(ProtocolFrameMagic.begin(), ProtocolFrameMagic.end(), frame.begin()))
            return error(FrameErrorStage::Header, FrameErrorCode::InvalidMagic);

        const std::uint8_t formatVersion = std::to_integer<std::uint8_t>(frame[4]);
        const std::uint8_t rawClass = std::to_integer<std::uint8_t>(frame[5]);
        const std::uint16_t rawKind = readLittleEndian16(frame.subspan(6, 2));
        if (formatVersion != ProtocolFrameFormatVersion)
        {
            return error(
                FrameErrorStage::Header, FrameErrorCode::UnsupportedFormatVersion, formatVersion, rawClass, rawKind);
        }

        const auto validated = validateDescriptor(rawClass, rawKind, formatVersion);
        if (const auto* descriptorError = std::get_if<FrameError>(&validated))
            return *descriptorError;
        const auto& descriptor = std::get<ValidatedDescriptor>(validated);

        const std::uint32_t declaredPayloadBytes = readLittleEndian32(frame.subspan(8, 4));
        if (declaredPayloadBytes == 0)
        {
            return error(FrameErrorStage::PayloadBudget, FrameErrorCode::EmptyPayload, formatVersion, rawClass, rawKind,
                0, descriptor.descriptor.maximumPayloadBytes);
        }
        if (declaredPayloadBytes > descriptor.descriptor.maximumPayloadBytes)
        {
            return error(FrameErrorStage::PayloadBudget, FrameErrorCode::PayloadTooLarge, formatVersion, rawClass,
                rawKind, declaredPayloadBytes, descriptor.descriptor.maximumPayloadBytes);
        }

        const std::size_t receivedPayloadBytes = frame.size() - ProtocolFrameHeaderBytes;
        if (declaredPayloadBytes != receivedPayloadBytes)
        {
            return error(FrameErrorStage::FrameLength, FrameErrorCode::FrameLengthMismatch, formatVersion, rawClass,
                rawKind, receivedPayloadBytes, declaredPayloadBytes);
        }

        std::vector<std::byte> ownedPayload(frame.begin() + ProtocolFrameHeaderBytes, frame.end());
        return DecodedFrame(descriptor.messageClass, descriptor.messageKind, std::move(ownedPayload));
    }

    FrameEncodeResult encodeProtocolFrame(
        MessageClass messageClass, MessageKind messageKind, std::span<const std::byte> payload)
    {
        const auto descriptor = messageDescriptor(messageKind);
        const auto rawClass = static_cast<std::uint8_t>(messageClass);
        const auto rawKind = static_cast<std::uint16_t>(messageKind);
        if (!maximumPayloadBytes(messageClass))
        {
            return error(FrameErrorStage::Classification, FrameErrorCode::UnknownMessageClass,
                ProtocolFrameFormatVersion, rawClass, rawKind);
        }
        if (!descriptor)
        {
            return error(FrameErrorStage::Classification, FrameErrorCode::UnknownMessageKind,
                ProtocolFrameFormatVersion, rawClass, rawKind);
        }
        if (descriptor->messageClass != messageClass)
        {
            return error(FrameErrorStage::Classification, FrameErrorCode::ClassKindMismatch, ProtocolFrameFormatVersion,
                rawClass, rawKind);
        }
        if (payload.empty())
        {
            return error(FrameErrorStage::PayloadBudget, FrameErrorCode::EmptyPayload, ProtocolFrameFormatVersion,
                rawClass, rawKind, 0, descriptor->maximumPayloadBytes);
        }
        if (payload.size() > descriptor->maximumPayloadBytes
            || payload.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return error(FrameErrorStage::PayloadBudget, FrameErrorCode::PayloadTooLarge, ProtocolFrameFormatVersion,
                rawClass, rawKind, payload.size(), descriptor->maximumPayloadBytes);
        }

        std::vector<std::byte> frame(ProtocolFrameHeaderBytes + payload.size());
        std::copy(ProtocolFrameMagic.begin(), ProtocolFrameMagic.end(), frame.begin());
        frame[4] = static_cast<std::byte>(ProtocolFrameFormatVersion);
        frame[5] = static_cast<std::byte>(rawClass);
        writeLittleEndian16(std::span(frame).subspan(6, 2), rawKind);
        writeLittleEndian32(std::span(frame).subspan(8, 4), static_cast<std::uint32_t>(payload.size()));
        std::copy(payload.begin(), payload.end(), frame.begin() + ProtocolFrameHeaderBytes);
        return frame;
    }
}
