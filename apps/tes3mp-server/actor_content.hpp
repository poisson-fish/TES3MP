#ifndef TES3MP_SERVER_ACTOR_CONTENT_HPP
#define TES3MP_SERVER_ACTOR_CONTENT_HPP

#include <tes3mp/actor_catalog.hpp>

#include <cstddef>
#include <filesystem>
#include <variant>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumActorContentBytes = 2 * 1024 * 1024;
    inline constexpr std::int64_t MaximumActorCoordinate = 2'147'483'647;

    enum class ActorContentError
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidCatalog,
        CellCapacityExceeded,
    };

    using ActorContentLoadResult = std::variant<ActorCatalog, ActorContentError>;
    ActorContentLoadResult loadActorContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
