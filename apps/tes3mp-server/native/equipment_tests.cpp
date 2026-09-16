#include "equipment_tests.hpp"
#include "test_allocations.hpp"

#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/customdata.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/plainequipment.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

namespace MWWorld::Testing
{
    namespace
    {
        void require(bool condition, const char* message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }
    }

    // Test-owned actors and true stock InventoryStores; no NPC custom-data or
    // Environment installation. Private access only constructs/corrupts fixtures.
    class PlainEquipmentFixture
    {
    public:
        struct Listener : InventoryStoreListener, ContainerStoreListener
        {
            int mCalls = 0;
            void equipmentChanged() override { ++mCalls; }
            void itemAdded(const ConstPtr&, int) override { ++mCalls; }
            void itemRemoved(const ConstPtr&, int) override { ++mCalls; }
        };
        ESMStore mStore;
        ESM::ReadersCache mReaders;
        WorldModel mWorld{ mStore, mReaders, 1 };
        LocalScripts mScripts{ mStore };
        std::array<std::unique_ptr<ManualRef>, 2> mActors;
        std::array<InventoryStore, 2> mInventories;
        std::array<Ptr, 2> mItems;
        std::vector<std::string> mEvents;
        Listener mListener;

        PlainEquipmentFixture()
        {
            MWClass::registerClasses();
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("equipment_actor");
            mStore.insertStatic(npc);
            ESM::Clothing shirt;
            shirt.blank();
            shirt.mId = ESM::RefId::stringRefId("equipment_shirt");
            shirt.mData.mType = ESM::Clothing::Shirt;
            mStore.insertStatic(shirt);
            for (size_t i = 0; i < 2; ++i)
            {
                mActors[i] = std::make_unique<ManualRef>(mStore, npc.mId);
                const Ptr actor = mActors[i]->getPtr();
                mWorld.registerPtr(actor);
                auto& inventory = mInventories[i];
                inventory.setPtr(actor, mWorld);
                Misc::Rng::Generator rng{ 0 };
                inventory.fill({}, {}, rng);
                ManualRef item(mStore, shirt.mId);
                mItems[i] = *inventory.addNewStack(item.getPtr(), i == 0 ? 3 : -5);
                mWorld.registerPtr(mItems[i]);
            }
        }

        InventoryStoreEquipmentContext context(size_t actor, size_t player)
        {
            return { { mStore,
                         [this](const Ptr& item) {
                             mWorld.registerPtr(item);
                             mEvents.emplace_back("register");
                         },
                         [this](const Ptr& item, int count) {
                             item.getCellRef().setCount(
                                 ContainerStore::subtractItems(item.getCellRef().getCount(false), count), mScripts);
                             mEvents.emplace_back("remove");
                         },
                         [this](const Ptr& item) {
                             item.getCellRef().setCount(0, mScripts);
                             mEvents.emplace_back("cleanup");
                         } },
                mActors[actor]->getPtr(), mActors[player]->getPtr(),
                [](const Ptr&, const ESM::RefId&) { throw std::runtime_error("unexpected script effect"); },
                [this, actor](const Ptr& owner) {
                    require(owner == mActors[actor]->getPtr(), "wrong equipment effect owner");
                    mEvents.emplace_back("equipment");
                } };
        }

        void checkSeam()
        {
            for (size_t i = 0; i < 2; ++i)
            {
                auto& inventory = mInventories[i];
                const auto ctx = context(i, i);
                const auto otherCount = mItems[1 - i].getCellRef().getCount(false);
                const auto item = inventory.begin();
                mEvents.clear();
                inventory.equip(InventoryStore::Slot_Shirt, item, ctx);
                require(inventory.getSlot(InventoryStore::Slot_Shirt) == item
                        && item->getCellRef().getCount(false) == (i == 0 ? 1 : -1)
                        && mItems[1 - i].getCellRef().getCount(false) == otherCount
                        && mEvents == std::vector<std::string>{ "register", "remove", "equipment" },
                    "shared equip split/effect isolation mismatch");
                mEvents.clear();
                const auto restacked = inventory.unequipSlot(InventoryStore::Slot_Shirt, ctx);
                require(inventory.getSlot(InventoryStore::Slot_Shirt) == inventory.end()
                        && restacked->getCellRef().getCount(false) == (i == 0 ? 3 : -5)
                        && item->getCellRef().getCount(false) == 0
                        && mItems[1 - i].getCellRef().getCount(false) == otherCount
                        && mEvents == std::vector<std::string>{ "cleanup", "equipment" },
                    "shared unequip restack/effect isolation mismatch");
            }
        }

        PlainEquipmentContext preparationContext(size_t actor) const
        {
            return { mStore, mWorld, mScripts, mActors[actor]->getPtr(), mActors[actor]->getPtr() };
        }

        PreparedPlainEquipment prepare(size_t actor, bool equip)
        {
            return PreparedPlainEquipment::prepare(
                ContainerStoreResolution(mInventories[actor], mActors[actor]->getPtr()), mItems[actor],
                mItems[actor].getCellRef().getRefNum(), mWorld.getPtrRegistryRevision(), equip,
                preparationContext(actor));
        }

        struct Snapshot
        {
            struct Node
            {
                const LiveCellRefBase* mAddress;
                std::string mCell;
                RefData mData;
                const SceneUtil::PositionAttitudeTransform* mScene;
            };
            PtrRegistry::Snapshot mRegistry;
            LocalScripts::List mScripts;
            std::array<std::vector<Node>, 2> mNodes;
            std::array<std::vector<ContainerStoreIterator>, 2> mSlots;
            std::array<std::tuple<float, unsigned int, bool, bool, bool, bool, bool, bool>, 2> mMetadata;
            std::vector<ContainerStoreIterator> mSelections;
            std::vector<std::string> mEvents;
            int mListenerCalls;
        };

        static std::string cellBytes(const CellRef& ref)
        {
            ESM::ObjectState state;
            ref.writeState(state);
            std::ostringstream out(std::ios::binary);
            ESM::ESMWriter writer;
            writer.save(out);
            writer.startRecord(ESM::REC_CLOT);
            state.mRef.save(writer, true, true);
            writer.endRecord(ESM::REC_CLOT);
            writer.close();
            out << ref.hasChanged() << ':' << ref.getChargeIntRemainder();
            return out.str();
        }

        Snapshot snapshot() const
        {
            Snapshot result;
            result.mRegistry = mWorld.snapshotPtrRegistry();
            result.mScripts = mScripts.snapshot();
            result.mEvents = mEvents;
            result.mListenerCalls = mListener.mCalls;
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& store = mInventories[i];
                result.mSlots[i] = store.mSlots;
                result.mMetadata[i] = { store.mCachedWeight, store.mSeed, store.mWeightUpToDate, store.mModified,
                    store.mResolved, store.mRechargingItemsUpToDate, store.mUpdatesEnabled, store.mFirstAutoEquip };
                result.mSelections.push_back(store.mSelectedEnchantItem);
                for (const auto& node : store.mLists.mClothes.mList)
                    result.mNodes[i].push_back({ &node, cellBytes(node.mRef), node.mData.copyForContainerTransfer(),
                        node.mData.getBaseNode() });
            }
            return result;
        }

        void unchanged(const Snapshot& before) const
        {
            require(mWorld.snapshotPtrRegistry() == before.mRegistry && mScripts.snapshot() == before.mScripts
                    && mEvents == before.mEvents && mListener.mCalls == before.mListenerCalls,
                "equipment rejection/preparation changed registry, exact counters, services or live effects");
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& store = mInventories[i];
                require(store.mSlots == before.mSlots[i] && store.mSelectedEnchantItem == before.mSelections[i]
                        && store.mLists.mClothes.mList.size() == before.mNodes[i].size()
                        && std::tuple{ store.mCachedWeight, store.mSeed, store.mWeightUpToDate, store.mModified,
                               store.mResolved, store.mRechargingItemsUpToDate, store.mUpdatesEnabled,
                               store.mFirstAutoEquip }
                            == before.mMetadata[i],
                    "equipment rejection/preparation changed slots, selection or storage");
                size_t index = 0;
                for (const auto& node : store.mLists.mClothes.mList)
                {
                    const auto& saved = before.mNodes[i][index++];
                    require(&node == saved.mAddress && cellBytes(node.mRef) == saved.mCell
                            && node.mData.matchesContainerTransferState(saved.mData)
                            && node.mData.getBaseNode() == saved.mScene,
                        "equipment rejection/preparation changed node lifetime, values or scene binding");
                }
            }
        }

        template <class F>
        void reject(F&& operation, std::string_view diagnostic)
        {
            const auto before = snapshot();
            bool rejected = false;
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                rejected = std::string_view(error.what()).find(diagnostic) != std::string_view::npos;
                if (!rejected)
                    std::cerr << "unexpected rejection: " << error.what() << '\n';
            }
            require(rejected, "equipment invalid operation did not reject with expected diagnostic");
            unchanged(before);
        }

        static void checkGuards()
        {
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                for (int test = 0; test < 25; ++test)
                {
                    PlainEquipmentFixture f;
                    const auto ctx = f.preparationContext(actor);
                    const auto& store = f.mInventories[actor];
                    const ContainerStoreResolution resolution(store, ctx.mActor);
                    const auto identity = f.mItems[actor].getCellRef().getRefNum();
                    auto output = f.prepare(1 - actor, true);
                    const auto* storage = &output.result();
                    const auto value = output.result();
                    auto requestItem = ConstPtr(f.mItems[actor]);
                    auto requestContext = ctx;
                    auto expected = identity;
                    size_t revision = f.mWorld.getPtrRegistryRevision();
                    std::string_view message;
                    switch (test)
                    {
                        case 0:
                            requestItem = f.mItems[1 - actor];
                            message = "owner/identity";
                            break;
                        case 1:
                            requestItem.mContainerStore = &f.mInventories[1 - actor];
                            message = "registry";
                            break;
                        case 2:
                            expected = {};
                            message = "owner/identity";
                            break;
                        case 3:
                            --revision;
                            message = "revision";
                            break;
                        case 4:
                            requestContext.mPlayer = f.mActors[1 - actor]->getPtr();
                            message = "actor/player";
                            break;
                        case 5:
                            requestContext.mActor = f.mActors[1 - actor]->getPtr();
                            message = "actor/player";
                            break;
                        case 6:
                            f.mItems[actor].getCellRef() = f.mItems[actor].getCellRef().copyWithCount(0);
                            message = "stale, dormant";
                            break;
                        case 7:
                            f.mItems[actor].getCellRef().setCount(std::numeric_limits<int>::min());
                            message = "plain non-scripted";
                            break;
                        case 8:
                            const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase)->mEnchant
                                = ESM::RefId::stringRefId("unsupported_enchantment");
                            message = "plain non-scripted";
                            break;
                        case 9:
                            const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase)->mScript
                                = ESM::RefId::stringRefId("unsupported_script");
                            message = "plain non-scripted";
                            break;
                        case 10:
                            const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase)->mData.mType
                                = ESM::Clothing::Robe;
                            message = "plain non-scripted";
                            break;
                        case 11:
                            f.mItems[actor].getRefData().setDeletedByContentFile(true);
                            message = "plain non-scripted";
                            break;
                        case 12:
                            f.mInventories[actor].mSlots[InventoryStore::Slot_Shirt]
                                = f.mInventories[1 - actor].begin();
                            message = "iterator";
                            break;
                        case 13:
                            f.mInventories[actor].mSelectedEnchantItem = f.mInventories[1 - actor].end();
                            message = "iterator";
                            break;
                        case 14:
                            f.mInventories[actor].mSlots[InventoryStore::Slot_Robe] = f.mInventories[actor].begin();
                            message = "only the shirt";
                            break;
                        case 15:
                            f.mInventories[actor].mUpdatesEnabled = false;
                            message = "updates";
                            break;
                        case 16:
                            f.mInventories[actor].mResolved = false;
                            message = "resolved inventory";
                            break;
                        case 17:
                            f.mInventories[actor].mLists.mClothes.mList.erase(
                                f.mInventories[actor].mLists.mClothes.mList.begin());
                            revision = f.mWorld.getPtrRegistryRevision();
                            message = "reference lifetime";
                            break;
                        case 18:
                            f.mItems[actor].getCellRef().setRefNum(f.mItems[1 - actor].getCellRef().getRefNum());
                            message = "registry";
                            break;
                        case 19:
                            f.mWorld.setLastGeneratedRefNum(
                                { std::numeric_limits<uint32_t>::max(), std::numeric_limits<int32_t>::min() });
                            message = "counter exhausted";
                            break;
                        case 20:
                            f.mWorld.setLastGeneratedRefNum({ identity.mIndex - 1, identity.mContentFile });
                            message = "identity collision";
                            break;
                        case 21:
                        {
                            auto& inventory = f.mInventories[actor];
                            while (inventory.mLists.mClothes.mList.size() <= PreparedPlainEquipment::MaxItems)
                            {
                                const Ptr extra = *inventory.addNewStack(f.mItems[actor], 1);
                                extra.getCellRef().unsetRefNum();
                                f.mWorld.registerPtr(extra);
                            }
                            revision = f.mWorld.getPtrRegistryRevision();
                            message = "bounded plain clothing";
                            break;
                        }
                        case 22:
                        {
                            auto& inventory = f.mInventories[actor];
                            const Ptr extra = *inventory.addNewStack(f.mItems[actor], std::numeric_limits<int>::max());
                            extra.getCellRef().unsetRefNum();
                            f.mWorld.registerPtr(extra);
                            revision = f.mWorld.getPtrRegistryRevision();
                            message = "count bound";
                            break;
                        }
                        case 23:
                            f.mItems[actor].getRefData().getLocals().mShorts.push_back(1);
                            message = "plain non-scripted";
                            break;
                        case 24:
                            f.mInventories[actor].mSlots[InventoryStore::Slot_Shirt] = f.mInventories[actor].begin();
                            message = "must contain one";
                            break;
                    }
                    f.reject(
                        [&] {
                            output = PreparedPlainEquipment::prepare(
                                resolution, requestItem, expected, revision, true, requestContext);
                        },
                        message);
                    require(&output.result() == storage && output.result() == value,
                        "equipment rejection replaced prior owned output storage/value");
                    ++rejected;
                }
                for (int test = 0; test < 12; ++test)
                {
                    PlainEquipmentFixture f;
                    const auto ctx = f.preparationContext(actor);
                    auto prepared = f.prepare(actor, true);
                    const auto value = prepared.result();
                    auto& store = f.mInventories[actor];
                    std::string_view message;
                    switch (test)
                    {
                        case 0:
                            f.mItems[actor].getCellRef().setCount(7);
                            message = "values";
                            break;
                        case 1:
                            f.mItems[actor].getCellRef().setSoul(ESM::RefId::stringRefId("changed_soul"));
                            message = "values";
                            break;
                        case 2:
                            f.mItems[actor].getRefData().disable();
                            message = "values";
                            break;
                        case 3:
                            store.mSlots[InventoryStore::Slot_Shirt] = f.mInventories[1 - actor].begin();
                            message = "iterator";
                            break;
                        case 4:
                            store.mSelectedEnchantItem = f.mInventories[1 - actor].end();
                            message = "iterator";
                            break;
                        case 5:
                            store.setInvListener(&f.mListener);
                            message = "source state";
                            break;
                        case 6:
                            store.setContListener(&f.mListener);
                            message = "source state";
                            break;
                        case 7:
                            store = InventoryStore();
                            message = "storage";
                            break;
                        case 8:
                            f.mActors[actor].reset();
                            message = "reference lifetime";
                            break;
                        case 9:
                            store.mLists.mClothes.mList.clear();
                            message = "source state";
                            break;
                        case 10:
                        {
                            auto& node = store.mLists.mClothes.mList.front();
                            const auto base = node.mBase;
                            const auto ref = node.mRef;
                            std::destroy_at(&node);
                            std::construct_at(&node, ESM::makeBlankCellRef(), base);
                            node.mRef = ref;
                            Ptr replacement(&node, nullptr);
                            replacement.mContainerStore = &store;
                            f.mWorld.registerPtr(replacement);
                            message = "source state";
                            break;
                        }
                        case 11:
                            f.mWorld.setLastGeneratedRefNum({ 99, -3 });
                            message = "source state";
                            break;
                    }
                    f.reject([&] { prepared.validate(ctx); }, message);
                    require(prepared.result() == value, "stale validation changed owned equipment result");
                    ++rejected;
                }
                PlainEquipmentFixture f;
                const auto ctx = f.preparationContext(actor);
                std::optional<InventoryStore> temporary(std::in_place);
                const ContainerStoreResolution dead(*temporary, ctx.mActor);
                temporary.reset();
                temporary.emplace(); // Same address, different lifetime.
                f.reject(
                    [&] {
                        PreparedPlainEquipment::prepare(dead, f.mItems[actor], f.mItems[actor].getCellRef().getRefNum(),
                            f.mWorld.getPtrRegistryRevision(), true, ctx);
                    },
                    "inventory lifetime");
                std::optional<LocalScripts> scripts(std::in_place, f.mStore);
                const PlainEquipmentContext serviceContext{ f.mStore, f.mWorld, *scripts, ctx.mActor, ctx.mPlayer };
                auto prepared = PreparedPlainEquipment::prepare(
                    ContainerStoreResolution(f.mInventories[actor], ctx.mActor), f.mItems[actor],
                    f.mItems[actor].getCellRef().getRefNum(), f.mWorld.getPtrRegistryRevision(), true, serviceContext);
                scripts.reset();
                scripts.emplace(f.mStore);
                f.reject([&] { prepared.validate(serviceContext); }, "context/service");
                rejected += 2;

                struct CloneTrap : CustomData
                {
                    int& mCalls;
                    explicit CloneTrap(int& calls)
                        : mCalls(calls)
                    {
                    }
                    std::unique_ptr<CustomData> clone() const override
                    {
                        ++mCalls;
                        throw std::runtime_error("live custom data clone invoked");
                    }
                };
                int cloneCalls = 0;
                auto& itemData = f.mItems[actor].getRefData();
                // Set/clear once to establish stock change tracking before snapshot.
                itemData.setCustomData(nullptr);
                const auto beforeCustom = f.snapshot();
                itemData.setCustomData(std::make_unique<CloneTrap>(cloneCalls));
                auto* custom = itemData.getCustomData();
                bool customRejected = false;
                try
                {
                    f.prepare(actor, true);
                }
                catch (const std::invalid_argument&)
                {
                    customRejected = true;
                }
                require(customRejected && cloneCalls == 0 && itemData.getCustomData() == custom,
                    "equipment preparation cloned or mutated unsupported live custom state");
                itemData.setCustomData(nullptr);
                f.unchanged(beforeCustom);
                ++rejected;

                auto oldOutput = f.prepare(actor, true);
                const auto* oldStorage = &oldOutput.result();
                const auto oldValue = oldOutput.result();
                const auto diagnosticBefore = f.snapshot();
                Allocations::Trace diagnosticTrace;
                bool allocationRejected = false;
                {
                    Allocations::Observe observe(diagnosticTrace, 1);
                    try
                    {
                        oldOutput = PreparedPlainEquipment::prepare(
                            ContainerStoreResolution(f.mInventories[actor], ctx.mActor), f.mItems[actor], {},
                            f.mWorld.getPtrRegistryRevision(), true, ctx);
                    }
                    catch (const std::bad_alloc&)
                    {
                        allocationRejected = true;
                    }
                    catch (const std::invalid_argument&)
                    {
                        // MSVC may allocate exception diagnostics outside the
                        // overridden C++ allocator. Rejection must still be atomic.
                        allocationRejected = true;
                    }
                }
                require(allocationRejected && diagnosticTrace.mFailures <= 1 && diagnosticTrace.mOutstanding == 0
                        && &oldOutput.result() == oldStorage && oldOutput.result() == oldValue,
                    "equipment rejection diagnostic allocation changed output or leaked");
                f.unchanged(diagnosticBefore);
                ++rejected;
            }
            std::cout << "equipment rejection guards=" << rejected << '\n';
        }

        static void checkAllocations()
        {
            using namespace Allocations;
            size_t failures = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    PlainEquipmentFixture f;
                    if (!equip)
                    {
                        f.mInventories[actor].equip(
                            InventoryStore::Slot_Shirt, f.mInventories[actor].begin(), f.context(actor, actor));
                        f.mEvents.clear();
                    }
                    f.mInventories[actor].setInvListener(&f.mListener);
                    f.mInventories[actor].setContListener(&f.mListener);
                    const auto ctx = f.preparationContext(actor);
                    const ContainerStoreResolution resolution(f.mInventories[actor], ctx.mActor);
                    const auto identity = f.mItems[actor].getCellRef().getRefNum();
                    const auto revision = f.mWorld.getPtrRegistryRevision();
                    auto output = f.prepare(1 - actor, true);
                    const auto* outputStorage = &output.result();
                    const auto outputValue = output.result();
                    const auto before = f.snapshot();
                    auto operation = [&] {
                        return PreparedPlainEquipment::prepare(
                            resolution, f.mItems[actor], identity, revision, equip, ctx);
                    };
                    Trace baseline;
                    {
                        Observe observe(baseline);
                        InPhase phase(Phase::Preparation);
                        auto result = operation();
                        result.validate(ctx);
                    }
                    require(baseline.mTotal > 0 && baseline.mOutstanding == 0 && baseline.mTrackingOverflow == 0,
                        "equipment allocation baseline leaked or did not observe work");
                    f.unchanged(before);
                    // Include preparation, final witness validation and result copy.
                    Trace count;
                    {
                        Observe observe(count);
                        auto prepared = operation();
                        auto owned = prepared.result();
                    }
                    for (size_t fail = 1; fail <= count.mTotal; ++fail)
                    {
                        Trace trace;
                        bool rejected = false;
                        {
                            Observe observe(trace, fail);
                            try
                            {
                                auto prepared = operation();
                                auto owned = prepared.result();
                                output = std::move(prepared);
                            }
                            catch (const std::bad_alloc&)
                            {
                                rejected = true;
                            }
                        }
                        require(
                            rejected && trace.mFailures == 1 && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                            "equipment failed preparation/result allocation leaked or escaped rejection");
                        require(&output.result() == outputStorage && output.result() == outputValue,
                            "equipment failed allocation changed prior output storage/value");
                        f.unchanged(before);
                        ++failures;
                    }
                    auto prepared = operation();
                    Trace validationCount;
                    {
                        Observe observe(validationCount);
                        prepared.validate(ctx);
                    }
                    for (size_t fail = 1; fail <= validationCount.mTotal; ++fail)
                    {
                        Trace trace;
                        bool rejected = false;
                        {
                            Observe observe(trace, fail);
                            try
                            {
                                prepared.validate(ctx);
                            }
                            catch (const std::bad_alloc&)
                            {
                                rejected = true;
                            }
                        }
                        require(rejected && trace.mFailures == 1 && trace.mOutstanding == 0,
                            "equipment failed validation allocation leaked or escaped rejection");
                        f.unchanged(before);
                        ++failures;
                    }
                    prepared.validate(ctx);
                    const auto recovered = operation().result();
                    require(recovered == prepared.result(), "equipment fresh recovery changed deterministic proposal");
                }
            std::cout << "equipment allocation failures=" << failures << '\n';
        }

        void checkPreparation()
        {
            using Kind = PlainEquipmentResult::EffectKind;
            for (size_t i = 0; i < 2; ++i)
            {
                const auto registry = mWorld.snapshotPtrRegistry();
                const auto scripts = mScripts.snapshot();
                const auto count = mItems[i].getCellRef().getCount(false);
                auto prepared = prepare(i, true);
                prepared.validate(preparationContext(i));
                const auto result = prepared.result();
                require(result.mActor == mActors[i]->getPtr().getCellRef().getRefNum()
                        && result.mShirt == mItems[i].getCellRef().getRefNum() && result.mItems.size() == 2
                        && result.mItems[0].mCount == (count > 0 ? 1 : -1)
                        && result.mItems[1].mCount == ContainerStore::subtractItems(count, 1)
                        && result.mEffects.size() == 4 && result.mEffects[0].mKind == Kind::RegisterSplit
                        && result.mEffects[1].mKind == Kind::InventoryUpdated
                        && result.mEffects[2].mKind == Kind::InventoryUpdated
                        && result.mEffects[3].mKind == Kind::EquipmentChanged
                        && mWorld.snapshotPtrRegistry() == registry && mScripts.snapshot() == scripts
                        && mItems[i].getCellRef().getCount(false) == count && mEvents.empty(),
                    "equipment preparation changed live state or lost owned result/effects");
                auto moved = std::move(prepared);
                moved.validate(preparationContext(i));
                require(moved.result() == result, "equipment preparation move lost values");

                // Separate stock mutation on the disposable fixture, never installing
                // the prepared object. Compare its resulting split identities/counts.
                auto& inventory = mInventories[i];
                inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), context(i, i));
                require(std::next(inventory.begin())->getCellRef().getRefNum() == result.mItems[1].mIdentity
                        && mWorld.getLastGeneratedRefNum() == result.mLastGenerated,
                    "equipment proposal differs from stock identity generation");
                mEvents.clear();
                const auto equippedRegistry = mWorld.snapshotPtrRegistry();
                auto unequip = prepare(i, false);
                unequip.validate(preparationContext(i));
                require(!unequip.result().mShirt.isSet() && unequip.result().mItems.size() == 2
                        && unequip.result().mItems[0].mCount == 0 && unequip.result().mItems[1].mCount == count
                        && unequip.result().mEffects.size() == 2
                        && unequip.result().mEffects[0].mKind == Kind::DeleteStackScript
                        && unequip.result().mEffects[1].mKind == Kind::EquipmentChanged
                        && inventory.getSlot(InventoryStore::Slot_Shirt)->getCellRef().getCount() == 1
                        && mWorld.snapshotPtrRegistry() == equippedRegistry && mEvents.empty(),
                    "equipment unequip preparation changed live state or lost restack intents");
            }
            for (size_t actor = 0; actor < 2; ++actor)
            {
                for (bool single : { true, false })
                {
                    PlainEquipmentFixture f;
                    auto& store = f.mInventories[actor];
                    if (single)
                        f.mItems[actor].getCellRef().setCount(actor == 0 ? 1 : -1);
                    f.mItems[actor].getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
                    f.mItems[actor].getRefData().activate();
                    store.setInvListener(&f.mListener);
                    store.setContListener(&f.mListener);
                    store.setSelectedEnchantItem(store.begin());
                    const auto before = f.snapshot();
                    const auto equipped = f.prepare(actor, true);
                    f.unchanged(before);
                    require(equipped.result().mSelected == f.mItems[actor].getCellRef().getRefNum()
                            && equipped.result().mItems.size() == (single ? 1 : 2)
                            && equipped.result().mEffects.size() == (single ? 1 : 5),
                        "equipment single/split or retained selection/effect intent mismatch");
                    store.equip(InventoryStore::Slot_Shirt, store.begin(), f.context(actor, actor));
                    f.mEvents.clear();
                    if (!single)
                        std::next(store.begin())->getCellRef().setSoul(ESM::RefId::stringRefId("incompatible_soul"));
                    const auto unequipBefore = f.snapshot();
                    auto unequipped = f.prepare(actor, false);
                    require(!unequipped.result().mShirt.isSet() && !unequipped.result().mSelected.isSet()
                            && unequipped.result().mItems[0].mCount == (actor == 0 ? 1 : -1)
                            && unequipped.result().mEffects.size() == 1,
                        "equipment no-restack selection cleanup/effects mismatch");
                    f.unchanged(unequipBefore);
                    f.reject([&] { f.prepare(actor, true); }, "already in requested state");
                }
                // Replace a different shirt through the same stock slot mechanics.
                PlainEquipmentFixture f;
                auto& store = f.mInventories[actor];
                store.equip(InventoryStore::Slot_Shirt, store.begin(), f.context(actor, actor));
                auto shirt = *f.mItems[actor].get<ESM::Clothing>()->mBase;
                shirt.mId = ESM::RefId::stringRefId("replacement_shirt");
                f.mStore.insertStatic(shirt);
                ManualRef replacement(f.mStore, shirt.mId);
                auto item = store.addNewStack(replacement.getPtr(), actor == 0 ? 2 : -2);
                f.mWorld.registerPtr(*item);
                f.mItems[actor] = *item;
                f.mEvents.clear();
                const auto before = f.snapshot();
                auto prepared = f.prepare(actor, true);
                require(prepared.result().mShirt == item->getCellRef().getRefNum()
                        && prepared.result().mItems.size() == 4 && prepared.result().mEffects.size() == 6
                        && prepared.result().mItems[0].mCount == 0
                        && prepared.result().mItems[1].mCount == (actor == 0 ? 3 : -5),
                    "equipment replacement did not restack old shirt then split/equip new shirt");
                f.unchanged(before);
            }
            PlainEquipmentResult retained;
            {
                PlainEquipmentFixture f;
                retained = f.prepare(0, true).result();
            }
            require(retained.mItems.size() == 2 && retained.mEffects.size() == 4 && retained.mShirt.isSet(),
                "owned equipment result did not survive fixture/preparation destruction");
        }
    };

    void checkPlainEquipment(std::string_view filter)
    {
        if (filter == "inventory-equipment-guards")
        {
            PlainEquipmentFixture::checkGuards();
            return;
        }
        if (filter == "inventory-equipment-allocations")
        {
            PlainEquipmentFixture::checkAllocations();
            return;
        }
        PlainEquipmentFixture fixture;
        if (filter == "inventory-equipment-seam")
            fixture.checkSeam();
        else if (filter == "inventory-equipment-preparation")
            fixture.checkPreparation();
        else
            throw std::runtime_error("unknown equipment filter");
        std::cout << "two-actor stock clothing split/restack and explicit effects verified\n";
    }
}
