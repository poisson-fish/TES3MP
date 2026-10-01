#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_HPP

#include "equipment_runtime.hpp"
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include "actor_spawns.hpp"
#include "actor_scene.hpp"
#include "actor_campaign.hpp"
#include "../inventory_command_binding.hpp"
#include "../inventory_interest_projection.hpp"
#include "../native_inventory_service.hpp"

namespace TES3MP::Native
{
    // Trusted server execution input, never decoded from player packets. The
    // scheduler owns castId; placement/life and source are revalidated at launch.
    struct ActorMagicCast
    {
        uint64_t actorId, life, castId;
        MagicUseSourceKind sourceKind;
        uint64_t sourceId;
        MagicUseTargetKind targetKind;
        uint64_t targetId;
    };
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
            ESM::RefId mSunRegion;
            bool mSunExposed = false;
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
        std::optional<std::array<float, 3>> mTravelDestination;
        std::optional<BoundMeleeAnimation> mBoundMelee;
        bool mMeleeContact = false;
        bool mCombatState = false;
        bool mCombatResolution = false;
        bool mNpcLifecycle = false;
        bool mMagicUse = false;
        bool mContainerMagic = false; // The 56c descriptor binds one ordinary container into the object spell image.
        bool mMagicProjectile = false;
        bool mMagicItemUse = false;
        bool mMagicTimed = false;
        bool mMagicArea = false;
        bool mMagicPlayerTarget = false;
        bool mMagicProjectileCollection = false;
        bool mKnockoutRules = false;
        bool mExpandedEffects = false; // V51 cross-actor effects and defenses.
        bool mActorPresentation = false; // V52 durable action and hit-clip identities.
        bool mClassicReflectedAbsorb = false;
        bool mKnockoutAnimation = false; // V50 retains the authored get-up tail.
        bool mMeleeDefenseRules = false;
        bool mActorEffectLifecycle = false;
        bool mConstantEffects = false;
        bool mGeneralConstants = false;
        bool mDurableCasters = false;
        bool mAutomaticNpcSpells = false;
        bool mNpcWeaponCompetition = false;
        bool mNpcFullSelection = false;
        bool mNpcCastLifecycle = false;
        bool mPlayerCastLifecycle = false;
        bool mPersistentConditions = false; // V54 source membership survives selective cures.
        bool mSpecialConditions = false; // V55 game-time Corprus and weather-sensitive effects.
        bool mMovementEffects = false; // V56 committed movement effects; inherited player movement remains client-side.
        bool mLevitationEnabled = true; // Trusted world rule; a disabled world removes Levitate effects.
        bool mScriptedMovementRules = false; // V65 persists stock Enable/DisableLevitation results.
        bool mPlayerTravel = false; // V67 persists one Mark per player and paid Recall relocation.
        bool mScriptedTravelRules = false; // V68 persists stock Enable/DisableTeleporting results.
        bool mAiDecisions = false; // V57 stock Fight/Flee selection and durable flee movement.
        bool mPlayerAi = false; // V58 content-bound player aggression and selected reach state.
        bool mSocialLifecycle = false; // V59 durable werewolf transformation and crime witnesses.
        bool mNeighborAi = false; // V60 first neighboring witness has a durable body and stock navigation.
        bool mPlacementCombat = false; // V61 bounds NPC combat slots by placement identity.
        bool mNeighborCombat = false; // V62 gives bound neighbors attack/effect/life state.
        uint8_t mNeighborLimit = 1; // V66 binds four neighboring placements to one scene and transaction.
        struct CrimeWitness
        {
            uint64_t placement = 0;
            CellId cell;
            Position3 position;
            ESM::RefId base;
        };
        std::vector<CrimeWitness> mCrimeWitnesses; // Content placements; V60 moves the first neighbor too.
        std::array<BoundCastAnimations, 2> mPlayerCasts;
        std::optional<BoundCastAnimations> mBoundCasts;
        // Combat slots follow the bounded placement domain, never inventory-owner indices.
        std::optional<std::vector<BoundHitAnimations>> mBoundHits;
        // V44: bind the selected NPC's actual weapon group and speed. The
        // Empty direction uses the descriptor clip (including the idle sentinel).
        std::function<MeleeAnimation(const ESM::Weapon*, std::string_view)> mWeaponMelee;
        std::function<MeleeAnimation(const ESM::Weapon*, std::string_view)> mNeighborWeaponMelee;
        std::vector<std::function<MeleeAnimation(const ESM::Weapon*, std::string_view)>> mNeighborMeleeSet;
        bool mGeneralAttackModes = false;
        // V46: independent player animation resources; empty for older campaigns.
        std::array<std::function<MeleeAnimation(const ESM::Weapon*, std::string_view)>, 2> mPlayerMelee;
        bool mBowRelease = false;
        bool mRangedRelease = false; // V48 extends the V47 layout to crossbows/thrown.
        bool mRangedFlight = false; // V49 persists physical flight and terminal outcomes.
        bool mAuthoritativeAim = false; // V64 binds world aim to the player swing.
        bool mOnlyAppropriateAmmunitionBypassesResistance = false;
        bool mEnchantedWeaponsAreMagical = false;
        bool mUncappedDamageFatigue = false;
        uint64_t mNpcRespawnDelayTicks = 27'000;
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
        MWScript::CompilerContext mCompilerContext;
        MWScript::ScriptManager mScriptManager;
        EquipmentRuntime mRuntime;
        std::map<std::pair<uint64_t, uint64_t>, std::string> mMagicVisualRecords;
        std::string magicVisualRecord(uint64_t sourceKind, uint64_t effectSource) const;
        EquipmentBytes mImage;
        EquipmentBytes mActorImage;
        class Transaction;
        class EquipmentTransaction;
        class WorldTransaction;
        class DoorTransaction;
        class TeleportTransaction;
        class AreaDoorTransaction;
        class ActorTransaction;
        bool validPlayerAiState(size_t index, const ActorCampaignCombat::PlayerAi& state,
            const PreparedNativeInventory* inventory = nullptr) const;
        class AttackTransaction;
        class SpellTransaction;
        std::unique_ptr<SpellTransaction> preparePlayerMagicSource(PlayerId player,
            const ClientMagicUseCommand& use, const ActorCampaignCombat& combat,
            std::span<const ActorCampaignTimedEffect> effects, const PreparedNativeInventory* inventory = nullptr);
        // Combat slots and inventory owners belong to different domains: the
        // selected NPC is combat slot 2 but may follow many shared containers.
        struct MagicCasterContext
        {
            size_t combatIndex;
            size_t inventoryOwner;
            ActorCasterIdentity identity;
        };
        MagicCasterContext magicCaster(size_t combatIndex) const;
        size_t combatOwner(size_t combatIndex) const;
        struct WeaponWear
        {
            size_t owner;
            EquipmentRuntime::EquippedWeaponCondition before;
            int condition;
            int slot = MWWorld::InventoryStore::Slot_CarriedRight;
            std::optional<float> remainder;
        };
        struct ItemCharge
        {
            size_t owner;
            ESM::RefNum item;
            float before;
            float after;
            bool consume = false;
        };
        struct ProjectileRecovery
        {
            size_t owner;
            ESM::RefId record;
            size_t sourceOwner;
            ESM::RefNum source;
        };
        uint64_t mActorTick = 0;
        std::array<float, 3> mActorVelocity{};
        std::optional<MeleeAnimation> mMelee;
        std::optional<MeleeAnimation> mIdleMelee;
        uint64_t mMeleeTarget = 0;
        bool mMeleeContacted = false;
        std::optional<ActorCampaignCombat> mCombat;
        std::optional<ActorCampaignLife> mLife;
        std::vector<ActorCampaignLife> mNeighborLives;
        std::vector<ActorCampaignProjectile> mProjectiles;
        std::vector<ActorCampaignTimedEffect> mTimedEffects;
        std::function<float(const CanonicalWorldState&, ESM::RefId)> mSunDamageScale;
        std::optional<ActorCampaignCast> mNpcCast;
        PlainEquipmentValues mRespawnInventory;
        std::vector<PlainEquipmentValues> mNeighborRespawnInventory;
        size_t mCombatNpcOwner = 0;
        EquipmentBytes sealActor(std::span<const char> core, std::span<const char> actor,
            uint64_t tick, const std::array<float, 3>& velocity,
            const std::optional<MeleeAnimation>& melee, uint64_t target, bool contact,
            const std::optional<ActorCampaignCombat>& combat,
            const std::optional<ActorCampaignLife>& life,
            std::span<const ActorCampaignLife> neighborLives,
            std::span<const ActorCampaignProjectile> projectiles,
            std::span<const ActorCampaignTimedEffect> timedEffects,
            const std::optional<ActorCampaignCast>& casting = {}) const;
        void installActorPosition() noexcept;
        CellId actorCell(const ActorSceneSnapshot& state) const;
        float meleeReach() const;
        uint64_t meleeContact(const CanonicalServerState& players, const ActorSceneSnapshot& actor,
            uint64_t requested, float reach) const;
        EquipmentBytes stagedWeaponCore(std::span<const WeaponWear> wear, const PreparedNativeInventory* command,
            std::span<const ItemCharge> charges = {},
            const EquipmentRuntime::PreparedRespawn* respawn = nullptr,
            std::span<const ProjectileRecovery> recoveries = {},
            EquipmentSessionValues* stagedValues = nullptr) const;
        PlainEquipmentValues combatEquipmentValues(size_t owner, const PreparedNativeInventory* command) const;
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
        struct ContainerLock
        {
            uint16_t level = 0;
            uint64_t revision = 1;
            friend bool operator==(const ContainerLock&, const ContainerLock&) = default;
        };
        std::vector<ContainerLock> mContainerLocks;
        EquipmentBytes mCoreImage;
        EquipmentBytes sealInventory(std::span<const char> core,
            std::span<const std::shared_ptr<const ESM::DoorState>> doors = {},
            std::span<const ContainerLock> locks = {}) const;
        void initializeAreaDoors();
        std::unique_ptr<PreparedNativeInventory> prepareAreaDoor(size_t index, bool activation,
            const CanonicalServerState& players, ServerTick tick, float seconds,
            std::unique_ptr<PreparedNativeInventory> command = {});
        std::vector<NativeDoorSnapshot> areaDoorSnapshots(CellId cell, const PreparedNativeInventory* candidate) const;
        ContainerLock areaContainerLock(size_t index, const PreparedNativeInventory* candidate) const;
        const PreparedNativeInventory* areaDoorCommand(const PreparedNativeInventory* candidate) const;
        std::vector<ActorSceneDoor> actorDoorFrames(const PreparedNativeInventory* candidate = nullptr) const;
        bool ownsAreaDoorCandidate(const PreparedNativeInventory* candidate) const;
        bool stageDoorSpell(std::unique_ptr<PreparedNativeInventory>& candidate,
            uint64_t placement, ESM::RefId effect, int magnitude);
        bool stageContainerSpell(std::unique_ptr<PreparedNativeInventory>& candidate,
            uint64_t placement, ESM::RefId effect, int magnitude);
        void recoverAreas(std::span<const std::byte> image, std::span<const ESM::RefId> references,
            std::span<const char> actor = {},
            const std::function<void(const EquipmentSessionValues&)>& validate = {});
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
        // App-local trusted gameplay state, never client-authored social values.
        struct PlayerAiUpdate
        {
            PlayerId player;
            ActorCampaignCombat::PlayerAi state;
        };
        struct FactionScriptRequest
        {
            PlayerId player;
            ESM::RefId script;
        };
        struct MovementRuleScriptRequest
        {
            ESM::RefId script;
        };
        // Trusted gameplay results. The caller has already established script or
        // crime authority; no packet decoder may construct these actions.
        struct PlayerSocialAction
        {
            enum class Kind : uint8_t
            {
                JoinFaction, SetFactionRank, SetFactionExpelled,
                ReportCrime, ClearBounty, SetCrimeDisposition,
                SetWerewolf, SetKnownWerewolf
            };
            PlayerId player;
            Kind kind;
            ESM::RefId faction;
            int value = 0; // Rank, bool, reported bounty, or stock crime disposition adjustment.
        };
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
        std::unique_ptr<PreparedNativeInventory> prepareMagicUse(
            const CanonicalServerState& players, const ServerCommandProposal& command, ServerTick tick) override;
        // Trusted object-effect entry point for the streamed ordinary door.
        // Player Touch casts stage the same change through the actor tick.
        std::unique_ptr<PreparedNativeInventory> prepareObjectMagic(
            uint64_t doorPlacement, ESM::RefId effect, int magnitude);
        bool appendMagicUse(const CanonicalServerState& players, const ServerCommandProposal& command,
            ServerTick tick, PreparedNativeInventory& candidate) override;
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
        bool hasNativeMagicUse() const noexcept override { return mBinding.mMagicUse; }
        std::optional<ServerApp::NativeTravelDiagnostics> travelDiagnostics() const override;
        size_t activeActorCollisionBodies() const
        { return mBinding.mNavigatingActor ? mBinding.mNavigatingActor->bodyCount() : 0; }
        std::optional<int> selectedNpcWeaponCondition() const
        {
            if (!mCombat) return {};
            const auto weapon = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
            return weapon ? std::optional{weapon->mCondition} : std::nullopt;
        }
        std::optional<int> selectedNpcArmorCondition(int slot) const
        {
            if (!mCombat) return {};
            const auto armor = mRuntime.equippedArmorCondition(mCombatNpcOwner, slot);
            return armor ? std::optional{armor->mCondition} : std::nullopt;
        }
        std::optional<float> selectedNpcWeaponCharge() const
        {
            if (!mCombat) return {};
            const auto weapon = mRuntime.equippedWeaponCondition(mCombatNpcOwner);
            if (!weapon) return {};
            return selectedNpcItemCharge(weapon->mItem);
        }
        std::optional<float> selectedNpcItemCharge(ESM::RefNum instance) const
        {
            if (!mCombat) return {};
            for (const auto& item : mRuntime.installedValues(mCombatNpcOwner).mObjects)
                if (item.mRef.mRefNum == instance) return item.mRef.mEnchantmentCharge;
            return {};
        }
        std::vector<ESM::RefNum> selectedNpcItemIdentities() const
        {
            std::vector<ESM::RefNum> result;
            if (!mCombat) return result;
            for (const auto& item : mRuntime.installedValues(mCombatNpcOwner).mObjects)
                result.push_back(item.mRef.mRefNum);
            return result;
        }
        std::unique_ptr<PreparedNativeInventory> prepareNativeTick(const CanonicalServerState& players,
            ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command,
            const CanonicalWorldState* world = nullptr) override;
        bool stageWaitRestRecovery(PreparedNativeInventory& candidate, const CanonicalServerState& players,
            const CanonicalWorldState& world, std::uint8_t hours, WaitRestMode mode) override;
        std::unique_ptr<PreparedNativeInventory> prepareNativeTick(const CanonicalServerState& players,
            ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command,
            std::optional<ActorMagicCast> actorCast, const CanonicalWorldState* world = nullptr,
            std::span<const PlayerAiUpdate> playerAiUpdates = {},
            std::span<const PlayerSocialAction> socialActions = {},
            std::optional<FactionScriptRequest> factionScript = {},
            std::optional<MovementRuleScriptRequest> movementRuleScript = {});
        void bindSunDamageScale(std::function<float(const CanonicalWorldState&, ESM::RefId)> callback)
        { mSunDamageScale = std::move(callback); }
        bool allowsPlayerMovement(PlayerId player) const override;
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
            std::span<const ItemCharge> charges = {},
            const ActorCampaignCombat* stagedCombat = nullptr,
            const EquipmentRuntime::PreparedRespawn* respawn = nullptr) const;
    };
}
#endif
