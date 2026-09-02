#ifndef TES3MP_SERVER_LIFECYCLE_HPP
#define TES3MP_SERVER_LIFECYCLE_HPP

#include "monotonic_clock.hpp"
#include "server_command_reducer.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace TES3MP
{
    enum class ServerLifecycleError : std::uint8_t
    {
        PreparationPending,
        StalePreparation,
        UnknownSession,
        UnknownPrincipal,
        AlreadyDisconnected,
        AlreadyLive,
        DeadlineReached,
        DeadlineOverflow,
        GenerationExhausted,
        CapacityExhausted,
        CanonicalStateRejected,
    };

    enum class ServerLifecycleAction : std::uint8_t
    {
        Disconnect,
        Resume,
        Expire,
    };

    struct ServerLifecyclePreparation
    {
        std::uint64_t id;
        ServerLifecycleAction action;
        PrincipalId principal;
        SessionId session;
        PlayerId player;
        SessionGeneration generation;
        MonotonicInstant deadline;
    };

    using ServerLifecyclePrepareResult = std::variant<ServerLifecyclePreparation, ServerLifecycleError>;

    class ServerLifecycleCoordinator
    {
    public:
        static std::optional<ServerLifecycleCoordinator> create(
            std::uint64_t graceNanoseconds, CanonicalCommandReducer& reducer) noexcept;

        bool registerJoined(PrincipalId principal, SessionId session) noexcept;
        ServerLifecyclePrepareResult prepareDisconnect(
            SessionId session, MonotonicInstant now, ServerTick tick);
        ServerLifecyclePrepareResult prepareResume(
            PrincipalId principal, SessionId session, MonotonicInstant now, ServerTick tick);
        ServerLifecyclePrepareResult prepareNextExpiration(MonotonicInstant now, ServerTick tick);
        bool commit(std::uint64_t preparationId) noexcept;
        bool cancel(std::uint64_t preparationId) noexcept;
        const CanonicalServerState* candidateState(std::uint64_t preparationId) const noexcept;
        std::size_t liveCount() const noexcept;
        std::size_t hiddenCount() const noexcept;

    private:
        struct Binding
        {
            PrincipalId principal;
            CanonicalSessionProgress session;
            bool live;
            MonotonicInstant deadline;
        };
        struct Pending
        {
            ServerLifecyclePreparation publicValue;
            std::size_t bindingIndex;
            CanonicalSessionProgress replacementSession;
            CanonicalCommandReducer::PreparedLifecycle canonical;
        };

        ServerLifecycleCoordinator(std::uint64_t graceNanoseconds, CanonicalCommandReducer& reducer) noexcept;

        std::uint64_t mGraceNanoseconds;
        CanonicalCommandReducer& mReducer;
        std::vector<Binding> mBindings;
        std::optional<Pending> mPending;
        std::uint64_t mNextPreparationId = 1;
    };
}

#endif
