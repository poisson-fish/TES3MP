#include "phase7_queue_telemetry.hpp"

#include <algorithm>

namespace TES3MP::ServerApp
{
    TransportTelemetryResult Phase7QueueTelemetry::tryRecord(
        const TransportTelemetryObservation& observation) noexcept
    {
        if (observation.direction != TransportTelemetryDirection::Outbound)
            return TransportTelemetryResult::Accepted;
        const auto channel = observation.channel == TransportChannel::ReliableOrdered ? 0u : 1u;
        if (observation.kind == TransportTelemetryKind::QueuedMessages)
        {
            mCurrentMessages[channel] = observation.value;
            mHighWaterMessages[channel] = std::max(mHighWaterMessages[channel], observation.value);
        }
        else if (observation.kind == TransportTelemetryKind::QueuedBytes)
        {
            mCurrentBytes[channel] = observation.value;
            mHighWaterBytes[channel] = std::max(mHighWaterBytes[channel], observation.value);
        }
        mReady = !mReported && mHighWaterMessages[0] > 0 && mHighWaterBytes[0] > 0
            && mHighWaterMessages[1] > 0 && mHighWaterBytes[1] > 0
            && mCurrentMessages[0] == 0 && mCurrentBytes[0] == 0
            && mCurrentMessages[1] == 0 && mCurrentBytes[1] == 0;
        return TransportTelemetryResult::Accepted;
    }

    std::optional<Phase7QueueDrainEvidence> Phase7QueueTelemetry::takeDrainEvidence() noexcept
    {
        if (!mReady || mReported) return std::nullopt;
        mReady = false;
        mReported = true;
        return Phase7QueueDrainEvidence{ mHighWaterMessages[0], mHighWaterBytes[0],
            mHighWaterMessages[1], mHighWaterBytes[1] };
    }
}
