#ifndef TES3MP_CLIENT_SESSION_RUNTIME_HPP
#define TES3MP_CLIENT_SESSION_RUNTIME_HPP

#include "actor_replication.hpp"
#include "authentication.hpp"
#include "client_locomotion.hpp"
#include "combat_replication.hpp"
#include "character_creation_protocol.hpp"
#include "headless_client_session.hpp"
#include "interactive_object_replication.hpp"
#include "inventory_replication.hpp"
#include "protocol_handshake.hpp"
#include "protocol_pose.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace TES3MP
{
    using ClientRuntimeMessage = std::variant<ServerHello, SessionRejected, AuthenticationAcceptedMessage,
        AuthenticationRejectedMessage, LatestWinsSnapshot, ReliableObservationBatch, ReliableInterestBaseline,
        LatestWinsActorSnapshot, ReliableActorInterestBaseline, ReliableInteractiveObjectInterestBaseline,
        ReliablePlayerInventoryBaseline, ReliableContainerInventoryBaseline, ReliableGroundItemBaseline,
        LatestWinsEquipmentSnapshot, LatestWinsCombatSnapshot, ReliableCombatEventBatch, ReliableCharacterProfile,
        ServerVrPoseSnapshot>;
// Combat is capability-gated and intentionally kept outside the spatial readiness lanes.

    enum class ClientRuntimeResult : std::uint8_t
    {
        Accepted,
        NotConnected,
        EncodeRejected,
        QueueRejected,
        TransportFailed,
        ProtocolRejected,
    };

    struct ClientRuntimeDrainResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        ClientSessionAction action = ClientSessionAction::None;
        std::size_t transportEvents = 0;
        std::vector<ClientRuntimeMessage> messages;
    };

    struct ClientRuntimeAdvanceResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        ClientSessionAction action = ClientSessionAction::None;
        std::size_t transportEvents = 0;
        bool snapshotApplied = false;
        bool observationApplied = false;
        bool baselineApplied = false;
        bool baselineCompleted = false;
        bool actorSnapshotApplied = false;
        bool actorBaselineApplied = false;
        bool actorBaselineCompleted = false;
        bool interactiveObjectBaselineApplied = false;
        bool interactiveObjectBaselineCompleted = false;
        bool playerInventoryApplied = false;
        bool containerInventoryApplied = false;
        bool groundItemsApplied = false;
        bool equipmentSnapshotApplied = false;
        bool inventoryReplicationCompleted = false;
        bool combatSnapshotApplied = false;
        bool resyncRequested = false;
        bool authenticationAccepted = false;
        bool characterProfileApplied = false;
        std::vector<ServerVrPoseSnapshot> poseSnapshots;
        std::vector<ReliableCombatEventBatch> combatEvents;
    };

    struct ClientRuntimeQueueResult
    {
        ClientRuntimeResult result = ClientRuntimeResult::Accepted;
        std::optional<CommandSequence> sequence;
    };

    using ClientRuntimeCreateResult = std::variant<std::unique_ptr<class ClientSessionRuntime>, SessionTransitionError>;

    class ClientSessionRuntime
    {
    public:
        static constexpr std::size_t MaximumInboundMessagesPerDrain = 32;

        static ClientRuntimeCreateResult create(TransportRuntime& transport, MonotonicClock& clock,
            SessionTimeoutPolicy timeoutPolicy, SessionGeneration generation, OutboundQueuePolicy outboundPolicy);

        HeadlessClientResult connect(const ConnectionEndpoint& endpoint) noexcept;
        HeadlessClientResult start(
            const ConnectionEndpoint& endpoint, ClientHello hello, AuthenticationRequest authentication) noexcept;
        ClientRuntimeAdvanceResult advance();
        ClientRuntimeQueueResult queueMotionIntent(PlayerMotionIntent intent);
        ClientRuntimeQueueResult queueLocomotionIntent(LocomotionIntent intent);
        ClientRuntimeQueueResult queueCellTransition(CellTransition transition);
        ClientRuntimeQueueResult queueInteractObject(InteractiveObjectId objectId, CellId targetCell,
            Position3 interactionOrigin, ObjectRevision expectedRevision,
            ObjectInteractionKind kind = ObjectInteractionKind::Activate,
            std::optional<KeyPrototypeId> requestedKey = std::nullopt);
        ClientRuntimeQueueResult queueInventoryTransaction(InventoryTransactionKind kind, ItemPrototypeId prototypeId,
            std::optional<ItemStackId> stackId, std::uint32_t count, InventoryRevision expectedInventoryRevision,
            Position3 interactionOrigin, std::optional<ContainerId> containerId = std::nullopt,
            std::optional<EquipmentSlot> slot = std::nullopt,
            std::optional<ContainerRevision> expectedContainerRevision = std::nullopt,
            std::optional<WorldItemRevision> expectedWorldItemRevision = std::nullopt);
        ClientRuntimeQueueResult queueMeleeAttack(std::optional<ActorId> target, ServerTick sourceTick,
            CombatRevision expectedAttackerRevision, CombatRevision expectedTargetRevision,
            MeleeAttackType attackType, float attackStrength);
        ClientRuntimeQueueResult queueCharacterCreation(CharacterCreationChoice choice,
            CharacterProfileRevision expectedRevision);
        std::optional<LocalLocomotionReconciliation> reconcileLocalPresentation(
            bool hardDiscontinuity = false) noexcept;
        ClientRuntimeResult requestResync(ResyncReason reason);
        ClientRuntimeResult queuePoseSample(const ClientVrPoseSample& sample);
        ClientRuntimeDrainResult drainInbound();
        ClientRuntimeResult queue(MessageClass messageClass, MessageKind kind, std::span<const std::byte> payload);
        ClientRuntimeResult flushOutbound() noexcept;
        HeadlessClientResult close() noexcept;

        HeadlessClientSession& session() noexcept { return *mSession; }
        const HeadlessClientSession& session() const noexcept { return *mSession; }
        std::optional<ResumeToken> takeResumeToken() noexcept;
        std::optional<ResumeToken> takeUnsubmittedResumeToken() noexcept;
        std::optional<PlayerCredential> takePlayerCredential() noexcept;
        std::uint64_t resumeLifetimeMilliseconds() const noexcept { return mResumeLifetimeMilliseconds; }
        CharacterLifecycle characterLifecycle() const noexcept { return mCharacterLifecycle; }
        CharacterProfileRevision characterProfileRevision() const noexcept { return mCharacterProfileRevision; }
        const std::optional<ReliableCharacterProfile>& confirmedCharacterProfile() const noexcept
        { return mCharacterProfile; }
        const std::optional<LatestWinsCombatSnapshot>& confirmedCombatSnapshot() const noexcept
        { return mCombatSnapshot; }

    private:
        ClientSessionRuntime(TransportRuntime& transport, MonotonicClock& clock,
            std::unique_ptr<HeadlessClientSession> session, OutboundQueuePolicy outboundPolicy) noexcept;
        ClientRuntimeDrainResult fail(ClientRuntimeResult result) noexcept;
        ClientRuntimeQueueResult queueReliable(ReliableOperationBody body);

        TransportRuntime& mTransport;
        MonotonicClock& mClock;
        std::unique_ptr<HeadlessClientSession> mSession;
        OutboundTransportQueue mOutbound;
        std::optional<ClientHello> mClientHello;
        std::optional<AuthenticationRequest> mAuthentication;
        std::optional<ResumeToken> mResumeToken;
        std::optional<PlayerCredential> mPlayerCredential;
        bool mMayAcceptPlayerCredential = false;
        std::uint64_t mResumeLifetimeMilliseconds = 0;
        CharacterLifecycle mCharacterLifecycle = CharacterLifecycle::NewCharacter;
        CharacterProfileRevision mCharacterProfileRevision = CharacterProfileRevision::initial();
        std::optional<ReliableCharacterProfile> mCharacterProfile;
        std::optional<ReliableCharacterProfile> mPendingCharacterProfile;
        std::vector<ReliableObservationBatch> mPendingObservations;
        bool mResyncPending = false;
        bool mResyncPlayerBaselineObserved = false;
        bool mResyncActorBaselineObserved = false;
        bool mResyncObjectBaselineObserved = false;
        bool mResyncInventoryObserved = false;
        bool mResyncPlayerInventoryObserved = false;
        bool mResyncGroundItemsObserved = false;
        bool mResyncEquipmentObserved = false;
        bool mResyncCombatObserved = false;
        std::optional<CommandSequence> mLastQueuedSequence;
        std::optional<CommandSequence> mLastCharacterCommandSequence;
        ClientLocomotionHistory mLocomotionHistory;
        std::optional<LocomotionInputTick> mLastLocomotionInputTick;
        std::optional<LocomotionInputSequence> mLastLocomotionInputSequence;
        std::optional<LatestWinsCombatSnapshot> mCombatSnapshot;
    };
}

#endif
