#ifndef TES3MP_SERVER_WEATHER_PROJECTION_HPP
#define TES3MP_SERVER_WEATHER_PROJECTION_HPP

#include <tes3mp/canonical_state.hpp>
#include <tes3mp/transport.hpp>
#include <tes3mp/weather_replication.hpp>
#include <tes3mp/world_state.hpp>

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct WeatherStateDelivery
    {
        SessionId targetSession;
        std::vector<ReliableWeatherState> chunks;
    };

    std::optional<WeatherStateDelivery> projectWeatherBaseline(const CanonicalServerState& players,
        const CanonicalWorldState& world, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision);
    std::optional<WeatherStateDelivery> projectWeatherUpdate(const CanonicalWorldState& before,
        const CanonicalWorldState& after, SessionId target, SessionGeneration generation, ServerTick tick,
        CanonicalRevision canonicalRevision);
    bool appendWeatherMessages(std::vector<std::vector<std::byte>>& frames,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const WeatherStateDelivery& delivery);
}

#endif
