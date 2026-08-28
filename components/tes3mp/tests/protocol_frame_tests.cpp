#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
#if !defined(TES3MP_TEST_TSAN_ALLOCATOR_INTERPOSITION)
    bool gTrackAllocations = false;
    std::size_t gTrackedAllocations = 0;
#endif
}

#if !defined(TES3MP_TEST_TSAN_ALLOCATOR_INTERPOSITION)
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
#endif

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

    bool deterministic_payload_properties_round_trip()
    {
        constexpr std::array kinds{
            TES3MP::MessageKind::ClientHello,
            TES3MP::MessageKind::ServerHello,
            TES3MP::MessageKind::SessionRejected,
            TES3MP::MessageKind::ReliableOperation,
            TES3MP::MessageKind::LatestWinsSnapshot,
        };
        constexpr std::array sampleSizes{ std::size_t{ 1 }, std::size_t{ 2 }, std::size_t{ 15 }, std::size_t{ 255 },
            std::size_t{ 256 }, std::size_t{ 4096 } };
        for (const auto kind : kinds)
        {
            const auto descriptor = TES3MP::messageDescriptor(kind);
            if (!descriptor)
                return false;
            for (const auto requestedSize : sampleSizes)
            {
                const auto size = std::min(requestedSize, descriptor->maximumPayloadBytes);
                std::vector<std::byte> payload(size);
                for (std::size_t index = 0; index < payload.size(); ++index)
                    payload[index] = static_cast<std::byte>((index * 131 + static_cast<std::uint16_t>(kind)) & 0xff);
                const auto frame = encode(descriptor->messageClass, kind, payload);
                const auto decoded = TES3MP::decodeProtocolFrame(frame);
                const auto* value = std::get_if<TES3MP::DecodedFrame>(&decoded);
                if (value == nullptr || value->messageClass() != descriptor->messageClass
                    || value->messageKind() != kind || !std::ranges::equal(value->payload(), payload))
                    return false;
                const auto encodedAgain
                    = TES3MP::encodeProtocolFrame(value->messageClass(), value->messageKind(), value->payload());
                const auto* canonical = std::get_if<std::vector<std::byte>>(&encodedAgain);
                if (canonical == nullptr || *canonical != frame)
                    return false;
            }
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

#if defined(TES3MP_TEST_TSAN_ALLOCATOR_INTERPOSITION)
        const auto result = TES3MP::decodeProtocolFrame(bad);
        return hasError(result, TES3MP::FrameErrorCode::FrameLengthMismatch)
            && !std::holds_alternative<TES3MP::DecodedFrame>(result);
#else
        gTrackedAllocations = 0;
        gTrackAllocations = true;
        const auto result = TES3MP::decodeProtocolFrame(bad);
        gTrackAllocations = false;
        return gTrackedAllocations == 0 && hasError(result, TES3MP::FrameErrorCode::FrameLengthMismatch)
            && !std::holds_alternative<TES3MP::DecodedFrame>(result);
#endif
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

    bool every_single_bit_mutation_is_rejected_or_canonical()
    {
        const auto payload = makeBytes({ 1, 2, 3 });
        const auto valid = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        for (std::size_t index = 0; index < valid.size(); ++index)
        {
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                auto mutated = valid;
                mutated[index] ^= static_cast<std::byte>(1u << bit);
                const auto result = TES3MP::decodeProtocolFrame(mutated);
                if (const auto* decoded = std::get_if<TES3MP::DecodedFrame>(&result))
                {
                    const auto encoded = TES3MP::encodeProtocolFrame(
                        decoded->messageClass(), decoded->messageKind(), decoded->payload());
                    const auto* canonical = std::get_if<std::vector<std::byte>>(&encoded);
                    if (canonical == nullptr || *canonical != mutated)
                        return false;
                }
                else if (!std::holds_alternative<TES3MP::FrameError>(result))
                    return false;
            }
        }
        return true;
    }

    bool writeCorpus(const std::filesystem::path& directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
        const auto payload = makeBytes({ 0xaa, 0xbb, 0xcc });
        const auto frame = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        std::ofstream stream(directory / "valid-client-hello-frame", std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
        return stream.good();
    }

    std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::nullopt;
        std::vector<std::byte> value(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
        if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            return std::nullopt;
        return value;
    }

    bool verifyCorpus(const std::filesystem::path& directory)
    {
        const auto stored = readFile(directory / "valid-client-hello-frame");
        const auto payload = makeBytes({ 0xaa, 0xbb, 0xcc });
        const auto expected = encode(TES3MP::MessageClass::SessionControl, TES3MP::MessageKind::ClientHello, payload);
        return stored && *stored == expected
            && std::holds_alternative<TES3MP::DecodedFrame>(TES3MP::decodeProtocolFrame(*stored));
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        return writeCorpus(argv[2]) ? 0 : 1;
    if (argc == 3 && std::string_view(argv[1]) == "--verify-corpus")
        return verifyCorpus(argv[2]) ? 0 : 1;

    return golden_frame_v1_is_exact_and_little_endian() && all_initial_kinds_round_trip_with_their_compiled_class()
            && deterministic_payload_properties_round_trip() && every_truncation_is_rejected()
            && malformed_headers_and_lengths_are_structured_failures()
            && exact_class_limits_pass_and_limit_plus_one_fails_before_copy()
            && failed_decode_allocates_nothing_and_returns_no_partial_frame()
            && decoded_payload_owns_bytes_after_source_overwrite()
            && every_single_bit_mutation_is_rejected_or_canonical()
        ? 0
        : 1;
}
