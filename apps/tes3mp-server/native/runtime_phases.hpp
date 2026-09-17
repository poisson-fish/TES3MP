#ifndef TES3MP_NATIVE_RUNTIME_PHASES_H
#define TES3MP_NATIVE_RUNTIME_PHASES_H
namespace TES3MP::Native::Allocations
{
    enum class Phase
    {
        Outside,
        Preparation,
        Result,
        ConsumerCopy,
        Validation,
        Setup,
        Exchange,
        Rollback,
        Revalidation,
        Persistence,
        Installation,
        Retirement,
        Publication,
        Delivery,
        Count
    };

    inline thread_local Phase currentPhase = Phase::Outside;
    inline thread_local void (*visitPhase)(Phase) noexcept = nullptr;
    class InPhase
    {
        Phase mPrevious;
    public:
        explicit InPhase(Phase phase) noexcept : mPrevious(currentPhase) { set(phase); }
        ~InPhase() { currentPhase = mPrevious; }
        void set(Phase phase) noexcept
        {
            currentPhase = phase;
            if (visitPhase) visitPhase(phase);
        }
        InPhase(const InPhase&) = delete;
        InPhase& operator=(const InPhase&) = delete;
    };
}
#endif
