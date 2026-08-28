#include <tes3mp/protocol_exchange.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    (void)TES3MP::decodeReliableOperation(bytes);
    (void)TES3MP::decodeLatestWinsSnapshot(bytes);
    return 0;
}
