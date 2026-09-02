#ifndef TES3MP_SERVER_APPLICATION_HPP
#define TES3MP_SERVER_APPLICATION_HPP

#include "server_config.hpp"
#include "connection_session_coordinator.hpp"
#include "tes3mp/server_lifecycle.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace TES3MP::ServerApp
{
    struct ServerApplicationWiring
    {
        ConnectionSessionCoordinator& sessions;
        AuthenticatedJoinCoordinator& joins;
        CredentialCrypto& crypto;
        OutboundQueueSet& queues;
        MonotonicClock& clock;
        ServerCommandIntakeCoordinator& intake;
        CanonicalCommandReducer& reducer;
        ServerLifecycleCoordinator& lifecycle;
    };

    class ServerApplication
    {
    public:
        ServerApplication(TransportRuntime& transport, const ServerConfig& config) noexcept;
        ServerApplication(TransportRuntime& transport, const ServerConfig& config,
            ServerApplicationWiring wiring) noexcept;
        ~ServerApplication();

        bool start() noexcept;
        bool pump() noexcept;
        bool pump(ServerTick tick) noexcept;
        bool stop() noexcept;
        bool running() const noexcept { return mRunning; }
        std::string_view failure() const noexcept { return mFailure; }

    private:
        TransportRuntime& mTransport;
        const ServerConfig& mConfig;
        std::optional<ListenerId> mListener;
        bool mRunning = false;
        std::string_view mFailure;
        std::optional<ServerApplicationWiring> mWiring;

        bool failConnection(TransportConnectionId connection, std::string_view failure) noexcept;
        bool disconnectConnection(TransportConnectionId connection, ServerTick tick) noexcept;
        bool disconnectConnections(std::span<const TransportConnectionId> connections, ServerTick tick) noexcept;
        bool resumeConnection(TransportConnectionId connection, ServerTick tick) noexcept;
        bool expireSessions(ServerTick tick) noexcept;
    };
}

#endif
