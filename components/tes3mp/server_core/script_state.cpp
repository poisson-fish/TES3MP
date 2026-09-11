#include <tes3mp/script_state.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    using namespace TES3MP;

    bool validValue(const ScriptVariableValue& value) noexcept
    {
        if (const auto* number = std::get_if<double>(&value))
            return std::isfinite(*number);
        if (const auto* text = std::get_if<std::string>(&value))
            return text->size() <= MaximumScriptStringBytes;
        return true;
    }
}

namespace TES3MP
{
    std::optional<ServerScriptStateCatalog> ServerScriptStateCatalog::create(
        std::span<const ServerScriptVariableCatalogEntry> entries) noexcept
    try
    {
        if (entries.size() > MaximumScriptVariables)
            return std::nullopt;
        std::size_t stringBytes = 0;
        std::size_t packageCount = 0;
        std::uint64_t currentPackage = 0;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const auto& entry = entries[index];
            if (entry.packageId == 0 || !validValue(entry.initialValue)
                || (index != 0
                    && (entries[index - 1].packageId > entry.packageId
                        || (entries[index - 1].packageId == entry.packageId && entries[index - 1].id >= entry.id))))
                return std::nullopt;
            if (entry.packageId != currentPackage)
            {
                currentPackage = entry.packageId;
                packageCount = 0;
            }
            if (++packageCount > MaximumScriptVariablesPerPackage)
                return std::nullopt;
            if (const auto* text = std::get_if<std::string>(&entry.initialValue))
            {
                if (text->size() > MaximumScriptStateStringBytes - stringBytes)
                    return std::nullopt;
                stringBytes += text->size();
            }
        }
        return ServerScriptStateCatalog(std::vector<ServerScriptVariableCatalogEntry>(entries.begin(), entries.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const ServerScriptVariableCatalogEntry* ServerScriptStateCatalog::find(
        std::uint64_t packageId, ScriptVariableId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mEntries, std::pair(packageId, id), {},
            [](const auto& value) { return std::pair(value.packageId, value.id); });
        return found != mEntries.end() && found->packageId == packageId && found->id == id ? &*found : nullptr;
    }

    std::optional<CanonicalScriptState> CanonicalScriptState::initial(const ServerScriptStateCatalog& catalog) noexcept
    try
    {
        std::vector<CanonicalScriptVariableState> variables;
        variables.reserve(catalog.entries().size());
        for (const auto& entry : catalog.entries())
            variables.push_back({ entry.packageId, entry.id, entry.initialValue, ScriptStateRevision::initial(),
                ServerTick::initial() });
        return CanonicalScriptState(std::move(variables));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalScriptState> CanonicalScriptState::restore(
        const ServerScriptStateCatalog& catalog, std::span<const CanonicalScriptVariableState> variables) noexcept
    try
    {
        if (variables.size() != catalog.entries().size())
            return std::nullopt;
        std::size_t stringBytes = 0;
        for (std::size_t index = 0; index < variables.size(); ++index)
        {
            const auto& entry = catalog.entries()[index];
            const auto& value = variables[index];
            if (value.packageId != entry.packageId || value.id != entry.id || value.type() != entry.type()
                || !validValue(value.value))
                return std::nullopt;
            if (const auto* text = std::get_if<std::string>(&value.value))
            {
                if (text->size() > MaximumScriptStateStringBytes - stringBytes)
                    return std::nullopt;
                stringBytes += text->size();
            }
        }
        return CanonicalScriptState(std::vector<CanonicalScriptVariableState>(variables.begin(), variables.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::span<const CanonicalScriptVariableState> CanonicalScriptState::package(std::uint64_t packageId) const noexcept
    {
        const auto first
            = std::ranges::lower_bound(mVariables, packageId, {}, &CanonicalScriptVariableState::packageId);
        const auto last = std::ranges::upper_bound(mVariables, packageId, {}, &CanonicalScriptVariableState::packageId);
        return { first, last };
    }

    const CanonicalScriptVariableState* CanonicalScriptState::find(
        std::uint64_t packageId, ScriptVariableId id) const noexcept
    {
        const auto found = std::ranges::lower_bound(mVariables, std::pair(packageId, id), {},
            [](const auto& value) { return std::pair(value.packageId, value.id); });
        return found != mVariables.end() && found->packageId == packageId && found->id == id ? &*found : nullptr;
    }

    CanonicalScriptStateMutationResult compareAndSetCanonicalScriptVariable(const CanonicalScriptState& state,
        const ServerScriptStateCatalog& catalog, std::uint64_t packageId, ScriptVariableId id,
        ScriptStateRevision expectedRevision, ScriptVariableValue value, ServerTick tick) noexcept
    try
    {
        const auto* declared = catalog.find(packageId, id);
        const auto* current = state.find(packageId, id);
        if (!declared || !current)
            return CanonicalScriptStateMutationError::UnknownVariable;
        if (current->type() != static_cast<ScriptVariableType>(value.index()))
            return CanonicalScriptStateMutationError::TypeMismatch;
        if (current->revision != expectedRevision)
            return CanonicalScriptStateMutationError::RevisionMismatch;
        if (tick < current->lastChangeTick)
            return CanonicalScriptStateMutationError::TickRegression;
        if (!validValue(value))
            return CanonicalScriptStateMutationError::InvalidValue;
        if (current->value == value)
            return state;
        const auto revision = current->revision.next();
        if (!revision)
            return CanonicalScriptStateMutationError::RevisionExhausted;
        std::vector<CanonicalScriptVariableState> variables(state.variables().begin(), state.variables().end());
        auto found = std::ranges::find_if(
            variables, [&](const auto& item) { return item.packageId == packageId && item.id == id; });
        found->value = std::move(value);
        found->revision = *revision;
        found->lastChangeTick = tick;
        auto restored = CanonicalScriptState::restore(catalog, variables);
        return restored ? CanonicalScriptStateMutationResult(std::move(*restored))
                        : CanonicalScriptStateMutationResult(CanonicalScriptStateMutationError::InvalidValue);
    }
    catch (...)
    {
        return CanonicalScriptStateMutationError::InvalidValue;
    }
}
