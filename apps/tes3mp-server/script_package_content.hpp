#ifndef TES3MP_SERVER_SCRIPT_PACKAGE_CONTENT_HPP
#define TES3MP_SERVER_SCRIPT_PACKAGE_CONTENT_HPP

#include <tes3mp/content_identity.hpp>
#include <tes3mp/server_scripting.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumScriptPackageContentBytes = 2 * 1024 * 1024;
    inline constexpr std::size_t ScriptModuleHashBytes = 32;
    inline constexpr std::size_t MaximumScriptModuleNameBytes = 128;
    inline constexpr std::size_t MaximumScriptEntrypointBytes = 64;
    inline constexpr std::uint32_t ServerScriptModuleAbiVersion = 1;
    inline constexpr std::uint32_t MaximumScriptModuleExecutionBudget = 4096;

    struct ScriptModuleBinding
    {
        ServerScriptPackage package;
        std::filesystem::path artifact;
        std::array<std::byte, ScriptModuleHashBytes> sha256{};
        std::string entrypoint;
        std::uint32_t executionBudget = 0;

        friend bool operator==(const ScriptModuleBinding&, const ScriptModuleBinding&) noexcept = default;
    };

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
        std::vector<ScriptModuleBinding> modules;
        ServerScriptStateCatalog stateCatalog;
    };

    using ScriptPackageContentLoadResult = std::variant<ScriptPackageContent, ScriptPackageContentError>;

    ScriptPackageContentLoadResult loadScriptPackageContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept;
}

#endif
