#ifndef TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP
#define TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP

#include "tes3mp/server_session.hpp"
#include "tes3mp/transport.hpp"

#include <map>

namespace TES3MP::ServerApp
{
    enum class ConnectionSessionResult : std::uint8_t
    {
        Accepted,
        Duplicate,
        AtCapacity,
        QueueRejected,
        SessionRejected,
        UnknownConnection,
    };

    class ConnectionSessionCoordinator
    {
    public:
        ConnectionSessionCoordinator(MonotonicClock& clock, Observability& observability,
            SessionTimeoutPolicy timeouts, CapabilityOffer offer, ServerAuthenticationService& authentication,
            OutboundQueueSet& queues, std::size_t capacity) noexcept;

        ConnectionSessionResult accept(TransportConnectionId connection, AdmissionScopeId scope) noexcept;
        ConnectionSessionResult close(TransportConnectionId connection) noexcept;
        ServerSessionStateMachine* session(TransportConnectionId connection) noexcept;
        const AdmissionScopeId* admissionScope(TransportConnectionId connection) const noexcept;
        std::size_t size() const noexcept { return mConnections.size(); }

    private:
        struct Connection
        {
            AdmissionScopeId scope;
            std::unique_ptr<ServerSessionStateMachine> session;
        };

        MonotonicClock& mClock;
        Observability& mObservability;
        SessionTimeoutPolicy mTimeouts;
        CapabilityOffer mOffer;
        ServerAuthenticationService& mAuthentication;
        OutboundQueueSet& mQueues;
        std::size_t mCapacity;
        std::map<TransportConnectionId, Connection> mConnections;
    };
}

#endif
