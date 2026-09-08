#ifndef TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP
#define TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP

#include "tes3mp/actor_simulation.hpp"
#include "tes3mp/authenticated_join.hpp"
#include "tes3mp/combat_world.hpp"
#include "tes3mp/inventory_world.hpp"
#include "tes3mp/server_authentication.hpp"
#include "tes3mp/transport.hpp"

#include <optional>
#include <span>

namespace TES3MP
{
    class CanonicalInteractiveObjectWorld;
}

namespace TES3MP::ServerApp
{
    class ConnectionSessionCoordinator;
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
            std::span<const std::byte> snapshot, const CanonicalServerState& before, const CanonicalServerState& after,
            const AuthenticatedJoinResult& join, ServerTick tick, CanonicalStateVersion stateVersion) noexcept = 0;
        virtual bool commitJoinState() noexcept { return true; }
    };

    class AuthenticatedJoinComposition
    {
    public:
        AuthenticatedJoinComposition(AuthenticatedJoinCoordinator& joins, ServerAuthenticationService& authentication,
            JoinResponseQueue& responses) noexcept
            : mJoins(joins)
            , mAuthentication(authentication)
            , mResponses(responses)
        {
        }

        JoinCompositionOutcome join(PrincipalId principal, SessionGeneration generation, ServerTick tick,
            ResumeTokenContext context,
            std::optional<AuthenticatedAdmission::PlayerClaim> playerClaim = std::nullopt) noexcept;

    private:
        AuthenticatedJoinCoordinator& mJoins;
        ServerAuthenticationService& mAuthentication;
        JoinResponseQueue& mResponses;
    };

    class TransportJoinResponseQueue final : public JoinResponseQueue
    {
    public:
        TransportJoinResponseQueue(OutboundQueueSet& queues, TransportConnectionId connection,
            ConnectionSessionCoordinator* sessions = nullptr, const CanonicalActorWorld* actors = nullptr,
            const CanonicalInteractiveObjectWorld* objects = nullptr,
            CanonicalInventoryWorld* inventory = nullptr, const CanonicalCombatWorld* combat = nullptr) noexcept
            : mQueues(queues)
            , mConnection(connection)
            , mSessions(sessions)
            , mActors(actors)
            , mObjects(objects)
            , mInventory(inventory)
            , mCombat(combat)
        {
        }

        bool enqueueJoinResponses(std::span<const std::byte> authentication, std::span<const std::byte> snapshot,
            const CanonicalServerState& before, const CanonicalServerState& after, const AuthenticatedJoinResult& join,
            ServerTick tick, CanonicalStateVersion stateVersion) noexcept override;
        bool commitJoinState() noexcept override;

    private:
        OutboundQueueSet& mQueues;
        TransportConnectionId mConnection;
        ConnectionSessionCoordinator* mSessions;
        const CanonicalActorWorld* mActors;
        const CanonicalInteractiveObjectWorld* mObjects;
        CanonicalInventoryWorld* mInventory;
        const CanonicalCombatWorld* mCombat;
        std::optional<CanonicalInventoryWorld> mPendingInventory;
    };
}

#endif
