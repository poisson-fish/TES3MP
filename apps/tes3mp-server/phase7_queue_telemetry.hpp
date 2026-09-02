#ifndef TES3MP_SERVER_PHASE7_QUEUE_TELEMETRY_HPP
#define TES3MP_SERVER_PHASE7_QUEUE_TELEMETRY_HPP

#include <tes3mp/transport.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace TES3MP::ServerApp
{
    struct Phase7QueueDrainEvidence
    {
        std::uint64_t reliableHighWaterMessages = 0;
        std::uint64_t reliableHighWaterBytes = 0;
        std::uint64_t latestHighWaterMessages = 0;
        std::uint64_t latestHighWaterBytes = 0;
    };

    class Phase7QueueTelemetry final : public TransportTelemetrySink
    {
    public:
        TransportTelemetryResult tryRecord(const TransportTelemetryObservation& observation) noexcept override;
        std::optional<Phase7QueueDrainEvidence> takeDrainEvidence() noexcept;

    private:
        std::array<std::uint64_t, 2> mCurrentMessages{};
        std::array<std::uint64_t, 2> mCurrentBytes{};
        std::array<std::uint64_t, 2> mHighWaterMessages{};
        std::array<std::uint64_t, 2> mHighWaterBytes{};
        bool mReady = false;
        bool mReported = false;
    };
}

#endif
