#ifndef TES3MP_TRANSPORT_GNS_HPP
#define TES3MP_TRANSPORT_GNS_HPP

#include "transport.hpp"

#include <memory>

namespace TES3MP
{
    enum class TransportFactoryFailure
    {
        None,
        InvalidLimits,
        DependencyInitialization,
        RuntimeAlreadyActive,
    };

    struct TransportFactoryResult
    {
        TransportFactoryFailure failure = TransportFactoryFailure::DependencyInitialization;
        std::unique_ptr<TransportRuntime> runtime;

        explicit operator bool() const noexcept { return runtime != nullptr; }
    };

    TransportFactoryResult makeGameNetworkingSocketsTransport(TransportLimits limits) noexcept;
    TransportFactoryResult makeGameNetworkingSocketsTransport(
        TransportLimits limits, TransportTelemetrySink& telemetry) noexcept;
}

#endif
