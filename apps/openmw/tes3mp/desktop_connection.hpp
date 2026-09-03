#ifndef OPENMW_TES3MP_DESKTOP_CONNECTION_HPP
#define OPENMW_TES3MP_DESKTOP_CONNECTION_HPP

#include "adapter.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <variant>

namespace TES3MP::OpenMWAdapter
{
    enum class DesktopConnectionFailure
    {
        InvalidEndpoint,
        InvalidTimeout,
        CredentialReadFailed,
        CredentialRejected,
        TransportUnavailable,
        RuntimeUnavailable,
        ConnectionRejected,
    };

    using DesktopCoordinatorResult = std::variant<std::unique_ptr<EngineCoordinator>, DesktopConnectionFailure>;

    DesktopCoordinatorResult makeDesktopCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile, SemanticInputProvider& input,
        PresentationProvider& presentation, ConnectionStatusProvider& status,
        ConnectionControlProvider* control = nullptr) noexcept;
}

#endif
