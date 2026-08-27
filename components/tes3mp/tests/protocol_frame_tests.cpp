#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
    bool gTrackAllocations = false;
    std::size_t gTrackedAllocations = 0;
}

void* operator new(std::size_t size)
{
    if (gTrackAllocations)
        ++gTrackedAllocations;
    if (void* result = std::malloc(size))
        return result;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* value) noexcept
{
    std::free(value);
}

void operator delete[](void* value) noexcept
{
    ::operator delete(value);
}

void operator delete(void* value, std::size_t) noexcept
{
    std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

namespace
{
    static_assert(std::is_trivially_copyable_v<TES3MP::FrameError>);
    static_assert(std::is_standard_layout_v<TES3MP::FrameError>);
    static_assert(!std::is_default_constructible_v<TES3MP::DecodedFrame>);
    static_assert(TES3MP::ProtocolFrameHeaderBytes == 12);
    static_assert(TES3MP::SessionControlMaximumPayloadBytes == 4096);
    static_assert(TES3MP::ReliableOperationMaximumPayloadBytes == 16384);
    static_assert(TES3MP::LatestWinsSnapshotMaximumPayloadBytes == 65536);

    std::vector<std::byte> makeBytes(std::initializer_list<std::uint8_t> values)
    {
        std::vector<std::byte> result;
        result.reserve(values.size());
        for (const std::uint8_t value : values)
            result.push_back(static_cast<std::byte>(value));
        return result;
    }

    std::vector<std::byte> encode(
        TES3MP::MessageClass messageClass, TES3MP::MessageKind messageKind, std::span<const std::byte> payload)
    {
        auto result = TES3MP::encodeProtocolFrame(messageClass, messageKind, payload);
        if (const auto* frame = std::get_if<std::vector<std::byte>>(&result))
            return *frame;
        return {};
    }

    bool hasError(const TES3MP::FrameDecodeResult& result, TES3MP::FrameErrorCode code)
    {
        const auto* value = std::get_if<TES3MP::FrameError>(&result);
        return value != nullptr && value->code == code;
    }

    bool golden_frame_v1_is_exact_and_little_endian()
    {
        const auto payload = makeBytes({ 0xaa, 0xbb, 0xcc });
        const auto frame = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        const auto expected = makeBytes({ 'T', '3', 'M', 'P', 1, 1, 1, 0, 3, 0, 0, 0, 0xaa, 0xbb, 0xcc });
        return frame == expected;
    }

    bool all_initial_kinds_round_trip_with_their_compiled_class()
    {
        constexpr std::array kinds{
            TES3MP::MessageKind::ClientHello,
            TES3MP::MessageKind::ServerHello,
            TES3MP::MessageKind::SessionRejected,
            TES3MP::MessageKind::ReliableOperation,
            TES3MP::MessageKind::LatestWinsSnapshot,
        };
        const auto payload = makeBytes({ 1, 2, 3, 4 });
        for (const auto kind : kinds)
        {
            const auto descriptor = TES3MP::messageDescriptor(kind);
            if (!descriptor)
                return false;
            const auto frame = encode(descriptor->messageClass, kind, payload);
            const auto decoded = TES3MP::decodeProtocolFrame(frame);
            const auto* value = std::get_if<TES3MP::DecodedFrame>(&decoded);
            if (value == nullptr || value->messageClass() != descriptor->messageClass || value->messageKind() != kind
                || !std::equal(value->payload().begin(), value->payload().end(), payload.begin(), payload.end()))
                return false;
        }
        return true;
    }

    bool every_truncation_is_rejected()
    {
        const auto payload = makeBytes({ 1, 2, 3 });
        const auto valid = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        for (std::size_t size = 0; size < valid.size(); ++size)
        {
            if (!std::holds_alternative<TES3MP::FrameError>(TES3MP::decodeProtocolFrame(std::span(valid).first(size))))
                return false;
        }
        return true;
    }

    bool malformed_headers_and_lengths_are_structured_failures()
    {
        const auto payload = makeBytes({ 1, 2, 3 });
        const auto valid = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);

        auto badMagic = valid;
        badMagic[0] = std::byte{ 'X' };
        auto badVersion = valid;
        badVersion[4] = std::byte{ 2 };
        auto badClass = valid;
        badClass[5] = std::byte{ 0xff };
        auto unknownKind = valid;
        unknownKind[6] = std::byte{ 0xff };
        unknownKind[7] = std::byte{ 0xff };
        auto classMismatch = valid;
        classMismatch[5] = static_cast<std::byte>(TES3MP::MessageClass::ReliableOperation);
        auto zeroPayload = valid;
        zeroPayload.resize(TES3MP::ProtocolFrameHeaderBytes);
        zeroPayload[8] = std::byte{ 0 };
        auto trailing = valid;
        trailing.push_back(std::byte{ 0 });
        auto concatenated = valid;
        concatenated.insert(concatenated.end(), valid.begin(), valid.end());
        auto declaredMaximum = valid;
        declaredMaximum[8] = std::byte{ 0xff };
        declaredMaximum[9] = std::byte{ 0xff };
        declaredMaximum[10] = std::byte{ 0xff };
        declaredMaximum[11] = std::byte{ 0xff };

        return hasError(TES3MP::decodeProtocolFrame(badMagic), TES3MP::FrameErrorCode::InvalidMagic)
            && hasError(TES3MP::decodeProtocolFrame(badVersion), TES3MP::FrameErrorCode::UnsupportedFormatVersion)
            && hasError(TES3MP::decodeProtocolFrame(badClass), TES3MP::FrameErrorCode::UnknownMessageClass)
            && hasError(TES3MP::decodeProtocolFrame(unknownKind), TES3MP::FrameErrorCode::UnknownMessageKind)
            && hasError(TES3MP::decodeProtocolFrame(classMismatch), TES3MP::FrameErrorCode::ClassKindMismatch)
            && hasError(TES3MP::decodeProtocolFrame(zeroPayload), TES3MP::FrameErrorCode::EmptyPayload)
            && hasError(TES3MP::decodeProtocolFrame(trailing), TES3MP::FrameErrorCode::FrameLengthMismatch)
            && hasError(TES3MP::decodeProtocolFrame(concatenated), TES3MP::FrameErrorCode::FrameLengthMismatch)
            && hasError(TES3MP::decodeProtocolFrame(declaredMaximum), TES3MP::FrameErrorCode::PayloadTooLarge);
    }

    bool exact_class_limits_pass_and_limit_plus_one_fails_before_copy()
    {
        struct Case
        {
            TES3MP::MessageClass messageClass;
            TES3MP::MessageKind messageKind;
            std::size_t limit;
        };
        constexpr std::array cases{
            Case{ TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello,
                TES3MP::SessionControlMaximumPayloadBytes },
            Case{ TES3MP::MessageClass::ReliableOperation, TES3MP::MessageKind::ReliableOperation,
                TES3MP::ReliableOperationMaximumPayloadBytes },
            Case{ TES3MP::MessageClass::LatestWinsSnapshot, TES3MP::MessageKind::LatestWinsSnapshot,
                TES3MP::LatestWinsSnapshotMaximumPayloadBytes },
        };

        for (const auto& current : cases)
        {
            const std::vector<std::byte> exact(current.limit, std::byte{ 0x5a });
            const auto encoded = TES3MP::encodeProtocolFrame(current.messageClass, current.messageKind, exact);
            const auto* frame = std::get_if<std::vector<std::byte>>(&encoded);
            if (frame == nullptr || !std::holds_alternative<TES3MP::DecodedFrame>(TES3MP::decodeProtocolFrame(*frame)))
                return false;

            const std::vector<std::byte> oversized(current.limit + 1, std::byte{ 0x5a });
            const auto rejected = TES3MP::encodeProtocolFrame(current.messageClass, current.messageKind, oversized);
            const auto* rejectedError = std::get_if<TES3MP::FrameError>(&rejected);
            if (rejectedError == nullptr || rejectedError->code != TES3MP::FrameErrorCode::PayloadTooLarge
                || rejectedError->observedBytes != current.limit + 1 || rejectedError->limitBytes != current.limit)
                return false;
        }
        return true;
    }

    bool failed_decode_allocates_nothing_and_returns_no_partial_frame()
    {
        std::array<std::byte, TES3MP::ProtocolFrameHeaderBytes> bad{};
        bad[0] = std::byte{ 'T' };
        bad[1] = std::byte{ '3' };
        bad[2] = std::byte{ 'M' };
        bad[3] = std::byte{ 'P' };
        bad[4] = std::byte{ 1 };
        bad[5] = std::byte{ 1 };
        bad[6] = std::byte{ 1 };
        bad[8] = std::byte{ 1 };

        gTrackedAllocations = 0;
        gTrackAllocations = true;
        const auto result = TES3MP::decodeProtocolFrame(bad);
        gTrackAllocations = false;
        return gTrackedAllocations == 0 && hasError(result, TES3MP::FrameErrorCode::FrameLengthMismatch)
            && !std::holds_alternative<TES3MP::DecodedFrame>(result);
    }

    bool decoded_payload_owns_bytes_after_source_overwrite()
    {
        const auto payload = makeBytes({ 7, 8, 9 });
        auto frame = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        auto decoded = TES3MP::decodeProtocolFrame(frame);
        std::fill(frame.begin(), frame.end(), std::byte{ 0 });
        const auto* value = std::get_if<TES3MP::DecodedFrame>(&decoded);
        return value != nullptr
            && std::equal(value->payload().begin(), value->payload().end(), payload.begin(), payload.end());
    }

    bool every_single_byte_mutation_is_bounded_and_never_partial()
    {
        const auto payload = makeBytes({ 1, 2, 3 });
        const auto valid = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        for (std::size_t index = 0; index < valid.size(); ++index)
        {
            auto mutated = valid;
            mutated[index] ^= std::byte{ 0xff };
            const auto result = TES3MP::decodeProtocolFrame(mutated);
            if (!std::holds_alternative<TES3MP::FrameError>(result)
                && !std::holds_alternative<TES3MP::DecodedFrame>(result))
                return false;
        }
        return true;
    }
}

int main()
{
    return golden_frame_v1_is_exact_and_little_endian() && all_initial_kinds_round_trip_with_their_compiled_class()
            && every_truncation_is_rejected() && malformed_headers_and_lengths_are_structured_failures()
            && exact_class_limits_pass_and_limit_plus_one_fails_before_copy()
            && failed_decode_allocates_nothing_and_returns_no_partial_frame()
            && decoded_payload_owns_bytes_after_source_overwrite()
            && every_single_byte_mutation_is_bounded_and_never_partial()
        ? 0
        : 1;
}
