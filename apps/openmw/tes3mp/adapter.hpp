#ifndef OPENMW_TES3MP_ADAPTER_HPP
#define OPENMW_TES3MP_ADAPTER_HPP

#include "engine_coordinator.hpp"
#include "providers.hpp"

#include <tes3mp/client_session_runtime.hpp>

#include <memory>

namespace TES3MP::OpenMWAdapter
{
    struct ReconnectConfiguration
    {
        ConnectionEndpoint endpoint;
        SessionTimeoutPolicy timeouts;
        OutboundQueuePolicy outbound;
    };

    std::unique_ptr<EngineCoordinator> makeCoordinator(std::unique_ptr<TransportRuntime> transport,
        std::unique_ptr<MonotonicClock> clock, std::unique_ptr<ClientSessionRuntime> runtime,
        ReconnectConfiguration reconnect, SemanticInputProvider& input, PresentationProvider& presentation,
        ConnectionStatusProvider& status, ConnectionControlProvider* control = nullptr) noexcept;
}

#endif
