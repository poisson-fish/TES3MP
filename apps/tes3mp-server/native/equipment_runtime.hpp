#ifndef TES3MP_NATIVE_EQUIPMENT_RUNTIME_H
#define TES3MP_NATIVE_EQUIPMENT_RUNTIME_H

#include "equipment_command.hpp"
#include "inventory_transfer_command.hpp"
#include "equipment_file.hpp"
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
        int mCount;
        bool mNpcStats = false;
    };

    class EquipmentRuntime
    {
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
        std::unique_ptr<ManualRef> mContainer;
        ContainerStore mContainerStore;
        std::array<Ptr, 2> mItems;
        // The runtime is the sole writer of each bound stock NPC stat context.
        // No NPC custom-data or global player is installed alongside it.
        std::array<std::shared_ptr<EquipmentNpcStats>, 2> mNpcStats;
        std::shared_ptr<const EquipmentScriptLocals> mScriptLocals;

        struct ActorEffects
        {
            static constexpr size_t MaxPending = 64;
            Listener mListener;
            size_t mInventoryUpdates = 0;
            std::vector<ESM::RefNum> mNotifications;
        };
        std::array<ActorEffects, 2> mActorEffects;
        ActorEffects mContainerEffects;
        bool mFailedClosed = false;
        const bool mConnected;
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
            ContainerStoreIterator mShirt, mSelected;
            Ptr mItem;

            Installation(PreparedPlainEquipment prepared, ContainerStore& target)
                : mPrepared(std::move(prepared))
                , mShirt(target.end())
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
            std::optional<ContainerStoreResolution> mContainer;
        };

        struct RestartInstallation
        {
            // Registry/iterators die before the owned detached nodes.
            std::unique_ptr<const RestoredPlainEquipment> mRestored;
            RestartBindings mFresh;
            PtrRegistry::Index mRegistry;
            std::unique_ptr<const PlainEquipmentValues> mValues;
            ContainerStoreIterator mShirt, mSelected;
            Ptr mItem;
            std::shared_ptr<EquipmentNpcStats> mNpcStats;

            RestartInstallation(const RestartBindings& fresh, InventoryStore& target)
                : mFresh(fresh)
                , mShirt(target.end())
                , mSelected(target.end())
            {
            }
        };

        void installInventory(size_t actor, InventoryStore& candidate, ContainerStoreIterator shirt,
            ContainerStoreIterator selected, const Ptr& item, std::shared_ptr<EquipmentNpcStats>& stats, bool replaceStorage = false) noexcept;
        void installStorage(ContainerStore& live, ContainerStore& candidate, bool replaceStorage) noexcept;
        void installPrepared(size_t owner, Installation& staged, ContainerStore& candidate) noexcept;
        ContainerStore& storage(size_t owner);
        const ContainerStore& storage(size_t owner) const;
        Ptr ownerPtr(size_t owner) const;
        ActorEffects& effects(size_t owner);
        const ActorEffects& effects(size_t owner) const;
        size_t registryBound() const { return (mContainer ? 3 : 2) * (PlainEquipmentValues::MaxItems + 1); }
        PersistenceResult persistSession(EquipmentSessionValues values, EquipmentFileSink& file,
            EquipmentBytes& bytes, FileFaults& faults) const;
        void bindEffects(size_t actor);
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
        static bool sameObject(const ESM::ObjectState& a, const ESM::ObjectState& b);
        static bool sameValues(const PlainEquipmentValues& a, const PlainEquipmentValues& b);
        static InventoryInstanceId ownedId(ESM::RefNum id);
        size_t validateCommand(EquipmentCaller caller, const EquipmentCommand& command) const;
        PersistenceResult execute(EquipmentCaller caller, EquipmentCommand command, EquipmentFileSink& file,
            const EquipmentBindings& bindings, std::unique_ptr<const EquipmentSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);

        EquipmentEnvelope expectedEnvelope(ESM::RefNum actor) const;

    public:
        // connected fixes this owner's persistence mode for its lifetime. Both
        // transfer and equipment then require a session sink; restartActor=2 means
        // fresh coherent recovery, with commands blocked until all owners install.
        // A supplied container base binds one empty diagnostic ContainerStore;
        // base inventory lists, placed-reference access and scripts are not loaded.
        EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
            std::string runtime, std::array<unsigned char, 32> contentIdentity,
            const std::array<EquipmentActorBinding, 2>& actors,
            std::shared_ptr<const EquipmentScriptLocals> locals = {},
            MWBase::ScriptManager* declarations = nullptr,
            std::optional<size_t> restartActor = {}, bool connected = false, ESM::RefId container = {});
        EquipmentRuntime(const EquipmentRuntime&) = delete;
        EquipmentRuntime& operator=(const EquipmentRuntime&) = delete;

        // Owned intent/observation; no borrowed engine object is published.
        EquipmentCommand command(size_t actor, bool equip) const;
        PersistenceResult execute(EquipmentCaller caller, EquipmentCommand command,
            EquipmentFileSink& file, std::unique_ptr<const EquipmentSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);
        InventoryTransferCommand transferCommand(size_t source, InventoryInstanceId item, int quantity) const;
        InventoryTransferCommand containerCommand(size_t actor, bool drop, InventoryInstanceId item, int quantity) const;
        PersistenceResult execute(InventoryTransferCaller caller, InventoryTransferCommand command,
            EquipmentFileSink& file, std::unique_ptr<const InventoryTransferSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults);
        FileReadResult restartSession(const std::filesystem::path& path, std::span<const ESM::RefId> referenceIds,
            std::unique_ptr<const EquipmentSessionValues>& output, EquipmentBytes& bytes, FileFaults& faults);
        FileReadResult restart(size_t actor, const std::filesystem::path& path,
            std::span<const ESM::RefId> referenceIds, std::unique_ptr<const PlainEquipmentValues>& output,
            EquipmentBytes& bytes, FileFaults& faults);
    };
}
#endif
