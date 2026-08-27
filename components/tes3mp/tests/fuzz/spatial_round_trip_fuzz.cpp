#include <tes3mp/test_support/spatial_round_trip.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace
{
    constexpr std::size_t MaxSpatialSnapshotBytes = 109;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size > MaxSpatialSnapshotBytes)
        return 0;

    std::span<const std::byte> bytes;
    if (size != 0)
        bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
    static_cast<void>(TES3MP::TestSupport::decodeSpatialEntitySnapshot(bytes));
    return 0;
}
