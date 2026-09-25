#ifndef TES3MP_NATIVE_EQUIPMENT_RUNTIME_H
#define TES3MP_NATIVE_EQUIPMENT_RUNTIME_H

#include "equipment_command.hpp"
#include "inventory_transfer_command.hpp"
#include "equipment_file.hpp"
#include "session_commit.hpp"
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

namespace TES3MP::Native
{
    using namespace MWWorld;

    // Trusted startup inputs, never command fields. Content/services must outlive
    // the nonmovable runtime. No other writer may install these actors' state.
    struct EquipmentActorBinding
    {
        ESM::RefId mBase, mShirt;
        int mCount = 0;
        bool mNpcStats = false;
        // Explicit startup mode; an empty base inventory is valid. Legacy
        // diagnostics/descriptors retain their seed and saved owner identities.
        // Fresh base inventories auto-equip; recovery always retains saved slots.
        bool mBaseInventory = false;
    };

    struct EquipmentContainerBinding
    {
        // Shared storage: container, NPC or creature. Actors require a placement.
        ESM::RefId mBase;
        std::optional<ESM::CellRef> mPlacement;
    };

    class EquipmentRuntime
    {
        friend class InventoryService; // Read-only wire projection; runtime remains the only writer.
        friend class MWWorld::Testing::PlainEquipmentFixture; // Seed/inspect/fault injection only.
        friend PersistenceResult executeEquipment(EquipmentRuntime&, EquipmentCaller, EquipmentCommand,
            EquipmentFileSink&, const EquipmentBindings&, std::unique_ptr<const EquipmentSuccess>&,
            std::vector<char>&, FileFaults&);
        struct Listener final : InventoryStoreListener, ContainerStoreListener
        {
            int mCalls = 0;
            std::vector<std::pair<ESM::RefNum, int>> mRemovals;
            void equipmentChanged() override { ++mCalls; }
            void itemAdded(const ConstPtr&, int) override { ++mCalls; }
            void itemRemoved(const ConstPtr& item, int count) override
            {
                mRemovals.emplace_back(item.getCellRef().getRefNum(), count);
                ++mCalls;
            }
        };
        const ESMStore& mStore;
        WorldModel& mWorld;
        LocalScripts& mScripts;
        const std::string mRuntime;
        const std::array<unsigned char, 32> mContent;
        std::array<std::unique_ptr<ManualRef>, 2> mActors;
        std::array<InventoryStore, 2> mInventories;
        std::array<Ptr, 2> mItems;
        // The runtime is the sole writer of each bound stock NPC stat context.
        // No NPC custom-data or global player is installed alongside it.
        std::array<std::shared_ptr<EquipmentNpcStats>, 2> mNpcStats;
        std::shared_ptr<const EquipmentScriptLocals> mScriptLocals;

        struct ActorEffects
        {
            // Stock transfer emits two updates per stack, including Take All.
            static constexpr size_t MaxPending = 2 * PreparedPlainEquipment::MaxItems;
            Listener mListener;
            size_t mInventoryUpdates = 0;
            std::vector<ESM::RefNum> mNotifications;
        };
        std::array<ActorEffects, 2> mActorEffects;
        struct SharedInventory
        {
            std::unique_ptr<ManualRef> mReference;
            std::unique_ptr<ContainerStore> mStore;
            ActorEffects mEffects;
        };
        // Stable addresses keep registry pointers and listener bindings valid.
        std::vector<std::unique_ptr<SharedInventory>> mContainers;
        std::optional<PlainEquipmentValues> mWorldItems;
        std::vector<ESM::CellRef> mPlacedItems;
        std::optional<EquipmentSessionValues::WorldCells> mWorldCells;
        EquipmentSessionValues::WorldCells mPlacedItemCells;
        size_t mCellCount = 1;
        size_t mWorldCapacity = PreparedPlainEquipment::MaxItems;
        std::optional<DoorBinding> mDoorBinding;
        std::shared_ptr<const ESM::DoorState> mDoorState;
        uint64_t mDoorMotion = 1; // Volatile: reconnect/restart establishes a new session generation.
        bool mDoorBlocked = false;
        void validateWorldItems(const EquipmentSessionValues& values) const;
        bool mFailedClosed = false;
        const bool mConnected;
        const std::shared_ptr<const void> mLifetime = std::make_shared<const char>(0);
        // Actor diagnostics recover 0 or 1; a connected session uses 2 to
        // authorize only recovery of the complete pair. Never a command field.
        // Only construction can authorize restart. Consumption closes this mode;
        // an ordinary or uncertain runtime can never opt back into it.
        std::optional<size_t> mRestartActor;

        // Private borrowed relocation data never leaves this runtime.
        // Destruction order keeps candidate nodes alive until registry/views die.
        struct Installation
        {
            PreparedPlainEquipment mPrepared;
            PtrRegistry::Index mRegistry;
            size_t mRevision = 0;
            PlainEquipmentValues mSaved;
            std::unique_ptr<const PlainEquipmentResult> mResult;
            ActorEffects mEffects;
            std::vector<ContainerStoreIterator> mSlots;
            ContainerStoreIterator mSelected;
            Ptr mItem;

            Installation(PreparedPlainEquipment prepared, ContainerStore& target)
                : mPrepared(std::move(prepared))
                , mSlots(InventoryStore::Slots, target.end())
                , mSelected(target.end())
            {
            }
        };

        struct RestartBindings
        {
            const EquipmentRuntime* mOwner;
            std::array<const InventoryStore*, 2> mStores;
            std::array<std::weak_ptr<const void>, 2> mLifetimes;
            std::array<std::shared_ptr<const void>, 2> mStorage;
            std::weak_ptr<const void> mScripts;
            PtrRegistry::Index mRegistry;
            size_t mRevision;
            ESM::RefNum mCounter, mSavedCounter;
            std::array<std::shared_ptr<const EquipmentNpcStats>, 2> mNpcStats;
            std::array<std::optional<EquipmentNpcStatsValues>, 2> mStatValues;
            std::shared_ptr<const EquipmentScriptLocals> mScriptLocals;
            std::vector<ContainerStoreResolution> mContainers;
        };

        struct RestartInstallation
        {
            // Registry/iterators die before the owned detached nodes.
            std::unique_ptr<const RestoredPlainEquipment> mRestored;
            RestartBindings mFresh;
            PtrRegistry::Index mRegistry;
            std::unique_ptr<const PlainEquipmentValues> mValues;
            std::vector<ContainerStoreIterator> mSlots;
            ContainerStoreIterator mSelected;
            Ptr mItem;
            std::shared_ptr<EquipmentNpcStats> mNpcStats;

            RestartInstallation(const RestartBindings& fresh, InventoryStore& target)
                : mFresh(fresh)
                , mSlots(InventoryStore::Slots, target.end())
                , mSelected(target.end())
            {
            }
        };

        void installInventory(size_t actor, InventoryStore& candidate, const std::vector<ContainerStoreIterator>& slots,
            ContainerStoreIterator selected, const Ptr& item, std::shared_ptr<EquipmentNpcStats>& stats, bool replaceStorage = false) noexcept;
        void installStorage(ContainerStore& live, ContainerStore& candidate, bool replaceStorage) noexcept;
        void installPrepared(size_t owner, Installation& staged, ContainerStore& candidate) noexcept;
        ContainerStore& storage(size_t owner);
        const ContainerStore& storage(size_t owner) const;
        InventoryStore* inventoryStorage(size_t owner);
        const InventoryStore* inventoryStorage(size_t owner) const;
        Ptr ownerPtr(size_t owner) const;
        ActorEffects& effects(size_t owner);
        const ActorEffects& effects(size_t owner) const;
        size_t ownerCount() const { return 2 + mContainers.size(); }
        size_t registryBound() const { return ownerCount() * (PlainEquipmentValues::MaxItems + 1); }
        PersistenceResult persistSession(EquipmentSessionValues values, EquipmentFileSink& file,
            EquipmentBytes& bytes, FileFaults& faults) const;
        void encodeSession(EquipmentSessionValues values, EquipmentBytes& bytes) const;
        void bindEffects(size_t actor);
        void initializeStartingEquipment(size_t actor);
        void validateCaller(size_t actor, const Ptr& caller) const;
        RestartBindings restartBindings(ESM::RefNum savedCounter) const;
        static bool sameReference(const ConstPtr& a, const ConstPtr& b);
        void validateRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
            const RestartBindings& fresh) const;
        std::unique_ptr<RestartInstallation> stageRestart(size_t actor, const Ptr& caller,
            const EquipmentBindings& bindings, const RestartBindings& fresh,
            std::unique_ptr<const RestoredPlainEquipment>& input);
        void installRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
            std::unique_ptr<RestartInstallation>& staged, EquipmentBytes& accepted,
            std::unique_ptr<const PlainEquipmentValues>& output, EquipmentBytes& bytes);
        FileReadResult restartEquipment(size_t actor, const Ptr& caller, const std::filesystem::path& path,
            const EquipmentBindings& bindings, const RestartBindings& fresh,
            std::unique_ptr<const PlainEquipmentValues>& output, EquipmentBytes& bytes, FileFaults& faults);
        std::unique_ptr<Installation> stageInstallation(size_t actor, const Ptr& caller, PreparedPlainEquipment input,
            size_t initiator = 0);
        PersistenceResult commitEquipment(size_t actor, const Ptr& caller, PreparedPlainEquipment input,
            EquipmentFileSink& file, const EquipmentBindings& bindings,
            std::unique_ptr<const PlainEquipmentResult>& output, EquipmentBytes& bytes, FileFaults& faults);
        PlainEquipmentValues installedValues(size_t actor) const;
        PlainEquipmentContext preparationContext(size_t owner, size_t initiator = 0) const;
        static auto cellValues(const ESM::CellRef& ref);
        static bool sameCellRef(const ESM::CellRef& a, const ESM::CellRef& b);
        static bool sameObject(const ESM::ObjectState& a, const ESM::ObjectState& b);
        static bool sameValues(const PlainEquipmentValues& a, const PlainEquipmentValues& b);
        static InventoryInstanceId ownedId(ESM::RefNum id);
        size_t validateCommand(EquipmentCaller caller, const EquipmentCommand& command) const;
        PersistenceResult execute(EquipmentCaller caller, EquipmentCommand command, EquipmentFileSink& file,
            const EquipmentBindings& bindings, std::unique_ptr<const EquipmentSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);

        EquipmentEnvelope expectedEnvelope(ESM::RefNum actor) const;

    public:
        struct EquippedWeaponCondition
        {
            ESM::RefNum mItem;
            int mCondition;
            bool operator==(const EquippedWeaponCondition&) const = default;
        };
        // Read the selected stock inventory slot and effective item health.
        // The inventory image remains the only durable writer of condition.
        std::optional<EquippedWeaponCondition> equippedWeaponCondition(size_t owner) const;
        // Called only after the enclosing actor image has been durably accepted.
        void installWeaponWear(size_t owner, ESM::RefNum item, int condition) noexcept;
        void installEnchantmentCharge(size_t owner, ESM::RefNum item, float charge) noexcept;
        void installConsumedMagicItem(size_t owner, ESM::RefNum item) noexcept;
        class PreparedRespawn
        {
            friend class EquipmentRuntime;
            struct State;
            std::unique_ptr<State> mState;
            explicit PreparedRespawn(std::unique_ptr<State> state);
        public:
            ~PreparedRespawn();
            std::span<const char> image() const;
            size_t owner() const;
            const PlainEquipmentValues& values() const;
            size_t revision() const;
        };
        // Rebuild one placed actor's starting inventory with fresh item identities.
        // The caller persists image() with the actor life and frame before install.
        std::unique_ptr<PreparedRespawn> prepareRespawn(size_t owner, const PlainEquipmentValues& baseline);
        void installRespawn(PreparedRespawn& prepared) noexcept;
        class PreparedDoor
        {
            friend class EquipmentRuntime;
            const EquipmentRuntime* mOwner;
            std::weak_ptr<const void> mLifetime;
            std::shared_ptr<const ESM::DoorState> mBefore, mAfter;
            uint64_t mRegistryRevision, mMotion;
            bool mBlocked;
            EquipmentBytes mImage;
            PreparedDoor(const EquipmentRuntime& owner, PreparedDoorChange change, bool activation, bool blocked);
        public:
            std::span<const char> image() const { return mImage; }
            const ESM::DoorState& state() const { return *mAfter; }
            uint64_t motion() const { return mMotion; }
            bool blocked() const { return mBlocked; }
        };
        PreparedDoor prepareDoor(bool activation, float seconds, bool blocked);
        PersistenceResult commit(PreparedDoor& prepared, EquipmentSessionCommitter& durability, EquipmentBytes& bytes);

        class PreparedWorldTransfer
        {
            friend class EquipmentRuntime;
            struct State;
            std::unique_ptr<State> mState;
            explicit PreparedWorldTransfer(std::unique_ptr<State> state);
        public:
            PreparedWorldTransfer(PreparedWorldTransfer&&) noexcept;
            ~PreparedWorldTransfer();
            std::span<const char> image() const;
            uint64_t revision() const;
        };
        PreparedWorldTransfer prepareWorldTransfer(size_t actor, InventoryInstanceId item, int count,
            bool pickup, ESM::Position dropPosition, uint64_t expectedRevision,
            const std::function<ESM::Position(const ESM::ObjectState&)>& placement = {}, uint8_t cell = 0);
        PersistenceResult commit(PreparedWorldTransfer& prepared, EquipmentSessionCommitter& durability,
            EquipmentBytes& bytes);
        // Detached preparation is not a published success. Content/services must
        // outlive it. No engine views escape; commit rechecks its exact runtime
        // lifetime and current state before calling the trusted durability port.
        class PreparedTransfer
        {
            friend class EquipmentRuntime;
            struct State;
            std::unique_ptr<State> mState;
            explicit PreparedTransfer(std::unique_ptr<State> state);
        public:
            PreparedTransfer(PreparedTransfer&&) noexcept;
            PreparedTransfer& operator=(PreparedTransfer&&) noexcept;
            ~PreparedTransfer();
            const InventoryTransferSuccess& candidate() const;
            std::span<const char> image() const;
        };
        PreparedTransfer prepare(InventoryTransferCaller caller, InventoryTransferCommand command,
            std::optional<size_t> newlyDeadOwner = {});
        PersistenceResult commit(PreparedTransfer& prepared, EquipmentSessionCommitter& durability,
            std::unique_ptr<const InventoryTransferSuccess>& output, EquipmentBytes& bytes);

        class PreparedEquipment
        {
            friend class EquipmentRuntime;
            struct State;
            std::unique_ptr<State> mState;
            explicit PreparedEquipment(std::unique_ptr<State> state);
        public:
            PreparedEquipment(PreparedEquipment&&) noexcept;
            PreparedEquipment& operator=(PreparedEquipment&&) noexcept;
            ~PreparedEquipment();
            const EquipmentSuccess& candidate() const;
            std::span<const char> image() const;
        };
        PreparedEquipment prepare(EquipmentCaller caller, EquipmentCommand command);
        PersistenceResult commit(PreparedEquipment& prepared, EquipmentSessionCommitter& durability,
            std::unique_ptr<const EquipmentSuccess>& output, EquipmentBytes& bytes);

        // connected fixes this owner's persistence mode for its lifetime. Both
        // transfer and equipment then require a session sink; restartActor=2 means
        // fresh coherent recovery, with commands blocked until all owners install.
        // Shared inventories include placed actors with their stock storage type.
        // They load stock loot once, in binding order. Recovery
        // constructs empty storage and retains placed reference identity/state.
        EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
            std::string runtime, std::array<unsigned char, 32> contentIdentity,
            const std::array<EquipmentActorBinding, 2>& actors,
            std::shared_ptr<const EquipmentScriptLocals> locals = {},
            MWBase::ScriptManager* declarations = nullptr,
            std::optional<size_t> restartActor = {}, bool connected = false,
            std::vector<EquipmentContainerBinding> containers = {}, int lootLevel = 1, uint32_t lootSeed = 0,
            std::optional<std::vector<ESM::CellRef>> worldItems = {}, std::optional<ESM::CellRef> door = {},
            std::optional<EquipmentSessionValues::WorldCells> cells = {}, size_t cellCount = 2,
            size_t worldCapacity = PreparedPlainEquipment::MaxItems);
        // Diagnostic convenience; delegates to the same multi-owner runtime.
        EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
            std::string runtime, std::array<unsigned char, 32> contentIdentity,
            const std::array<EquipmentActorBinding, 2>& actors,
            std::shared_ptr<const EquipmentScriptLocals> locals, MWBase::ScriptManager* declarations,
            std::optional<size_t> restartActor, bool connected, ESM::RefId container);
        EquipmentRuntime(const EquipmentRuntime&) = delete;
        EquipmentRuntime& operator=(const EquipmentRuntime&) = delete;

        // Owned intent/observation; no borrowed engine object is published.
        EquipmentCommand command(size_t actor, bool equip) const;
        PersistenceResult execute(EquipmentCaller caller, EquipmentCommand command,
            EquipmentFileSink& file, std::unique_ptr<const EquipmentSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);
        InventoryTransferCommand transferCommand(size_t source, InventoryInstanceId item, int quantity) const;
        InventoryTransferCommand containerCommand(size_t actor, bool drop, InventoryInstanceId item, int quantity,
            size_t container = 0) const;
        PersistenceResult execute(InventoryTransferCaller caller, InventoryTransferCommand command,
            EquipmentFileSink& file, std::unique_ptr<const InventoryTransferSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);
        FileReadResult restartSession(const std::filesystem::path& path, std::span<const ESM::RefId> referenceIds,
            std::unique_ptr<const EquipmentSessionValues>& output, EquipmentBytes& bytes, FileFaults& faults);
        void restoreSession(EquipmentBytes image, std::span<const ESM::RefId> referenceIds,
            std::unique_ptr<const EquipmentSessionValues>& output, EquipmentBytes& bytes);
        FileReadResult restart(size_t actor, const std::filesystem::path& path,
            std::span<const ESM::RefId> referenceIds, std::unique_ptr<const PlainEquipmentValues>& output,
            EquipmentBytes& bytes, FileFaults& faults);
    private:
        PlainEquipmentValues preparedValues(const PreparedWorldTransfer& prepared, size_t owner) const;
        std::optional<EquipmentSessionValues::WorldCells> preparedWorldCells(const PreparedWorldTransfer& prepared) const;
        const PlainEquipmentValues& worldValues(const PreparedWorldTransfer* prepared = nullptr) const;
        uint8_t worldCell(ESM::RefNum ref, const PreparedWorldTransfer* prepared = nullptr) const;
        PlainEquipmentValues preparedValues(const PreparedTransfer& prepared, size_t owner) const;
        PlainEquipmentValues preparedValues(const PreparedEquipment& prepared, size_t owner) const;
    };
}
#endif
