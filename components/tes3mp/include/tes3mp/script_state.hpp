#ifndef TES3MP_SCRIPT_STATE_HPP
#define TES3MP_SCRIPT_STATE_HPP

#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumScriptVariablesPerPackage = 256;
    inline constexpr std::size_t MaximumScriptVariables = 16'384;
    inline constexpr std::size_t MaximumScriptStringBytes = 4'096;
    inline constexpr std::size_t MaximumScriptStateStringBytes = 1024 * 1024;

    enum class ScriptVariableType : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        String,
    };

    using ScriptVariableValue = std::variant<bool, std::int64_t, double, std::string>;

    struct ServerScriptVariableCatalogEntry
    {
        std::uint64_t packageId = 0;
        ScriptVariableId id;
        ScriptVariableValue initialValue;

        constexpr ScriptVariableType type() const noexcept
        {
            return static_cast<ScriptVariableType>(initialValue.index());
        }
        friend bool operator==(
            const ServerScriptVariableCatalogEntry&, const ServerScriptVariableCatalogEntry&) noexcept = default;
    };

    class ServerScriptStateCatalog
    {
    public:
        static std::optional<ServerScriptStateCatalog> create(
            std::span<const ServerScriptVariableCatalogEntry> entries) noexcept;
        std::span<const ServerScriptVariableCatalogEntry> entries() const noexcept { return mEntries; }
        const ServerScriptVariableCatalogEntry* find(std::uint64_t packageId, ScriptVariableId id) const noexcept;
        friend bool operator==(const ServerScriptStateCatalog&, const ServerScriptStateCatalog&) noexcept = default;

    private:
        explicit ServerScriptStateCatalog(std::vector<ServerScriptVariableCatalogEntry> entries) noexcept
            : mEntries(std::move(entries))
        {
        }
        std::vector<ServerScriptVariableCatalogEntry> mEntries;
    };

    struct CanonicalScriptVariableState
    {
        std::uint64_t packageId = 0;
        ScriptVariableId id;
        ScriptVariableValue value;
        ScriptStateRevision revision = ScriptStateRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();

        constexpr ScriptVariableType type() const noexcept { return static_cast<ScriptVariableType>(value.index()); }
        friend bool operator==(const CanonicalScriptVariableState&, const CanonicalScriptVariableState&) noexcept
            = default;
    };

    class CanonicalScriptState
    {
    public:
        static std::optional<CanonicalScriptState> initial(const ServerScriptStateCatalog& catalog) noexcept;
        static std::optional<CanonicalScriptState> restore(
            const ServerScriptStateCatalog& catalog, std::span<const CanonicalScriptVariableState> variables) noexcept;

        std::span<const CanonicalScriptVariableState> variables() const noexcept { return mVariables; }
        std::span<const CanonicalScriptVariableState> package(std::uint64_t packageId) const noexcept;
        const CanonicalScriptVariableState* find(std::uint64_t packageId, ScriptVariableId id) const noexcept;
        friend bool operator==(const CanonicalScriptState&, const CanonicalScriptState&) noexcept = default;

    private:
        explicit CanonicalScriptState(std::vector<CanonicalScriptVariableState> variables) noexcept
            : mVariables(std::move(variables))
        {
        }
        std::vector<CanonicalScriptVariableState> mVariables;
    };

    enum class CanonicalScriptStateMutationError : std::uint8_t
    {
        UnknownVariable,
        TypeMismatch,
        RevisionMismatch,
        RevisionExhausted,
        TickRegression,
        InvalidValue,
    };

    using CanonicalScriptStateMutationResult = std::variant<CanonicalScriptState, CanonicalScriptStateMutationError>;

    CanonicalScriptStateMutationResult compareAndSetCanonicalScriptVariable(const CanonicalScriptState& state,
        const ServerScriptStateCatalog& catalog, std::uint64_t packageId, ScriptVariableId id,
        ScriptStateRevision expectedRevision, ScriptVariableValue value, ServerTick tick) noexcept;
}

#endif
