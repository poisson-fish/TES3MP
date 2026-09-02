#include <tes3mp/authenticated_join.hpp>
#include <tes3mp/server_lifecycle.hpp>

#include <array>
#include <cassert>
#include <iostream>
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

    struct Fixture
    {
        NullMetricSink metrics;
        NullStructuredEventSink events;
        Observability observability{ metrics, events };
        CanonicalCommandReducer reducer{ std::get<CanonicalServerState>(createCanonicalServerState({}, {})), observability };
        AuthenticatedJoinCoordinator joins{ *AuthenticatedJoinCoordinator::create(spawn(),
            { id<SessionId>(1), id<PlayerId>(11), id<EntityId>(21) }, reducer) };
        ServerLifecycleCoordinator lifecycle{ *ServerLifecycleCoordinator::create(100, reducer) };
        AuthenticatedJoinResult joined;

        Fixture()
            : joined(std::get<AuthenticatedJoinResult>(
                joins.join(id<PrincipalId>(31), SessionGeneration::initial(), ServerTick::initial())))
        {
            const bool registered = lifecycle.registerJoined(joined.principal, joined.session);
            assert(registered);
        }
    };

    void disconnectIsPreparedAndFailureAtomic()
    {
        Fixture value;
        const auto before = value.reducer.state();
        const auto publication = value.reducer.latestPublication();
        auto prepared = value.lifecycle.prepareDisconnect(value.joined.session,
            MonotonicInstant::fromNanoseconds(10), id<ServerTick>(1));
        const auto first = std::get<ServerLifecyclePreparation>(prepared);
        assert(first.deadline == MonotonicInstant::fromNanoseconds(110));
        assert(value.reducer.state() == before && value.reducer.latestPublication() == publication);
        assert(std::get<ServerLifecycleError>(value.lifecycle.prepareDisconnect(value.joined.session,
                   MonotonicInstant::fromNanoseconds(11), id<ServerTick>(1)))
            == ServerLifecycleError::PreparationPending);
        const bool staleCommit = value.lifecycle.commit(first.id + 1);
        const bool cancelled = value.lifecycle.cancel(first.id);
        assert(!staleCommit && cancelled);
        assert(value.reducer.state() == before && value.lifecycle.liveCount() == 1);

        const auto second = std::get<ServerLifecyclePreparation>(value.lifecycle.prepareDisconnect(
            value.joined.session, MonotonicInstant::fromNanoseconds(10), id<ServerTick>(1)));
        const bool disconnected = value.lifecycle.commit(second.id);
        assert(disconnected);
        assert(value.reducer.state().activeSessions().empty());
        assert(value.reducer.state().players().size() == 1);
        assert(value.lifecycle.liveCount() == 0 && value.lifecycle.hiddenCount() == 1);
        const auto changes = value.reducer.latestPublication()->sessionLifecycle();
        assert(changes.size() == 1 && changes.front().kind == CanonicalSessionLifecycleKind::Disconnected);
    }

    void resumePreservesIdentityAndRejectsDeadline()
    {
        Fixture value;
        const auto playerBefore = value.reducer.state().players().front();
        const auto disconnected = std::get<ServerLifecyclePreparation>(value.lifecycle.prepareDisconnect(
            value.joined.session, MonotonicInstant::fromNanoseconds(10), id<ServerTick>(1)));
        const bool disconnectCommitted = value.lifecycle.commit(disconnected.id);
        assert(disconnectCommitted);
        assert(std::get<ServerLifecycleError>(value.lifecycle.prepareResume(value.joined.principal,
                   value.joined.session, MonotonicInstant::fromNanoseconds(110), id<ServerTick>(2)))
            == ServerLifecycleError::DeadlineReached);
        const auto resumed = std::get<ServerLifecyclePreparation>(value.lifecycle.prepareResume(
            value.joined.principal, value.joined.session, MonotonicInstant::fromNanoseconds(109), id<ServerTick>(2)));
        assert(resumed.generation == *SessionGeneration::initial().next());
        const bool resumeCommitted = value.lifecycle.commit(resumed.id);
        assert(resumeCommitted);
        const auto* session = value.reducer.state().findActiveSession(value.joined.session);
        assert(session && session->playerId() == value.joined.player && session->entityId() == value.joined.entity);
        assert(session->sessionGeneration() == resumed.generation);
        assert(value.reducer.state().players().front() == playerBefore);
        assert(value.lifecycle.liveCount() == 1 && value.lifecycle.hiddenCount() == 0);
    }

    void expirationWinsAtDeadlineAndRemovesPlayer()
    {
        Fixture value;
        const auto disconnected = std::get<ServerLifecyclePreparation>(value.lifecycle.prepareDisconnect(
            value.joined.session, MonotonicInstant::fromNanoseconds(20), id<ServerTick>(1)));
        const bool disconnectCommitted = value.lifecycle.commit(disconnected.id);
        assert(disconnectCommitted);
        assert(std::get<ServerLifecycleError>(value.lifecycle.prepareNextExpiration(
                   MonotonicInstant::fromNanoseconds(119), id<ServerTick>(2)))
            == ServerLifecycleError::DeadlineReached);
        const auto expired = std::get<ServerLifecyclePreparation>(value.lifecycle.prepareNextExpiration(
            MonotonicInstant::fromNanoseconds(120), id<ServerTick>(2)));
        const bool expirationCommitted = value.lifecycle.commit(expired.id);
        assert(expired.action == ServerLifecycleAction::Expire && expirationCommitted);
        assert(value.reducer.state().players().empty() && value.reducer.state().activeSessions().empty());
        assert(value.lifecycle.liveCount() == 0 && value.lifecycle.hiddenCount() == 0);
        assert(std::get<ServerLifecycleError>(value.lifecycle.prepareResume(value.joined.principal,
                   value.joined.session, MonotonicInstant::fromNanoseconds(120), id<ServerTick>(3)))
            == ServerLifecycleError::UnknownPrincipal);
    }

    void simultaneousDisconnectsCommitAsOneBatch()
    {
        Fixture value;
        const auto second = std::get<AuthenticatedJoinResult>(value.joins.join(
            id<PrincipalId>(32), SessionGeneration::initial(), id<ServerTick>(1)));
        const bool registered = value.lifecycle.registerJoined(second.principal, second.session);
        assert(registered);
        const std::array sessions{ value.joined.session, second.session };
        const auto before = value.reducer.state();
        auto result = value.lifecycle.prepareDisconnectBatch(
            sessions, MonotonicInstant::fromNanoseconds(10), id<ServerTick>(2));
        auto prepared = std::get<ServerLifecycleBatchPreparation>(std::move(result));
        const bool cancelled = value.lifecycle.cancel(prepared.id);
        assert(prepared.entries.size() == 2 && value.lifecycle.liveCount() == 2
            && value.reducer.state() == before && cancelled);
        prepared = std::get<ServerLifecycleBatchPreparation>(value.lifecycle.prepareDisconnectBatch(
            sessions, MonotonicInstant::fromNanoseconds(10), id<ServerTick>(2)));
        const bool committed = value.lifecycle.commit(prepared.id);
        assert(committed);
        assert(value.lifecycle.liveCount() == 0 && value.lifecycle.hiddenCount() == 2
            && value.reducer.state().activeSessions().empty());
        const auto publication = value.reducer.latestPublication();
        assert(publication && publication->sessionLifecycle().size() == 2);
        for (const auto& change : publication->sessionLifecycle())
            assert(change.kind == CanonicalSessionLifecycleKind::Disconnected);
    }
}

int main()
{
    disconnectIsPreparedAndFailureAtomic();
    resumePreservesIdentityAndRejectsDeadline();
    expirationWinsAtDeadlineAndRemovesPlayer();
    simultaneousDisconnectsCommitAsOneBatch();
    std::cout << "server lifecycle contracts passed\n";
}
