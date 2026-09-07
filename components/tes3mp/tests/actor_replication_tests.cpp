#include <tes3mp/actor_replication.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>

#include <array>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class T> T id(std::uint64_t value) { return *T::fromValue(value); }

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); }
    };

    ActorSpatialSnapshot entry(std::uint64_t actor, std::uint64_t tick = 5)
    {
        const auto zero = Turn32::fromValue(0);
        return { id<ServerTick>(tick), id<ActorId>(actor), id<EntityId>(actor + 10),
            id<ActorPrototypeId>(actor + 20), id<EntityRevision>(actor), AuthorityEpoch::initial(),
            Transform(CellId::interior(id<CellSpaceId>(7)), Position3(actor, 2, 3),
                Orientation3(zero, zero, zero)), LinearVelocity3(4, 0, 0), ActorActivity::Travel };
    }

    bool roundTripsAndBounds()
    {
        const auto session = id<SessionId>(3);
        const auto generation = id<SessionGeneration>(2);
        const std::array members{ ActorInterestMember{ id<ActorId>(1), id<EntityId>(11),
                                      id<ActorPrototypeId>(21) },
            ActorInterestMember{ id<ActorId>(2), id<EntityId>(12), id<ActorPrototypeId>(22) } };
        auto baseline = ReliableActorInterestBaseline::create(
            session, generation, id<ServerTick>(5), id<CanonicalRevision>(7), members);
        if (!std::holds_alternative<ReliableActorInterestBaseline>(baseline)) return false;
        const auto& originalBaseline = std::get<ReliableActorInterestBaseline>(baseline);
        const auto encodedBaseline = encodeReliableActorInterestBaseline(originalBaseline);
        const auto decodedBaseline = decodeReliableActorInterestBaseline(encodedBaseline);
        const std::array entries{ entry(1), entry(2) };
        auto view = ActorWorldView::create(entries);
        if (!std::holds_alternative<ActorWorldView>(view)) return false;
        const LatestWinsActorSnapshot snapshot(session, generation, id<ServerTick>(5), id<CanonicalRevision>(7),
            std::get<ActorWorldView>(std::move(view)));
        const auto encodedSnapshot = encodeLatestWinsActorSnapshot(snapshot);
        const auto decodedSnapshot = decodeLatestWinsActorSnapshot(encodedSnapshot);
        for (std::size_t size = 0; size < encodedBaseline.size(); ++size)
            if (!std::holds_alternative<ActorReplicationDecodeError>(
                    decodeReliableActorInterestBaseline(std::span(encodedBaseline).first(size))))
                return false;
        for (std::size_t size = 0; size < encodedSnapshot.size(); ++size)
            if (!std::holds_alternative<ActorReplicationDecodeError>(
                    decodeLatestWinsActorSnapshot(std::span(encodedSnapshot).first(size))))
                return false;
        std::vector<ActorInterestMember> tooMany(MaximumActorInterestMembers + 1, members[0]);
        const std::array unordered{ entries[1], entries[0] };
        return std::get_if<ReliableActorInterestBaseline>(&decodedBaseline)
            && std::get<ReliableActorInterestBaseline>(decodedBaseline) == originalBaseline
            && std::get_if<LatestWinsActorSnapshot>(&decodedSnapshot)
            && std::get<LatestWinsActorSnapshot>(decodedSnapshot) == snapshot
            && std::holds_alternative<ActorReplicationDecodeError>(
                ReliableActorInterestBaseline::create(
                    session, generation, id<ServerTick>(5), id<CanonicalRevision>(7), tooMany))
            && std::holds_alternative<ActorReplicationDecodeError>(ActorWorldView::create(unordered))
            && messageDescriptor(MessageKind::ReliableActorInterestBaseline)->messageClass
                == MessageClass::ReliableOperation
            && messageDescriptor(MessageKind::LatestWinsActorSnapshot)->messageClass
                == MessageClass::LatestWinsSnapshot;
    }

    bool capabilityAndClientBaselineAreAdditive()
    {
        const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 2, 3));
        const std::array actorCapability{ actorReplicationCapability() };
        auto clientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, actorCapability, {}));
        auto serverOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, actorCapability, {}));
        auto negotiated = negotiateClientHello(ClientHello::fromOffer(std::move(clientOffer)), serverOffer);
        auto* hello = std::get_if<ServerHello>(&negotiated);
        if (!hello || hello->negotiatedCapabilities().size() != 1) return false;
        FixedClock clock;
        auto timeouts = *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000);
        auto created = ClientSessionStateMachine::create(clock, timeouts, SessionGeneration::initial());
        auto client = std::get<std::unique_ptr<ClientSessionStateMachine>>(std::move(created));
        client->handle(ClientEncryptedTransportReady{});
        client->handle(ClientServerHelloReceived{ std::move(*hello) });
        client->handle(ClientAuthenticationSubmitted{});
        client->handle(ClientAuthenticationAccepted{});
        const auto session = id<SessionId>(1);
        if (client->bindEstablishedSession(session) != ClientSessionBindingResult::Bound) return false;
        const std::array members{ ActorInterestMember{ id<ActorId>(1), id<EntityId>(11),
            id<ActorPrototypeId>(21) } };
        auto baseline = std::get<ReliableActorInterestBaseline>(ReliableActorInterestBaseline::create(
            session, SessionGeneration::initial(), id<ServerTick>(5), id<CanonicalRevision>(3), members));
        const std::array entries{ entry(1, 5) };
        auto snapshot = LatestWinsActorSnapshot(session, SessionGeneration::initial(), id<ServerTick>(5),
            id<CanonicalRevision>(3),
            std::get<ActorWorldView>(ActorWorldView::create(entries)));
        auto resync = std::get<ReliableActorInterestBaseline>(ReliableActorInterestBaseline::create(
            session, SessionGeneration::initial(), id<ServerTick>(6), id<CanonicalRevision>(3), members));
        return !client->actorInterestBaselineComplete()
            && client->receiveReliableActorInterestBaseline(std::move(baseline))
                == ActorReplicationReceiveResult::Applied
            && !client->actorInterestBaselineComplete()
            && client->receiveLatestWinsActorSnapshot(std::move(snapshot)) == ActorReplicationReceiveResult::Applied
            && client->actorInterestBaselineComplete() && client->observedActors().size() == 1
            && client->receiveReliableActorInterestBaseline(std::move(resync))
                == ActorReplicationReceiveResult::IdenticalDuplicate;
    }
}

int main()
{
    return roundTripsAndBounds() && capabilityAndClientBaselineAreAdditive() ? 0 : 1;
}
