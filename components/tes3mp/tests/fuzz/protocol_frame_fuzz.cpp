#include <tes3mp/protocol_frame.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

namespace
{
    constexpr std::size_t MaximumFuzzFrameBytes
        = TES3MP::ProtocolFrameHeaderBytes + TES3MP::LatestWinsSnapshotMaximumPayloadBytes + 1;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > MaximumFuzzFrameBytes)
        return 0;

    std::span<const std::byte> bytes;
    if (size != 0)
        bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
    const auto decoded = TES3MP::decodeProtocolFrame(bytes);
    if (const auto* frame = std::get_if<TES3MP::DecodedFrame>(&decoded))
    {
        const auto normalized
            = TES3MP::encodeProtocolFrame(frame->messageClass(), frame->messageKind(), frame->payload());
        const auto* normalizedBytes = std::get_if<std::vector<std::byte>>(&normalized);
        if (normalizedBytes == nullptr
            || !std::holds_alternative<TES3MP::DecodedFrame>(TES3MP::decodeProtocolFrame(*normalizedBytes)))
            std::abort();
    }
    return 0;
}
