#include "client_connection.hpp"

#ifdef TES3MP_OPENMW_HAS_GNS
#include <tes3mp/transport_gns.hpp>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        class SteadyClock final : public MonotonicClock
        {
        public:
            MonotonicInstant now() const noexcept override
            {
                const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
                return MonotonicInstant::fromNanoseconds(static_cast<std::uint64_t>(value));
            }
        };

        std::optional<OutboundQueuePolicy> outboundPolicy()
        {
            return OutboundQueuePolicy::create(64, 512 * 1024, 8, 4, 8, 1, 4, 1, 8, 250);
        }

        bool replaceCredentialFile(
            const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
        {
#ifdef _WIN32
            return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                != 0;
#else
            return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
        }

        class TemporaryCredentialFileCleanup
        {
        public:
            explicit TemporaryCredentialFileCleanup(std::filesystem::path path) noexcept
                : mPath(std::move(path))
            {
            }

            void activate() noexcept { mActive = true; }

            ~TemporaryCredentialFileCleanup()
            {
                if (!mActive)
                    return;
                std::error_code ignored;
                std::filesystem::remove(mPath, ignored);
            }

        private:
            std::filesystem::path mPath;
            bool mActive = false;
        };

        struct CredentialBuffer
        {
            ~CredentialBuffer()
            {
                volatile std::byte* destination = bytes.data();
                for (std::size_t index = 0; index < bytes.size(); ++index)
                    destination[index] = std::byte{};
            }
            std::array<std::byte, PlayerCredentialBytes> bytes{};
        };

        class FilePlayerCredentialPersistence final : public PlayerCredentialPersistence
        {
        public:
            explicit FilePlayerCredentialPersistence(std::filesystem::path path) : mPath(std::move(path)) {}

            bool store(PlayerCredential credential) noexcept override
            try
            {
                CredentialBuffer buffer;
                if (!credential.copyTo(buffer.bytes))
                    return false;
                auto temporary = mPath;
                temporary += ".tmp";
                TemporaryCredentialFileCleanup cleanup(temporary);
                {
                    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                    if (!stream)
                        return false;
                    cleanup.activate();
#ifndef _WIN32
                    if (::chmod(temporary.c_str(), S_IRUSR | S_IWUSR) != 0)
                        return false;
#endif
                    stream.write(reinterpret_cast<const char*>(buffer.bytes.data()), buffer.bytes.size());
                    stream.flush();
                    if (!stream)
                        return false;
                }
                return replaceCredentialFile(temporary, mPath);
            }
            catch (...)
            {
                return false;
            }

        private:
            std::filesystem::path mPath;
        };

        std::optional<PlayerCredential> loadPlayerCredential(const std::filesystem::path& path) noexcept
        try
        {
            if (path.empty() || !std::filesystem::exists(path))
                return std::nullopt;
            if (!std::filesystem::is_regular_file(path)
                || std::filesystem::file_size(path) != PlayerCredentialBytes)
                return std::nullopt;
            std::array<std::byte, PlayerCredentialBytes> bytes{};
            std::ifstream stream(path, std::ios::binary);
            stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            auto credential = stream && stream.peek() == std::char_traits<char>::eof()
                ? PlayerCredential::create(bytes) : std::nullopt;
            std::fill(bytes.begin(), bytes.end(), std::byte{});
            return credential;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::unique_ptr<PlayerCredentialPersistence> makeFilePlayerCredentialPersistence(
        std::filesystem::path path)
    {
        return std::make_unique<FilePlayerCredentialPersistence>(std::move(path));
    }

    ClientCoordinatorResult makeClientCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile,
        const std::filesystem::path& playerCredentialFile, ContentManifestId contentManifest,
        ClientProviders providers) noexcept
    try
    {
        if (!providers.input || !providers.presentation || !providers.status)
            return ClientCompositionFailure::ProvidersUnavailable;
        if (port == 0 || port > (std::numeric_limits<std::uint16_t>::max)())
            return ClientCompositionFailure::InvalidEndpoint;
        auto endpoint = ConnectionEndpoint::create(host, static_cast<std::uint16_t>(port));
        if (!endpoint)
            return ClientCompositionFailure::InvalidEndpoint;
        if (timeoutMilliseconds == 0 || timeoutMilliseconds > 60'000)
            return ClientCompositionFailure::InvalidTimeout;

        std::vector<std::byte> bytes;
        if (!passwordFile.empty())
        {
            std::ifstream stream(passwordFile, std::ios::binary);
            char byte = 0;
            while (stream.get(byte) && bytes.size() <= MaximumAuthenticationMaterialBytes)
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
            if (!stream.eof())
            {
                std::fill(bytes.begin(), bytes.end(), std::byte{});
                return ClientCompositionFailure::CredentialReadFailed;
            }
            if (!bytes.empty() && bytes.back() == std::byte{ '\n' })
                bytes.pop_back();
            if (!bytes.empty() && bytes.back() == std::byte{ '\r' })
                bytes.pop_back();
        }
        auto password = AuthenticationMaterial::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        if (!password)
            return ClientCompositionFailure::CredentialRejected;
        if (playerCredentialFile.empty())
            return ClientCompositionFailure::CredentialRejected;
        const bool playerCredentialExists = std::filesystem::exists(playerCredentialFile);
        auto playerCredential = loadPlayerCredential(playerCredentialFile);
        if (playerCredentialExists && !playerCredential)
            return ClientCompositionFailure::CredentialReadFailed;

#ifdef TES3MP_OPENMW_HAS_GNS
        auto limits = TransportLimits::create(1, 1, 1, 32);
        auto transport = limits ? makeGameNetworkingSocketsTransport(*limits) : TransportFactoryResult{};
        if (!transport)
            return ClientCompositionFailure::TransportUnavailable;
        auto clock = std::make_unique<SteadyClock>();
        auto timeouts = SessionTimeoutPolicy::create(
            timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000);
        auto queue = outboundPolicy();
        auto created = timeouts && queue
            ? ClientSessionRuntime::create(*transport.runtime, *clock, *timeouts, SessionGeneration::initial(), *queue)
            : ClientRuntimeCreateResult{ SessionTransitionError{} };
        auto* runtime = std::get_if<std::unique_ptr<ClientSessionRuntime>>(&created);
        if (!runtime || !*runtime)
            return ClientCompositionFailure::RuntimeUnavailable;
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 1, 1));
        const std::array optional{ vrPoseCapability() };
        auto offer = std::get<CapabilityOffer>(
            CapabilityOffer::create(std::move(versions), optional, {}, contentManifest));
        if ((*runtime)->start(
                *endpoint, ClientHello::fromOffer(std::move(offer)),
                AuthenticationRequest::join(std::move(*password), std::move(playerCredential)))
            != HeadlessClientResult::Accepted)
            return ClientCompositionFailure::ConnectionRejected;
        return makeCoordinator(std::move(transport.runtime), std::move(clock), std::move(*runtime),
            ReconnectConfiguration{ *endpoint, *timeouts, *queue, contentManifest },
            *providers.input, *providers.presentation,
            *providers.status, providers.control, providers.poseInput,
            makeFilePlayerCredentialPersistence(playerCredentialFile));
#else
        return ClientCompositionFailure::TransportUnavailable;
#endif
    }
    catch (...)
    {
        return ClientCompositionFailure::RuntimeUnavailable;
    }
}
