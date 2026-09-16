#include "equipment_tests.hpp"
#include "equipment_codec.hpp"
#include "test_allocations.hpp"

#include <bit>
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
#include <components/compiler/locals.hpp>
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
                // RefData's stock copy constructor clears activation flags.
                // Keep snapshot nodes stable instead of copying on vector growth.
                result.mNodes[i].reserve(store.mLists.mClothes.mList.size());
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

        static auto cellValues(const ESM::CellRef& ref)
        {
            return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
                ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
                ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
                ref.mTrap, ref.mReferenceBlocked, ref.mPos);
        }

        static bool sameObject(const ESM::ObjectState& a, const ESM::ObjectState& b)
        {
            return cellValues(a.mRef) == cellValues(b.mRef) && a.mPosition == b.mPosition && a.mFlags == b.mFlags
                && a.mEnabled == b.mEnabled && a.mHasLocals == b.mHasLocals && a.mVersion == b.mVersion
                && a.mActorIdConverter == b.mActorIdConverter && a.mHasCustomState == b.mHasCustomState
                && a.mLocals.mVariables.empty() && b.mLocals.mVariables.empty() && a.mLuaScripts.mScripts.empty()
                && b.mLuaScripts.mScripts.empty()
                && std::equal(a.mAnimationState.mScriptedAnims.begin(), a.mAnimationState.mScriptedAnims.end(),
                    b.mAnimationState.mScriptedAnims.begin(), b.mAnimationState.mScriptedAnims.end(),
                    [](const auto& x, const auto& y) {
                        return std::tie(x.mGroup, x.mTime, x.mAbsolute, x.mLoopCount)
                            == std::tie(y.mGroup, y.mTime, y.mAbsolute, y.mLoopCount);
                    });
        }

        static bool sameValues(const PlainEquipmentValues& a, const PlainEquipmentValues& b)
        {
            return std::tie(a.mActor, a.mShirt, a.mSelected, a.mLastGenerated)
                == std::tie(b.mActor, b.mShirt, b.mSelected, b.mLastGenerated)
                && std::equal(a.mObjects.begin(), a.mObjects.end(), b.mObjects.begin(), b.mObjects.end(), sameObject);
        }

        ESM::ObjectState seedValues(size_t actor)
        {
            ESM::ObjectState object;
            object.blank();
            mItems[actor].getCellRef().writeState(object);
            auto& ref = object.mRef;
            ref.mScale = 1.25f;
            ref.mOwner = ESM::RefId::stringRefId("shirt_owner");
            ref.mGlobalVariable = "long_equipment_global_value_for_allocation";
            ref.mSoul = ESM::RefId::stringRefId("shirt_soul");
            ref.mFaction = ESM::RefId::stringRefId("shirt_faction");
            ref.mFactionRank = 4;
            ref.mChargeInt = 17;
            ref.mChargeIntRemainder = 0.375f;
            ref.mEnchantmentCharge = 9.5f;
            ref.mTeleport = true;
            ref.mDestCell = "long_equipment_destination_for_allocation";
            ref.mLockLevel = 31;
            ref.mIsLocked = true;
            ref.mKey = ESM::RefId::stringRefId("shirt_key");
            ref.mTrap = ESM::RefId::stringRefId("shirt_trap");
            ref.mReferenceBlocked = 1;
            ref.mPos = { { 1, 2, 3 }, { 4, 5, 6 } };
            ref.mDoorDest = { { 7, 8, 9 }, { 10, 11, 12 } };
            object.mPosition = { { 13, 14, 15 }, { 16, 17, 18 } };
            object.mFlags = 7;
            object.mEnabled = actor == 0;
            object.mHasCustomState = false;
            auto& animation = object.mAnimationState.mScriptedAnims.emplace_back();
            animation.mGroup = "long_equipment_animation_for_allocation";
            animation.mTime = 2.75f;
            animation.mAbsolute = true;
            animation.mLoopCount = 0x100000002ull;
            mItems[actor].getCellRef() = CellRef(ref);
            mItems[actor].getRefData() = RefData::restore(object, {}, Compiler::Locals{});
            mItems[actor].getRefData().setBaseNode(new SceneUtil::PositionAttitudeTransform);
            return object;
        }

        static void checkExport()
        {
            PlainEquipmentValues retained;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool single : { false, true })
                    for (bool equip : { true, false })
                    {
                        PlainEquipmentFixture f;
                        if (single)
                            f.mItems[actor].getCellRef().setCount(actor == 0 ? 1 : -1);
                        auto expected = f.seedValues(actor);
                        auto& inventory = f.mInventories[actor];
                        inventory.setSelectedEnchantItem(inventory.begin());
                        if (!equip)
                            inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                        f.mEvents.clear();
                        const auto before = f.snapshot();
                        auto prepared = f.prepare(actor, equip);
                        const auto result = prepared.result();
                        prepared.exportValues(f.preparationContext(actor), retained);
                        require(retained.mActor == result.mActor && retained.mShirt == result.mShirt
                                && retained.mSelected == result.mSelected
                                && retained.mLastGenerated == result.mLastGenerated
                                && retained.mObjects.size() == result.mItems.size(),
                            "equipment export lost metadata or dormant membership");
                        for (size_t i = 0; i < retained.mObjects.size(); ++i)
                        {
                            auto node = expected;
                            node.mRef.mRefNum = result.mItems[i].mIdentity;
                            node.mRef.mCount = result.mItems[i].mCount;
                            if (i != 0)
                                node.mFlags = 0; // Stock split clears activation flags only on the new stack.
                            require(sameObject(retained.mObjects[i], node), "equipment export lost full owned values");
                        }
                        require(prepared.result() == result, "equipment export changed captured effect intents");
                        f.unchanged(before);
                    }
            require(!retained.mObjects.empty() && retained.mObjects[0].mRef.mOwner == "shirt_owner"
                    && retained.mObjects[0].mAnimationState.mScriptedAnims[0].mLoopCount == 0x100000002ull,
                "equipment exported values did not survive preparation/content destruction");
            std::cout << "equipment full-value exports=8\n";
        }

        static std::vector<ESM::RefId> referenceIds(const PlainEquipmentValues& values)
        {
            std::vector<ESM::RefId> ids;
            for (const auto& object : values.mObjects)
                for (const auto& id : { object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul, object.mRef.mFaction,
                         object.mRef.mKey, object.mRef.mTrap })
                    if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end())
                        ids.push_back(id);
            return ids;
        }

        static EquipmentEnvelope envelope(ESM::RefNum actor)
        {
            return { "synthetic-equipment-runtime-1", { 1, 2, 3 }, actor };
        }

        static void checkCodecEncode()
        {
            EquipmentBytes retained;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture f;
                f.seedValues(actor);
                auto prepared = f.prepare(actor, true);
                const auto effects = prepared.result();
                PlainEquipmentValues values;
                prepared.exportValues(f.preparationContext(actor), values);
                const auto ids = referenceIds(values);
                const auto e = envelope(values.mActor);
                const EquipmentBindings bindings{ e, f.mStore, ids };
                const auto before = f.snapshot();
                encodeEquipment(values, bindings, retained);
                EquipmentBytes again;
                encodeEquipment(values, bindings, again);
                require(!retained.empty() && retained == again && retained.size() < MaxEquipmentObjectBytes,
                    "equipment encoding is empty, oversized or nondeterministic");
                const auto saved = retained;
                const auto* storage = retained.data();
                values.mActor = f.mActors[1 - actor]->getPtr().getCellRef().getRefNum();
                f.reject([&] { encodeEquipment(values, bindings, retained); }, "owner");
                require(retained == saved && retained.data() == storage && prepared.result() == effects,
                    "equipment encode rejection changed prior bytes or effects");
                f.unchanged(before);
            }
            require(!retained.empty(), "equipment bytes did not survive fixture destruction");
            std::cout << "equipment deterministic owned encodes=2\n";
        }

        static void checkRestore(bool codec = false)
        {
            size_t roundTrips = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (int variant = 0; variant < 8; ++variant)
                {
                    PlainEquipmentFixture f;
                    f.seedValues(actor);
                    auto& inventory = f.mInventories[actor];
                    const bool equip = variant < 3 || variant >= 6;
                    if (variant == 0 || variant == 3 || variant == 7)
                        f.mItems[actor].getCellRef().setCount(actor == 0 ? 1 : -1);
                    if (!equip)
                        inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                    if (variant == 5)
                        std::next(inventory.begin())->getCellRef().setSoul(ESM::RefId::stringRefId("different_soul"));
                    if (variant == 2 || variant == 6)
                    {
                        const size_t nodes = variant == 6 ? PreparedPlainEquipment::MaxItems - 1 : 1;
                        for (size_t i = 0; i < nodes; ++i)
                        {
                            auto dormant = inventory.addNewStack(f.mItems[actor], 1);
                            dormant->getCellRef().unsetRefNum();
                            f.mWorld.registerPtr(*dormant);
                            dormant->getCellRef() = dormant->getCellRef().copyWithCount(0);
                            inventory.mSelectedEnchantItem = dormant;
                        }
                    }
                    else
                        inventory.setSelectedEnchantItem(inventory.begin());
                    // Deliberately above every surviving identity; never infer it.
                    f.mWorld.setLastGeneratedRefNum({ 900, -2 });
                    if (variant == 6)
                        f.mWorld.setLastGeneratedRefNum({ std::numeric_limits<uint32_t>::max(), -1 });
                    if (variant == 7)
                        f.mWorld.setLastGeneratedRefNum(
                            { std::numeric_limits<uint32_t>::max(), std::numeric_limits<int32_t>::min() });
                    f.mEvents.clear();
                    const auto before = f.snapshot();
                    PlainEquipmentValues saved;
                    {
                        auto prepared = f.prepare(actor, equip);
                        const auto effects = prepared.result();
                        prepared.exportValues(f.preparationContext(actor), saved);
                        require(prepared.result() == effects, "equipment round trip altered prepared effects");
                    }
                    EquipmentBytes bytes;
                    if (codec)
                    {
                        // Exercise stock clamping/omissions, including fields
                        // whose door/lock switch is off and negative anim time.
                        auto& object = saved.mObjects[0];
                        object.mRef.mScale = actor == 0 ? 0.125f : 8.f;
                        object.mRef.mTeleport = false;
                        object.mRef.mIsLocked = false;
                        object.mAnimationState.mScriptedAnims[0].mTime = -3.5f;
                        const auto expected = saved;
                        const auto ids = referenceIds(saved);
                        const auto e = envelope(saved.mActor);
                        const EquipmentBindings bindings{ e, f.mStore, ids };
                        encodeEquipment(saved, bindings, bytes);
                        decodeEquipment(bytes, bindings, saved); // Replace a nonempty output.
                        require(sameValues(saved, expected), "equipment byte codec lost supported values");
                    }
                    auto restored = RestoredPlainEquipment::restore(saved, f.mStore, saved.mActor);
                    PlainEquipmentValues again;
                    auto moved = std::move(restored);
                    moved.exportValues(again);
                    require(sameValues(saved, again),
                        "equipment export/restore/export lost values, slot, selection or exact counter");
                    if (codec)
                    {
                        EquipmentBytes reencoded;
                        const auto ids = referenceIds(again);
                        const auto e = envelope(again.mActor);
                        encodeEquipment(again, { e, f.mStore, ids }, reencoded);
                        require(bytes == reencoded, "equipment restored re-encoding changed bytes");
                    }
                    if (variant == 6)
                        require(again.mObjects.size() == PlainEquipmentValues::MaxItems
                                && again.mLastGenerated == ESM::RefNum{ 0, -2 },
                            "equipment maximum split membership/counter rollover changed");
                    if (variant == 2)
                        require(again.mObjects[1].mRef.mCount == 0 && again.mSelected == again.mObjects[1].mRef.mRefNum,
                            "equipment restore lost dormant selection");
                    // The restored stock nodes own all mutable values independently.
                    saved.mObjects[0].mRef.mGlobalVariable = "changed input";
                    saved.mObjects[0].mAnimationState.mScriptedAnims.clear();
                    PlainEquipmentValues independent;
                    moved.exportValues(independent);
                    require(sameValues(again, independent), "restored equipment aliased mutable input values");
                    f.unchanged(before);
                    ++roundTrips;
                }
            std::cout << "equipment " << (codec ? "byte/detached" : "detached") << " round trips=" << roundTrips
                      << '\n';
        }

        static uint32_t numberAt(const EquipmentBytes& bytes, size_t offset)
        {
            require(offset + 4 <= bytes.size(), "test field offset is outside equipment bytes");
            uint32_t value = 0;
            for (size_t i = 0; i < 4; ++i)
                value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
            return value;
        }

        static void putNumber(EquipmentBytes& bytes, size_t offset, uint32_t value)
        {
            require(offset + 4 <= bytes.size(), "test mutation is outside equipment bytes");
            for (size_t i = 0; i < 4; ++i)
                bytes[offset + i] = static_cast<char>(value >> (8 * i));
        }

        struct ByteField
        {
            size_t mRecord, mData, mSize;
        };

        static ByteField fieldAt(const EquipmentBytes& bytes, uint32_t tag, size_t occurrence = 0)
        {
            for (size_t record = 0; record < bytes.size();)
            {
                const size_t end = record + 16 + numberAt(bytes, record + 4);
                for (size_t field = record + 16; field < end;)
                {
                    const auto size = numberAt(bytes, field + 4);
                    if (numberAt(bytes, field) == tag && occurrence-- == 0)
                        return { record, field + 8, size };
                    field += 8 + size;
                }
                record = end;
            }
            throw std::runtime_error("equipment test field missing");
        }

        static void replaceField(EquipmentBytes& bytes, uint32_t tag, std::span<const char> replacement)
        {
            const auto field = fieldAt(bytes, tag);
            const auto recordSize = numberAt(bytes, field.mRecord + 4);
            bytes.erase(bytes.begin() + field.mData, bytes.begin() + field.mData + field.mSize);
            bytes.insert(bytes.begin() + field.mData, replacement.begin(), replacement.end());
            putNumber(bytes, field.mData - 4, static_cast<uint32_t>(replacement.size()));
            putNumber(bytes, field.mRecord + 4, static_cast<uint32_t>(recordSize - field.mSize + replacement.size()));
        }

        static void checkCodecGuards()
        {
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture f;
                f.seedValues(actor);
                auto& inventory = f.mInventories[actor];
                inventory.setInvListener(&f.mListener);
                inventory.setContListener(&f.mListener);
                auto prepared = f.prepare(actor, true);
                PlainEquipmentValues saved, output;
                prepared.exportValues(f.preparationContext(actor), saved);
                f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), output);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                const EquipmentBindings bindings{ e, f.mStore, ids };
                EquipmentBytes good;
                encodeEquipment(saved, bindings, good);
                EquipmentBytes byteOutput{ 'p', 'r', 'i', 'o', 'r' };
                const auto byteValue = byteOutput;
                const auto* byteStorage = byteOutput.data();
                const auto outputValue = output;
                const auto* outputStorage = output.mObjects.data();
                auto restored = RestoredPlainEquipment::restore(output, f.mStore, output.mActor);
                const auto* restoredStorage = restored.mState.get();
                const auto effects = prepared.result();
                const auto* effectStorage = prepared.result().mEffects.data();
                const auto before = f.snapshot();
                const auto unchanged = [&] {
                    require(output.mObjects.data() == outputStorage && sameValues(output, outputValue)
                            && byteOutput.data() == byteStorage && byteOutput == byteValue
                            && restored.mState.get() == restoredStorage && prepared.result() == effects
                            && prepared.result().mEffects.data() == effectStorage,
                        "equipment codec rejection changed output storage/value or captured effects");
                    PlainEquipmentValues retained;
                    restored.exportValues(retained);
                    require(sameValues(retained, outputValue), "codec rejection changed restored nodes");
                    f.unchanged(before);
                };
                const auto rejectBytes = [&](std::span<const char> bytes, const EquipmentBindings& expected,
                                             bool preflightOnly = true) {
                    Allocations::Trace trace;
                    bool failed = false;
                    {
                        Allocations::Observe observe(trace);
                        try
                        {
                            decodeEquipment(bytes, expected, output);
                        }
                        catch (const std::exception&)
                        {
                            failed = true;
                        }
                    }
                    require(failed && trace.mOutstanding == 0, "malformed equipment bytes accepted or leaked");
                    // The sole permitted preflight allocation is the exception's
                    // diagnostic. No stream/object/string storage may be staged.
                    if (preflightOnly)
                        require(trace.mTotal <= 1, "equipment preflight allocated external data before rejection");
                    unchanged();
                    ++rejected;
                };
                for (size_t size = 0; size < good.size(); ++size)
                    rejectBytes(std::span(good).first(size), bindings);
                auto bad = good;
                bad.push_back('x');
                rejectBytes(bad, bindings);
                bad.assign(MaxEquipmentBytes + 1, 0);
                rejectBytes(bad, bindings);
                for (int test = 0; test < 40; ++test)
                {
                    bad = good;
                    const auto field = [&](const char (&tag)[5], size_t occurrence = 0) {
                        return fieldAt(bad, ESM::fourCC(tag), occurrence).mData;
                    };
                    bool preflightOnly = true;
                    switch (test)
                    {
                        case 0:
                            putNumber(bad, 4, 0xffffffff);
                            break;
                        case 1:
                            putNumber(bad, 8, 1);
                            break;
                        case 2:
                            putNumber(bad, field("FORM"), ESM::DefaultFormatVersion);
                            break;
                        case 3:
                            putNumber(bad, field("HEDR") + 8, 0xffffffff);
                            break;
                        case 4:
                            putNumber(bad, field("HEDR") + 16, 0);
                            break;
                        case 5:
                            putNumber(bad, field("FVER"), EquipmentFormatVersion + 1);
                            break;
                        case 6:
                            bad[field("RUNT")] ^= 1;
                            break;
                        case 7:
                            bad[field("CONT")] ^= 1;
                            break;
                        case 8:
                            putNumber(bad, field("ACTR"), output.mActor.mIndex);
                            break;
                        case 9:
                            putNumber(bad, field("SIZE"), PlainEquipmentValues::MaxItems + 1);
                            break;
                        case 10:
                            putNumber(bad, field("SHRT"), output.mActor.mIndex);
                            break;
                        case 11:
                            putNumber(bad, field("SELE") + 4, 0);
                            break;
                        case 12:
                            putNumber(bad, field("LGEN") + 4, 0);
                            break;
                        case 13:
                            putNumber(bad, field("LGEN"), 1);
                            break;
                        case 14:
                            putNumber(bad, field("FRMR"), saved.mActor.mIndex);
                            break;
                        case 15:
                            putNumber(bad, field("FRMR", 1), saved.mObjects[0].mRef.mRefNum.mIndex);
                            break;
                        case 16:
                            bad[field("NAME") + 1] = '!';
                            break;
                        case 17:
                            bad[field("ANAM")] = static_cast<char>(ESM::RefIdType::SizedString);
                            break;
                        case 18:
                            putNumber(bad, field("NAME") - 4, 0xffffffff);
                            break;
                        case 19:
                            bad[field("BNAM")] = 0;
                            break;
                        case 20:
                            bad[field("BNAM") + fieldAt(bad, ESM::fourCC("BNAM")).mSize - 1] = '!';
                            break;
                        case 21:
                            putNumber(bad, field("NAM9"), 0x80000000);
                            break;
                        case 22:
                            putNumber(bad, field("XSCL") - 8, ESM::fourCC("HLOC"));
                            break;
                        case 23:
                            bad[field("HCUS")] = 1;
                            break;
                        case 24:
                            bad[field("ABST")] = 2;
                            break;
                        case 25:
                            bad[field("XSAV") + 8] = 2;
                            break;
                        case 26:
                            putNumber(bad, field("COUN") - 4, 4);
                            break;
                        case 27:
                            putNumber(bad, field("XTIM") - 8, ESM::fourCC("TIME"));
                            break;
                        case 28:
                            putNumber(bad, field("DATA") - 4, 20);
                            break;
                        case 29:
                            putNumber(bad, field("XSAV"), 0x7fc00000);
                            preflightOnly = false;
                            break;
                        case 30:
                            putNumber(bad, field("FLAG"), 8);
                            preflightOnly = false;
                            break;
                        case 31:
                            putNumber(bad, field("XPOS"), 0x7fc00000);
                            preflightOnly = false;
                            break;
                        case 32:
                            putNumber(bad, field("XTIM"), 0x7fc00000);
                            preflightOnly = false;
                            break;
                        case 33:
                            putNumber(bad, field("XSCL"), std::bit_cast<uint32_t>(1.75f));
                            preflightOnly = false;
                            break;
                        case 34:
                            putNumber(bad, field("TIME"), std::bit_cast<uint32_t>(1.5f));
                            preflightOnly = false;
                            break;
                        case 35:
                            replaceField(bad, ESM::fourCC("ANIS"), std::string(PlainEquipmentValues::MaxText + 1, 'x'));
                            break;
                        case 36:
                            replaceField(bad, ESM::fourCC("XDST"), std::string(PlainEquipmentValues::MaxText + 1, 'x'));
                            break;
                        case 37:
                            replaceField(bad, ESM::fourCC("FRMR"), {});
                            break;
                        case 38:
                        {
                            const auto value = fieldAt(bad, ESM::fourCC("XSCL"));
                            const EquipmentBytes duplicate(
                                bad.begin() + value.mData - 8, bad.begin() + value.mData + value.mSize);
                            bad.insert(bad.begin() + value.mData + value.mSize, duplicate.begin(), duplicate.end());
                            putNumber(bad, value.mRecord + 4,
                                numberAt(bad, value.mRecord + 4) + static_cast<uint32_t>(duplicate.size()));
                            break;
                        }
                        case 39:
                        {
                            const auto value = fieldAt(bad, ESM::fourCC("FRMR"));
                            const auto oldSize = numberAt(bad, value.mRecord + 4);
                            bad.insert(
                                bad.begin() + value.mRecord + 16 + oldSize, MaxEquipmentObjectBytes + 1 - oldSize, 0);
                            putNumber(bad, value.mRecord + 4, MaxEquipmentObjectBytes + 1);
                            break;
                        }
                    }
                    rejectBytes(bad, bindings, preflightOnly);
                }
                for (int test = 0; test < 9; ++test)
                {
                    auto foreign = e;
                    auto foreignIds = ids;
                    switch (test)
                    {
                        case 0:
                            foreign.mRuntime += "other";
                            break;
                        case 1:
                            foreign.mContent[0] ^= 1;
                            break;
                        case 2:
                            foreign.mActor = output.mActor;
                            break;
                        case 3:
                            foreign.mRuntime.clear();
                            break;
                        case 4:
                            foreign.mRuntime.assign(129, 'x');
                            break;
                        case 5:
                            foreign.mContent.fill(0);
                            break;
                        case 6:
                            foreign.mActor = {};
                            break;
                        case 7:
                            foreignIds.clear();
                            break;
                        case 8:
                            foreignIds.resize(4097, ids[0]);
                            break;
                    }
                    rejectBytes(good, { foreign, f.mStore, foreignIds });
                }
                ESMStore foreignContent;
                rejectBytes(good, { e, foreignContent, ids });
                auto* base = const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase);
                base->mScript = ESM::RefId::stringRefId("unsupported_equipment_script");
                rejectBytes(good, bindings);
                base->mScript = {};
                for (int test = 0; test < 6; ++test)
                {
                    auto invalid = saved;
                    if (test == 0)
                        invalid.mActor = output.mActor;
                    if (test == 1)
                        invalid.mObjects.resize(PlainEquipmentValues::MaxItems + 1);
                    if (test == 2)
                        invalid.mObjects[0].mAnimationState.mScriptedAnims.resize(257);
                    if (test == 3)
                        invalid.mObjects[0].mRef.mGlobalVariable.assign(4097, 'x');
                    if (test == 4)
                        invalid.mObjects[0].mActorIdConverter = reinterpret_cast<ESM::ActorIdConverter*>(&f);
                    if (test == 5)
                        invalid.mObjects[0].mRef.mOwner = ESM::RefId::stringRefId("unbound_equipment_owner");
                    bool failed = false;
                    try
                    {
                        encodeEquipment(invalid, bindings, byteOutput);
                    }
                    catch (const std::invalid_argument&)
                    {
                        failed = true;
                    }
                    require(failed, "equipment encoder accepted invalid values");
                    unchanged();
                    ++rejected;
                }
                // Rejection cannot poison a subsequent complete owned result.
                decodeEquipment(good, bindings, output);
                require(sameValues(output, saved), "equipment codec retry lost values");
            }
            std::cout << "equipment byte rejection guards=" << rejected << '\n';
        }

        static void checkCodecBounds()
        {
            size_t cases = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture f;
                f.seedValues(actor);
                PlainEquipmentValues values;
                auto prepared = f.prepare(actor, true);
                prepared.exportValues(f.preparationContext(actor), values);
                const auto effects = prepared.result();
                const auto before = f.snapshot();
                auto& object = values.mObjects[0];
                object.mRef.mGlobalVariable.assign(PlainEquipmentValues::MaxText, 'g');
                object.mRef.mDestCell.assign(PlainEquipmentValues::MaxText, 'd');
                auto animation = object.mAnimationState.mScriptedAnims[0];
                animation.mGroup.assign(PlainEquipmentValues::MaxText, 'a');
                animation.mTime = -2;
                object.mAnimationState.mScriptedAnims.assign(PlainEquipmentValues::MaxAnimations, animation);
                // Distinct clothing base and maximum trusted reference text.
                auto shirt = *f.mItems[actor].get<ESM::Clothing>()->mBase;
                shirt.mId = ESM::RefId::stringRefId(std::string(PlainEquipmentValues::MaxText, 's'));
                f.mStore.insertStatic(shirt);
                object.mRef.mRefID = shirt.mId;
                object.mRef.mOwner = shirt.mId;
                object.mRef.mSoul = shirt.mId;
                object.mRef.mFaction = shirt.mId;
                object.mRef.mKey = shirt.mId;
                object.mRef.mTrap = shirt.mId;
                auto ids = referenceIds(values);
                const auto e = envelope(values.mActor);
                const EquipmentBindings bindings{ e, f.mStore, ids };
                EquipmentBytes bytes;
                encodeEquipment(values, bindings, bytes);
                PlainEquipmentValues decoded;
                decodeEquipment(bytes, bindings, decoded);
                require(sameValues(values, decoded), "equipment codec rejected/lost maximum supported text/animations");
                auto restored = RestoredPlainEquipment::restore(decoded, f.mStore, e.mActor);
                restored.exportValues(decoded);
                require(sameValues(values, decoded), "maximum equipment values changed on detached restore");
                ++cases;

                // External animation count is implicit in repeated stock fields.
                // Insert one complete group, updating its enclosing record size.
                auto bad = bytes;
                const auto first = fieldAt(bad, ESM::fourCC("ANIS"));
                const auto second = fieldAt(bad, ESM::fourCC("ANIS"), 1);
                const EquipmentBytes group(bad.begin() + first.mData - 8, bad.begin() + second.mData - 8);
                bad.insert(bad.begin() + first.mData - 8, group.begin(), group.end());
                putNumber(
                    bad, first.mRecord + 4, numberAt(bad, first.mRecord + 4) + static_cast<uint32_t>(group.size()));
                const auto* storage = decoded.mObjects.data();
                Allocations::Trace trace;
                bool rejected = false;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        decodeEquipment(bad, bindings, decoded);
                    }
                    catch (const std::invalid_argument&)
                    {
                        rejected = true;
                    }
                }
                require(rejected && trace.mTotal <= 1 && trace.mOutstanding == 0 && decoded.mObjects.data() == storage
                        && sameValues(values, decoded),
                    "oversized equipment animation list allocated or changed output");
                ++cases;

                // Empty equipment remains a complete actor/counter-bound state.
                values.mObjects.clear();
                values.mShirt = {};
                values.mSelected = {};
                encodeEquipment(values, bindings, bytes);
                decodeEquipment(bytes, bindings, decoded);
                require(sameValues(values, decoded), "empty equipment lost exact metadata");
                ++cases;
                require(prepared.result() == effects, "equipment bound checks changed captured effects");
                f.unchanged(before);
            }
            // Retain bytes after every source fixture and input value is gone.
            EquipmentBytes bytes;
            EquipmentEnvelope e;
            std::vector<ESM::RefId> ids;
            PlainEquipmentValues expected;
            {
                PlainEquipmentFixture source;
                source.seedValues(0);
                PlainEquipmentValues input;
                source.prepare(0, true).exportValues(source.preparationContext(0), input);
                expected = input;
                ids = referenceIds(input);
                e = envelope(input.mActor);
                encodeEquipment(input, { e, source.mStore, ids }, bytes);
            }
            PlainEquipmentFixture replacement;
            PlainEquipmentValues decoded;
            decodeEquipment(bytes, { e, replacement.mStore, ids }, decoded);
            auto restored = RestoredPlainEquipment::restore(decoded, replacement.mStore, e.mActor);
            bytes.assign(1, '!');
            decoded.mObjects.clear();
            restored.exportValues(decoded);
            require(sameValues(decoded, expected), "equipment decoded/restored values borrowed source storage");
            std::cout << "equipment codec bounds/owned lifetime cases=" << cases + 1 << '\n';
        }

        static void checkCodecAllocations()
        {
            using namespace Allocations;
            size_t failures = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    PlainEquipmentFixture f;
                    f.seedValues(actor);
                    auto& inventory = f.mInventories[actor];
                    inventory.setInvListener(&f.mListener);
                    inventory.setContListener(&f.mListener);
                    inventory.setSelectedEnchantItem(inventory.begin());
                    if (!equip)
                        inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                    f.mEvents.clear();
                    auto prepared = f.prepare(actor, equip);
                    const auto effects = prepared.result();
                    const auto* effectStorage = prepared.result().mEffects.data();
                    PlainEquipmentValues saved, output;
                    prepared.exportValues(f.preparationContext(actor), saved);
                    f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), output);
                    const auto outputValue = output;
                    const auto* outputStorage = output.mObjects.data();
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const EquipmentBindings bindings{ e, f.mStore, ids };
                    EquipmentBytes bytes;
                    encodeEquipment(saved, bindings, bytes);
                    EquipmentBytes byteOutput{ 'o', 'l', 'd' };
                    const auto byteValue = byteOutput;
                    const auto* byteStorage = byteOutput.data();
                    auto oldRestored = RestoredPlainEquipment::restore(output, f.mStore, output.mActor);
                    const auto* restoredStorage = oldRestored.mState.get();
                    const auto before = f.snapshot();
                    auto malformed = bytes;
                    putNumber(malformed, fieldAt(malformed, ESM::fourCC("SIZE")).mData, 66);
                    auto semantic = bytes;
                    putNumber(semantic, fieldAt(semantic, ESM::fourCC("FLAG")).mData, 8);
                    auto noncanonical = bytes;
                    putNumber(
                        noncanonical, fieldAt(noncanonical, ESM::fourCC("XSCL")).mData, std::bit_cast<uint32_t>(1.75f));
                    const auto chain = [&](auto& valueOutput, auto& bytesOutput, auto& restoredOutput) {
                        PlainEquipmentValues staged;
                        decodeEquipment(bytes, bindings, staged);
                        auto restored = RestoredPlainEquipment::restore(staged, f.mStore, e.mActor);
                        restored.exportValues(staged);
                        EquipmentBytes stagedBytes;
                        encodeEquipment(staged, bindings, stagedBytes);
                        // Each publication is nonthrowing after all preparation.
                        valueOutput.swap(staged);
                        bytesOutput.swap(stagedBytes);
                        restoredOutput = std::move(restored);
                    };
                    for (int operation = 0; operation < 6; ++operation)
                    {
                        const auto run = [&](auto& values, auto& encoded, auto& restored) {
                            if (operation == 0)
                                encodeEquipment(saved, bindings, encoded);
                            if (operation == 1)
                                decodeEquipment(bytes, bindings, values);
                            if (operation == 2)
                                chain(values, encoded, restored);
                            if (operation == 3)
                                decodeEquipment(malformed, bindings, values);
                            if (operation == 4)
                                decodeEquipment(semantic, bindings, values);
                            if (operation == 5)
                                decodeEquipment(noncanonical, bindings, values);
                        };
                        Trace count;
                        // Warm library internals outside observation. All RefIds
                        // already exist in caller content, including semantic IDs.
                        {
                            PlainEquipmentValues temporary;
                            EquipmentBytes encoded;
                            auto restored = RestoredPlainEquipment::restore(saved, f.mStore, e.mActor);
                            try
                            {
                                run(temporary, encoded, restored);
                            }
                            catch (const std::invalid_argument&)
                            {
                                require(operation >= 3, "unexpected codec rejection");
                            }
                        }
                        {
                            Observe observe(count);
                            PlainEquipmentValues temporary;
                            EquipmentBytes encoded;
                            std::optional<RestoredPlainEquipment> restored;
                            // optional assignment still publishes a complete owned restore.
                            try
                            {
                                run(temporary, encoded, restored);
                            }
                            catch (const std::invalid_argument&)
                            {
                                require(operation >= 3, "unexpected codec rejection");
                            }
                        }
                        if ((count.mTotal == 0 && operation != 3) || count.mOutstanding != 0
                            || count.mTrackingOverflow != 0)
                            std::cerr << "equipment codec baseline operation=" << operation
                                      << " allocations=" << count.mTotal << " outstanding=" << count.mOutstanding
                                      << " overflow=" << count.mTrackingOverflow << '\n';
                        // MSVC's exception diagnostic may use direct C allocation;
                        // this malformed preflight then makes no observed C++ allocation.
                        require((count.mTotal > 0 || operation == 3) && count.mOutstanding == 0
                                && count.mTrackingOverflow == 0,
                            "equipment codec allocation baseline leaked or missed work");
                        for (size_t fail = 1; fail <= count.mTotal; ++fail)
                        {
                            Trace trace;
                            bool failed = false;
                            {
                                Observe observe(trace, fail);
                                try
                                {
                                    run(output, byteOutput, oldRestored);
                                }
                                catch (const std::exception&)
                                {
                                    failed = true;
                                }
                            }
                            if (!failed || trace.mFailures != 1 || trace.mOutstanding != 0)
                                std::cerr << "equipment codec allocation operation=" << operation << " fail=" << fail
                                          << " rejected=" << failed << " injected=" << trace.mFailures
                                          << " outstanding=" << trace.mOutstanding << '\n';
                            require(failed && trace.mFailures == 1 && trace.mOutstanding == 0
                                    && trace.mTrackingOverflow == 0 && output.mObjects.data() == outputStorage
                                    && sameValues(output, outputValue) && byteOutput.data() == byteStorage
                                    && byteOutput == byteValue && oldRestored.mState.get() == restoredStorage
                                    && prepared.result() == effects
                                    && prepared.result().mEffects.data() == effectStorage,
                                "equipment codec allocation failure changed output storage/value or effects");
                            PlainEquipmentValues retained;
                            oldRestored.exportValues(retained);
                            require(sameValues(retained, outputValue), "codec failure altered restored values");
                            f.unchanged(before);
                            ++failures;
                        }
                    }
                    chain(output, byteOutput, oldRestored);
                    require(sameValues(output, saved) && byteOutput == bytes, "equipment codec retry changed result");
                    f.unchanged(before);
                }
            std::cout << "equipment codec allocation failures=" << failures << '\n';
        }

        static void checkValueGuards()
        {
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture f;
                f.seedValues(actor);
                auto prepared = f.prepare(actor, true);
                PlainEquipmentValues good, output;
                prepared.exportValues(f.preparationContext(actor), good);
                f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), output);
                auto restored = RestoredPlainEquipment::restore(output, f.mStore, output.mActor);
                const auto* restoredStorage = restored.mState.get();
                const auto* outputStorage = output.mObjects.data();
                const auto outputValue = output;
                const auto result = prepared.result();
                const auto* effects = prepared.result().mEffects.data();
                const auto before = f.snapshot();
                for (int test = 0; test < 40; ++test)
                {
                    auto bad = good;
                    auto& object = bad.mObjects[0];
                    auto& ref = object.mRef;
                    const auto nan = std::numeric_limits<float>::quiet_NaN();
                    switch (test)
                    {
                        case 0:
                            bad.mActor = output.mActor;
                            break;
                        case 1:
                            bad.mActor = {};
                            break;
                        case 2:
                            bad.mObjects.resize(PlainEquipmentValues::MaxItems + 1);
                            break;
                        case 3:
                            bad.mLastGenerated.mContentFile = 0;
                            break;
                        case 4:
                            bad.mLastGenerated = {};
                            break;
                        case 5:
                            --bad.mLastGenerated.mIndex;
                            break;
                        case 6:
                            ref.mRefNum = {};
                            break;
                        case 7:
                            ref.mRefNum = bad.mActor;
                            break;
                        case 8:
                            ref.mRefNum = bad.mObjects[1].mRef.mRefNum;
                            break;
                        case 9:
                            ref.mRefID = {};
                            break;
                        case 10:
                            ref.mRefID = ESM::RefId::stringRefId("missing_shirt");
                            break;
                        case 11:
                            ref.mRefID = ESM::RefId::stringRefId("equipment_actor");
                            break;
                        case 12:
                            ref.mGlobalVariable.assign(PlainEquipmentValues::MaxText + 1, 'x');
                            break;
                        case 13:
                            ref.mDestCell = std::string("bad\0cell", 8);
                            break;
                        case 14:
                            ref.mScale = nan;
                            break;
                        case 15:
                            ref.mScale = 0;
                            break;
                        case 16:
                            ref.mChargeInt = -2;
                            break;
                        case 17:
                            ref.mChargeIntRemainder = nan;
                            break;
                        case 18:
                            ref.mEnchantmentCharge = -2;
                            break;
                        case 19:
                            ref.mPos.rot[2] = nan;
                            break;
                        case 20:
                            ref.mDoorDest.pos[1] = nan;
                            break;
                        case 21:
                            ref.mCount = std::numeric_limits<int>::min();
                            break;
                        case 22:
                            ref.mCount = std::numeric_limits<int>::max();
                            break;
                        case 23:
                            ref.mCount = 0;
                            break;
                        case 24:
                            ref.mCount = -2;
                            break;
                        case 25:
                            bad.mShirt = output.mObjects[0].mRef.mRefNum;
                            break;
                        case 26:
                            bad.mSelected = output.mObjects[0].mRef.mRefNum;
                            break;
                        case 27:
                            object.mVersion = ESM::DefaultFormatVersion + 1;
                            break;
                        case 28:
                            object.mActorIdConverter = reinterpret_cast<ESM::ActorIdConverter*>(&f);
                            break;
                        case 29:
                            object.mHasCustomState = true;
                            break;
                        case 30:
                            object.mHasLocals = 1;
                            break;
                        case 31:
                            object.mLocals.mVariables.emplace_back();
                            break;
                        case 32:
                            object.mLuaScripts.mScripts.emplace_back();
                            break;
                        case 33:
                            object.mEnabled = 2;
                            break;
                        case 34:
                            object.mFlags = 8;
                            break;
                        case 35:
                            object.mPosition.rot[0] = nan;
                            break;
                        case 36:
                            object.mAnimationState.mScriptedAnims.resize(PlainEquipmentValues::MaxAnimations + 1);
                            break;
                        case 37:
                            object.mAnimationState.mScriptedAnims[0].mGroup.clear();
                            break;
                        case 38:
                            object.mAnimationState.mScriptedAnims[0].mGroup.assign(
                                PlainEquipmentValues::MaxText + 1, 'x');
                            break;
                        case 39:
                            object.mAnimationState.mScriptedAnims[0].mTime = nan;
                            break;
                    }
                    bool failed = false;
                    try
                    {
                        restored = RestoredPlainEquipment::restore(bad, f.mStore, good.mActor);
                    }
                    catch (const std::invalid_argument&)
                    {
                        failed = true;
                    }
                    if (!failed)
                        std::cerr << "accepted malformed equipment case=" << test << '\n';
                    require(failed && restored.mState.get() == restoredStorage,
                        "malformed equipment restore changed prior storage");
                    PlainEquipmentValues retained;
                    restored.exportValues(retained);
                    require(sameValues(retained, outputValue) && output.mObjects.data() == outputStorage
                            && sameValues(output, outputValue) && prepared.result() == result
                            && prepared.result().mEffects.data() == effects,
                        "malformed equipment restore changed prior values or captured effects");
                    f.unchanged(before);
                    ++rejected;
                }
                // Content constraints are checked at restore, not trusted from IDs.
                auto* base = const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase);
                for (int test = 0; test < 3; ++test)
                {
                    if (test == 0)
                        base->mScript = ESM::RefId::stringRefId("unsupported_script");
                    if (test == 1)
                        base->mEnchant = ESM::RefId::stringRefId("unsupported_enchantment");
                    if (test == 2)
                        base->mData.mType = ESM::Clothing::Robe;
                    f.reject([&] { restored = RestoredPlainEquipment::restore(good, f.mStore, good.mActor); },
                        "plain shirt content");
                    require(restored.mState.get() == restoredStorage, "bad equipment content replaced output");
                    base->mScript = {};
                    base->mEnchant = {};
                    base->mData.mType = ESM::Clothing::Shirt;
                    ++rejected;
                }
                f.reject([&] { prepared.exportValues(f.preparationContext(1 - actor), output); }, "context/service");
                require(sameValues(output, outputValue) && output.mObjects.data() == outputStorage,
                    "foreign equipment export context replaced output");
                ++rejected;

                // Invalid-input diagnostics may themselves fail to allocate.
                auto bad = good;
                bad.mSelected = output.mActor;
                Allocations::Trace trace;
                bool failed = false;
                {
                    Allocations::Observe observe(trace, 1);
                    try
                    {
                        restored = RestoredPlainEquipment::restore(bad, f.mStore, good.mActor);
                    }
                    catch (const std::bad_alloc&)
                    {
                        failed = true;
                    }
                    catch (const std::invalid_argument&)
                    {
                        failed = true;
                    }
                }
                require(failed && trace.mOutstanding == 0 && restored.mState.get() == restoredStorage,
                    "equipment restore diagnostic allocation changed output or leaked");
                PlainEquipmentValues retained;
                restored.exportValues(retained);
                require(sameValues(retained, outputValue) && prepared.result() == result,
                    "equipment restore rejection changed output or effects");
                f.unchanged(before);
                ++rejected;

                // Preparation can retain runtime-only state, but export must
                // explicitly reject it when stock semantic serializers omit it.
                f.mItems[actor].getRefData().mPhysicsPostponed = true;
                auto postponed = f.prepare(actor, true);
                const auto postponedResult = postponed.result();
                f.reject([&] { postponed.exportValues(f.preparationContext(actor), output); }, "runtime state");
                require(output.mObjects.data() == outputStorage && sameValues(output, outputValue)
                        && postponed.result() == postponedResult,
                    "unsupported equipment export changed output or captured effects");
                ++rejected;

                auto moved = std::move(prepared);
                f.reject([&] { prepared.exportValues(f.preparationContext(actor), output); }, "was moved");
                auto movedRestored = std::move(restored);
                f.reject([&] { restored.exportValues(output); }, "was moved");
                require(output.mObjects.data() == outputStorage && sameValues(output, outputValue)
                        && moved.result() == result,
                    "moved equipment export changed prior values or effects");
                rejected += 2;
            }
            std::cout << "equipment value rejection guards=" << rejected << '\n';
        }

        static void checkValueAllocations()
        {
            using namespace Allocations;
            size_t failures = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    PlainEquipmentFixture f;
                    f.seedValues(actor);
                    auto& inventory = f.mInventories[actor];
                    inventory.setInvListener(&f.mListener);
                    inventory.setContListener(&f.mListener);
                    inventory.setSelectedEnchantItem(inventory.begin());
                    if (!equip)
                        inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                    f.mEvents.clear();
                    auto prepared = f.prepare(actor, equip);
                    const auto result = prepared.result();
                    const auto* effects = prepared.result().mEffects.data();
                    const auto ctx = f.preparationContext(actor);
                    PlainEquipmentValues saved, output;
                    prepared.exportValues(ctx, saved);
                    f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), output);
                    const auto outputValue = output;
                    const auto* outputStorage = output.mObjects.data();
                    auto restored = RestoredPlainEquipment::restore(saved, f.mStore, saved.mActor);
                    auto oldRestored = RestoredPlainEquipment::restore(output, f.mStore, output.mActor);
                    const auto* oldStorage = oldRestored.mState.get();
                    const auto before = f.snapshot();
                    for (int operation = 0; operation < 3; ++operation)
                    {
                        Trace count;
                        {
                            Observe observe(count);
                            PlainEquipmentValues temporary;
                            if (operation == 0)
                                prepared.exportValues(ctx, temporary);
                            if (operation == 1)
                            {
                                auto temporaryRestore = RestoredPlainEquipment::restore(saved, f.mStore, saved.mActor);
                            }
                            if (operation == 2)
                                restored.exportValues(temporary);
                        }
                        if (count.mTotal == 0 || count.mOutstanding != 0 || count.mTrackingOverflow != 0)
                            std::cerr << "equipment value baseline operation=" << operation
                                      << " allocations=" << count.mTotal << " outstanding=" << count.mOutstanding
                                      << '\n';
                        require(count.mTotal > 0 && count.mOutstanding == 0 && count.mTrackingOverflow == 0,
                            "equipment value allocation baseline leaked or did not observe work");
                        for (size_t fail = 1; fail <= count.mTotal; ++fail)
                        {
                            Trace trace;
                            bool failed = false;
                            {
                                Observe observe(trace, fail);
                                try
                                {
                                    if (operation == 0)
                                        prepared.exportValues(ctx, output);
                                    if (operation == 1)
                                        oldRestored = RestoredPlainEquipment::restore(saved, f.mStore, saved.mActor);
                                    if (operation == 2)
                                        restored.exportValues(output);
                                }
                                catch (const std::bad_alloc&)
                                {
                                    failed = true;
                                }
                            }
                            require(failed && trace.mFailures == 1 && trace.mOutstanding == 0
                                    && trace.mTrackingOverflow == 0 && output.mObjects.data() == outputStorage
                                    && sameValues(output, outputValue) && oldRestored.mState.get() == oldStorage,
                                "equipment value allocation failure leaked or changed prior output storage/value");
                            PlainEquipmentValues retained;
                            oldRestored.exportValues(retained);
                            require(sameValues(retained, outputValue) && prepared.result() == result
                                    && prepared.result().mEffects.data() == effects,
                                "equipment value allocation failure changed restored values or captured effects");
                            f.unchanged(before);
                            ++failures;
                        }
                    }
                    PlainEquipmentValues recovered;
                    prepared.exportValues(ctx, recovered);
                    require(sameValues(recovered, saved), "equipment export retry changed values");
                    auto retry = RestoredPlainEquipment::restore(recovered, f.mStore, saved.mActor);
                    retry.exportValues(recovered);
                    require(sameValues(recovered, saved), "equipment restore retry changed values");
                    f.unchanged(before);
                }
            std::cout << "equipment value allocation failures=" << failures << '\n';
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
                    PlainEquipmentValues exported;
                    prepared.exportValues(ctx, exported);
                    const auto exportedValue = exported;
                    const auto* exportedStorage = exported.mObjects.data();
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
                    f.reject([&] { prepared.exportValues(ctx, exported); }, message);
                    require(exported.mObjects.data() == exportedStorage && sameValues(exported, exportedValue),
                        "stale equipment export changed prior output storage/value");
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
        if (filter == "inventory-equipment-codec-bounds")
        {
            PlainEquipmentFixture::checkCodecBounds();
            return;
        }
        if (filter == "inventory-equipment-codec-allocations")
        {
            PlainEquipmentFixture::checkCodecAllocations();
            return;
        }
        if (filter == "inventory-equipment-codec-guards")
        {
            PlainEquipmentFixture::checkCodecGuards();
            return;
        }
        if (filter == "inventory-equipment-codec")
        {
            PlainEquipmentFixture::checkRestore(true);
            return;
        }
        if (filter == "inventory-equipment-codec-encode")
        {
            PlainEquipmentFixture::checkCodecEncode();
            return;
        }
        if (filter == "inventory-equipment-export")
        {
            PlainEquipmentFixture::checkExport();
            return;
        }
        if (filter == "inventory-equipment-restore")
        {
            PlainEquipmentFixture::checkRestore();
            return;
        }
        if (filter == "inventory-equipment-value-guards")
        {
            PlainEquipmentFixture::checkValueGuards();
            return;
        }
        if (filter == "inventory-equipment-value-allocations")
        {
            PlainEquipmentFixture::checkValueAllocations();
            return;
        }
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
