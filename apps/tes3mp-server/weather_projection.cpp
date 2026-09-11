#include "weather_projection.hpp"

#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <ranges>

namespace TES3MP::ServerApp
{
    namespace
    {
        WeatherRegionSnapshot snapshot(const CanonicalWeatherRegionState& state) noexcept
        {
            return { state.region, state.currentWeather, state.targetWeather, state.transitionStartTick,
                state.transitionEndTick, state.nextSelectionTick, state.revision };
        }

        std::optional<WeatherStateDelivery> project(SessionId target, SessionGeneration generation,
            ServerTick tick, CanonicalRevision canonicalRevision, bool completeBaseline,
            std::span<const WeatherRegionSnapshot> regions)
        try
        {
            if (regions.empty())
                return std::nullopt;
            const std::size_t chunkCount
                = (regions.size() + MaximumWeatherReplicationEntriesPerChunk - 1)
                / MaximumWeatherReplicationEntriesPerChunk;
            if (chunkCount == 0 || chunkCount > MaximumWeatherReplicationChunks)
                return std::nullopt;
            WeatherStateDelivery delivery{ target, {} };
            delivery.chunks.reserve(chunkCount);
            for (std::size_t index = 0; index < chunkCount; ++index)
            {
                const auto offset = index * MaximumWeatherReplicationEntriesPerChunk;
                const auto count = std::min(MaximumWeatherReplicationEntriesPerChunk, regions.size() - offset);
                WeatherStateHeader header{ target, generation, tick, canonicalRevision,
                    static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(chunkCount), completeBaseline };
                auto created = ReliableWeatherState::create(header, regions.subspan(offset, count));
                auto* state = std::get_if<ReliableWeatherState>(&created);
                if (!state)
                    return std::nullopt;
                delivery.chunks.push_back(std::move(*state));
            }
            return delivery;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<WeatherStateDelivery> projectWeatherBaseline(const CanonicalServerState& players,
        const CanonicalWorldState& world, SessionId target, ServerTick tick, CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        if (!session || !world.weather())
            return std::nullopt;
        std::vector<WeatherRegionSnapshot> regions;
        regions.reserve(world.weather()->regions.size());
        for (const auto& region : world.weather()->regions)
            regions.push_back(snapshot(region));
        return project(target, session->sessionGeneration(), tick, canonicalRevision, true, regions);
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<WeatherStateDelivery> projectWeatherUpdate(const CanonicalWorldState& before,
        const CanonicalWorldState& after, SessionId target, SessionGeneration generation, ServerTick tick,
        CanonicalRevision canonicalRevision)
    try
    {
        if (!before.weather() || !after.weather()
            || before.weather()->regions.size() != after.weather()->regions.size())
            return std::nullopt;
        std::vector<WeatherRegionSnapshot> changed;
        for (std::size_t index = 0; index < after.weather()->regions.size(); ++index)
        {
            const auto& oldState = before.weather()->regions[index];
            const auto& newState = after.weather()->regions[index];
            if (oldState.region != newState.region)
                return std::nullopt;
            if (oldState != newState)
                changed.push_back(snapshot(newState));
        }
        if (changed.empty())
            return WeatherStateDelivery{ target, {} };
        return project(target, generation, tick, canonicalRevision, false, changed);
    }
    catch (...)
    {
        return std::nullopt;
    }

    bool appendWeatherMessages(std::vector<std::vector<std::byte>>& frames,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const WeatherStateDelivery& delivery)
    try
    {
        for (const auto& chunk : delivery.chunks)
        {
            auto encoded = encodeProtocolFrame(MessageClass::ReliableOperation, MessageKind::ReliableWeatherState,
                encodeReliableWeatherState(chunk));
            auto* frame = std::get_if<std::vector<std::byte>>(&encoded);
            if (!frame)
                return false;
            frames.push_back(std::move(*frame));
            messages.push_back({ connection, TransportChannel::ReliableOrdered, frames.back() });
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
