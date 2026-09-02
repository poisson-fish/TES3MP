#ifndef TES3MP_AUTHENTICATED_JOIN_HPP
#define TES3MP_AUTHENTICATED_JOIN_HPP

#include "canonical_state.hpp"
#include "protocol_exchange.hpp"
#include "server_command_reducer.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace TES3MP
{
    enum class AuthenticatedJoinError : std::uint8_t
    {
        DuplicatePrincipal,
        CapacityExhausted,
        IdentityExhausted,
        CanonicalStateRejected,
        SnapshotRejected,
        PreparationPending,
        StalePreparation,
    };

    struct AuthenticatedJoinIdentitySeed
    {
        SessionId nextSession;
        PlayerId nextPlayer;
        EntityId nextEntity;
    };

    struct AuthenticatedJoinResult
    {
        PrincipalId principal;
        SessionId session;
        PlayerId player;
        EntityId entity;
        LatestWinsSnapshot initialSnapshot;
    };

    struct AuthenticatedJoinPreparation
    {
        std::uint64_t id;
        AuthenticatedJoinResult join;
    };

    using AuthenticatedJoinOutcome = std::variant<AuthenticatedJoinResult, AuthenticatedJoinError>;
    using AuthenticatedJoinPrepareOutcome = std::variant<AuthenticatedJoinPreparation, AuthenticatedJoinError>;

    class AuthenticatedJoinCoordinator
    {
    public:
        static std::optional<AuthenticatedJoinCoordinator> create(
            Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer);

        AuthenticatedJoinOutcome join(
            PrincipalId principal, SessionGeneration generation, ServerTick serverTick);
        AuthenticatedJoinPrepareOutcome prepare(
            PrincipalId principal, SessionGeneration generation, ServerTick serverTick);
        AuthenticatedJoinOutcome commit(std::uint64_t preparationId);
        bool cancel(std::uint64_t preparationId) noexcept;
        const CanonicalServerState* candidateState(std::uint64_t preparationId) const noexcept;

        const CanonicalServerState& state() const noexcept { return mReducer.state(); }
        std::size_t liveBindings() const noexcept { return mPrincipals.size(); }

    private:
        AuthenticatedJoinCoordinator(Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed,
            CanonicalCommandReducer& reducer) noexcept;

        Transform mFixtureSpawn;
        AuthenticatedJoinIdentitySeed mSeed;
        bool mIdentityExhausted = false;
        CanonicalCommandReducer& mReducer;
        std::vector<PrincipalId> mPrincipals;
        struct PendingJoin
        {
            std::uint64_t id;
            CanonicalCommandReducer::PreparedJoin state;
            AuthenticatedJoinResult result;
        };
        std::optional<PendingJoin> mPending;
        std::uint64_t mNextPreparationId = 1;
    };
}

#endif
