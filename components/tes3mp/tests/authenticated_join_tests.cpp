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

    AuthenticatedJoinCoordinator coordinator(std::uint64_t session = 1, std::uint64_t player = 1,
        std::uint64_t entity = 1)
    {
        return *AuthenticatedJoinCoordinator::create(
            spawn(), { id<SessionId>(session), id<PlayerId>(player), id<EntityId>(entity) });
    }

    void distinctAtomicJoins()
    {
        auto value = coordinator();
        auto first = value.join(id<PrincipalId>(41), SessionGeneration::initial(), ServerTick::initial());
        auto second = value.join(id<PrincipalId>(42), SessionGeneration::initial(), id<ServerTick>(1));
        const auto& left = std::get<AuthenticatedJoinResult>(first);
        const auto& right = std::get<AuthenticatedJoinResult>(second);
        assert(left.session != right.session && left.player != right.player && left.entity != right.entity);
        assert(value.state().players().size() == 2 && value.state().activeSessions().size() == 2);
        assert(left.initialSnapshot.header().targetSessionId() == left.session);
        assert(left.initialSnapshot.view().entries().size() == 1);
        const auto& entry = left.initialSnapshot.view().entries().front();
        assert(entry.transform() == spawn() && entry.linearVelocity() == LinearVelocity3(0, 0, 0));
        assert(entry.entityRevision() == EntityRevision::initial()
            && entry.authorityEpoch() == AuthorityEpoch::initial());
    }

    void duplicatePrincipalDoesNotMutate()
    {
        auto value = coordinator();
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
        auto value = coordinator(maximum, 1, 1);
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
        auto value = coordinator();
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
}

int main()
{
    distinctAtomicJoins();
    duplicatePrincipalDoesNotMutate();
    exhaustedIdentityDoesNotMutate();
    capacityFailureDoesNotMutate();
    std::cout << "authenticated join contracts passed\n";
}
