#ifndef TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP
#define TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP

#include "tes3mp/server_session.hpp"
#include "tes3mp/transport.hpp"
#include "tes3mp/authenticated_join.hpp"

#include <map>
#include <vector>

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
        ProtocolRejected,
        AuthenticationPending,
        Joined,
        CommandSubmitted,
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
        ConnectionSessionResult dispatch(TransportConnectionId connection, const TransportMessage& message,
            AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto, ServerCommandIntakeCoordinator& intake,
            ServerTick tick) noexcept;
        ConnectionSessionResult dispatch(TransportConnectionId connection, const TransportMessage& message,
            AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto, ServerTick tick) noexcept;
        ConnectionSessionResult pollAuthentication(TransportConnectionId connection,
            AuthenticatedJoinCoordinator& joins, CredentialCrypto& crypto, ServerTick tick) noexcept;
        std::size_t size() const noexcept { return mConnections.size(); }
        std::vector<TransportConnectionId> connections() const;
        std::optional<TransportConnectionId> connectionForSession(SessionId session) const noexcept;

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
