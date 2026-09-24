#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_HPP

#include "equipment_runtime.hpp"
#include "actor_spawns.hpp"
#include "actor_scene.hpp"
#include "actor_campaign.hpp"
#include "../inventory_command_binding.hpp"
#include "../inventory_interest_projection.hpp"
#include "../native_inventory_service.hpp"

namespace TES3MP::Native
{
    // Trusted composition for the loaded placed inventory domain. Bindings
    // come from server startup, never first arrival/client fields.
    // Production resolves placements from OpenMW; synthetic tests may use
    // a base-only container. Item identities derive from the loaded records.
    struct InventoryContainerBinding
    {
        ContainerId mId;
        CellId mCell;
        Position3 mPosition;
        ESM::RefId mBase;
        std::optional<ESM::CellRef> mPlacement;
    };
    struct InventoryServiceBinding
    {
        std::array<PlayerId, 2> mPlayers;
        std::optional<ItemPrototypeId> mShirt; // Legacy seed/projection override only.
        std::array<EquipmentActorBinding, 2> mActors;
        std::array<unsigned char, 32> mContent;
        std::vector<InventoryContainerBinding> mContainers;
        int mLootLevel = 1;
        uint32_t mLootSeed = 0;
        struct WorldItems
        {
            CellId mCell;
            std::vector<std::pair<uint64_t, ESM::CellRef>> mPlacements;
            // Trusted composition. The query reads the committed world and never mutates it.
            std::function<ESM::Position(const ESM::Position&, const MWWorld::Ptr&,
                const DropPlacementView&, std::span<const ESM::ObjectState>)> mPlacement;
            std::vector<NativeActorSpawn> mActorSpawns;
        };
        std::optional<WorldItems> mWorldItems;
        std::optional<ESM::CellRef> mDoor;
        uint64_t mDoorId = 0;
        // V10: two interiors; V13 also permits exterior cells. One ordinary door
        // belongs to the first cell. Budgets
        // (32 shared stores / 64 ground references) remain campaign-wide.
        std::optional<WorldItems> mSecondWorldItems;
        std::function<void(const std::array<bool, 2>&)> mCellActivity;
        struct TeleportDoor
        {
            uint64_t mId;
            CellId mCell;
            Position3 mPosition;
            Transform mDestination;
        };
        // V11: immutable, OpenMW-resolved doors, at most 32 across both cells.
        std::optional<std::vector<TeleportDoor>> mTeleportDoors;
        // V14 expands the content-bound domain. The first two fields remain
        // compatibility inputs for older descriptors; every cell uses the same
        // registry and durable image. Player and active traveler demand retain scenes.
        std::vector<WorldItems> mAdditionalWorldItems;
        struct OrdinaryDoorPlacement
        {
            uint64_t mId;
            CellId mCell;
            ESM::CellRef mRef;
        };
        std::vector<OrdinaryDoorPlacement> mDoors;
        bool mStreamExteriors = false;
        std::function<void(const std::vector<bool>&)> mAreaActivity;
        std::optional<std::vector<ActorSpawnSelection>> mActorSelections;
        std::shared_ptr<InteriorActorScene> mNavigatingActor;
        std::optional<BoundMeleeAnimation> mBoundMelee;
        bool mMeleeContact = false;
        bool mCombatState = false;
        bool mCombatResolution = false;
        float mNavigationSpeed = 120;
        // V19: one traveler pins its bounded interior independently of clients.
        bool mRetainTraveler = false;
        // V20 processing admission; never partially step an actor on saturation.
        bool mTravelerNeighborhood = false;
        size_t mTravelerCellBudget = 9;
        size_t mTravelerStepBudget = 2;
        std::function<void(bool)> mNavigationActivity;
        std::vector<const WorldItems*> worldDomains() const
        {
            std::vector<const WorldItems*> result;
            if (mWorldItems) result.push_back(&*mWorldItems);
            if (mSecondWorldItems) result.push_back(&*mSecondWorldItems);
            for (const auto& area : mAdditionalWorldItems) result.push_back(&area);
            return result;
        }
    };

    // One long-lived engine service group for all shared inventories. Loaded
    // content/readers outlive this object; registry/scripts precede the runtime
    // and die after it. There is no CanonicalInventoryWorld or shadow writer.
    class InventoryService final : public ServerApp::NativeInventoryService
    {
        const InventoryServiceBinding mBinding;
        std::map<ESM::RefId, ItemPrototypeId> mItemIds;
        MWWorld::WorldModel mWorld;
        MWWorld::LocalScripts mScripts;
        EquipmentRuntime mRuntime;
        EquipmentBytes mImage;
        EquipmentBytes mActorImage;
        class Transaction;
        class EquipmentTransaction;
        class WorldTransaction;
        class DoorTransaction;
        class TeleportTransaction;
        class AreaDoorTransaction;
        class ActorTransaction;
        class AttackTransaction;
        struct WeaponWear
        {
            size_t owner;
            EquipmentRuntime::EquippedWeaponCondition before;
            int condition;
        };
        uint64_t mActorTick = 0;
        std::array<float, 3> mActorVelocity{};
        std::optional<MeleeAnimation> mMelee;
        uint64_t mMeleeTarget = 0;
        bool mMeleeContacted = false;
        std::optional<ActorCampaignCombat> mCombat;
        size_t mCombatNpcOwner = 0;
        EquipmentBytes sealActor(std::span<const char> core, std::span<const char> actor,
            uint64_t tick, const std::array<float, 3>& velocity,
            const std::optional<MeleeAnimation>& melee, uint64_t target, bool contact,
            const std::optional<ActorCampaignCombat>& combat) const;
        void installActorPosition() noexcept;
        CellId actorCell(const ActorSceneSnapshot& state) const;
        float meleeReach() const;
        uint64_t meleeContact(const CanonicalServerState& players, const ActorSceneSnapshot& actor,
            uint64_t requested, float reach) const;
        EquipmentBytes stagedWeaponCore(std::span<const WeaponWear> wear, const PreparedNativeInventory* command) const;
        EquipmentBytes replaceAreaCore(std::span<const char> area, std::span<const char> core) const;
        ServerApp::NativeTravelDiagnostics mTravelDiagnostics;
        struct AreaDoor
        {
            DoorBinding binding;
            std::shared_ptr<const ESM::DoorState> state;
            uint64_t motion = 1;
            bool blocked = false;
            std::array<std::optional<ClientDoorObstruction>, 2> reports;
        };
        std::vector<AreaDoor> mAreaDoors;
        EquipmentBytes mCoreImage;
        EquipmentBytes sealInventory(std::span<const char> core,
            std::span<const std::shared_ptr<const ESM::DoorState>> doors = {}) const;
        void initializeAreaDoors();
        std::unique_ptr<PreparedNativeInventory> prepareAreaDoor(size_t index, bool activation,
            const CanonicalServerState& players, ServerTick tick, float seconds,
            std::unique_ptr<PreparedNativeInventory> command = {});
        std::vector<NativeDoorSnapshot> areaDoorSnapshots(CellId cell, const PreparedNativeInventory* candidate) const;
        const PreparedNativeInventory* areaDoorCommand(const PreparedNativeInventory* candidate) const;
        std::vector<ActorSceneDoor> actorDoorFrames(const PreparedNativeInventory* candidate = nullptr) const;
        bool ownsAreaDoorCandidate(const PreparedNativeInventory* candidate) const;
        void recoverAreas(std::span<const std::byte> image, std::span<const ESM::RefId> references,
            std::span<const char> actor = {});
        std::array<std::optional<ClientDoorObstruction>, 2> mDoorReports;
        float mDoorStepSeconds = 1.f / 30.f;
        std::array<bool, 2> mActiveCells{};
        std::vector<bool> mActiveAreas;
        const InventoryServiceBinding::WorldItems* worldDomain(CellId cell) const;
        uint8_t worldIndex(CellId cell) const;
        PlainEquipmentValues cellWorldValues(CellId cell,
            const EquipmentRuntime::PreparedWorldTransfer* prepared = nullptr) const;
        bool doorBlocked(const CanonicalServerState& players, ServerTick tick) const;
        size_t actor(PlayerId player) const;
        size_t container(std::optional<ContainerId> id) const;
        void validate(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& command) const;
        void retireCommittedEffects() noexcept;

    public:
        class PreparedCommand
        {
            friend class InventoryService;
            ServerApp::InventoryCommandBinding mBinding;
            EquipmentRuntime::PreparedTransfer mTransfer;
            PreparedCommand(ServerApp::InventoryCommandBinding binding, EquipmentRuntime::PreparedTransfer transfer)
                : mBinding(std::move(binding)), mTransfer(std::move(transfer)) {}
        public:
            const InventoryTransferSuccess& candidate() const { return mTransfer.candidate(); }
            std::span<const char> image() const { return mTransfer.image(); }
        };
        InventoryService(MWWorld::ESMStore& content, ESM::ReadersCache& readers,
            InventoryServiceBinding binding, bool recovering = false);
        InventoryService(const InventoryService&) = delete;
        InventoryService& operator=(const InventoryService&) = delete;
        PreparedCommand prepare(const CanonicalServerState& players, const ServerApp::InventoryCommandBinding& command);
        PersistenceResult commit(const CanonicalServerState& players, PreparedCommand& command,
            EquipmentSessionCommitter& durability, std::unique_ptr<const InventoryTransferSuccess>& success,
            EquipmentBytes& bytes);
        FileReadResult recover(const std::filesystem::path& path, std::span<const ESM::RefId> references,
            EquipmentBytes& bytes, FileFaults& faults);
        void recover(std::span<const std::byte> image, std::span<const ESM::RefId> references);
        std::unique_ptr<PreparedNativeInventory> prepareInventory(
            const CanonicalServerState& players, const ServerCommandProposal& command) override;
        std::unique_ptr<PreparedNativeInventory> prepareMeleeAttack(
            const CanonicalServerState& players, const ServerCommandProposal& command, ServerTick tick) override;
        std::span<const std::byte> inventoryImage() const noexcept override;
        void synchronizeCells(const CanonicalServerState& players) override;
        std::array<bool, 2> activeCells() const noexcept { return mActiveCells; }
        const std::vector<bool>& activeAreas() const noexcept { return mActiveAreas; }
        bool hasNativeDoor() const noexcept override { return mBinding.mDoor.has_value() || mBinding.mStreamExteriors; }
        bool ownsNativeDoor(InteractiveObjectId id) const noexcept override
        {
            if (mBinding.mDoor && id.value() == mBinding.mDoorId) return true;
            for (const auto& door : mBinding.mDoors) if (door.mId == id.value()) return true;
            if (mBinding.mTeleportDoors)
                for (const auto& door : *mBinding.mTeleportDoors)
                    if (door.mId == id.value()) return true;
            return false;
        }
        bool requiresDoorTraversal() const noexcept override { return mBinding.mTeleportDoors.has_value(); }
        bool streamsPlayerAreas() const noexcept override { return mBinding.mStreamExteriors; }
        bool hasLeveledActors() const noexcept override { return mBinding.mActorSelections.has_value(); }
        bool hasActorMotion() const noexcept override { return bool(mBinding.mNavigatingActor); }
        bool hasNativeCombat() const noexcept override { return mBinding.mCombatResolution; }
        std::optional<ServerApp::NativeTravelDiagnostics> travelDiagnostics() const override;
        size_t activeActorCollisionBodies() const
        { return mBinding.mNavigatingActor ? mBinding.mNavigatingActor->bodyCount() : 0; }
        std::optional<int> selectedNpcWeaponCondition() const
        {
            if (!mCombat) return {};
            const auto weapon = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
            return weapon ? std::optional{weapon->mCondition} : std::nullopt;
        }
        std::unique_ptr<PreparedNativeInventory> prepareNativeTick(const CanonicalServerState& players,
            ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command) override;
        std::optional<CellId> movementCell(CellId current, Position3 position) const override;
        bool allowsCellTransition(CellId current, CellId requested, Position3 position) const override;
        std::unique_ptr<PreparedNativeInventory> prepareDoorActivation(
            const CanonicalServerState& players, const ServerCommandProposal& command) override;
        std::unique_ptr<PreparedNativeInventory> prepareDoorStep(
            const CanonicalServerState& players, ServerTick tick, float seconds) override;
        void reportDoorObstruction(const CanonicalServerState& players,
            const ClientDoorObstruction& report, ServerTick tick) override;
        std::optional<ServerApp::InventoryInterestDelivery> projectInventory(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate = nullptr) const override;
        std::optional<LatestWinsCombatSnapshot> projectCombat(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate = nullptr) const override;
        std::optional<ReliableCombatEventBatch> projectCombatEvents(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate) const override;
        // Direct projection to the existing owned wire values. No retained or
        // writable mirror. Candidate baselines stay staged until durable commit.
        std::optional<ServerApp::InventoryInterestDelivery> project(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedCommand* candidate = nullptr,
            const EquipmentRuntime::PreparedEquipment* equipment = nullptr,
            const EquipmentRuntime::PreparedWorldTransfer* world = nullptr,
            const EquipmentRuntime::PreparedDoor* door = nullptr, std::optional<CellId> area = {},
            const ActorSceneSnapshot* moving = nullptr, std::span<const WeaponWear> wear = {},
            const ActorCampaignCombat* stagedCombat = nullptr) const;
    };
}
#endif
