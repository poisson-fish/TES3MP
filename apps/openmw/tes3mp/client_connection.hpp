#ifndef OPENMW_TES3MP_CLIENT_CONNECTION_HPP
#define OPENMW_TES3MP_CLIENT_CONNECTION_HPP

#include "adapter.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
        VrPoseInputProvider* poseInput = nullptr;
        MovementMetricSink* movementMetrics = nullptr;
    };

    using ClientCoordinatorResult = std::variant<std::unique_ptr<EngineCoordinator>, ClientCompositionFailure>;

    struct ClientLauncherConfiguration
    {
        std::uint16_t defaultPort = 0;
        std::uint64_t timeoutMilliseconds = 0;
        std::filesystem::path passwordFile;
        std::filesystem::path playerCredentialDirectory;
        std::filesystem::path serverExecutable;
        std::filesystem::path serverConfig;
        ContentManifestId contentManifest;
        ClientProviders providers;
    };

    std::optional<ConnectionEndpoint> parseServerAddress(
        std::string_view address, std::uint16_t defaultPort) noexcept;

    std::unique_ptr<EngineCoordinator> makeClientLauncher(ClientLauncherConfiguration configuration) noexcept;

    std::unique_ptr<PlayerCredentialPersistence> makeFilePlayerCredentialPersistence(
        std::filesystem::path path);

    ClientCoordinatorResult makeClientCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile,
        const std::filesystem::path& playerCredentialFile, ContentManifestId contentManifest,
        ClientProviders providers, std::string_view passwordOverride = {}) noexcept;
}

#endif
