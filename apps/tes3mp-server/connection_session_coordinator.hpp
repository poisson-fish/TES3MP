#ifndef TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP
#define TES3MP_SERVER_CONNECTION_SESSION_COORDINATOR_HPP

#include "native_inventory_service.hpp"
#include "tes3mp/actor_simulation.hpp"
#include "tes3mp/authenticated_join.hpp"
#include "tes3mp/character_creation_protocol.hpp"
#include "tes3mp/combat_world.hpp"
#include "tes3mp/dialogue_choice_protocol.hpp"
#include "tes3mp/interactive_object_world.hpp"
#include "tes3mp/inventory_world.hpp"
#include "tes3mp/server_session.hpp"
#include "tes3mp/transport.hpp"
#include "tes3mp/world_state.hpp"

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
        ResumePrepared,
        Joined,
        CommandSubmitted,
        ResyncRequested,
        ResyncCoalesced,
        TimedOut,
    };

    class ConnectionSessionCoordinator
    {
    public:
        ConnectionSessionCoordinator(MonotonicClock& clock, Observability& observability, SessionTimeoutPolicy timeouts,
            CapabilityOffer offer, ServerAuthenticationService& authentication, OutboundQueueSet& queues,
            std::size_t capacity, const CanonicalActorWorld* actors = nullptr,
            const CanonicalInteractiveObjectWorld* objects = nullptr, CanonicalInventoryWorld* inventory = nullptr,
            CanonicalCombatWorld* combat = nullptr, const CanonicalPlayerCombatTemplate* playerCombatTemplate = nullptr,
            const ItemPrototypeCatalog* itemCatalog = nullptr,
            const CharacterContentCatalog* characterContent = nullptr,
            const CanonicalWorldState* world = nullptr, NativeInventoryService* nativeInventory = nullptr) noexcept;

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
        ConnectionSessionResult checkTimeout(TransportConnectionId connection) noexcept;
        const NativeInventoryService* nativeInventoryService() const noexcept { return mNativeInventory; }
        std::size_t size() const noexcept { return mConnections.size(); }
        std::vector<TransportConnectionId> connections() const;
        std::optional<TransportConnectionId> connectionForSession(SessionId session) const noexcept;
        std::optional<SessionResyncRequest> takeResyncRequest(TransportConnectionId connection) noexcept;

    private:
        struct Connection
        {
            AdmissionScopeId scope;
            std::unique_ptr<ServerSessionStateMachine> session;
            std::optional<SessionResyncRequest> pendingResync;
            std::optional<LocomotionInputTick> lastLocomotionInputTick;
            std::optional<LocomotionInputSequence> lastLocomotionInputSequence;
            std::optional<CommandSequence> lastCharacterCommandSequence;
        };

        MonotonicClock& mClock;
        Observability& mObservability;
        SessionTimeoutPolicy mTimeouts;
        CapabilityOffer mOffer;
        ServerAuthenticationService& mAuthentication;
        OutboundQueueSet& mQueues;
        std::size_t mCapacity;
        const CanonicalActorWorld* mActors;
        const CanonicalInteractiveObjectWorld* mObjects;
        CanonicalInventoryWorld* mInventory;
        NativeInventoryService* mNativeInventory;
        CanonicalCombatWorld* mCombat;
        const CanonicalPlayerCombatTemplate* mPlayerCombatTemplate;
        const ItemPrototypeCatalog* mItemCatalog;
        const CharacterContentCatalog* mCharacterContent;
        const CanonicalWorldState* mWorld;
        std::map<TransportConnectionId, Connection> mConnections;

        ConnectionSessionResult enqueueProtocolRejection(
            TransportConnectionId connection, const SessionRejected& rejection) noexcept;
        ConnectionSessionResult enqueueAuthenticationRejection(
            TransportConnectionId connection, AuthenticationRejectionReason reason) noexcept;
    };
}

#endif
