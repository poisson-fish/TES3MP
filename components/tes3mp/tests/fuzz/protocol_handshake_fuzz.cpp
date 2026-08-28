#include <tes3mp/protocol_handshake.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto client = TES3MP::decodeClientHello(bytes);
    if (const auto* value = std::get_if<TES3MP::ClientHello>(&client); value != nullptr
        && !std::holds_alternative<TES3MP::ClientHello>(TES3MP::decodeClientHello(TES3MP::encodeClientHello(*value))))
        std::abort();

    const auto server = TES3MP::decodeServerHello(bytes);
    if (const auto* value = std::get_if<TES3MP::ServerHello>(&server); value != nullptr
        && !std::holds_alternative<TES3MP::ServerHello>(TES3MP::decodeServerHello(TES3MP::encodeServerHello(*value))))
        std::abort();

    const auto rejected = TES3MP::decodeSessionRejected(bytes);
    if (const auto* value = std::get_if<TES3MP::SessionRejected>(&rejected); value != nullptr
        && !std::holds_alternative<TES3MP::SessionRejected>(
            TES3MP::decodeSessionRejected(TES3MP::encodeSessionRejected(*value))))
        std::abort();
    return 0;
}
