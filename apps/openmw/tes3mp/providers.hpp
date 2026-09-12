#ifndef OPENMW_TES3MP_PROVIDERS_HPP
#define OPENMW_TES3MP_PROVIDERS_HPP

#include <tes3mp/actor_replication.hpp>
#include <tes3mp/client_locomotion.hpp>
#include <tes3mp/client_session.hpp>
#include <tes3mp/combat_replication.hpp>
#include <tes3mp/interactive_object_replication.hpp>
#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/protocol_exchange.hpp>
#include <tes3mp/protocol_pose.hpp>
#include <tes3mp/world_state.hpp>

#include <optional>
#include <span>

namespace TES3MP::OpenMWAdapter
{
    enum class ConnectionStatus
    {
        ProtocolRejected,
        ProtocolVersionMismatch,
        RequiredCapabilityMissing,
        ContentManifestMismatch,
        AuthenticationRejected,
        AuthenticationUnavailable,
        TimedOut,
        TransportFailed,
        Disconnected,
        Reconnecting,
        Resumed,
        ResumeFailed,
        ContentMappingFailed,
        PresentationFailed,
    };

    enum class ProviderResult
    {
        Accepted,
        ContentMappingFailed,
        PresentationFailed,
    };

    struct CellTransitionCapture
    {
        ProviderResult result = ProviderResult::Accepted;
        std::optional<CellTransition> transition;
    };

    struct LocalVrPose
    {
        VrTrackedTransform head;
        std::optional<VrTrackedTransform> leftHand;
        std::optional<VrTrackedTransform> rightHand;
    };

    class ConnectionStatusProvider
    {
    public:
        virtual ~ConnectionStatusProvider() = default;
        virtual void report(ConnectionStatus status) noexcept = 0;
    };

    class ConnectionControlProvider
    {
    public:
        virtual ~ConnectionControlProvider() = default;
        virtual bool disconnectRequested() noexcept = 0;
        virtual std::uint64_t reconnectDelayNanoseconds() noexcept { return 0; }
        virtual std::optional<ResyncReason> resyncRequested() noexcept { return std::nullopt; }
        virtual void resyncCompleted() noexcept {}
    };

    struct ObjectInteractionCapture
    {
        InteractiveObjectId objectId;
        CellId targetCell;
        Position3 interactionOrigin;
        ObjectRevision expectedRevision;
        ObjectInteractionKind kind = ObjectInteractionKind::Activate;
        std::optional<KeyPrototypeId> requestedKey = std::nullopt;
        std::optional<ItemStackId> requestedTool = std::nullopt;
        std::optional<InventoryRevision> expectedInventoryRevision = std::nullopt;
        std::optional<CombatRevision> expectedCombatRevision = std::nullopt;
    };

    struct InventoryTransactionCapture
    {
        InventoryTransactionKind kind = InventoryTransactionKind::TakeFromContainer;
        ItemPrototypeId prototypeId;
        std::optional<ItemStackId> stackId;
        std::uint32_t count = 1;
        InventoryRevision expectedInventoryRevision = InventoryRevision::initial();
        Position3 interactionOrigin;
        std::optional<ContainerId> containerId;
        std::optional<EquipmentSlot> slot;
        std::optional<ContainerRevision> expectedContainerRevision;
        std::optional<WorldItemRevision> expectedWorldItemRevision;
    };

    struct MeleeAttackCapture
    {
        std::optional<ActorId> target;
        ServerTick sourceTick;
        CombatRevision expectedAttackerRevision = CombatRevision::initial();
        CombatRevision expectedTargetRevision = CombatRevision::initial();
        MeleeAttackType attackType = MeleeAttackType::Chop;
        float attackStrength = 0.f;
    };

    class SemanticInputProvider
    {
    public:
        virtual ~SemanticInputProvider() = default;
        virtual CellTransitionCapture captureCellTransition() noexcept = 0;
        virtual std::optional<LocomotionIntent> sampleCurrentIntent() noexcept = 0;
        virtual std::optional<ObjectInteractionCapture> captureObjectInteraction() noexcept { return std::nullopt; }
        virtual std::optional<InventoryTransactionCapture> captureInventoryTransaction() noexcept
        {
            return std::nullopt;
        }
        virtual std::optional<MeleeAttackCapture> captureMeleeAttack() noexcept { return std::nullopt; }
        virtual std::optional<DialogueChoiceId> mapDialogueChoice(int) const noexcept { return std::nullopt; }
        virtual void clearSessionState() noexcept {}
    };

    class VrPoseInputProvider
    {
    public:
        virtual ~VrPoseInputProvider() = default;
        virtual std::optional<LocalVrPose> sampleVrPose() noexcept = 0;
    };

    class PresentationProvider
    {
    public:
        virtual ~PresentationProvider() = default;
        virtual ProviderResult applyAuthoritative(const LatestWinsSnapshot& snapshot,
            std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation = std::nullopt) noexcept = 0;
        virtual ProviderResult advance(MonotonicInstant now) noexcept = 0;
        virtual ProviderResult applyActors(
            const LatestWinsActorSnapshot&, std::span<const ActorInterestMember>, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyInteractiveObjects(
            const ReliableInteractiveObjectInterestBaseline&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyInventory(const ReliablePlayerInventoryBaseline&,
            std::span<const ReliableContainerInventoryBaseline>, const ReliableGroundItemBaseline&,
            const LatestWinsEquipmentSnapshot&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyCombat(
            const LatestWinsCombatSnapshot&, std::span<const ReliableCombatEventBatch>, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyQuestJournal(
            const QuestJournalCatalog&, const CanonicalPlayerQuestJournalState&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyWeather(
            std::span<const WeatherRegionSnapshot>, ServerTick, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyWorldTime(const ReliableWorldTimeState&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual std::optional<ObjectRevision> observedObjectRevision(InteractiveObjectId) const noexcept
        {
            return std::nullopt;
        }
        virtual ProviderResult applyVrPose(const ServerVrPoseSnapshot&, MonotonicInstant) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual ProviderResult applyVrPoseWeight(EntityId, AuthorityEpoch, double) noexcept
        {
            return ProviderResult::Accepted;
        }
        virtual void clear() noexcept = 0;
    };

    ProviderResult applyCommittedQuestJournal(PresentationProvider& presentation,
        const CanonicalWorldState& committedWorld, PlayerId player, MonotonicInstant receivedAt) noexcept;
}

#endif
