#ifndef TES3MP_WORLD_STATE_HPP
#define TES3MP_WORLD_STATE_HPP

#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumGlobalVariables = 65'536;
    inline constexpr std::uint32_t WorldMillisecondsPerHour = 3'600'000;
    inline constexpr std::uint32_t WorldMillisecondsPerDay = 24 * WorldMillisecondsPerHour;
    inline constexpr std::uint32_t WorldTimeScaleUnitsPerOne = 1'000;
    inline constexpr std::uint32_t MaximumWorldTimeScaleUnits = 1'000'000;

    enum class GlobalVariableType : std::uint8_t
    {
        Short,
        Long,
        Float,
    };

    using GlobalVariableValue = std::variant<std::int16_t, std::int32_t, float>;

    struct GlobalVariableCatalogEntry
    {
        GlobalVariableId id;
        GlobalVariableValue initialValue;

        constexpr GlobalVariableType type() const noexcept
        {
            return static_cast<GlobalVariableType>(initialValue.index());
        }
        friend constexpr bool operator==(const GlobalVariableCatalogEntry&, const GlobalVariableCatalogEntry&) noexcept
            = default;
    };

    class GlobalVariableCatalog
    {
    public:
        static std::optional<GlobalVariableCatalog> create(
            std::span<const GlobalVariableCatalogEntry> entries) noexcept;
        std::span<const GlobalVariableCatalogEntry> entries() const noexcept { return mEntries; }
        const GlobalVariableCatalogEntry* find(GlobalVariableId id) const noexcept;
        friend bool operator==(const GlobalVariableCatalog&, const GlobalVariableCatalog&) noexcept = default;

    private:
        explicit GlobalVariableCatalog(std::vector<GlobalVariableCatalogEntry> entries) noexcept
            : mEntries(std::move(entries))
        {
        }
        std::vector<GlobalVariableCatalogEntry> mEntries;
    };

    struct CanonicalWorldTimeState
    {
        std::uint8_t day = 1;
        std::uint8_t month = 0;
        std::int32_t year = 0;
        std::uint32_t millisecondsSinceMidnight = 0;
        std::uint32_t timeScaleUnits = 30 * WorldTimeScaleUnitsPerOne;
        std::uint16_t subMillisecondRemainder = 0;
        WorldTimeRevision revision = WorldTimeRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();
        ServerTick lastAdvanceTick = ServerTick::initial();

        double hour() const noexcept
        {
            return static_cast<double>(millisecondsSinceMidnight) / WorldMillisecondsPerHour;
        }
        double timeScale() const noexcept
        {
            return static_cast<double>(timeScaleUnits) / WorldTimeScaleUnitsPerOne;
        }
        friend constexpr bool operator==(const CanonicalWorldTimeState&, const CanonicalWorldTimeState&) noexcept
            = default;
    };

    struct CanonicalGlobalVariableState
    {
        GlobalVariableId id;
        GlobalVariableValue value;
        GlobalVariableRevision revision = GlobalVariableRevision::initial();
        ServerTick lastChangeTick = ServerTick::initial();

        constexpr GlobalVariableType type() const noexcept
        {
            return static_cast<GlobalVariableType>(value.index());
        }
        friend constexpr bool operator==(const CanonicalGlobalVariableState&,
            const CanonicalGlobalVariableState&) noexcept = default;
    };

    class CanonicalWorldState
    {
    public:
        static std::optional<CanonicalWorldState> create(
            CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
        static std::optional<CanonicalWorldState> initial(
            CanonicalWorldTimeState time, const GlobalVariableCatalog& catalog) noexcept;

        constexpr const CanonicalWorldTimeState& time() const noexcept { return mTime; }
        std::span<const CanonicalGlobalVariableState> globals() const noexcept { return mGlobals; }
        const CanonicalGlobalVariableState* find(GlobalVariableId id) const noexcept;
        friend bool operator==(const CanonicalWorldState&, const CanonicalWorldState&) noexcept = default;

    private:
        CanonicalWorldState(CanonicalWorldTimeState time, std::vector<CanonicalGlobalVariableState> globals) noexcept
            : mTime(time)
            , mGlobals(std::move(globals))
        {
        }
        CanonicalWorldTimeState mTime;
        std::vector<CanonicalGlobalVariableState> mGlobals;
    };

    enum class CanonicalWorldMutationError : std::uint8_t
    {
        InvalidState,
        TickRegression,
        RevisionExhausted,
        ArithmeticOverflow,
        UnknownGlobal,
        TypeMismatch,
        RevisionMismatch,
        CatalogMismatch,
    };

    using CanonicalWorldMutationResult = std::variant<CanonicalWorldState, CanonicalWorldMutationError>;

    CanonicalWorldMutationResult advanceCanonicalWorldTime(
        const CanonicalWorldState& state, ServerTick tick, std::uint64_t tickIntervalMilliseconds) noexcept;
    CanonicalWorldMutationResult setCanonicalWorldTime(const CanonicalWorldState& state,
        WorldTimeRevision expectedRevision, CanonicalWorldTimeState replacement, ServerTick tick) noexcept;
    CanonicalWorldMutationResult setCanonicalGlobal(const CanonicalWorldState& state,
        const GlobalVariableCatalog& catalog, GlobalVariableId id, GlobalVariableRevision expectedRevision,
        GlobalVariableValue value, ServerTick tick) noexcept;
    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& catalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept;
}

#endif
