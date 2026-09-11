#ifndef TES3MP_SERVER_WORLD_CONTENT_HPP
#define TES3MP_SERVER_WORLD_CONTENT_HPP

#include <tes3mp/content_identity.hpp>
#include <tes3mp/world_state.hpp>

#include <cstddef>
#include <filesystem>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumWorldContentBytes = 4 * 1024 * 1024;

    enum class WorldContentError : std::uint8_t
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidCatalog,
    };

    struct WorldContent
    {
        GlobalVariableCatalog globals;
        QuestJournalCatalog questJournal;
        CanonicalWorldState world;
    };

    using WorldContentLoadResult = std::variant<WorldContent, WorldContentError>;
    WorldContentLoadResult loadWorldContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
