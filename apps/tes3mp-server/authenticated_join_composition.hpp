#ifndef TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP
#define TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP

#include "tes3mp/authenticated_join.hpp"
#include "tes3mp/server_authentication.hpp"
#include "tes3mp/transport.hpp"

#include <span>
#include <optional>

namespace TES3MP::ServerApp
{
    enum class JoinCompositionResult : std::uint8_t
    {
        Committed,
        JoinRejected,
        TokenRejected,
        EncodingRejected,
        QueueRejected,
        CommitRejected,
    };

    struct JoinCompositionOutcome
    {
        JoinCompositionResult result = JoinCompositionResult::JoinRejected;
        std::optional<AuthenticatedJoinResult> committed;
    };

    class JoinResponseQueue
    {
    public:
        virtual ~JoinResponseQueue() = default;
        virtual bool enqueueJoinResponses(std::span<const std::byte> authentication,
            std::span<const std::byte> snapshot) noexcept = 0;
    };

    class AuthenticatedJoinComposition
    {
    public:
        AuthenticatedJoinComposition(AuthenticatedJoinCoordinator& joins,
            ServerAuthenticationService& authentication, JoinResponseQueue& responses) noexcept
            : mJoins(joins), mAuthentication(authentication), mResponses(responses)
        {
        }

        JoinCompositionOutcome join(PrincipalId principal, SessionGeneration generation,
            ServerTick tick, ResumeTokenContext context) noexcept;

    private:
        AuthenticatedJoinCoordinator& mJoins;
        ServerAuthenticationService& mAuthentication;
        JoinResponseQueue& mResponses;
    };

    class TransportJoinResponseQueue final : public JoinResponseQueue
    {
    public:
        TransportJoinResponseQueue(OutboundQueueSet& queues, TransportConnectionId connection) noexcept
            : mQueues(queues), mConnection(connection) {}

        bool enqueueJoinResponses(std::span<const std::byte> authentication,
            std::span<const std::byte> snapshot) noexcept override;

    private:
        OutboundQueueSet& mQueues;
        TransportConnectionId mConnection;
    };
}

#endif
