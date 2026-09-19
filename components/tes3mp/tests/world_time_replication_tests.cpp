#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/world_time_replication.hpp>

#include <array>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value) { return *T::fromValue(value); }

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); }
    };

    ReliableWorldTimeState state(std::uint64_t tick, std::uint64_t revision, bool baseline,
        std::uint32_t milliseconds = 43'200'000)
    {
        CanonicalWorldTimeState time;
        time.day = 31;
        time.month = 0;
        time.daysPassed = 42;
        time.year = 427;
        time.millisecondsSinceMidnight = milliseconds;
        time.timeScaleUnits = 30 * WorldTimeScaleUnitsPerOne;
        time.revision = id<WorldTimeRevision>(revision);
        time.lastChangeTick = id<ServerTick>(tick);
        time.lastAdvanceTick = id<ServerTick>(tick);
        return { id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(tick),
            id<CanonicalRevision>(tick), time, baseline };
    }

    std::unique_ptr<ClientSessionStateMachine> client()
    {
        static FixedClock clock;
        const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 7, 7));
        const std::array capabilities{ worldTimeReplicationCapability() };
        auto clientOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
        auto serverOffer = std::get<CapabilityOffer>(CapabilityOffer::create(versions, capabilities, {}));
        auto hello = std::get<ServerHello>(
            negotiateClientHello(ClientHello::fromOffer(std::move(clientOffer)), serverOffer));
        auto created = ClientSessionStateMachine::create(clock,
            *SessionTimeoutPolicy::create(1'000'000, 1'000'000, 1'000'000), SessionGeneration::initial());
        auto result = std::get<std::unique_ptr<ClientSessionStateMachine>>(std::move(created));
        result->handle(ClientEncryptedTransportReady{});
        result->handle(ClientServerHelloReceived{ std::move(hello) });
        result->handle(ClientAuthenticationSubmitted{});
        result->handle(ClientAuthenticationAccepted{});
        result->bindEstablishedSession(id<SessionId>(1));
        return result;
    }

    bool codecIsBoundedAndValidated()
    {
        const auto original = state(5, 2, true);
        const auto encoded = encodeReliableWorldTimeState(original);
        const auto decoded = decodeReliableWorldTimeState(encoded);
        if (!std::holds_alternative<ReliableWorldTimeState>(decoded)
            || std::get<ReliableWorldTimeState>(decoded) != original
            || encoded.size() > ReliableOperationMaximumPayloadBytes
            || messageDescriptor(MessageKind::ReliableWorldTimeState)->messageClass != MessageClass::ReliableOperation)
            return false;
        auto legacy = original;
        legacy.time.daysPassed.reset();
        if (std::get<ReliableWorldTimeState>(decodeReliableWorldTimeState(encodeReliableWorldTimeState(legacy))) != legacy)
            return false;
        auto invalid = original;
        invalid.time.day = 32;
        if (!std::holds_alternative<WorldTimeReplicationDecodeError>(decodeReliableWorldTimeState(encodeReliableWorldTimeState(invalid))))
            return false;
        invalid = original;
        invalid.time.daysPassed = 100000001;
        if (!std::holds_alternative<WorldTimeReplicationDecodeError>(decodeReliableWorldTimeState(encodeReliableWorldTimeState(invalid))))
            return false;
        for (std::size_t size = 0; size < encoded.size(); ++size)
            if (!std::holds_alternative<WorldTimeReplicationDecodeError>(
                    decodeReliableWorldTimeState(std::span(encoded).first(size))))
                return false;
        return true;
    }

    bool baselineAndRevisionRulesConverge()
    {
        auto receiver = client();
        const auto updateBeforeBaseline = state(5, 2, false);
        if (receiver->receiveReliableWorldTimeState(updateBeforeBaseline)
            != WorldTimeReplicationReceiveResult::BaselineMissing)
            return false;
        const auto baseline = state(5, 2, true);
        if (receiver->receiveReliableWorldTimeState(baseline) != WorldTimeReplicationReceiveResult::Applied
            || !receiver->worldTimeBaselineComplete()
            || receiver->receiveReliableWorldTimeState(baseline)
                != WorldTimeReplicationReceiveResult::IdenticalDuplicate)
            return false;
        const auto newer = state(9, 6, false, 44'000'000);
        if (receiver->receiveReliableWorldTimeState(newer) != WorldTimeReplicationReceiveResult::Applied
            || !receiver->confirmedWorldTime() || receiver->confirmedWorldTime()->time.revision != id<WorldTimeRevision>(6))
            return false;
        auto contradiction = newer;
        contradiction.time.millisecondsSinceMidnight += 1;
        if (receiver->receiveReliableWorldTimeState(contradiction)
            != WorldTimeReplicationReceiveResult::ContradictorySameRevision)
            return false;
        auto pausedAdvance = newer;
        pausedAdvance.serverTick = id<ServerTick>(10);
        pausedAdvance.canonicalRevision = id<CanonicalRevision>(10);
        pausedAdvance.time.lastAdvanceTick = id<ServerTick>(10);
        return receiver->receiveReliableWorldTimeState(pausedAdvance)
                == WorldTimeReplicationReceiveResult::Applied
            && receiver->receiveReliableWorldTimeState(baseline)
                == WorldTimeReplicationReceiveResult::StaleRevision;
    }
}

int main()
{
    return codecIsBoundedAndValidated() && baselineAndRevisionRulesConverge() ? 0 : 1;
}
