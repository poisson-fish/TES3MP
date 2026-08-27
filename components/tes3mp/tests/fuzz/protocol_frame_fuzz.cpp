#include <tes3mp/protocol_frame.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

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
    static_cast<void>(TES3MP::decodeProtocolFrame(bytes));
    return 0;
}
