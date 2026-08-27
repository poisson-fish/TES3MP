#include <tes3mp/protocol_handshake.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    (void)TES3MP::decodeClientHello(bytes);
    (void)TES3MP::decodeServerHello(bytes);
    (void)TES3MP::decodeSessionRejected(bytes);
    return 0;
}
