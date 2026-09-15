#ifndef TES3MP_NATIVE_TEST_ALLOCATIONS_H
#define TES3MP_NATIVE_TEST_ALLOCATIONS_H

#include <array>
#include <cstddef>

namespace MWWorld::Testing::Allocations
{
    // Linked only into tes3mp_native_loadout_tests. Observe C++ allocations on
    // the calling thread; fixture construction, assertions and logging stay outside.
    enum class Phase
    {
        Outside,
        Validation,
        Setup,
        Exchange,
        Rollback,
        Revalidation,
        Count
    };
    struct Trace
    {
        std::array<std::size_t, static_cast<std::size_t>(Phase::Count)> mAllocations{}, mVisits{};
        std::size_t mTotal = 0, mFailures = 0;
        Phase mFailedPhase = Phase::Outside;
        std::size_t allocations(Phase phase) const { return mAllocations[static_cast<std::size_t>(phase)]; }
        std::size_t visits(Phase phase) const { return mVisits[static_cast<std::size_t>(phase)]; }
    };
    class Observe
    {
    public:
        // Zero counts without failing; otherwise fail exactly the one-based Nth
        // allocation. Keep observing unwinding/discard after the injected failure.
        explicit Observe(Trace& trace, std::size_t failAt = 0) noexcept;
        ~Observe();
        Observe(const Observe&) = delete;
        Observe& operator=(const Observe&) = delete;
    };
    class InPhase
    {
        Phase mPrevious;

    public:
        explicit InPhase(Phase phase) noexcept;
        ~InPhase();
        void set(Phase phase) noexcept;
        InPhase(const InPhase&) = delete;
        InPhase& operator=(const InPhase&) = delete;
    };
}

#endif
