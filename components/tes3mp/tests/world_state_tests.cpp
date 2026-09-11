#include <tes3mp/world_state.hpp>

#include <array>
#include <bit>
#include <iostream>
#include <limits>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    GlobalVariableCatalog catalog()
    {
        const std::array entries{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int16_t{ 2 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(2), std::int32_t{ 3 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(3), 4.5f } };
        return GlobalVariableCatalog::create(entries).value();
    }

    CanonicalWorldState world()
    {
        CanonicalWorldTimeState time;
        time.day = 30;
        time.month = 11;
        time.year = 427;
        time.millisecondsSinceMidnight = 23 * WorldMillisecondsPerHour + 59 * 60 * 1000;
        time.timeScaleUnits = 30 * WorldTimeScaleUnitsPerOne;
        return CanonicalWorldState::initial(time, catalog()).value();
    }

    bool time_advances_exactly_across_calendar_boundaries()
    {
        const auto advanced = advanceCanonicalWorldTime(world(), id<ServerTick>(125), 16);
        const auto* result = std::get_if<CanonicalWorldState>(&advanced);
        return result && result->time().year == 428 && result->time().month == 0 && result->time().day == 1
            && result->time().millisecondsSinceMidnight == 0 && result->time().revision.value() == 2
            && result->time().lastChangeTick == id<ServerTick>(125)
            && result->time().lastAdvanceTick == id<ServerTick>(125);
    }

    bool typed_global_updates_preserve_revision_and_tick()
    {
        const auto initial = world();
        const auto changed = setCanonicalGlobal(initial, catalog(), id<GlobalVariableId>(2),
            GlobalVariableRevision::initial(), std::int32_t{ 99 }, id<ServerTick>(7));
        const auto* result = std::get_if<CanonicalWorldState>(&changed);
        const auto* global = result ? result->find(id<GlobalVariableId>(2)) : nullptr;
        if (!global || std::get<std::int32_t>(global->value) != 99 || global->revision.value() != 2
            || global->lastChangeTick != id<ServerTick>(7))
            return false;
        return std::get<CanonicalWorldMutationError>(setCanonicalGlobal(*result, catalog(), id<GlobalVariableId>(2),
                   GlobalVariableRevision::initial(), std::int32_t{ 100 }, id<ServerTick>(8)))
                == CanonicalWorldMutationError::RevisionMismatch
            && std::get<CanonicalWorldMutationError>(setCanonicalGlobal(*result, catalog(), id<GlobalVariableId>(2),
                   id<GlobalVariableRevision>(2), 1.f, id<ServerTick>(8)))
                == CanonicalWorldMutationError::TypeMismatch;
    }

    bool restore_requires_the_exact_ordered_typed_catalog()
    {
        const auto initial = world();
        auto missing = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        missing.pop_back();
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), missing))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto extra = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        extra.push_back({ id<GlobalVariableId>(4), std::int16_t{ 0 }, GlobalVariableRevision::initial(),
            ServerTick::initial() });
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), extra))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto reordered = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        std::swap(reordered[0], reordered[1]);
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), reordered))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto mismatched = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        mismatched[1].value = 3.f;
        return std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), mismatched))
            == CanonicalWorldMutationError::CatalogMismatch;
    }

    bool invalid_catalog_and_nonfinite_values_reject_without_partial_state()
    {
        const std::array duplicate{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int16_t{ 1 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 2 } } };
        const std::array nonfinite{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1),
            std::bit_cast<float>(std::uint32_t{ 0x7f800000 }) } };
        return !GlobalVariableCatalog::create(duplicate) && !GlobalVariableCatalog::create(nonfinite);
    }
}

int main()
{
    const std::array tests{
        std::pair{ "time_advances_exactly_across_calendar_boundaries",
            &time_advances_exactly_across_calendar_boundaries },
        std::pair{ "typed_global_updates_preserve_revision_and_tick",
            &typed_global_updates_preserve_revision_and_tick },
        std::pair{ "restore_requires_the_exact_ordered_typed_catalog",
            &restore_requires_the_exact_ordered_typed_catalog },
        std::pair{ "invalid_catalog_and_nonfinite_values_reject_without_partial_state",
            &invalid_catalog_and_nonfinite_values_reject_without_partial_state },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
