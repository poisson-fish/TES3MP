#include "desktop_connection.hpp"

#ifdef TES3MP_OPENMW_HAS_GNS
#include <tes3mp/transport_gns.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <vector>

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
    }

    DesktopCoordinatorResult makeDesktopCoordinator(std::string_view host, std::uint64_t port,
        std::uint64_t timeoutMilliseconds, const std::filesystem::path& passwordFile, SemanticInputProvider& input,
        PresentationProvider& presentation, ConnectionStatusProvider& status) noexcept try
    {
        if (port == 0 || port > std::numeric_limits<std::uint16_t>::max())
            return DesktopConnectionFailure::InvalidEndpoint;
        auto endpoint = ConnectionEndpoint::create(host, static_cast<std::uint16_t>(port));
        if (!endpoint)
            return DesktopConnectionFailure::InvalidEndpoint;
        if (timeoutMilliseconds == 0 || timeoutMilliseconds > 60'000)
            return DesktopConnectionFailure::InvalidTimeout;

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
                return DesktopConnectionFailure::CredentialReadFailed;
            }
            if (!bytes.empty() && bytes.back() == std::byte{ '\n' })
                bytes.pop_back();
            if (!bytes.empty() && bytes.back() == std::byte{ '\r' })
                bytes.pop_back();
        }
        auto password = AuthenticationMaterial::create(bytes);
        std::fill(bytes.begin(), bytes.end(), std::byte{});
        if (!password)
            return DesktopConnectionFailure::CredentialRejected;

#ifdef TES3MP_OPENMW_HAS_GNS
        auto limits = TransportLimits::create(1, 1, 1, 32);
        auto transport = limits ? makeGameNetworkingSocketsTransport(*limits) : TransportFactoryResult{};
        if (!transport)
            return DesktopConnectionFailure::TransportUnavailable;
        auto clock = std::make_unique<SteadyClock>();
        auto timeouts = SessionTimeoutPolicy::create(
            timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000, timeoutMilliseconds * 1'000'000);
        auto queue = outboundPolicy();
        auto created = timeouts && queue
            ? ClientSessionRuntime::create(*transport.runtime, *clock, *timeouts, SessionGeneration::initial(), *queue)
            : ClientRuntimeCreateResult{ SessionTransitionError{} };
        auto* runtime = std::get_if<std::unique_ptr<ClientSessionRuntime>>(&created);
        if (!runtime || !*runtime)
            return DesktopConnectionFailure::RuntimeUnavailable;
        auto versions = std::get<ProtocolVersionRange>(ProtocolVersionRange::create(1, 0, 0));
        auto offer = std::get<CapabilityOffer>(CapabilityOffer::create(std::move(versions), {}, {}));
        if ((*runtime)->start(*endpoint, ClientHello::fromOffer(std::move(offer)),
                AuthenticationRequest::join(std::move(*password)))
            != HeadlessClientResult::Accepted)
            return DesktopConnectionFailure::ConnectionRejected;
        return makeCoordinator(
            std::move(transport.runtime), std::move(clock), std::move(*runtime), input, presentation, status);
#else
        return DesktopConnectionFailure::TransportUnavailable;
#endif
    }
    catch (...)
    {
        return DesktopConnectionFailure::RuntimeUnavailable;
    }
}
