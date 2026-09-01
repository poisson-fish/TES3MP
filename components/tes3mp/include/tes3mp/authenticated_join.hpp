#ifndef TES3MP_AUTHENTICATED_JOIN_HPP
#define TES3MP_AUTHENTICATED_JOIN_HPP

#include "canonical_state.hpp"
#include "protocol_exchange.hpp"

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

    using AuthenticatedJoinOutcome = std::variant<AuthenticatedJoinResult, AuthenticatedJoinError>;

    class AuthenticatedJoinCoordinator
    {
    public:
        static std::optional<AuthenticatedJoinCoordinator> create(
            Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed);

        AuthenticatedJoinOutcome join(
            PrincipalId principal, SessionGeneration generation, ServerTick serverTick);

        const CanonicalServerState& state() const noexcept { return mState; }
        std::size_t liveBindings() const noexcept { return mPrincipals.size(); }

    private:
        AuthenticatedJoinCoordinator(Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed,
            CanonicalServerState state) noexcept;

        Transform mFixtureSpawn;
        AuthenticatedJoinIdentitySeed mSeed;
        bool mIdentityExhausted = false;
        CanonicalServerState mState;
        std::vector<PrincipalId> mPrincipals;
    };
}

#endif
