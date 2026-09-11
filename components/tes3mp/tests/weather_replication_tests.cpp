#include <tes3mp/client_session.hpp>
#include <tes3mp/protocol_frame.hpp>
#include <tes3mp/weather_replication.hpp>

#include <array>
#include <vector>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }

    class FixedClock final : public MonotonicClock
    {
    public:
        MonotonicInstant now() const noexcept override { return MonotonicInstant::fromNanoseconds(0); }
    };

    WeatherRegionSnapshot settled(std::uint64_t region, std::uint64_t weather, std::uint64_t revision = 1)
    {
        return { id<WeatherRegionId>(region), id<WeatherId>(weather), id<WeatherId>(weather),
            ServerTick::initial(), ServerTick::initial(), id<ServerTick>(10), id<WeatherRevision>(revision) };
    }

    WeatherRegionSnapshot transitioning(std::uint64_t region, std::uint64_t revision,
        std::uint64_t target = 2)
    {
        return { id<WeatherRegionId>(region), id<WeatherId>(1), id<WeatherId>(target), id<ServerTick>(5),
            id<ServerTick>(15), id<ServerTick>(25), id<WeatherRevision>(revision) };
    }

    ReliableWeatherState state(std::span<const WeatherRegionSnapshot> regions, std::uint64_t canonicalRevision,
        bool baseline, std::uint32_t chunkIndex = 0, std::uint32_t chunkCount = 1,
        std::uint64_t serverTick = 5)
    {
        WeatherStateHeader header{ id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(serverTick),
            id<CanonicalRevision>(canonicalRevision), chunkIndex, chunkCount, baseline };
        return std::get<ReliableWeatherState>(ReliableWeatherState::create(header, regions));
    }

    std::unique_ptr<ClientSessionStateMachine> client()
    {
        static FixedClock clock;
        const auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 7, 7));
        const std::array capabilities{ weatherReplicationCapability() };
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

    bool codecRejectsMalformedState()
    {
        const std::array regions{ settled(1, 1), transitioning(2, 2) };
        const auto original = state(regions, 5, true);
        const auto encoded = encodeReliableWeatherState(original);
        const auto decoded = decodeReliableWeatherState(encoded);
        for (std::size_t size = 0; size < encoded.size(); ++size)
            if (!std::holds_alternative<WeatherReplicationDecodeError>(
                    decodeReliableWeatherState(std::span(encoded).first(size))))
                return false;
        const std::array unordered{ regions[1], regions[0] };
        auto invalidHeader = original.header();
        invalidHeader.chunkIndex = 1;
        invalidHeader.chunkCount = 1;
        auto badTicks = regions[1];
        badTicks.transitionEndTick = id<ServerTick>(4);
        return std::get_if<ReliableWeatherState>(&decoded) && std::get<ReliableWeatherState>(decoded) == original
            && encoded.size() <= ReliableOperationMaximumPayloadBytes
            && messageDescriptor(MessageKind::ReliableWeatherState)->messageClass == MessageClass::ReliableOperation
            && std::holds_alternative<WeatherReplicationDecodeError>(
                ReliableWeatherState::create(invalidHeader, regions))
            && std::holds_alternative<WeatherReplicationDecodeError>(
                ReliableWeatherState::create(original.header(), unordered))
            && std::holds_alternative<WeatherReplicationDecodeError>(
                ReliableWeatherState::create(original.header(), std::span(&badTicks, 1)));
    }

    bool revisionsAndChunksAreAtomic()
    {
        auto receiver = client();
        const std::array baselineRegions{ settled(1, 1), settled(2, 1) };
        auto baseline = state(baselineRegions, 5, true);
        if (receiver->receiveReliableWeatherState(baseline) != WeatherReplicationReceiveResult::Applied
            || !receiver->weatherBaselineComplete() || receiver->confirmedWeather().size() != 2)
            return false;
        const std::array changed{ transitioning(1, 2) };
        auto update = state(changed, 6, false, 0, 1, 6);
        if (receiver->receiveReliableWeatherState(update) != WeatherReplicationReceiveResult::Applied
            || receiver->receiveReliableWeatherState(update) != WeatherReplicationReceiveResult::IdenticalDuplicate)
            return false;
        auto contradictoryRegion = changed[0];
        contradictoryRegion.targetWeather = id<WeatherId>(3);
        const std::array contradictory{ contradictoryRegion };
        auto contradictoryUpdate = state(contradictory, 6, false, 0, 1, 6);
        const std::array gapRegion{ transitioning(1, 4) };
        auto gap = state(gapRegion, 7, false, 0, 1, 7);
        auto stale = state(baselineRegions, 5, true, 0, 1, 5);
        if (receiver->receiveReliableWeatherState(contradictoryUpdate)
                != WeatherReplicationReceiveResult::ContradictorySameRevision
            || receiver->receiveReliableWeatherState(gap) != WeatherReplicationReceiveResult::RevisionGap
            || receiver->receiveReliableWeatherState(stale) != WeatherReplicationReceiveResult::StaleRevision)
            return false;

        const std::array resumedRegions{ changed[0], baselineRegions[1] };
        auto resumedBaseline = state(resumedRegions, 6, true, 0, 1, 9);
        if (receiver->receiveReliableWeatherState(resumedBaseline) != WeatherReplicationReceiveResult::Applied
            || receiver->confirmedWeatherServerTick() != id<ServerTick>(9)
            || receiver->receiveReliableWeatherState(resumedBaseline)
                != WeatherReplicationReceiveResult::IdenticalDuplicate)
            return false;
        auto contradictoryBaselineRegion = changed[0];
        contradictoryBaselineRegion.targetWeather = id<WeatherId>(3);
        const std::array contradictoryBaseline{ contradictoryBaselineRegion, baselineRegions[1] };
        if (receiver->receiveReliableWeatherState(state(contradictoryBaseline, 6, true, 0, 1, 9))
            != WeatherReplicationReceiveResult::ContradictorySameRevision)
            return false;

        auto chunked = client();
        const std::array first{ settled(1, 1) };
        const std::array second{ settled(2, 1) };
        auto chunk1 = state(second, 5, true, 1, 2);
        auto chunk0 = state(first, 5, true, 0, 2);
        return chunked->receiveReliableWeatherState(chunk1) == WeatherReplicationReceiveResult::ChunkAccepted
            && chunked->receiveReliableWeatherState(chunk1) == WeatherReplicationReceiveResult::ChunkAccepted
            && !chunked->weatherBaselineComplete()
            && chunked->receiveReliableWeatherState(chunk0) == WeatherReplicationReceiveResult::Applied
            && chunked->weatherBaselineComplete() && chunked->confirmedWeather().size() == 2;
    }
}

int main()
{
    return codecRejectsMalformedState() && revisionsAndChunksAreAtomic() ? 0 : 1;
}
