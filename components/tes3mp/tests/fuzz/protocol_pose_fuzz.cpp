#include <tes3mp/protocol_pose.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
    (void)TES3MP::decodeClientVrPoseSample(bytes);
    (void)TES3MP::decodeServerVrPoseSnapshot(bytes);
    return 0;
}
