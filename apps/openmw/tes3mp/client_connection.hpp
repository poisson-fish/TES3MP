#ifndef OPENMW_TES3MP_CLIENT_CONNECTION_HPP
#define OPENMW_TES3MP_CLIENT_CONNECTION_HPP

#include "adapter.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <variant>

namespace TES3MP::OpenMWAdapter
{
    enum class ClientCompositionFailure
    {
        ProvidersUnavailable,
        InvalidEndpoint,
        InvalidTimeout,
        CredentialReadFailed,
        CredentialRejected,
        TransportUnavailable,
        RuntimeUnavailable,
        ConnectionRejected,
    };

    struct ClientProviders
    {
        SemanticInputProvider* input = nullptr;
        PresentationProvider* presentation = nullptr;
        ConnectionStatusProvider* status = nullptr;
        ConnectionControlProvider* control = nullptr;
    };

    using ClientCoordinatorResult = std::variant<std::unique_ptr<EngineCoordinator>, ClientCompositionFailure>;

    ClientCoordinatorResult makeClientCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile,
        ClientProviders providers) noexcept;
}

#endif
