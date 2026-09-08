#ifndef TES3MP_SERVER_INVENTORY_CONTENT_HPP
#define TES3MP_SERVER_INVENTORY_CONTENT_HPP

#include <tes3mp/inventory_world.hpp>

#include <filesystem>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumInventoryContentBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t MaximumInventoryReliableMessagesPerCell = 240;

    enum class InventoryContentError
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidCatalog,
        InvalidWorld,
    };

    struct InventoryContent
    {
        ItemPrototypeCatalog catalog;
        CanonicalInventoryWorld world;
    };

    using InventoryContentLoadResult = std::variant<InventoryContent, InventoryContentError>;
    InventoryContentLoadResult loadInventoryContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
