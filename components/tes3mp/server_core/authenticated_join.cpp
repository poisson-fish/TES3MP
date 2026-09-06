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
    AuthenticatedJoinCoordinator::AuthenticatedJoinCoordinator(Transform spawn,
        AppearanceId appearance, AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer,
        ContentManifest contentManifest, PlayerIdentityRegistry* playerIdentities) noexcept
        : mSpawn(spawn)
        , mAppearance(appearance)
        , mSeed(seed)
        , mReducer(reducer)
        , mContentManifest(contentManifest)
        , mPlayerIdentities(playerIdentities)
    {
        mPrincipals.reserve(MaximumCanonicalActiveSessions);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(
        Transform spawn, AppearanceId appearance, AuthenticatedJoinIdentitySeed seed,
        CanonicalCommandReducer& reducer)
    {
        return AuthenticatedJoinCoordinator(
            spawn, appearance, seed, reducer, testContentManifest(), nullptr);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(Transform spawn,
        ContentManifest contentManifest, SessionId nextSession, PlayerIdentityRegistry& playerIdentities,
        CanonicalCommandReducer& reducer)
    {
        return AuthenticatedJoinCoordinator(spawn, contentManifest.defaultAppearance(),
            { nextSession, PlayerId::fromValue(1).value(), EntityId::fromValue(1).value() },
            reducer, contentManifest, &playerIdentities);
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
        if (std::any_of(mPrincipals.begin(), mPrincipals.end(),
                [principal](const auto& binding) { return binding.principal == principal; }))
            return AuthenticatedJoinError::DuplicatePrincipal;
        if (mReducer.state().players().size() >= MaximumCanonicalPlayerEntities
            || mReducer.state().activeSessions().size() >= MaximumCanonicalActiveSessions)
            return AuthenticatedJoinError::CapacityExhausted;

        if (mIdentityExhausted)
            return AuthenticatedJoinError::IdentityExhausted;
        if (mPlayerIdentities)
        {
            auto identity = mPlayerIdentities->prepareCreate(mContentManifest);
            auto* prepared = std::get_if<PreparedPlayerIdentity>(&identity);
            if (!prepared)
                return std::get<PlayerIdentityError>(identity) == PlayerIdentityError::Full
                    ? AuthenticatedJoinError::CapacityExhausted : AuthenticatedJoinError::IdentityExhausted;
            auto result = prepareIdentity(principal, prepared->claim, true, prepared->id, generation, serverTick);
            if (!std::holds_alternative<AuthenticatedJoinPreparation>(result))
                (void)mPlayerIdentities->cancel(prepared->id);
            return result;
        }
        const AuthenticatedAdmission::PlayerClaim claim{ mSeed.nextPlayer, mSeed.nextEntity,
            mAppearance, mContentManifest.id() };
        return prepareIdentity(principal, claim, true, std::nullopt, generation, serverTick);
    }

    AuthenticatedJoinPrepareOutcome AuthenticatedJoinCoordinator::prepareReattach(PrincipalId principal,
        AuthenticatedAdmission::PlayerClaim claim, SessionGeneration generation, ServerTick serverTick)
    {
        if (!mPlayerIdentities || claim.contentManifest != mContentManifest.id())
            return AuthenticatedJoinError::CanonicalStateRejected;
        if (mPending)
            return AuthenticatedJoinError::PreparationPending;
        if (std::any_of(mPrincipals.begin(), mPrincipals.end(), [&](const auto& binding) {
                return binding.principal == principal || binding.player == claim.player;
            }))
            return AuthenticatedJoinError::DuplicatePrincipal;
        if (mReducer.state().activeSessions().size() >= MaximumCanonicalActiveSessions)
            return AuthenticatedJoinError::CapacityExhausted;
        return prepareIdentity(principal, claim, false, std::nullopt, generation, serverTick);
    }

    AuthenticatedJoinPrepareOutcome AuthenticatedJoinCoordinator::prepareIdentity(PrincipalId principal,
        AuthenticatedAdmission::PlayerClaim claim, bool createsIdentity,
        std::optional<std::uint64_t> identityPreparation, SessionGeneration generation, ServerTick serverTick)
    {
        if (mNextPreparationId == 0)
            return AuthenticatedJoinError::IdentityExhausted;

        CanonicalPlayerEntityState canonicalPlayer(claim.player, claim.entity, claim.appearance, mSpawn,
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), serverTick);
        if (const auto* existing = mReducer.state().findPlayer(claim.player))
        {
            if (createsIdentity || existing->entityId() != claim.entity || existing->appearanceId() != claim.appearance)
                return AuthenticatedJoinError::CanonicalStateRejected;
            canonicalPlayer = *existing;
        }
        CanonicalSessionProgress canonicalSession(mSeed.nextSession, generation, claim.player, claim.entity, std::nullopt);
        auto candidate = mReducer.prepareJoin(canonicalPlayer, canonicalSession, serverTick);
        if (!candidate)
            return AuthenticatedJoinError::CanonicalStateRejected;

        std::vector<SpatialEntitySnapshot> entries;
        entries.reserve(candidate->candidateState().players().size());
        for (const auto& visible : candidate->candidateState().players())
            if (visible.transform().cell() == mSpawn.cell())
                entries.emplace_back(serverTick, visible.playerId(), visible.entityId(), visible.appearanceId(),
                    visible.entityRevision(), visible.authorityEpoch(), visible.transform(), visible.linearVelocity());
        auto view = SpatialWorldView::create(entries);
        auto* acceptedView = std::get_if<SpatialWorldView>(&view);
        if (!acceptedView)
            return AuthenticatedJoinError::SnapshotRejected;

        const auto session = mSeed.nextSession;
        const auto player = claim.player;
        const auto entity = claim.entity;
        LatestWinsSnapshot snapshot(
            LatestWinsSnapshotHeader(session, generation, player, entity, candidate->candidateRevision(), std::nullopt),
            std::move(*acceptedView));

        AuthenticatedJoinResult result{ principal, session, player, entity, std::move(snapshot) };
        const auto preparationId = mNextPreparationId++;
        mPending.emplace(PendingJoin{ preparationId, std::move(*candidate), result, identityPreparation });
        return AuthenticatedJoinPreparation{ preparationId, std::move(result) };
    }

    AuthenticatedJoinOutcome AuthenticatedJoinCoordinator::commit(std::uint64_t preparationId)
    {
        if (!mPending || mPending->id != preparationId)
            return AuthenticatedJoinError::StalePreparation;

        auto result = std::move(mPending->result);
        const auto identityPreparation = mPending->identityPreparation;
        if (identityPreparation && (!mPlayerIdentities || !mPlayerIdentities->commit(*identityPreparation)))
        {
            if (mPlayerIdentities)
                (void)mPlayerIdentities->cancel(*identityPreparation);
            mPending.reset();
            return AuthenticatedJoinError::CanonicalStateRejected;
        }
        if (!mReducer.commit(std::move(mPending->state)))
        {
            if (identityPreparation && mPlayerIdentities)
                (void)mPlayerIdentities->rollback(*identityPreparation);
            mPending.reset();
            return AuthenticatedJoinError::StalePreparation;
        }
        mPending.reset();
        if (identityPreparation && mPlayerIdentities)
            (void)mPlayerIdentities->finalize(*identityPreparation);
        mPrincipals.push_back({ result.principal, result.player });
        const auto nextSession = advance(mSeed.nextSession);
        const auto nextPlayer = mPlayerIdentities ? std::optional<PlayerId>(mSeed.nextPlayer) : advance(mSeed.nextPlayer);
        const auto nextEntity = mPlayerIdentities ? std::optional<EntityId>(mSeed.nextEntity) : advance(mSeed.nextEntity);
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
        if (mPending->identityPreparation && mPlayerIdentities)
            (void)mPlayerIdentities->cancel(*mPending->identityPreparation);
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

    std::optional<CanonicalStateVersion> AuthenticatedJoinCoordinator::candidateStateVersion(
        std::uint64_t preparationId) const noexcept
    {
        if (!mPending || mPending->id != preparationId)
            return std::nullopt;
        return mPending->state.candidateStateVersion();
    }

    std::optional<PlayerCredential> AuthenticatedJoinCoordinator::copyPendingPlayerCredential(
        std::uint64_t preparationId) const noexcept
    {
        if (!mPending || mPending->id != preparationId || !mPending->identityPreparation || !mPlayerIdentities)
            return std::nullopt;
        return mPlayerIdentities->copyPreparedCredential(*mPending->identityPreparation);
    }

    bool AuthenticatedJoinCoordinator::pendingCreatesPersistentIdentity(std::uint64_t preparationId) const noexcept
    {
        return mPending && mPending->id == preparationId && mPending->identityPreparation.has_value();
    }

    bool AuthenticatedJoinCoordinator::releasePrincipal(PrincipalId principal) noexcept
    {
        const auto found = std::find_if(mPrincipals.begin(), mPrincipals.end(),
            [principal](const auto& binding) { return binding.principal == principal; });
        if (found == mPrincipals.end())
            return false;
        mPrincipals.erase(found);
        return true;
    }
}
