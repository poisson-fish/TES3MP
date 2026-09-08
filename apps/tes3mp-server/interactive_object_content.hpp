#ifndef TES3MP_SERVER_INTERACTIVE_OBJECT_CONTENT_HPP
#define TES3MP_SERVER_INTERACTIVE_OBJECT_CONTENT_HPP

#include <tes3mp/interactive_object_catalog.hpp>

#include <cstddef>
#include <filesystem>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumInteractiveObjectContentBytes = 2 * 1024 * 1024;
    inline constexpr std::int64_t MaximumInteractiveObjectCoordinate = 2'147'483'647;

    enum class InteractiveObjectContentError
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidCatalog,
        CellCapacityExceeded,
    };

    using InteractiveObjectContentLoadResult = std::variant<InteractiveObjectCatalog, InteractiveObjectContentError>;
    InteractiveObjectContentLoadResult loadInteractiveObjectContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
