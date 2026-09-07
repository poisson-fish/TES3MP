#include <tes3mp/authenticated_join.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;

    void require(bool condition)
    {
        if (!condition)
            std::abort();
    }

#undef assert
#define assert(condition) require(static_cast<bool>(condition))

    template <class Value>
    Value id(std::uint64_t value) { return Value::fromValue(value).value(); }

    Transform spawn()
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(7)), Position3(10, 20, 30),
            Orientation3(zero, zero, zero));
    }

    struct JoinFixture
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        CanonicalCommandReducer reducer{ std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability };
        AuthenticatedJoinCoordinator joins;
        JoinFixture(std::uint64_t session = 1, std::uint64_t player = 1, std::uint64_t entity = 1)
            : joins(*AuthenticatedJoinCoordinator::create(spawn(), id<AppearanceId>(1),
                { id<SessionId>(session), id<PlayerId>(player), id<EntityId>(entity) }, reducer)) {}
    };

    class FakeCrypto final : public CredentialCrypto
    {
    public:
        bool randomBytes(std::span<std::byte> destination) noexcept override
        {
            std::fill(destination.begin(), destination.end(), static_cast<std::byte>(seed++));
            return true;
        }
        bool sha256(std::span<const std::byte> source, CredentialDigest& destination) noexcept override
        {
            destination.bytes.fill(std::byte{});
            for (std::size_t index = 0; index < source.size(); ++index)
                destination.bytes[index % destination.bytes.size()] ^= source[index];
            return true;
        }
        bool constantTimeEqual(std::span<const std::byte> left, std::span<const std::byte> right) noexcept override
        {
            if (left.size() != right.size()) return false;
            std::byte difference{};
            for (std::size_t index = 0; index < left.size(); ++index) difference |= left[index] ^ right[index];
            return difference == std::byte{};
        }
        std::uint8_t seed = 1;
    };

    class MemoryPersistence final : public PlayerIdentityPersistence
    {
    public:
        bool replace(std::span<const PersistedPlayerIdentity> replacement) noexcept override
        {
            if (reject) return false;
            records.assign(replacement.begin(), replacement.end());
            return true;
        }
        bool reject = false;
        std::vector<PersistedPlayerIdentity> records;
    };

    void distinctAtomicJoins()
    {
        JoinFixture fixture;
        auto& value = fixture.joins;
        auto first = value.join(id<PrincipalId>(41), SessionGeneration::initial(), ServerTick::initial());
        auto second = value.join(id<PrincipalId>(42), SessionGeneration::initial(), id<ServerTick>(1));
        const auto& left = std::get<AuthenticatedJoinResult>(first);
        const auto& right = std::get<AuthenticatedJoinResult>(second);
        assert(left.session != right.session && left.player != right.player && left.entity != right.entity);
        assert(value.state().players().size() == 2 && value.state().activeSessions().size() == 2);
        const auto publication = fixture.reducer.latestPublication();
        assert(publication->stateVersion().value() == 2 && publication->joinedSessions().size() == 1
            && publication->joinedSessions().front().session.sessionId() == right.session);
        assert(left.initialSnapshot.header().targetSessionId() == left.session);
        assert(left.initialSnapshot.view().entries().size() == 1);
        const auto& entry = left.initialSnapshot.view().entries().front();
        assert(entry.transform() == spawn() && entry.linearVelocity() == LinearVelocity3(0, 0, 0));
        assert(entry.entityRevision() == EntityRevision::initial()
            && entry.authorityEpoch() == AuthorityEpoch::initial());
    }

    void configuredSpawnsAreDeterministic()
    {
        FakeCrypto crypto;
        MemoryPersistence persistence;
        auto registry = std::move(std::get<std::unique_ptr<PlayerIdentityRegistry>>(
            PlayerIdentityRegistry::create(crypto, persistence, {})));
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        CanonicalCommandReducer reducer(
            std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability, testContentManifest());
        const auto zero = Turn32::fromValue(0);
        const std::array spawns{
            Transform(CellId::interior(id<CellSpaceId>(7)), Position3(10, 20, 30), Orientation3(zero, zero, zero)),
            Transform(CellId::interior(id<CellSpaceId>(7)), Position3(40, 50, 60), Orientation3(zero, zero, zero)) };
        auto joins = *AuthenticatedJoinCoordinator::create(
            spawns, testContentManifest(), id<SessionId>(1), *registry, reducer);

        assert(std::holds_alternative<AuthenticatedJoinResult>(
            joins.join(id<PrincipalId>(1), SessionGeneration::initial(), ServerTick::initial())));
        assert(std::holds_alternative<AuthenticatedJoinResult>(
            joins.join(id<PrincipalId>(2), SessionGeneration::initial(), ServerTick::initial())));
        assert(joins.state().findPlayer(id<PlayerId>(1))->transform() == spawns[0]);
        assert(joins.state().findPlayer(id<PlayerId>(2))->transform() == spawns[1]);
    }

    void duplicatePrincipalDoesNotMutate()
    {
        JoinFixture fixture;
        auto& value = fixture.joins;
        assert(std::holds_alternative<AuthenticatedJoinResult>(
            value.join(id<PrincipalId>(9), SessionGeneration::initial(), ServerTick::initial())));
        const auto before = value.state();
        const auto rejected = value.join(id<PrincipalId>(9), SessionGeneration::initial(), id<ServerTick>(1));
        assert(std::get<AuthenticatedJoinError>(rejected) == AuthenticatedJoinError::DuplicatePrincipal);
        assert(value.state() == before && value.liveBindings() == 1);
    }

    void exhaustedIdentityDoesNotMutate()
    {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        JoinFixture fixture(maximum, 1, 1);
        auto& value = fixture.joins;
        assert(std::holds_alternative<AuthenticatedJoinResult>(
            value.join(id<PrincipalId>(1), SessionGeneration::initial(), ServerTick::initial())));
        const auto before = value.state();
        const auto rejected
            = value.join(id<PrincipalId>(2), SessionGeneration::initial(), id<ServerTick>(1));
        assert(std::get<AuthenticatedJoinError>(rejected) == AuthenticatedJoinError::IdentityExhausted);
        assert(value.state() == before && value.liveBindings() == 1);
    }

    void capacityFailureDoesNotMutate()
    {
        JoinFixture fixture;
        auto& value = fixture.joins;
        for (std::uint64_t index = 1; index <= MaximumCanonicalActiveSessions; ++index)
        {
            assert(std::holds_alternative<AuthenticatedJoinResult>(value.join(
                id<PrincipalId>(index), SessionGeneration::initial(), id<ServerTick>(index - 1))));
        }
        const auto before = value.state();
        const auto rejected = value.join(id<PrincipalId>(MaximumCanonicalActiveSessions + 1),
            SessionGeneration::initial(), id<ServerTick>(MaximumCanonicalActiveSessions));
        assert(std::get<AuthenticatedJoinError>(rejected) == AuthenticatedJoinError::CapacityExhausted);
        assert(value.state() == before && value.liveBindings() == MaximumCanonicalActiveSessions);
    }

    void preparationIsInvisibleUntilCommit()
    {
        JoinFixture fixture;
        auto& value = fixture.joins;
        const auto before = value.state();
        const auto beforePublication = fixture.reducer.latestPublication();
        auto prepared = value.prepare(
            id<PrincipalId>(77), SessionGeneration::initial(), ServerTick::initial());
        const auto& pending = std::get<AuthenticatedJoinPreparation>(prepared);
        assert(value.state() == before && value.liveBindings() == 0
            && fixture.reducer.latestPublication() == beforePublication);
        assert(std::get<AuthenticatedJoinError>(value.prepare(
                   id<PrincipalId>(78), SessionGeneration::initial(), ServerTick::initial()))
            == AuthenticatedJoinError::PreparationPending);
        assert(std::get<AuthenticatedJoinError>(value.commit(pending.id + 1))
            == AuthenticatedJoinError::StalePreparation);
        assert(value.state() == before && value.liveBindings() == 0);

        const auto committed = value.commit(pending.id);
        assert(std::holds_alternative<AuthenticatedJoinResult>(committed));
        assert(value.state().players().size() == 1 && value.state().activeSessions().size() == 1
            && value.liveBindings() == 1);
        assert(std::get<AuthenticatedJoinError>(value.commit(pending.id))
            == AuthenticatedJoinError::StalePreparation);
    }

    void cancelledPreparationLeavesNoStateAndReusesIdentity()
    {
        JoinFixture fixture;
        auto& value = fixture.joins;
        const auto before = value.state();
        auto prepared = value.prepare(
            id<PrincipalId>(81), SessionGeneration::initial(), ServerTick::initial());
        const auto& pending = std::get<AuthenticatedJoinPreparation>(prepared);
        const auto reservedSession = pending.join.session;
        assert(value.cancel(pending.id));
        assert(!value.cancel(pending.id));
        assert(value.state() == before && value.liveBindings() == 0);

        auto replacement = value.prepare(
            id<PrincipalId>(82), SessionGeneration::initial(), ServerTick::initial());
        assert(std::get<AuthenticatedJoinPreparation>(replacement).join.session == reservedSession);
    }

    void persistent_identity_reattaches_and_failed_commit_is_atomic()
    {
        FakeCrypto crypto;
        MemoryPersistence persistence;
        auto registryResult = PlayerIdentityRegistry::create(crypto, persistence, {});
        auto registry = std::move(std::get<std::unique_ptr<PlayerIdentityRegistry>>(registryResult));
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        CanonicalCommandReducer reducer(
            std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability, testContentManifest());
        auto joins = *AuthenticatedJoinCoordinator::create(
            spawn(), testContentManifest(), id<SessionId>(1), *registry, reducer);

        const auto prepared = std::get<AuthenticatedJoinPreparation>(
            joins.prepare(id<PrincipalId>(1), SessionGeneration::initial(), ServerTick::initial()));
        auto credential = joins.copyPendingPlayerCredential(prepared.id);
        assert(credential && joins.pendingCreatesPersistentIdentity(prepared.id));
        const auto joined = std::get<AuthenticatedJoinResult>(joins.commit(prepared.id));
        assert(joined.player == id<PlayerId>(1) && joined.entity == id<EntityId>(1)
            && persistence.records.size() == 1);

        auto claim = registry->authenticate(*credential, testContentManifestId());
        assert(claim && claim->player == joined.player && claim->entity == joined.entity);
        auto disconnected = reducer.prepareDisconnect(joined.session, id<ServerTick>(1));
        assert(disconnected && reducer.commit(std::move(*disconnected)));
        auto expired = reducer.prepareExpiration(joined.player, joined.session,
            SessionGeneration::initial(), id<ServerTick>(2));
        assert(expired && reducer.commit(std::move(*expired)) && reducer.state().players().empty());
        assert(joins.releasePrincipal(joined.principal));
        auto reattached = joins.prepareReattach(
            id<PrincipalId>(2), *claim, SessionGeneration::initial(), id<ServerTick>(3));
        const auto reattachPreparation = std::get<AuthenticatedJoinPreparation>(std::move(reattached));
        assert(!joins.pendingCreatesPersistentIdentity(reattachPreparation.id));
        const auto reattachResult = std::get<AuthenticatedJoinResult>(joins.commit(reattachPreparation.id));
        assert(reattachResult.player == joined.player && reattachResult.entity == joined.entity
            && reattachResult.session == id<SessionId>(2) && persistence.records.size() == 1);

        auto restartedRegistry = std::move(std::get<std::unique_ptr<PlayerIdentityRegistry>>(
            PlayerIdentityRegistry::create(crypto, persistence, persistence.records)));
        auto restartedClaim = restartedRegistry->authenticate(*credential, testContentManifestId());
        assert(restartedClaim && restartedClaim->player == joined.player
            && restartedClaim->entity == joined.entity);
        CanonicalCommandReducer restartedReducer(
            std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability, testContentManifest());
        auto restartedJoins = *AuthenticatedJoinCoordinator::create(
            spawn(), testContentManifest(), id<SessionId>(1), *restartedRegistry, restartedReducer);
        const auto restartedPreparation = std::get<AuthenticatedJoinPreparation>(restartedJoins.prepareReattach(
            id<PrincipalId>(1), *restartedClaim, SessionGeneration::initial(), ServerTick::initial()));
        const auto restartedJoin = std::get<AuthenticatedJoinResult>(
            restartedJoins.commit(restartedPreparation.id));
        assert(restartedJoin.player == joined.player && restartedJoin.entity == joined.entity
            && restartedJoin.session == id<SessionId>(1));

        MemoryPersistence failing;
        failing.reject = true;
        auto failingRegistry = std::move(std::get<std::unique_ptr<PlayerIdentityRegistry>>(
            PlayerIdentityRegistry::create(crypto, failing, {})));
        CanonicalCommandReducer failingReducer(
            std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability, testContentManifest());
        auto failingJoins = *AuthenticatedJoinCoordinator::create(
            spawn(), testContentManifest(), id<SessionId>(1), *failingRegistry, failingReducer);
        const auto failedPreparation = std::get<AuthenticatedJoinPreparation>(
            failingJoins.prepare(id<PrincipalId>(3), SessionGeneration::initial(), ServerTick::initial()));
        assert(std::get<AuthenticatedJoinError>(failingJoins.commit(failedPreparation.id))
            == AuthenticatedJoinError::CanonicalStateRejected);
        assert(failingRegistry->records().empty() && failingReducer.state().players().empty());
        failing.reject = false;
        const auto retry = std::get<AuthenticatedJoinPreparation>(
            failingJoins.prepare(id<PrincipalId>(3), SessionGeneration::initial(), ServerTick::initial()));
        assert(retry.join.player == id<PlayerId>(1) && retry.join.entity == id<EntityId>(1));
    }

    void persistent_identity_skips_reserved_actor_entities()
    {
        FakeCrypto crypto;
        MemoryPersistence persistence;
        const std::array reserved{ id<EntityId>(1), id<EntityId>(3) };
        auto registryResult = PlayerIdentityRegistry::create(crypto, persistence, {}, reserved);
        auto registry = std::move(std::get<std::unique_ptr<PlayerIdentityRegistry>>(registryResult));

        const auto first = std::get<PreparedPlayerIdentity>(registry->prepareCreate(testContentManifest()));
        assert(first.claim.entity == id<EntityId>(2));
        assert(registry->commit(first.id) && registry->finalize(first.id));
        const auto second = std::get<PreparedPlayerIdentity>(registry->prepareCreate(testContentManifest()));
        assert(second.claim.entity == id<EntityId>(4));
        assert(registry->cancel(second.id));

        const std::array conflicting{ id<EntityId>(2) };
        assert(std::get<PlayerIdentityError>(PlayerIdentityRegistry::create(
                   crypto, persistence, persistence.records, conflicting))
            == PlayerIdentityError::InvalidInitialState);
        const std::array duplicate{ id<EntityId>(5), id<EntityId>(5) };
        assert(std::get<PlayerIdentityError>(PlayerIdentityRegistry::create(
                   crypto, persistence, persistence.records, duplicate))
            == PlayerIdentityError::InvalidInitialState);
    }
}

int main()
{
    distinctAtomicJoins();
    configuredSpawnsAreDeterministic();
    duplicatePrincipalDoesNotMutate();
    exhaustedIdentityDoesNotMutate();
    capacityFailureDoesNotMutate();
    preparationIsInvisibleUntilCommit();
    cancelledPreparationLeavesNoStateAndReusesIdentity();
    persistent_identity_reattaches_and_failed_commit_is_atomic();
    persistent_identity_skips_reserved_actor_entities();
    std::cout << "authenticated join contracts passed\n";
}
