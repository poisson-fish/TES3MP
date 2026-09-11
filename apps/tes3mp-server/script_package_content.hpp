#ifndef TES3MP_SERVER_SCRIPT_PACKAGE_CONTENT_HPP
#define TES3MP_SERVER_SCRIPT_PACKAGE_CONTENT_HPP

#include <tes3mp/content_identity.hpp>
#include <tes3mp/server_scripting.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <variant>
#include <vector>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumScriptPackageContentBytes = 2 * 1024 * 1024;

    enum class ScriptPackageContentError : std::uint8_t
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        InvalidCatalog,
    };

    struct ScriptPackageContent
    {
        std::vector<ServerScriptPackage> packages;
        ServerScriptStateCatalog stateCatalog;
    };

    using ScriptPackageContentLoadResult = std::variant<ScriptPackageContent, ScriptPackageContentError>;

    ScriptPackageContentLoadResult loadScriptPackageContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
