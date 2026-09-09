#include <tes3mp/character_creation_protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::span(reinterpret_cast<const std::byte*>(data), size);
    auto command = TES3MP::decodeClientCharacterCreationCommand(bytes);
    if (const auto* value = std::get_if<TES3MP::ClientCharacterCreationCommand>(&command))
        (void)TES3MP::encodeClientCharacterCreationCommand(*value);
    auto profile = TES3MP::decodeReliableCharacterProfile(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableCharacterProfile>(&profile))
        (void)TES3MP::encodeReliableCharacterProfile(*value);
    return 0;
}
