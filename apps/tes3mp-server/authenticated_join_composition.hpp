#ifndef TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP
#define TES3MP_SERVER_AUTHENTICATED_JOIN_COMPOSITION_HPP

#include "native_inventory_service.hpp"
#include "tes3mp/actor_simulation.hpp"
#include "tes3mp/authenticated_join.hpp"
#include "tes3mp/combat_world.hpp"
#include "tes3mp/inventory_world.hpp"
#include "tes3mp/server_authentication.hpp"
#include "tes3mp/transport.hpp"
#include "tes3mp/world_state.hpp"

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
        virtual const CanonicalInventoryWorld* pendingInventory() const noexcept { return nullptr; }
        virtual const CanonicalCombatWorld* pendingCombat() const noexcept { return nullptr; }
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
            ResumeTokenContext context, std::optional<AuthenticatedAdmission::PlayerClaim> playerClaim = std::nullopt,
            std::optional<PlayerCredential> providedCredential = std::nullopt, std::string username = {}) noexcept;

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
            const CanonicalInteractiveObjectWorld* objects = nullptr, CanonicalInventoryWorld* inventory = nullptr,
            CanonicalCombatWorld* combat = nullptr, const CanonicalPlayerCombatTemplate* playerCombatTemplate = nullptr,
            const ItemPrototypeCatalog* itemCatalog = nullptr,
            const CharacterContentCatalog* characterContent = nullptr,
            const CanonicalWorldState* world = nullptr, NativeInventoryService* nativeInventory = nullptr) noexcept
            : mQueues(queues)
            , mConnection(connection)
            , mSessions(sessions)
            , mActors(actors)
            , mObjects(objects)
            , mInventory(inventory)
            , mNativeInventory(nativeInventory)
            , mCombat(combat)
            , mPlayerCombatTemplate(playerCombatTemplate)
            , mItemCatalog(itemCatalog)
            , mCharacterContent(characterContent)
            , mWorld(world)
        {
        }

        bool enqueueJoinResponses(std::span<const std::byte> authentication, std::span<const std::byte> snapshot,
            const CanonicalServerState& before, const CanonicalServerState& after, const AuthenticatedJoinResult& join,
            ServerTick tick, CanonicalStateVersion stateVersion) noexcept override;
        bool commitJoinState() noexcept override;
        const CanonicalInventoryWorld* pendingInventory() const noexcept override
        {
            return mPendingInventory ? &*mPendingInventory : mInventory;
        }
        const CanonicalCombatWorld* pendingCombat() const noexcept override
        {
            return mPendingCombat ? &*mPendingCombat : mCombat;
        }

    private:
        OutboundQueueSet& mQueues;
        TransportConnectionId mConnection;
        ConnectionSessionCoordinator* mSessions;
        const CanonicalActorWorld* mActors;
        const CanonicalInteractiveObjectWorld* mObjects;
        CanonicalInventoryWorld* mInventory;
        NativeInventoryService* mNativeInventory;
        CanonicalCombatWorld* mCombat;
        const CanonicalPlayerCombatTemplate* mPlayerCombatTemplate;
        const ItemPrototypeCatalog* mItemCatalog;
        const CharacterContentCatalog* mCharacterContent;
        const CanonicalWorldState* mWorld = nullptr;
        std::optional<CanonicalInventoryWorld> mPendingInventory;
        std::optional<CanonicalCombatWorld> mPendingCombat;
    };
}

#endif
