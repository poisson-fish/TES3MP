#ifndef TES3MP_SERVER_SCRIPT_MODULE_HPP
#define TES3MP_SERVER_SCRIPT_MODULE_HPP

#include "script_package_content.hpp"

#include <tes3mp/server_scripting.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumScriptModuleBytes = 64 * 1024;
    inline constexpr std::size_t MaximumScriptModuleCallbacks = 16;
    inline constexpr std::size_t MaximumScriptModuleInstructionsPerCallback = 64;

    enum class ExecutableScriptModuleError : std::uint8_t
    {
        Unavailable,
        TooLarge,
        HashMismatch,
        Malformed,
        ApiMismatch,
        AbiMismatch,
        EntrypointMissing,
        InvalidResourceBounds,
        InvalidStateCatalog,
        InvalidWorldCatalog,
        RegistrationFailed,
    };

    class ExecutableScriptModules
    {
    public:
        ExecutableScriptModules() = default;
        ExecutableScriptModules(ExecutableScriptModules&&) noexcept = default;
        ExecutableScriptModules& operator=(ExecutableScriptModules&&) noexcept = default;
        ExecutableScriptModules(const ExecutableScriptModules&) = delete;
        ExecutableScriptModules& operator=(const ExecutableScriptModules&) = delete;

        std::size_t callbackCount() const noexcept { return mCallbacks.size(); }

    private:
        friend std::variant<ExecutableScriptModules, ExecutableScriptModuleError> loadExecutableScriptModules(
            const std::filesystem::path&, const ScriptPackageContent&, const GlobalVariableCatalog&,
            const QuestJournalCatalog&, const FactionDialogueCatalog&, DeterministicServerScriptRuntime&) noexcept;
        std::vector<std::unique_ptr<ServerScriptCallback>> mCallbacks;
    };

    using ExecutableScriptModuleLoadResult = std::variant<ExecutableScriptModules, ExecutableScriptModuleError>;

    ExecutableScriptModuleLoadResult loadExecutableScriptModules(const std::filesystem::path& packageContentPath,
        const ScriptPackageContent& content, const GlobalVariableCatalog& globalCatalog,
        const QuestJournalCatalog& questJournalCatalog, const FactionDialogueCatalog& factionDialogueCatalog,
        DeterministicServerScriptRuntime& runtime) noexcept;
    ExecutableScriptModuleLoadResult loadExecutableScriptModules(const std::filesystem::path& packageContentPath,
        const ScriptPackageContent& content, const GlobalVariableCatalog& globalCatalog,
        const QuestJournalCatalog& questJournalCatalog, DeterministicServerScriptRuntime& runtime) noexcept;
}

#endif
