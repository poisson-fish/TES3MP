#include <tes3mp/authenticated_join.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <variant>

namespace
{
    using namespace TES3MP;

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
            : joins(*AuthenticatedJoinCoordinator::create(spawn(),
                { id<SessionId>(session), id<PlayerId>(player), id<EntityId>(entity) }, reducer)) {}
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
}

int main()
{
    distinctAtomicJoins();
    duplicatePrincipalDoesNotMutate();
    exhaustedIdentityDoesNotMutate();
    capacityFailureDoesNotMutate();
    preparationIsInvisibleUntilCommit();
    cancelledPreparationLeavesNoStateAndReusesIdentity();
    std::cout << "authenticated join contracts passed\n";
}
