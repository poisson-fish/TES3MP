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
        AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer) noexcept
        : mFixtureSpawn(fixtureSpawn)
        , mSeed(seed)
        , mReducer(reducer)
    {
        mPrincipals.reserve(MaximumCanonicalActiveSessions);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(
        Transform fixtureSpawn, AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer)
    {
        return AuthenticatedJoinCoordinator(fixtureSpawn, seed, reducer);
    }

    AuthenticatedJoinOutcome AuthenticatedJoinCoordinator::join(
        PrincipalId principal, SessionGeneration generation, ServerTick serverTick)
    {
        auto prepared = prepare(principal, generation, serverTick);
        auto* value = std::get_if<AuthenticatedJoinPreparation>(&prepared);
        if (!value)
            return std::get<AuthenticatedJoinError>(prepared);
        return commit(value->id);
    }

    AuthenticatedJoinPrepareOutcome AuthenticatedJoinCoordinator::prepare(
        PrincipalId principal, SessionGeneration generation, ServerTick serverTick)
    {
        if (mPending)
            return AuthenticatedJoinError::PreparationPending;
        if (std::find(mPrincipals.begin(), mPrincipals.end(), principal) != mPrincipals.end())
            return AuthenticatedJoinError::DuplicatePrincipal;
        if (mReducer.state().players().size() >= MaximumCanonicalPlayerEntities
            || mReducer.state().activeSessions().size() >= MaximumCanonicalActiveSessions)
            return AuthenticatedJoinError::CapacityExhausted;

        if (mIdentityExhausted)
            return AuthenticatedJoinError::IdentityExhausted;
        const auto nextSession = advance(mSeed.nextSession);
        const auto nextPlayer = advance(mSeed.nextPlayer);
        const auto nextEntity = advance(mSeed.nextEntity);

        CanonicalPlayerEntityState canonicalPlayer(mSeed.nextPlayer, mSeed.nextEntity, mFixtureSpawn,
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), serverTick);
        CanonicalSessionProgress canonicalSession(
            mSeed.nextSession, generation, mSeed.nextPlayer, mSeed.nextEntity, std::nullopt);
        auto candidate = mReducer.prepareJoin(canonicalPlayer, canonicalSession, serverTick);
        if (!candidate)
            return AuthenticatedJoinError::CanonicalStateRejected;

        std::vector<SpatialEntitySnapshot> entries;
        entries.reserve(candidate->candidateState().players().size());
        for (const auto& visible : candidate->candidateState().players())
            if (visible.transform().cell() == mFixtureSpawn.cell())
                entries.emplace_back(serverTick, visible.playerId(), visible.entityId(), visible.entityRevision(),
                    visible.authorityEpoch(), visible.transform(), visible.linearVelocity());
        auto view = SpatialWorldView::create(entries);
        auto* acceptedView = std::get_if<SpatialWorldView>(&view);
        if (!acceptedView)
            return AuthenticatedJoinError::SnapshotRejected;

        const auto session = mSeed.nextSession;
        const auto player = mSeed.nextPlayer;
        const auto entity = mSeed.nextEntity;
        LatestWinsSnapshot snapshot(
            LatestWinsSnapshotHeader(session, generation, candidate->candidateRevision(), std::nullopt),
            std::move(*acceptedView));

        AuthenticatedJoinResult result{ principal, session, player, entity, std::move(snapshot) };
        const auto preparationId = mNextPreparationId++;
        mPending.emplace(PendingJoin{ preparationId, std::move(*candidate), result });
        return AuthenticatedJoinPreparation{ preparationId, std::move(result) };
    }

    AuthenticatedJoinOutcome AuthenticatedJoinCoordinator::commit(std::uint64_t preparationId)
    {
        if (!mPending || mPending->id != preparationId)
            return AuthenticatedJoinError::StalePreparation;

        auto result = std::move(mPending->result);
        if (!mReducer.commit(std::move(mPending->state)))
        {
            mPending.reset();
            return AuthenticatedJoinError::StalePreparation;
        }
        mPending.reset();
        mPrincipals.push_back(result.principal);
        const auto nextSession = advance(mSeed.nextSession);
        const auto nextPlayer = advance(mSeed.nextPlayer);
        const auto nextEntity = advance(mSeed.nextEntity);
        if (nextSession && nextPlayer && nextEntity)
            mSeed = { *nextSession, *nextPlayer, *nextEntity };
        else
            mIdentityExhausted = true;
        return result;
    }

    bool AuthenticatedJoinCoordinator::cancel(std::uint64_t preparationId) noexcept
    {
        if (!mPending || mPending->id != preparationId)
            return false;
        mPending.reset();
        return true;
    }

    const CanonicalServerState* AuthenticatedJoinCoordinator::candidateState(std::uint64_t preparationId) const noexcept
    {
        return mPending && mPending->id == preparationId ? &mPending->state.candidateState() : nullptr;
    }

    std::optional<CanonicalRevision> AuthenticatedJoinCoordinator::candidateRevision(
        std::uint64_t preparationId) const noexcept
    {
        return mPending && mPending->id == preparationId
            ? std::optional<CanonicalRevision>(mPending->state.candidateRevision()) : std::nullopt;
    }
}
