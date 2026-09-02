#include "connection_session_coordinator.hpp"

#include <variant>

namespace TES3MP::ServerApp
{
    ConnectionSessionCoordinator::ConnectionSessionCoordinator(MonotonicClock& clock, Observability& observability,
        SessionTimeoutPolicy timeouts, CapabilityOffer offer, ServerAuthenticationService& authentication,
        OutboundQueueSet& queues, std::size_t capacity) noexcept
        : mClock(clock), mObservability(observability), mTimeouts(timeouts), mOffer(std::move(offer)),
          mAuthentication(authentication), mQueues(queues), mCapacity(capacity)
    {
    }

    ConnectionSessionResult ConnectionSessionCoordinator::accept(
        TransportConnectionId connection, AdmissionScopeId scope) noexcept
    {
        if (mConnections.contains(connection)) return ConnectionSessionResult::Duplicate;
        if (mConnections.size() >= mCapacity) return ConnectionSessionResult::AtCapacity;
        if (mQueues.attach(connection) != TransportResult::Accepted) return ConnectionSessionResult::QueueRejected;

        try
        {
            auto created = ServerSessionStateMachine::create(mClock, mObservability, mTimeouts,
                SessionGeneration::initial(), mOffer, mAuthentication);
            if (!std::holds_alternative<std::unique_ptr<ServerSessionStateMachine>>(created))
            {
                (void)mQueues.detach(connection);
                return ConnectionSessionResult::SessionRejected;
            }
            auto state = std::get<std::unique_ptr<ServerSessionStateMachine>>(std::move(created));
            if (!state->handle(ServerEncryptedTransportReady{}).accepted())
            {
                (void)mQueues.detach(connection);
                return ConnectionSessionResult::SessionRejected;
            }
            mConnections.emplace(connection, Connection{ std::move(scope), std::move(state) });
            return ConnectionSessionResult::Accepted;
        }
        catch (...)
        {
            (void)mQueues.detach(connection);
            return ConnectionSessionResult::SessionRejected;
        }
    }

    ConnectionSessionResult ConnectionSessionCoordinator::close(TransportConnectionId connection) noexcept
    {
        const auto found = mConnections.find(connection);
        if (found == mConnections.end()) return ConnectionSessionResult::UnknownConnection;
        (void)found->second.session->handle(ServerClose{});
        mConnections.erase(found);
        (void)mQueues.detach(connection);
        return ConnectionSessionResult::Accepted;
    }

    ServerSessionStateMachine* ConnectionSessionCoordinator::session(TransportConnectionId connection) noexcept
    {
        const auto found = mConnections.find(connection);
        return found == mConnections.end() ? nullptr : found->second.session.get();
    }

    const AdmissionScopeId* ConnectionSessionCoordinator::admissionScope(TransportConnectionId connection) const noexcept
    {
        const auto found = mConnections.find(connection);
        return found == mConnections.end() ? nullptr : &found->second.scope;
    }
}
