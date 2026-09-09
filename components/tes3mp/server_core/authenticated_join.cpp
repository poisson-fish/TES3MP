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
    AuthenticatedJoinCoordinator::AuthenticatedJoinCoordinator(std::vector<Transform> spawns,
        AppearanceId appearance, AuthenticatedJoinIdentitySeed seed, CanonicalCommandReducer& reducer,
        ContentManifest contentManifest, PlayerIdentityRegistry* playerIdentities) noexcept
        : mSpawns(std::move(spawns))
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
            { spawn }, appearance, seed, reducer, testContentManifest(), nullptr);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(Transform spawn,
        ContentManifest contentManifest, SessionId nextSession, PlayerIdentityRegistry& playerIdentities,
        CanonicalCommandReducer& reducer)
    {
        return create(std::span<const Transform>(&spawn, 1), contentManifest, nextSession, playerIdentities, reducer);
    }

    std::optional<AuthenticatedJoinCoordinator> AuthenticatedJoinCoordinator::create(std::span<const Transform> spawns,
        ContentManifest contentManifest, SessionId nextSession, PlayerIdentityRegistry& playerIdentities,
        CanonicalCommandReducer& reducer)
    {
        if (spawns.empty() || std::any_of(spawns.begin(), spawns.end(), [&](const auto& spawn) {
                return !contentManifest.contains(spawn.cell());
            }))
            return std::nullopt;
        return AuthenticatedJoinCoordinator(std::vector<Transform>(spawns.begin(), spawns.end()),
            contentManifest.defaultAppearance(),
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
        if (std::any_of(mPrincipals.begin(), mPrincipals.end(),
                [principal](const auto& binding) { return binding.principal == principal; }))
            return AuthenticatedJoinError::DuplicatePrincipal;
        const auto previous = std::find_if(mPrincipals.begin(), mPrincipals.end(),
            [&](const auto& binding) { return binding.player == claim.player; });
        std::optional<PrincipalId> replacedPrincipal;
        if (previous != mPrincipals.end())
        {
            const bool stillActive = std::any_of(mReducer.state().activeSessions().begin(),
                mReducer.state().activeSessions().end(), [&](const auto& session) {
                    return session.playerId() == claim.player;
                });
            if (stillActive)
                return AuthenticatedJoinError::DuplicatePrincipal;
            replacedPrincipal = previous->principal;
        }
        if (!mPlayerIdentities->restartIncompleteCharacter(claim.player))
            return AuthenticatedJoinError::CanonicalStateRejected;
        if (mReducer.state().activeSessions().size() >= MaximumCanonicalActiveSessions)
            return AuthenticatedJoinError::CapacityExhausted;
        return prepareIdentity(principal, claim, false, std::nullopt, generation, serverTick, replacedPrincipal);
    }

    AuthenticatedJoinPrepareOutcome AuthenticatedJoinCoordinator::prepareIdentity(PrincipalId principal,
        AuthenticatedAdmission::PlayerClaim claim, bool createsIdentity,
        std::optional<std::uint64_t> identityPreparation, SessionGeneration generation, ServerTick serverTick,
        std::optional<PrincipalId> replacedPrincipal)
    {
        if (mNextPreparationId == 0)
            return AuthenticatedJoinError::IdentityExhausted;

        const auto& spawn = mSpawns[(claim.player.value() - 1) % mSpawns.size()];
        CanonicalPlayerEntityState canonicalPlayer(claim.player, claim.entity, claim.appearance, spawn,
            LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), serverTick);
        const auto* profile = mPlayerIdentities ? mPlayerIdentities->characterProfile(claim.player) : nullptr;
        const bool established = profile && profile->lifecycle() == CharacterLifecycle::EstablishedCharacter;
        if (!createsIdentity && mPlayerIdentities && established)
            if (const auto* saved = mPlayerIdentities->savedPlayer(claim.player))
            {
                const auto revision = saved->entityRevision().next();
                const auto authority = saved->authorityEpoch().next();
                if (!revision || !authority)
                    return AuthenticatedJoinError::CanonicalStateRejected;
                canonicalPlayer = CanonicalPlayerEntityState(claim.player, claim.entity, claim.appearance,
                    saved->transform(), LinearVelocity3(0, 0, 0), *revision, *authority, serverTick,
                    LocomotionMode::Walk);
            }
        if (const auto* existing = mReducer.state().findPlayer(claim.player))
        {
            if (createsIdentity || existing->entityId() != claim.entity || existing->appearanceId() != claim.appearance)
                return AuthenticatedJoinError::CanonicalStateRejected;
            const auto revision = existing->entityRevision().next();
            const auto authority = existing->authorityEpoch().next();
            if (!revision || !authority)
                return AuthenticatedJoinError::CanonicalStateRejected;
            canonicalPlayer = CanonicalPlayerEntityState(claim.player, claim.entity, claim.appearance,
                established ? existing->transform() : spawn, LinearVelocity3(0, 0, 0),
                *revision, *authority, serverTick, LocomotionMode::Walk);
        }
        CanonicalSessionProgress canonicalSession(mSeed.nextSession, generation, claim.player, claim.entity, std::nullopt);
        auto candidate = mReducer.prepareJoin(canonicalPlayer, canonicalSession, serverTick);
        if (!candidate)
            return AuthenticatedJoinError::CanonicalStateRejected;

        std::vector<SpatialEntitySnapshot> entries;
        entries.reserve(candidate->candidateState().players().size());
        for (const auto& visible : candidate->candidateState().players())
            if (visible.transform().cell() == canonicalPlayer.transform().cell())
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

        AuthenticatedJoinResult result{ principal, session, player, entity, std::move(snapshot),
            profile ? profile->lifecycle() : CharacterLifecycle::NewCharacter,
            profile ? profile->revision() : CharacterProfileRevision::initial(),
            profile ? *profile : CharacterProfile::fresh() };
        const auto preparationId = mNextPreparationId++;
        mPending.emplace(PendingJoin{
            preparationId, std::move(*candidate), result, identityPreparation, replacedPrincipal });
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
        const auto replacedPrincipal = mPending->replacedPrincipal;
        mPending.reset();
        if (identityPreparation && mPlayerIdentities)
            (void)mPlayerIdentities->finalize(*identityPreparation);
        if (replacedPrincipal)
        {
            const auto previous = std::find_if(mPrincipals.begin(), mPrincipals.end(),
                [&](const auto& binding) { return binding.principal == *replacedPrincipal; });
            if (previous != mPrincipals.end())
                *previous = { result.principal, result.player };
            else
                mPrincipals.push_back({ result.principal, result.player });
        }
        else
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

    bool AuthenticatedJoinCoordinator::persistPlayer(PlayerId player) noexcept
    {
        return persistPlayers(std::span<const PlayerId>(&player, 1));
    }

    bool AuthenticatedJoinCoordinator::persistPlayers(std::span<const PlayerId> players) noexcept
    {
        if (!mPlayerIdentities)
            return true;
        std::vector<CanonicalPlayerEntityState> canonical;
        canonical.reserve(players.size());
        for (const auto player : players)
        {
            const auto* found = mReducer.state().findPlayer(player);
            if (!found)
                return false;
            canonical.push_back(*found);
        }
        return mPlayerIdentities->savePlayers(canonical);
    }

    const CharacterProfile* AuthenticatedJoinCoordinator::characterProfile(PlayerId player) const noexcept
    {
        return mPlayerIdentities ? mPlayerIdentities->characterProfile(player) : nullptr;
    }

    CharacterProfileApplyResult AuthenticatedJoinCoordinator::applyCharacterCreation(PlayerId player,
        const CharacterContentCatalog& catalog, const CharacterCreationCommand& command,
        ServerTick tick) noexcept
    {
        if (!mPlayerIdentities)
            return CharacterProfileError::PersistenceFailed;
        const auto* current = mPlayerIdentities->characterProfile(player);
        if (!current)
            return CharacterProfileError::InvalidInitialState;
        auto preview = TES3MP::applyCharacterCreation(*current, catalog, command);
        auto* profile = std::get_if<CharacterProfile>(&preview);
        if (!profile)
            return std::get<CharacterProfileError>(preview);
        if (profile->lifecycle() != CharacterLifecycle::EstablishedCharacter)
            return mPlayerIdentities->applyCharacterCreation(player, catalog, command);

        auto safePoint = mReducer.preparePlayerSafePoint(player, catalog.completionSpawn(), tick);
        if (!safePoint)
            return CharacterProfileError::InvalidInitialState;
        const auto* checkpoint = safePoint->candidateState().findPlayer(player);
        if (!checkpoint)
            return CharacterProfileError::InvalidInitialState;
        auto persisted = mPlayerIdentities->applyCharacterCreation(player, catalog, command, checkpoint);
        if (!std::holds_alternative<CharacterProfile>(persisted))
            return persisted;
        if (!mReducer.commit(std::move(*safePoint)))
            return CharacterProfileError::PersistenceFailed;
        return persisted;
    }
}
