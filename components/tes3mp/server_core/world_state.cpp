#include <tes3mp/world_state.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace
{
    using namespace TES3MP;

    bool validValue(const GlobalVariableValue& value) noexcept
    {
        const auto* number = std::get_if<float>(&value);
        return !number || std::isfinite(*number);
    }

    bool validTime(const CanonicalWorldTimeState& time) noexcept
    {
        return time.day >= 1 && time.day <= 30 && time.month <= 11 && time.year >= 0
            && time.millisecondsSinceMidnight < WorldMillisecondsPerDay
            && time.timeScaleUnits <= MaximumWorldTimeScaleUnits
            && time.subMillisecondRemainder < WorldTimeScaleUnitsPerOne
            && time.lastChangeTick <= time.lastAdvanceTick;
    }

    bool sameValue(const GlobalVariableValue& left, const GlobalVariableValue& right) noexcept
    {
        if (left.index() != right.index())
            return false;
        if (const auto* value = std::get_if<float>(&left))
            return std::bit_cast<std::uint32_t>(*value) == std::bit_cast<std::uint32_t>(std::get<float>(right));
        return left == right;
    }
}

namespace TES3MP
{
    std::optional<GlobalVariableCatalog> GlobalVariableCatalog::create(
        std::span<const GlobalVariableCatalogEntry> entries) noexcept
    try
    {
        if (entries.size() > MaximumGlobalVariables)
            return std::nullopt;
        std::vector<GlobalVariableCatalogEntry> copy(entries.begin(), entries.end());
        for (std::size_t index = 0; index < copy.size(); ++index)
            if (!validValue(copy[index].initialValue)
                || std::ranges::any_of(std::span(copy).first(index),
                    [&](const auto& prior) { return prior.id == copy[index].id; }))
                return std::nullopt;
        return GlobalVariableCatalog(std::move(copy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const GlobalVariableCatalogEntry* GlobalVariableCatalog::find(GlobalVariableId id) const noexcept
    {
        const auto found = std::ranges::find(mEntries, id, &GlobalVariableCatalogEntry::id);
        return found == mEntries.end() ? nullptr : &*found;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::create(
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept
    try
    {
        if (!validTime(time) || globals.size() > MaximumGlobalVariables)
            return std::nullopt;
        std::vector<CanonicalGlobalVariableState> copy(globals.begin(), globals.end());
        for (std::size_t index = 0; index < copy.size(); ++index)
            if (!validValue(copy[index].value) || std::ranges::any_of(std::span(copy).first(index),
                    [&](const auto& prior) { return prior.id == copy[index].id; }))
                return std::nullopt;
        return CanonicalWorldState(time, std::move(copy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::initial(
        CanonicalWorldTimeState time, const GlobalVariableCatalog& catalog) noexcept
    try
    {
        std::vector<CanonicalGlobalVariableState> globals;
        globals.reserve(catalog.entries().size());
        for (const auto& entry : catalog.entries())
            globals.push_back({ entry.id, entry.initialValue, GlobalVariableRevision::initial(), time.lastAdvanceTick });
        return create(time, globals);
    }
    catch (...)
    {
        return std::nullopt;
    }

    const CanonicalGlobalVariableState* CanonicalWorldState::find(GlobalVariableId id) const noexcept
    {
        const auto found = std::ranges::find(mGlobals, id, &CanonicalGlobalVariableState::id);
        return found == mGlobals.end() ? nullptr : &*found;
    }

    CanonicalWorldMutationResult advanceCanonicalWorldTime(
        const CanonicalWorldState& state, ServerTick tick, std::uint64_t tickIntervalMilliseconds) noexcept
    try
    {
        const auto& current = state.time();
        if (!validTime(current) || tick < current.lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        if (tick == current.lastAdvanceTick)
            return state;
        const auto elapsedTicks = tick.value() - current.lastAdvanceTick.value();
        if (tickIntervalMilliseconds != 0
            && elapsedTicks > std::numeric_limits<std::uint64_t>::max() / tickIntervalMilliseconds)
            return CanonicalWorldMutationError::ArithmeticOverflow;
        const auto realMilliseconds = elapsedTicks * tickIntervalMilliseconds;
        if (current.timeScaleUnits != 0
            && realMilliseconds > (std::numeric_limits<std::uint64_t>::max() - current.subMillisecondRemainder)
                    / current.timeScaleUnits)
            return CanonicalWorldMutationError::ArithmeticOverflow;
        const auto scaled = realMilliseconds * current.timeScaleUnits + current.subMillisecondRemainder;
        const auto elapsedWorldMilliseconds = scaled / WorldTimeScaleUnitsPerOne;
        CanonicalWorldTimeState next = current;
        next.subMillisecondRemainder = static_cast<std::uint16_t>(scaled % WorldTimeScaleUnitsPerOne);
        next.lastAdvanceTick = tick;
        if (elapsedWorldMilliseconds != 0)
        {
            if (!current.revision.next())
                return CanonicalWorldMutationError::RevisionExhausted;
            if (elapsedWorldMilliseconds > std::numeric_limits<std::uint64_t>::max()
                    - current.millisecondsSinceMidnight)
                return CanonicalWorldMutationError::ArithmeticOverflow;
            const auto total
                = static_cast<std::uint64_t>(current.millisecondsSinceMidnight) + elapsedWorldMilliseconds;
            const auto elapsedDays = total / WorldMillisecondsPerDay;
            next.millisecondsSinceMidnight = static_cast<std::uint32_t>(total % WorldMillisecondsPerDay);
            const auto currentDay = static_cast<std::uint64_t>(current.year) * 360
                + static_cast<std::uint64_t>(current.month) * 30 + current.day - 1;
            if (elapsedDays > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) * 360
                    + 359 - currentDay)
                return CanonicalWorldMutationError::ArithmeticOverflow;
            const auto absoluteDay = currentDay + elapsedDays;
            next.year = static_cast<std::int32_t>(absoluteDay / 360);
            next.month = static_cast<std::uint8_t>((absoluteDay % 360) / 30);
            next.day = static_cast<std::uint8_t>((absoluteDay % 30) + 1);
            next.revision = *current.revision.next();
            next.lastChangeTick = tick;
        }
        auto result = CanonicalWorldState::create(next, state.globals());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalWorldTime(const CanonicalWorldState& state,
        WorldTimeRevision expectedRevision, CanonicalWorldTimeState replacement, ServerTick tick) noexcept
    try
    {
        const auto& current = state.time();
        if (expectedRevision != current.revision)
            return CanonicalWorldMutationError::RevisionMismatch;
        if (tick < current.lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        const auto revision = current.revision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        replacement.revision = *revision;
        replacement.lastChangeTick = tick;
        replacement.lastAdvanceTick = tick;
        replacement.subMillisecondRemainder = 0;
        auto result = CanonicalWorldState::create(replacement, state.globals());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalGlobal(const CanonicalWorldState& state,
        const GlobalVariableCatalog& catalog, GlobalVariableId id, GlobalVariableRevision expectedRevision,
        GlobalVariableValue value, ServerTick tick) noexcept
    try
    {
        const auto* declaration = catalog.find(id);
        const auto* current = state.find(id);
        if (!declaration || !current)
            return CanonicalWorldMutationError::UnknownGlobal;
        if (declaration->type() != static_cast<GlobalVariableType>(value.index()) || current->type() != declaration->type()
            || !validValue(value))
            return CanonicalWorldMutationError::TypeMismatch;
        if (current->revision != expectedRevision)
            return CanonicalWorldMutationError::RevisionMismatch;
        if (tick < current->lastChangeTick)
            return CanonicalWorldMutationError::TickRegression;
        if (sameValue(current->value, value))
            return state;
        const auto revision = current->revision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        std::vector<CanonicalGlobalVariableState> globals(state.globals().begin(), state.globals().end());
        auto found = std::ranges::find(globals, id, &CanonicalGlobalVariableState::id);
        found->value = std::move(value);
        found->revision = *revision;
        found->lastChangeTick = tick;
        auto result = CanonicalWorldState::create(state.time(), globals);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& catalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept
    try
    {
        if (catalog.entries().size() != globals.size())
            return CanonicalWorldMutationError::CatalogMismatch;
        for (std::size_t index = 0; index < globals.size(); ++index)
            if (catalog.entries()[index].id != globals[index].id
                || catalog.entries()[index].type() != globals[index].type())
                return CanonicalWorldMutationError::CatalogMismatch;
        auto result = CanonicalWorldState::create(time, globals);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }
}
