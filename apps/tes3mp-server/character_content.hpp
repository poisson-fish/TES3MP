#ifndef TES3MP_SERVER_CHARACTER_CONTENT_HPP
#define TES3MP_SERVER_CHARACTER_CONTENT_HPP

#include <tes3mp/character_profile.hpp>

#include <filesystem>
#include <string>
#include <variant>

namespace TES3MP::ServerApp
{
    enum class CharacterContentErrorCode : std::uint8_t
    {
        Unavailable,
        TooLarge,
        InvalidHeader,
        Malformed,
        ManifestMismatch,
        UnknownCell,
        InvalidCatalog,
    };
    struct CharacterContentError
    {
        CharacterContentErrorCode code;
        std::size_t line = 0;
        friend constexpr bool operator==(CharacterContentError, CharacterContentError) noexcept = default;
    };
    using CharacterContentResult = std::variant<CharacterContentCatalog, CharacterContentError>;
    CharacterContentResult loadCharacterContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
    std::string describeCharacterContentError(CharacterContentError error);
}

#endif
