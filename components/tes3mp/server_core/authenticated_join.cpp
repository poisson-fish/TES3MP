#include <tes3mp/authenticated_join.hpp>

#include <algorithm>
#include <limits>

namespace
{
    template <class Value>
    std::optional<Value> advance(Value value) noexcept
    {
        if (value.value() == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        return Value::fromValue(value.value() + 1);
    }
}

namespace TES3MP
{
    AuthenticatedJoinCoordinator::AuthenticatedJoinCoordinator(Transform fixtureSpawn,
        AuthenticatedJoinIdentitySeed seed, CanonicalServerState state) noexcept
        : mFixtureSpawn(fixtureSpawn)
        , mSeed(seed)
        , mState(std::move(state))
    {
        mPrincipals.reserve(MaximumCanonicalActiveSessions);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(
        Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed)
    {
        auto empty = createCanonicalServerState({}, {});
        if (const auto* state = std::get_if<CanonicalServerState>(&empty))
            return AuthenticatedJoinCoordinator(fixtureSpawn, seed, *state);
        return std::nullopt;
    }

    AuthenticatedJoinOutcome AuthenticatedJoinCoordinator::join(
        PrincipalId principal, SessionGeneration generation, ServerTick serverTick)
    {
        if (std::find(mPrincipals.begin(), mPrincipals.end(), principal) != mPrincipals.end())
            return AuthenticatedJoinError::DuplicatePrincipal;
        if (mState.players().size() >= MaximumCanonicalPlayerEntities
            || mState.activeSessions().size() >= MaximumCanonicalActiveSessions)
            return AuthenticatedJoinError::CapacityExhausted;

        if (mIdentityExhausted)
            return AuthenticatedJoinError::IdentityExhausted;
        const auto nextSession = advance(mSeed.nextSession);
        const auto nextPlayer = advance(mSeed.nextPlayer);
        const auto nextEntity = advance(mSeed.nextEntity);

        std::vector<CanonicalPlayerEntityState> players(mState.players().begin(), mState.players().end());
        players.emplace_back(mSeed.nextPlayer, mSeed.nextEntity, mFixtureSpawn, LinearVelocity3(0, 0, 0),
            EntityRevision::initial(), AuthorityEpoch::initial(), serverTick);
        std::vector<CanonicalSessionProgress> sessions(
            mState.activeSessions().begin(), mState.activeSessions().end());
        sessions.emplace_back(mSeed.nextSession, generation, mSeed.nextPlayer, mSeed.nextEntity, std::nullopt);

        auto candidate = createCanonicalServerState(players, sessions);
        auto* accepted = std::get_if<CanonicalServerState>(&candidate);
        if (!accepted)
            return AuthenticatedJoinError::CanonicalStateRejected;

        const SpatialEntitySnapshot entry(serverTick, mSeed.nextEntity, EntityRevision::initial(),
            AuthorityEpoch::initial(), mFixtureSpawn, LinearVelocity3(0, 0, 0));
        auto view = SpatialWorldView::create(std::span<const SpatialEntitySnapshot>(&entry, 1));
        auto* acceptedView = std::get_if<SpatialWorldView>(&view);
        if (!acceptedView)
            return AuthenticatedJoinError::SnapshotRejected;

        const auto session = mSeed.nextSession;
        const auto player = mSeed.nextPlayer;
        const auto entity = mSeed.nextEntity;
        LatestWinsSnapshot snapshot(
            LatestWinsSnapshotHeader(session, generation, serverTick, std::nullopt), std::move(*acceptedView));

        mState = std::move(*accepted);
        mPrincipals.push_back(principal);
        if (nextSession && nextPlayer && nextEntity)
            mSeed = { *nextSession, *nextPlayer, *nextEntity };
        else
            mIdentityExhausted = true;
        return AuthenticatedJoinResult{ principal, session, player, entity, std::move(snapshot) };
    }
}
