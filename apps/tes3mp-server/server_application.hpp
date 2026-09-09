#ifndef TES3MP_SERVER_APPLICATION_HPP
#define TES3MP_SERVER_APPLICATION_HPP

#include "connection_session_coordinator.hpp"
#include "server_config.hpp"
#include "tes3mp/actor_simulation.hpp"
#include "tes3mp/combat_world.hpp"
#include "tes3mp/interactive_object_catalog.hpp"
#include "tes3mp/interactive_object_world.hpp"
#include "tes3mp/inventory_world.hpp"
#include "tes3mp/protocol_pose.hpp"
#include "tes3mp/server_lifecycle.hpp"

#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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
        const ActorCatalog* actorCatalog = nullptr;
        CanonicalActorWorld* actors = nullptr;
        ServerCollisionQuery* actorCollision = nullptr;
        const InteractiveObjectCatalog* interactiveObjectCatalog = nullptr;
        CanonicalInteractiveObjectWorld* interactiveObjects = nullptr;
        const ItemPrototypeCatalog* itemCatalog = nullptr;
        CanonicalInventoryWorld* inventory = nullptr;
        CanonicalCombatWorld* combat = nullptr;
        const MeleeWeaponCatalog* meleeWeapons = nullptr;
        const CanonicalPlayerCombatTemplate* playerCombatTemplate = nullptr;
        const OpenMwMeleeSettings* meleeSettings = nullptr;
        const MeleeAuthorityPolicy* meleePolicy = nullptr;
        ServerMeleeContactQuery* meleeContact = nullptr;
    };

    class ServerApplication
    {
    public:
        ServerApplication(TransportRuntime& transport, const ServerConfig& config) noexcept;
        ServerApplication(
            TransportRuntime& transport, const ServerConfig& config, ServerApplicationWiring wiring) noexcept;
        ~ServerApplication();

        bool start() noexcept;
        bool pump() noexcept;
        bool pump(ServerTick tick) noexcept;
        bool stop() noexcept;
        bool running() const noexcept { return mRunning; }
        std::string_view failure() const noexcept { return mFailure; }

    private:
        struct RetainedPose
        {
            ClientVrPoseSample sample;
            std::vector<std::byte> payload;
        };

        TransportRuntime& mTransport;
        const ServerConfig& mConfig;
        std::optional<ListenerId> mListener;
        bool mRunning = false;
        std::string_view mFailure;
        std::optional<ServerApplicationWiring> mWiring;
        std::map<SessionId, RetainedPose> mLatestPoses;

        bool failConnection(TransportConnectionId connection, std::string_view failure) noexcept;
        bool disconnectConnection(TransportConnectionId connection, ServerTick tick) noexcept;
        bool disconnectConnections(std::span<const TransportConnectionId> connections, ServerTick tick) noexcept;
        bool resumeConnection(TransportConnectionId connection, ServerTick tick) noexcept;
        bool resyncConnection(TransportConnectionId connection, ServerTick tick) noexcept;
        bool expireSessions(ServerTick tick) noexcept;
        bool relayPose(TransportConnectionId connection, const TransportMessage& message) noexcept;
        bool supportsActors(TransportConnectionId connection) const noexcept;
        bool supportsInteractiveObjects(TransportConnectionId connection) const noexcept;
        bool supportsInventory(TransportConnectionId connection) const noexcept;
        bool supportsCombat(TransportConnectionId connection) const noexcept;
        bool supportsCharacterCreation(TransportConnectionId connection) const noexcept;
    };
}

#endif
