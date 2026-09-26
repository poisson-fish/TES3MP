#include "equipment_runtime.hpp"
#include "actor_inventory.hpp"
#include "runtime_phases.hpp"
#include <components/esm3/loadweap.hpp>
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwclass/armor.hpp>
#include <apps/openmw/mwclass/clothing.hpp>
#include <apps/openmw/mwclass/creature.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryitem.hpp>
#include <components/esm3/loadnpc.hpp>
#include <limits>
#include <set>
#include <stdexcept>

namespace TES3MP::Native
{
    std::optional<EquipmentRuntime::EquippedWeaponCondition> EquipmentRuntime::equippedWeaponCondition(size_t owner) const
    {
        const auto* equipped = inventoryStorage(owner);
        if (!equipped) return {};
        const auto selected = equipped->mSlots[InventoryStore::Slot_CarriedRight];
        if (selected == equipped->end()) return {};
        const Ptr item = *selected;
        if (item.getType() != ESM::Weapon::sRecordId) return {};
        const auto maximum = mStore.get<ESM::Weapon>().find(item.getCellRef().getRefId())->mData.mHealth;
        const int condition = item.getClass().hasItemHealth(item) ? item.getClass().getItemHealth(item) : 0;
        if (condition < 0 || condition > maximum)
            throw std::invalid_argument("Native equipped weapon condition invalid");
        return EquippedWeaponCondition{item.getCellRef().getRefNum(), condition};
    }
    std::optional<EquipmentRuntime::EquippedWeaponCondition> EquipmentRuntime::equippedArmorCondition(size_t owner, int slot) const
    {
        if (slot < 0 || slot >= InventoryStore::Slots) return {};
        const auto* equipped = inventoryStorage(owner);
        if (!equipped) return {};
        const auto selected = equipped->mSlots[slot];
        if (selected == equipped->end()) return {};
        const Ptr item = *selected;
        if (item.getType() != ESM::Armor::sRecordId) return {};
        const auto maximum = mStore.get<ESM::Armor>().find(item.getCellRef().getRefId())->mData.mHealth;
        const int condition = item.getClass().hasItemHealth(item) ? item.getClass().getItemHealth(item) : 0;
        if (condition < 0 || condition > maximum)
            throw std::invalid_argument("Native equipped armor condition invalid");
        return EquippedWeaponCondition{item.getCellRef().getRefNum(), condition};
    }
    void EquipmentRuntime::installWeaponWear(size_t owner, ESM::RefNum item, int condition) noexcept
    {
        auto* inventory = inventoryStorage(owner);
        auto& slot = inventory->mSlots[InventoryStore::Slot_CarriedRight];
        Ptr weapon = *slot;
        if (weapon.getCellRef().getRefNum() != item) std::terminate();
        weapon.getCellRef().setCharge(condition);
        if (condition == 0) slot = inventory->end();
        ++mWorld.mPtrRegistry.mRevision;
    }
    void EquipmentRuntime::installArmorWear(size_t owner, int index, ESM::RefNum item, int condition) noexcept
    {
        auto* inventory = inventoryStorage(owner);
        auto& slot = inventory->mSlots[index];
        Ptr armor = *slot;
        if (armor.getCellRef().getRefNum() != item) std::terminate();
        armor.getCellRef().setCharge(condition);
        if (condition == 0) slot = inventory->end();
        ++mWorld.mPtrRegistry.mRevision;
    }
    void EquipmentRuntime::installEnchantmentCharge(size_t owner, ESM::RefNum item, float charge) noexcept
    {
        Ptr source = mWorld.getPtr(item);
        if (!source.hasLiveReference() || source.mContainerStore != &storage(owner)
            || source.getCellRef().getRefNum() != item) std::terminate();
        source.getCellRef().setEnchantmentCharge(charge);
        ++mWorld.mPtrRegistry.mRevision;
    }
    void EquipmentRuntime::installConsumedMagicItem(size_t owner, ESM::RefNum item) noexcept
    {
        Ptr source = mWorld.getPtr(item);
        if (!source.hasLiveReference() || source.mContainerStore != &storage(owner)
            || source.getCellRef().getRefNum() != item || source.getCellRef().getCount() <= 0)
            std::terminate();
        const int remaining = source.getCellRef().getCount() - 1;
        source.getCellRef().setCount(remaining, mScripts);
        storage(owner).flagAsModified();
        if (remaining == 0)
        {
            if (auto* inventory = inventoryStorage(owner))
                for (auto& slot : inventory->mSlots)
                    if (slot != inventory->end() && (*slot).getCellRef().getRefNum() == item)
                        slot = inventory->end();
        }
        ++mWorld.mPtrRegistry.mRevision;
    }
    EquipmentRuntime::EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
        std::string runtime, std::array<unsigned char, 32> contentIdentity,
        const std::array<EquipmentActorBinding, 2>& actors,
        std::shared_ptr<const EquipmentScriptLocals> locals, MWBase::ScriptManager* declarations,
        std::optional<size_t> restartActor, bool connected, std::vector<EquipmentContainerBinding> containers,
        int lootLevel, uint32_t lootSeed, std::optional<std::vector<ESM::CellRef>> worldItems, std::optional<ESM::CellRef> door,
        std::optional<EquipmentSessionValues::WorldCells> cells, size_t cellCount, size_t worldCapacity,
        bool constantEffects)
        : mStore(content), mWorld(world), mScripts(scripts), mRuntime(std::move(runtime)), mContent(contentIdentity)
        , mScriptLocals(std::move(locals)), mConnected(connected), mConstantEffects(constantEffects)
    {
        if (&world.mStore != &content || !scripts.usesStore(content) || mRuntime.empty()
            || mRuntime.size() > 128 || mRuntime.find('\0') != std::string::npos
            || std::none_of(mContent.begin(), mContent.end(), [](auto byte) { return byte != 0; })
            || (restartActor && (connected ? *restartActor != 2 : *restartActor >= 2)))
            throw std::invalid_argument("Equipment runtime content/service/startup binding mismatch");
        if (connected && (!world.mPtrRegistry.mIndex.empty() || !scripts.snapshot().mEntries.empty()))
            throw std::invalid_argument("Connected runtime requires exclusive fresh registry and script services");
        MWClass::registerClasses();
        // Validate all trusted startup records before constructing registered nodes.
        for (const auto& actor : actors)
        {
            const auto* npc = content.get<ESM::NPC>().find(actor.mBase);
            if (actor.mBaseInventory)
            {
                if (!connected || !actor.mShirt.empty() || actor.mCount != 0 || !npc->mScript.empty())
                    throw std::invalid_argument("Base actor inventory requires connected mode, no seed and no actor script");
                continue;
            }
            const auto* shirt = content.get<ESM::Clothing>().find(actor.mShirt);
            if (shirt->mData.mType != ESM::Clothing::Shirt || actor.mCount == 0
                || actor.mCount == std::numeric_limits<int>::min())
                throw std::invalid_argument("Equipment runtime requires a nonempty shirt stack");
            if (!shirt->mScript.empty())
            {
                if (!mScriptLocals || !declarations || !actor.mNpcStats || std::abs(actor.mCount) != 1)
                    throw std::invalid_argument("Equipment runtime scripted startup services missing");
                mScriptLocals->declarations(content, shirt->mScript);
            }
        }
        if (containers.size() > MaxEquipmentContainers)
            throw std::invalid_argument("Shared container budget exceeded");
        if (cells)
        {
            if (!worldItems || !cellCount || cellCount > MaxEquipmentCells || cells->size() != worldItems->size())
                throw std::invalid_argument("Native runtime requires bounded complete world membership");
            for (const auto& ref : *worldItems)
            {
                const auto found = cells->find(ref.mRefNum);
                if (found == cells->end() || found->second >= cellCount)
                    throw std::invalid_argument("Invalid placed cell membership");
            }
            mCellCount = cellCount;
            mPlacedItemCells = *cells;
            mWorldCells = restartActor ? EquipmentSessionValues::WorldCells{} : std::move(*cells);
        }
        std::set<ESM::RefNum> placements;
        if (door)
        {
            if (!connected || !worldItems || !content.getLuaScriptsCfg().mScripts.empty())
                throw std::invalid_argument("Native door requires a connected world domain without Lua services");
            mDoorBinding.emplace(*content.get<ESM::Door>().find(door->mRefID), *door);
            placements.insert(door->mRefNum);
            if (!restartActor) mDoorState = std::make_shared<const ESM::DoorState>(mDoorBinding->door().initialState());
        }
        for (const auto& binding : containers)
        {
            const auto& placement = binding.mPlacement;
            if (placement && (!placement->mRefNum.hasContentFile() || placement->mRefID != binding.mBase
                || placement->mIsLocked || !placement->mTrap.empty()
                || !placements.insert(placement->mRefNum).second))
                throw std::invalid_argument("Placed container runtime binding invalid or duplicate");
            ManualRef reference(content, binding.mBase);
            const auto ptr = reference.getPtr();
            if (!connected || (!actorInventory(ptr) && ptr.getType() != ESM::Container::sRecordId)
                || !ptr.getClass().getScript(ptr).empty() || (actorInventory(ptr) && !placement))
                throw std::invalid_argument("Shared inventory requires an unscripted container or placed actor");
        }
        // Bind lazy service identity during trusted startup, never during a
        // rejectable command/recovery allocation observation.
        scripts.lifetimeWitness();
        for (size_t i = 0; i < actors.size(); ++i)
        {
            mActors[i] = std::make_unique<ManualRef>(content, actors[i].mBase);
            const Ptr actor = mActors[i]->getPtr();
            world.registerPtr(actor);
            auto& inventory = mInventories[i];
            inventory.setPtr(actor, world);
            Misc::Rng::Generator rng{ 0 };
            inventory.fill({}, {}, rng);
            if (!actors[i].mBaseInventory)
            {
                ManualRef item(content, actors[i].mShirt);
                mItems[i] = *inventory.addNewStack(item.getPtr(), actors[i].mCount);
                world.registerPtr(mItems[i]);
                const auto script = content.get<ESM::Clothing>().find(actors[i].mShirt)->mScript;
                if (!script.empty())
                    mItems[i].getRefData().setLocals(*content.get<ESM::Script>().find(script), *declarations);
            }
            ContainerStoreResolution witness(inventory, actor);
            if (actors[i].mNpcStats)
                mNpcStats[i] = std::make_shared<EquipmentNpcStats>(actor, content);
            bindEffects(i);
        }
        for (const auto& binding : containers)
        {
            auto& shared = *mContainers.emplace_back(std::make_unique<SharedInventory>());
            shared.mReference = std::make_unique<ManualRef>(content, binding.mBase);
            if (binding.mPlacement)
            {
                // Detached engine reference; discovery never initializes its inventory.
                shared.mReference->getPtr().getCellRef() = CellRef(*binding.mPlacement);
                shared.mReference->getPtr().getRefData().setPosition(binding.mPlacement->mPos);
            }
            world.registerPtr(shared.mReference->getPtr());
            const auto ptr = shared.mReference->getPtr();
            shared.mStore = ptr.getClass().hasInventoryStore(ptr)
                ? std::make_unique<InventoryStore>() : std::make_unique<ContainerStore>();
            shared.mStore->setPtr(ptr, world);
        }
        // Bind every owner before loot consumes dynamic IDs, so even base-only
        // diagnostic owners have identical identities during empty recovery.
        Misc::Rng::Generator rng{ lootSeed };
        // All owner identities exist before variable-size character loot. A
        // restart must bind exactly the same owners without generating items.
        for (size_t i = 0; i < actors.size(); ++i)
            if (actors[i].mBaseInventory && !restartActor)
            {
                auto& inventory = mInventories[i];
                inventory.fill(content.get<ESM::NPC>().find(actors[i].mBase)->mInventory, actors[i].mBase, rng,
                    {content, world, lootLevel});
                initializeStartingEquipment(i);
                if (inventory.begin() != inventory.end()) mItems[i] = *inventory.begin();
            }
        for (size_t i = 0; i < containers.size(); ++i)
        {
            const auto& binding = containers[i];
            auto& shared = *mContainers[i];
            const auto ptr = shared.mReference->getPtr();
            auto& store = *shared.mStore;
            if (restartActor)
                store.fill({}, {}, rng);
            else
            {
                const auto& items = ptr.getType() == ESM::NPC::sRecordId ? ptr.get<ESM::NPC>()->mBase->mInventory
                    : ptr.getType() == ESM::Creature::sRecordId ? ptr.get<ESM::Creature>()->mBase->mInventory
                    : ptr.get<ESM::Container>()->mBase->mInventory;
                store.fill(items, actorInventory(ptr) ? binding.mBase : ESM::RefId{}, rng,
                    {content, world, lootLevel});
                if (inventoryStorage(i + 2)) initializeStartingEquipment(i + 2);
            }
            ContainerStoreResolution witness(store, ptr);
            store.setContListener(&shared.mEffects.mListener);
            if (auto* inventory = inventoryStorage(i + 2)) inventory->setInvListener(&shared.mEffects.mListener);
        }
        if (worldItems)
        {
            if (!worldCapacity || worldCapacity > PlainEquipmentValues::MaxWorldItems)
                throw std::invalid_argument("Native world storage capacity invalid");
            mWorldCapacity = worldCapacity;
            if (!connected || worldItems->size() > mWorldCapacity)
                throw std::invalid_argument("World item domain exceeds its bound");
            mPlacedItems = std::move(*worldItems);
            mWorldItems.emplace();
            mWorldItems->mActor = ownerPtr(0).getCellRef().getRefNum();
            mWorldItems->mLastGenerated = world.getLastGeneratedRefNum();
            for (const auto& placed : mPlacedItems)
            {
                if (!placed.mRefNum.hasContentFile() || !placements.insert(placed.mRefNum).second
                    || placed.mCount <= 0 || !inventoryItemRecord(content, placed.mRefID).mScript.empty())
                    throw std::invalid_argument("World item placement invalid or scripted");
                ESM::ObjectState object; object.blank(); object.mRef = placed;
                object.mPosition = placed.mPos;
                object.mHasCustomState = false;
                if (!restartActor) mWorldItems->mObjects.push_back(std::move(object));
            }
            mWorldItems->validate(content, mWorldItems->mActor, nullptr, mWorldCapacity);
        }
        if (restartActor)
        {
            for (size_t i = 0; i < 2; ++i)
                if (connected || i == *restartActor)
                {
                    mItems[i] = Ptr();
                    mInventories[i].mLists.mClothes.mList.clear();
                }
            mRestartActor = restartActor;
        }
    }

    void EquipmentRuntime::initializeStartingEquipment(size_t index)
    {
        // Constructor-only unpublished storage: failure rejects the whole host.
        // Stock NPC startup selects against NPDT skills before spell activation.
        // This temporary stat context is not a second gameplay-state writer.
        const auto actor = ownerPtr(index);
        const bool isNpc = actor.getType() == ESM::NPC::sRecordId;
        auto& inventory = *inventoryStorage(index);
        MWMechanics::NpcStats stats(mStore);
        if (isNpc)
        {
            const auto& npc = *actor.get<ESM::NPC>()->mBase;
            if (npc.mNpdtType == ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS) stats.initializeAutoSkills(npc, mStore);
            else stats.initializeExplicitSkills(npc);
        }
        const auto skill = [&](ESM::RefId id) {
            return isNpc ? stats.getSkill(id).getModified()
                : static_cast<const MWClass::Creature&>(actor.getClass()).getSkill(actor, id, mStore);
        };
        const ContainerStoreStackContext split{mStore,
            [&](const Ptr& item) { mWorld.registerPtr(item); },
            [&](const Ptr& item, int count) {
                const auto removal = ContainerStore::prepareRemoveCount(item.getCellRef(), count);
                if (removal.mFullRemoval) throw std::logic_error("Starting equipment split removed its source");
                item.getCellRef() = item.getCellRef().copyWithCount(removal.mRemainingCount);
                inventory.flagAsModified();
            }, {}};
        inventory.autoEquip({mStore, isNpc, isNpc ? 0 : actor.get<ESM::Creature>()->mBase->mAiData.mServices, skill,
            [&](const ConstPtr& item) {
                const auto& armor = static_cast<const MWClass::Armor&>(item.getClass());
                return armor.getSkillAdjustedArmorRating(item, skill(armor.getEquipmentSkill(item, mStore)), mStore);
            },
            [&](const ConstPtr& item) {
                if (item.getType() == ESM::Armor::sRecordId)
                    return static_cast<const MWClass::Armor&>(item.getClass()).canBeEquipped(item, actor, mStore, inventory).first != 0;
                if (item.getType() == ESM::Clothing::sRecordId)
                    return static_cast<const MWClass::Clothing&>(item.getClass()).canBeEquipped(item, actor, mStore).first != 0;
                return item.getClass().canBeEquipped(item, actor).first != 0;
            },
            [&](const Ptr& item) {
                if (inventory.storedSize() >= PreparedPlainEquipment::MaxItems)
                    throw std::invalid_argument("Starting equipment split exceeds inventory node budget");
                inventory.unstack(item, 1, split);
            },
            [&] {
                // Auto-selection doesn't activate scripts or constant effects.
                // A strike-only weapon has no equip-time effect; its charge and
                // effects are handled at confirmed combat contact.
                for (const auto& slot : inventory.mSlots)
                    if (slot != inventory.end())
                    {
                        const auto record = inventoryItemRecord(mStore, slot->getCellRef().getRefId());
                        const bool supportedConstant = mConstantEffects && record.mConstant;
                        if (!record.mStrikeOnly && !supportedConstant
                            && (!record.mScript.empty() || !record.mEnchant.empty()))
                            throw std::invalid_argument("Starting equipment needs unavailable script/enchantment services");
                    }
                effects(index).mListener.equipmentChanged();
            }});
    }

    EquipmentRuntime::EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
        std::string runtime, std::array<unsigned char, 32> contentIdentity,
        const std::array<EquipmentActorBinding, 2>& actors,
        std::shared_ptr<const EquipmentScriptLocals> locals, MWBase::ScriptManager* declarations,
        std::optional<size_t> restartActor, bool connected, ESM::RefId container)
        : EquipmentRuntime(content, world, scripts, std::move(runtime), contentIdentity, actors,
            std::move(locals), declarations, restartActor, connected, container.empty()
                ? std::vector<EquipmentContainerBinding>{} : std::vector<EquipmentContainerBinding>{{container, {}}}) {}

    EquipmentEnvelope EquipmentRuntime::expectedEnvelope(ESM::RefNum actor) const
    {
        return { mRuntime, mContent, actor };
    }

    EquipmentCommand EquipmentRuntime::command(size_t actor, bool equip) const
    {
        const auto& inventory = mInventories.at(actor);
        const auto item = equip ? inventory.begin()
            : ConstContainerStoreIterator(inventory.mSlots[InventoryStore::Slot_Shirt]);
        if (item == inventory.end())
            throw std::invalid_argument("Equipment command requires a current active shirt");
        return { ownedId(mActors[actor]->getPtr().getCellRef().getRefNum()),
            ownedId(item->getCellRef().getRefNum()), mWorld.getPtrRegistryRevision(),
            equip ? EquipmentRequestedState::Equipped : EquipmentRequestedState::Unequipped };
    }

    PersistenceResult EquipmentRuntime::execute(EquipmentCaller caller, EquipmentCommand command,
        EquipmentFileSink& file, std::unique_ptr<const EquipmentSuccess>& output, EquipmentBytes& bytes, FileFaults& faults)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed || file.failedClosed())
        {
            mFailedClosed = true;
            return PersistenceResult::Uncertain;
        }
        const auto actor = validateCommand(caller, command);
        // Current trusted records only. External save bytes never intern IDs.
        std::vector<ESM::RefId> ids;
        {
            const auto values = installedValues(actor);
            if (values.mNpcStats)
            {
                ids.push_back(values.mNpcStats->mBase);
                for (auto id : values.mNpcStats->mSpells)
                    if (!id.empty()) ids.push_back(id);
            }
            for (const auto& object : values.mObjects)
                for (auto id : { object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                         object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap })
                    if (!id.empty()) ids.push_back(id);
        }
        const auto envelope = expectedEnvelope({ command.mActor.mIndex, command.mActor.mContentFile });
        return execute(caller, command, file, { envelope, mStore, ids, mScriptLocals }, output, bytes, faults);
    }

    FileReadResult EquipmentRuntime::restart(size_t actor, const std::filesystem::path& path,
        std::span<const ESM::RefId> referenceIds, std::unique_ptr<const PlainEquipmentValues>& output,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mConnected) throw std::invalid_argument("Connected runtime requires pair recovery");
        const auto caller = mActors.at(actor)->getPtr();
        const auto envelope = expectedEnvelope(caller.getCellRef().getRefNum());
        const EquipmentBindings bindings{ envelope, mStore, referenceIds, mScriptLocals };
        auto fresh = restartBindings(mWorld.getLastGeneratedRefNum());
        validateRestart(actor, caller, bindings, fresh);
        phase.set(Allocations::Phase::Preparation);
        // One opened-file read supplies both the validated counter and installation bytes.
        std::unique_ptr<const RestoredPlainEquipment> restored;
        EquipmentBytes accepted;
        const auto result = restartEquipmentFile(path, bindings, restored, faults, &accepted);
        if (result != FileReadResult::Read) return result;
        PlainEquipmentValues values;
        restored->exportValues(values);
        fresh.mSavedCounter = values.mLastGenerated;
        auto staged = stageRestart(actor, caller, bindings, fresh, restored);
        installRestart(actor, caller, bindings, staged, accepted, output, bytes);
        return result;
    }

    PersistenceResult executeEquipment(EquipmentRuntime& runtime, EquipmentCaller caller, EquipmentCommand command,
        EquipmentFileSink& file, const EquipmentBindings& bindings, std::unique_ptr<const EquipmentSuccess>& output,
        std::vector<char>& bytes, FileFaults& faults)
    {
        return runtime.execute(caller, command, file, bindings, output, bytes, faults);
    }

    void EquipmentRuntime::bindEffects(size_t actor)
    {
        mInventories.at(actor).setInvListener(&mActorEffects.at(actor).mListener);
        mInventories.at(actor).setContListener(&mActorEffects.at(actor).mListener);
    }

    void EquipmentRuntime::validateCaller(size_t actor, const Ptr& caller) const
    {
        if (actor >= ownerCount() || !caller.hasLiveReference())
            throw std::invalid_argument("Equipment trusted caller lifetime or actor changed");
        const auto expected = ownerPtr(actor);
        if (caller != expected || caller.mCell != expected.mCell
            || caller.mContainerStore != expected.mContainerStore
            || caller.getReferenceLifetime() != expected.getReferenceLifetime())
            throw std::invalid_argument("Equipment trusted caller does not match actor");
    }

    EquipmentRuntime::RestartBindings EquipmentRuntime::restartBindings(ESM::RefNum savedCounter) const
    {
        if (mFailedClosed || !mRestartActor || mWorld.mPtrRegistry.mIndex.size() > registryBound())
            throw std::invalid_argument("Equipment restart requires an explicit fresh bounded runtime");
        RestartBindings result{ this, { &mInventories[0], &mInventories[1] },
            { mInventories[0].mResolutionLifetime, mInventories[1].mResolutionLifetime },
            { mInventories[0].mStorageIdentity, mInventories[1].mStorageIdentity },
            mScripts.lifetimeWitness(), mWorld.mPtrRegistry.mIndex, mWorld.getPtrRegistryRevision(),
            mWorld.getLastGeneratedRefNum(), savedCounter, { mNpcStats[0], mNpcStats[1] },
            { mNpcStats[0] ? std::optional{ mNpcStats[0]->values() } : std::nullopt,
                mNpcStats[1] ? std::optional{ mNpcStats[1]->values() } : std::nullopt }, mScriptLocals,
            {} };
        for (const auto& shared : mContainers)
            result.mContainers.emplace_back(*shared->mStore, shared->mReference->getPtr());
        return result;
    }

    bool EquipmentRuntime::sameReference(const ConstPtr& a, const ConstPtr& b)
    {
        return a == b && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore
            && a.getReferenceLifetime() == b.getReferenceLifetime();
    }

    void EquipmentRuntime::validateRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
        const RestartBindings& fresh) const
    {
        const auto valid = [](bool value) {
            if (!value)
                throw std::invalid_argument("Equipment fresh restart binding, lifetime or registry changed");
        };
        valid(!mFailedClosed && mRestartActor && (mConnected ? *mRestartActor == 2 : *mRestartActor == actor) && fresh.mOwner == this
            && fresh.mScriptLocals == mScriptLocals && bindings.mScriptLocals == mScriptLocals);
        validateCaller(actor, caller);
        const auto trusted = expectedEnvelope(caller.getCellRef().getRefNum());
        valid(&bindings.mContent == &mStore && bindings.mEnvelope.mRuntime == trusted.mRuntime
            && bindings.mEnvelope.mContent == trusted.mContent && bindings.mEnvelope.mActor == trusted.mActor);
        valid(!fresh.mScripts.expired() && fresh.mScripts.lock() == mScripts.lifetimeWitness().lock()
            && mScripts.usesStore(mStore) && mScripts.snapshot().mEntries.empty());
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& store = mInventories[i];
            valid(fresh.mStores[i] == &store && !fresh.mLifetimes[i].expired()
                && fresh.mLifetimes[i].lock() == store.mResolutionLifetime
                && fresh.mStorage[i] == store.mStorageIdentity && store.mResolved && store.mUpdatesEnabled);
            valid(mActors[i] && mActors[i]->getPtr().hasLiveReference()
                && sameReference(store.getPtr(mWorld), mActors[i]->getPtr()));
            valid(fresh.mNpcStats[i] == mNpcStats[i]);
            if (mNpcStats[i])
            {
                mNpcStats[i]->validate(mActors[i]->getPtr(), mStore);
                valid(fresh.mStatValues[i] == mNpcStats[i]->values());
            }
        }
        valid(fresh.mContainers.size() == mContainers.size());
        for (size_t i = 0; i < mContainers.size(); ++i)
        {
            const auto& shared = *mContainers[i];
            const auto& store = *shared.mStore;
            const auto& witness = fresh.mContainers[i];
            valid(!witness.mLifetime.expired() && witness.mLifetime.lock() == store.mResolutionLifetime
                && witness.mStorage == store.mStorageIdentity && witness.mStore == &store
                && store.mResolved && store.storedSize() == 0);
            store.forEachStored([&](const auto&, auto) { valid(false); });
            valid(sameReference(shared.mReference->getPtr(), mWorld.getPtr(shared.mReference->getPtr().getCellRef().getRefNum()))
                && sameReference(store.getPtr(mWorld), shared.mReference->getPtr())
                && store.mListener == &shared.mEffects.mListener);
        }
        const auto& target = mInventories[actor];
        const auto& lists = target.mLists;
        valid(lists.mClothes.mList.empty() && lists.mPotions.mList.empty() && lists.mAppas.mList.empty()
            && lists.mArmors.mList.empty() && lists.mBooks.mList.empty() && lists.mIngreds.mList.empty()
            && lists.mLights.mList.empty() && lists.mLockpicks.mList.empty() && lists.mMiscItems.mList.empty()
            && lists.mProbes.mList.empty() && lists.mRepairs.mList.empty() && lists.mWeapons.mList.empty()
            && target.mSelectedEnchantItem == target.end() && target.mRechargingItems.empty());
        for (const auto& slot : target.mSlots)
            valid(slot == target.end());
        valid(target.mInventoryListener == &mActorEffects[actor].mListener
            && target.mListener == &mActorEffects[actor].mListener && mItems[actor].isEmpty());
        const auto& registry = mWorld.mPtrRegistry;
        valid(registry.mIndex.size() <= registryBound() && registry.mIndex.size() == fresh.mRegistry.size()
            && registry.mRevision == fresh.mRevision && registry.mLastGenerated == fresh.mCounter
            && fresh.mSavedCounter.mContentFile < 0);
        for (const auto& [id, ptr] : registry.mIndex)
        {
            const auto old = fresh.mRegistry.find(id);
            valid(old != fresh.mRegistry.end() && old->second.hasLiveReference() && ptr.hasLiveReference()
                && sameReference(ptr, old->second));
            // Follow only a current, lifetime-checked mapping. No saved Ptr
            // or iterator is used to recover a missing node.
            valid(ptr.getCellRef().getRefNum() == id && ptr.mRef->mWorldModel == &mWorld
                && ptr.mContainerStore != &target);
            valid(id.mContentFile >= 0 || id.mContentFile > fresh.mSavedCounter.mContentFile
                || (id.mContentFile == fresh.mSavedCounter.mContentFile && id.mIndex <= fresh.mSavedCounter.mIndex));
        }
        for (const auto& owner : mActors)
        {
            const auto ptr = owner->getPtr();
            valid(sameReference(ptr, mWorld.getPtr(ptr.getCellRef().getRefNum())));
        }
    }

    std::unique_ptr<EquipmentRuntime::RestartInstallation> EquipmentRuntime::stageRestart(size_t actor, const Ptr& caller,
        const EquipmentBindings& bindings, const RestartBindings& fresh,
        std::unique_ptr<const RestoredPlainEquipment>& input)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        validateRestart(actor, caller, bindings, fresh);
        if (!input)
            throw std::invalid_argument("Equipment restart requires detached storage");
        auto& candidate = input->installationCandidate(mStore, bindings.mEnvelope.mActor, fresh.mSavedCounter);
        PlainEquipmentValues saved;
        input->exportValues(saved);
        saved.validate(mStore, bindings.mEnvelope.mActor, mScriptLocals.get());
        if (fresh.mRegistry.size() + saved.mObjects.size() > registryBound())
            throw std::invalid_argument("Equipment restart registry bound exceeded");
        for (const auto& object : saved.mObjects)
            if (fresh.mRegistry.contains(object.mRef.mRefNum))
                throw std::invalid_argument("Equipment restart identity collides with fresh runtime");

        phase.set(Phase::Setup);
        auto& live = mInventories[actor];
        auto staged = std::make_unique<RestartInstallation>(fresh, live);
        staged->mRegistry = fresh.mRegistry;
        candidate.forEachStored([&](auto& ref, auto it) {
            it.mContainer = &live;
            Ptr node(&ref, nullptr);
            node.mContainerStore = &live;
            const auto id = ref.mRef.getRefNum();
            staged->mRegistry.emplace(id, node);
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (id == saved.mSlots[slot]) staged->mSlots[slot] = it;
            if (id == saved.mSelected)
                staged->mSelected = it;
            if (staged->mItem.isEmpty() && ref.mRef.getCount(false) != 0)
                staged->mItem = node;
        });
        phase.set(Phase::Result);
        if (saved.mNpcStats.has_value() != static_cast<bool>(mNpcStats[actor]))
            throw std::invalid_argument("Equipment restart stat context/save mode mismatch");
        if (saved.mNpcStats)
        {
            mNpcStats[actor]->validate(caller, mStore);
            staged->mNpcStats = std::make_shared<EquipmentNpcStats>(caller, mStore);
            staged->mNpcStats->restore(*saved.mNpcStats, candidate);
        }
        staged->mValues = std::make_unique<const PlainEquipmentValues>(std::move(saved));
        phase.set(Phase::Revalidation);
        validateRestart(actor, caller, bindings, fresh);
        staged->mRestored.swap(input); // Only complete staging consumes input.
        return staged;
    }

    void EquipmentRuntime::installRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
        std::unique_ptr<EquipmentRuntime::RestartInstallation>& staged, EquipmentBytes& accepted,
        std::unique_ptr<const PlainEquipmentValues>& output, EquipmentBytes& bytes)
    {
        using namespace Allocations;
        InPhase phase(Phase::Revalidation);
        if (!staged || !staged->mRestored || !staged->mValues)
            throw std::invalid_argument("Equipment restart installation is incomplete or consumed");
        validateRestart(actor, caller, bindings, staged->mFresh);
        auto& candidate = staged->mRestored->installationCandidate(
            mStore, bindings.mEnvelope.mActor, staged->mFresh.mSavedCounter);
        PlainEquipmentValues restored;
        staged->mRestored->exportValues(restored);
        EquipmentBytes encoded;
        encodeEquipment(restored, bindings, encoded);
        if (encoded != accepted || !sameValues(restored, *staged->mValues))
            throw std::invalid_argument("Equipment restart values differ from accepted file");
        if (static_cast<bool>(staged->mNpcStats) != restored.mNpcStats.has_value())
            throw std::invalid_argument("Equipment restart candidate stat mode changed");
        if (staged->mNpcStats)
        {
            staged->mNpcStats->validate(caller, mStore);
            if (staged->mNpcStats->values() != *restored.mNpcStats)
                throw std::invalid_argument("Equipment restart candidate stats differ from saved values");
            const auto& stats = staged->mNpcStats->stats();
            stats.getActiveSpells().validateConstantFortifyLuck(caller, candidate, mStore, stats);
        }
        auto& live = mInventories[actor];
        if (staged->mRegistry.size() != staged->mFresh.mRegistry.size() + restored.mObjects.size())
            throw std::invalid_argument("Equipment restart relocation membership changed");
        for (const auto& [id, node] : staged->mFresh.mRegistry)
            if (!sameReference(node, staged->mRegistry.at(id)))
                throw std::invalid_argument("Equipment restart changed unrelated registry binding");
        std::vector<ContainerStoreIterator> slots(InventoryStore::Slots, live.end());
        auto selected = live.end();
        Ptr first;
        candidate.forEachStored([&](auto& ref, auto it) {
            it.mContainer = &live;
            Ptr node(&ref, nullptr);
            node.mContainerStore = &live;
            const auto id = ref.mRef.getRefNum();
            if (!sameReference(node, staged->mRegistry.at(id)))
                throw std::invalid_argument("Equipment restart node relocation changed");
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (id == restored.mSlots[slot]) slots[slot] = it;
            if (id == restored.mSelected)
                selected = it;
            if (first.isEmpty() && ref.mRef.getCount(false) != 0)
                first = node;
        });
        if (slots != staged->mSlots || selected != staged->mSelected || !sameReference(first, staged->mItem))
            throw std::invalid_argument("Equipment restart slot, selection or item relocation changed");
        validateRestart(actor, caller, bindings, staged->mFresh);

        // All reading, decoding, allocation, validation and relocation is
        // complete. No equip/removal callbacks are replayed on restart.
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            installInventory(actor, candidate, staged->mSlots, staged->mSelected, staged->mItem, staged->mNpcStats, true);
            auto& registry = mWorld.mPtrRegistry;
            registry.mIndex.swap(staged->mRegistry);
            // Format 1 saves the generation counter, not a registry epoch.
            // Invalidate old fresh-runtime preparations with one revision.
            registry.mRevision = staged->mFresh.mRevision + 1;
            registry.mLastGenerated = restored.mLastGenerated;
            mRestartActor.reset();
            phase.set(Phase::Publication);
            output.swap(staged->mValues);
            bytes.swap(accepted);
            phase.set(Phase::Retirement);
            staged.reset();
        };
        install();
    }

    FileReadResult EquipmentRuntime::restartEquipment(size_t actor, const Ptr& caller, const std::filesystem::path& path,
        const EquipmentBindings& bindings, const RestartBindings& fresh,
        std::unique_ptr<const PlainEquipmentValues>& output, EquipmentBytes& bytes, FileFaults& faults)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        validateRestart(actor, caller, bindings, fresh);
        phase.set(Allocations::Phase::Preparation);
        std::unique_ptr<const RestoredPlainEquipment> restored;
        EquipmentBytes accepted;
        const auto result = restartEquipmentFile(path, bindings, restored, faults, &accepted);
        if (result != FileReadResult::Read)
            return result;
        auto staged = stageRestart(actor, caller, bindings, fresh, restored);
        installRestart(actor, caller, bindings, staged, accepted, output, bytes);
        return FileReadResult::Read;
    }

    std::unique_ptr<EquipmentRuntime::Installation> EquipmentRuntime::stageInstallation(size_t actor, const Ptr& caller, PreparedPlainEquipment input, size_t initiator)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        validateCaller(actor, caller);
        const auto context = preparationContext(actor, initiator);
        auto& live = storage(actor);
        const auto& pending = effects(actor);
        if (mWorld.mPtrRegistry.mIndex.size() > registryBound()
            || pending.mNotifications.size() > ActorEffects::MaxPending
            || pending.mListener.mRemovals.size() > ActorEffects::MaxPending)
            throw std::invalid_argument("Equipment runtime registry/effect bound exceeded");
        auto& candidate = input.installationCandidate(context, live);
        // Runtime listeners have fully owned, stageable semantics. Unknown
        // callbacks (including real mechanics listeners) cannot be dropped
        // or invoked after durable acceptance and are rejected visibly.
        if ((inventoryStorage(actor) && inventoryStorage(actor)->mInventoryListener != &pending.mListener) || live.mListener != &pending.mListener)
            throw std::invalid_argument("Unsupported equipment effect listener");
        if (actor < 2 && !mItems[actor].isEmpty() && (!mItems[actor].hasLiveReference() || mItems[actor].mContainerStore != &live))
            throw std::invalid_argument("Equipment runtime item lifetime or binding changed");

        phase.set(Phase::Setup);
        auto staged = std::make_unique<Installation>(std::move(input), live);
        staged->mPrepared.exportValues(context, staged->mSaved);
        staged->mRegistry = mWorld.mPtrRegistry.mIndex;
        // Replace this owner's membership, including retirement of empty nodes.
        live.forEachStored([&](const auto& ref, auto) { staged->mRegistry.erase(ref.mRef.getRefNum()); });
        staged->mRevision = mWorld.getPtrRegistryRevision();
        staged->mEffects = pending;
        std::fill(staged->mSlots.begin(), staged->mSlots.end(), live.end());
        staged->mSelected = live.end();
        const auto& result = staged->mPrepared.result();
        candidate.forEachStored([&](auto& ref, auto it) {
            it.mContainer = &live;
            Ptr node(&ref, nullptr); // Allocate lifetime witnesses before persistence.
            node.mContainerStore = &live;
            const auto id = ref.mRef.getRefNum();
            staged->mRegistry.insert_or_assign(id, node);
            for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                if (id == result.mSlots[slot]) staged->mSlots[slot] = it;
            if (id == result.mSelected)
                staged->mSelected = it;
            if (actor < 2 && (mItems[actor].isEmpty() ? staged->mItem.isEmpty()
                : node.getCellRef().getRefNum() == mItems[actor].getCellRef().getRefNum()))
                staged->mItem = node;
        });
        using Kind = PlainEquipmentResult::EffectKind;
        for (const auto& effect : result.mEffects)
        {
            if (effect.mActor != result.mActor)
                throw std::invalid_argument("Equipment effect owner changed");
            if (staged->mEffects.mListener.mCalls == std::numeric_limits<int>::max()
                || staged->mEffects.mInventoryUpdates == std::numeric_limits<size_t>::max())
                throw std::invalid_argument("Equipment effect counter exhausted");
            switch (effect.mKind)
            {
                case Kind::RegisterSplit:
                    ++staged->mRevision; // Exactly stock insert, including unsigned rollover.
                    break;
                case Kind::InventoryUpdated:
                    if (staged->mEffects.mNotifications.size() == ActorEffects::MaxPending)
                        throw std::invalid_argument("Equipment inventory notification bound exceeded");
                    staged->mEffects.mNotifications.push_back(effect.mActor);
                    ++staged->mEffects.mInventoryUpdates;
                    break;
                case Kind::ItemRemoved:
                    if (staged->mEffects.mListener.mRemovals.size() == ActorEffects::MaxPending)
                        throw std::invalid_argument("Equipment removal notification bound exceeded");
                    staged->mEffects.mListener.itemRemoved(staged->mRegistry.at(effect.mItem), effect.mCount);
                    break;
                case Kind::ItemAdded:
                    ++staged->mEffects.mListener.mCalls;
                    break;
                case Kind::EquipmentChanged:
                    staged->mEffects.mListener.equipmentChanged();
                    break;
                case Kind::DeleteStackScript:
                    // validate() proved every source/candidate is plain and
                    // has no script registration. Stock cleanup is empty.
                    break;
                default:
                    throw std::invalid_argument("Unsupported equipment effect kind");
            }
        }
        phase.set(Phase::Result);
        staged->mResult = std::make_unique<const PlainEquipmentResult>(result);
        phase.set(Phase::Revalidation);
        validateCaller(actor, caller);
        staged->mPrepared.validate(context);
        return staged;
    }

    PersistenceResult EquipmentRuntime::commitEquipment(size_t actor, const Ptr& caller, PreparedPlainEquipment input,
        EquipmentFileSink& file, const EquipmentBindings& bindings,
        std::unique_ptr<const PlainEquipmentResult>& output, EquipmentBytes& bytes, FileFaults& faults)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed || file.failedClosed())
        {
            mFailedClosed = true;
            return PersistenceResult::Uncertain;
        }
        if (mRestartActor || file.session() != mConnected)
            throw std::invalid_argument("Equipment runtime recovery or persistence mode mismatch");
        validateCaller(actor, caller);
        const auto trusted = expectedEnvelope(caller.getCellRef().getRefNum());
        if (bindings.mScriptLocals != mScriptLocals
            || &bindings.mContent != &mStore || bindings.mEnvelope.mActor != trusted.mActor
            || bindings.mEnvelope.mRuntime != trusted.mRuntime || bindings.mEnvelope.mContent != trusted.mContent)
            throw std::invalid_argument("Equipment commit runtime/content/actor binding changed");
        auto staged = stageInstallation(actor, caller, std::move(input));
        phase.set(Phase::Revalidation);
        auto& live = mInventories[actor];
        auto& candidate = staged->mPrepared.installationCandidate(preparationContext(actor), live);
        EquipmentBytes encoded;
        phase.set(Phase::Persistence);
        const auto outcome = mConnected
            ? persistSession({ actor == 0 ? std::array{ staged->mSaved, installedValues(1) }
                                         : std::array{ installedValues(0), staged->mSaved }, staged->mRevision },
                  file, encoded, faults)
            : file.write(staged->mSaved, bindings, encoded, faults);
        if (outcome != PersistenceResult::Accepted)
        {
            mFailedClosed = outcome == PersistenceResult::Uncertain;
            return outcome;
        }

        // Acceptance is the last fallible call. All Ptrs, iterators, map
        // capacity, effects and owned publication storage already exist.
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            installPrepared(actor, *staged, candidate);
            auto& registry = mWorld.mPtrRegistry;
            registry.mIndex.swap(staged->mRegistry);
            registry.mRevision = staged->mRevision;
            registry.mLastGenerated = staged->mSaved.mLastGenerated;
            phase.set(Phase::Publication);
            output.swap(staged->mResult);
            bytes.swap(encoded);
            phase.set(Phase::Retirement);
            staged.reset();
        };
        install();
        return PersistenceResult::Accepted;
    }

    PlainEquipmentValues EquipmentRuntime::installedValues(size_t actor) const
    {
        const auto& inventory = storage(actor);
        const auto* equipped = inventoryStorage(actor);
        if (inventory.storedSize() > PlainEquipmentValues::MaxItems)
            throw std::invalid_argument("Equipment inventory export bound exceeded");
        PlainEquipmentValues result;
        result.mActor = ownerPtr(actor).getCellRef().getRefNum();
        result.mLastGenerated = mWorld.getLastGeneratedRefNum();
        inventory.forEachStored([&](const auto& ref, auto position) {
            if (equipped)
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    if (position == equipped->mSlots[slot]) result.mSlots[slot] = ref.mRef.getRefNum();
            if (equipped && position == equipped->mSelectedEnchantItem)
                result.mSelected = ref.mRef.getRefNum();
            auto& object = result.mObjects.emplace_back();
            object.blank();
            ref.mRef.writeState(object);
            ref.mData.write(object, equipmentDeclarations(mStore, ref.mBase->mScript, mScriptLocals.get()));
            object.mHasCustomState = false;
        });
        if (actor < 2 && mNpcStats[actor])
            result.mNpcStats = mNpcStats[actor]->values();
        return result;
    }

    PlainEquipmentContext EquipmentRuntime::preparationContext(size_t actor, size_t initiator) const
    {
        const auto player = mActors.at(actor < 2 ? actor : initiator)->getPtr();
        return { mStore, mWorld, mScripts, actor >= 2 && actorInventory(ownerPtr(actor)) ? ownerPtr(actor) : player,
            player, actor < 2 ? mNpcStats[actor] : nullptr, mScriptLocals, mConstantEffects };
    }

    auto EquipmentRuntime::cellValues(const ESM::CellRef& ref)
    {
        return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
            ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
            ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
            ref.mTrap, ref.mReferenceBlocked, ref.mPos);
    }

    bool EquipmentRuntime::sameCellRef(const ESM::CellRef& a, const ESM::CellRef& b)
    {
        return cellValues(a) == cellValues(b);
    }

    bool EquipmentRuntime::sameObject(const ESM::ObjectState& a, const ESM::ObjectState& b)
    {
        return cellValues(a.mRef) == cellValues(b.mRef) && a.mPosition == b.mPosition && a.mFlags == b.mFlags
            && a.mEnabled == b.mEnabled && a.mHasLocals == b.mHasLocals && a.mVersion == b.mVersion
            && a.mActorIdConverter == b.mActorIdConverter && a.mHasCustomState == b.mHasCustomState
            && a.mLocals.mVariables == b.mLocals.mVariables && a.mLuaScripts.mScripts.empty()
            && b.mLuaScripts.mScripts.empty()
            && std::equal(a.mAnimationState.mScriptedAnims.begin(), a.mAnimationState.mScriptedAnims.end(),
                b.mAnimationState.mScriptedAnims.begin(), b.mAnimationState.mScriptedAnims.end(),
                [](const auto& x, const auto& y) {
                    return std::tie(x.mGroup, x.mTime, x.mAbsolute, x.mLoopCount)
                        == std::tie(y.mGroup, y.mTime, y.mAbsolute, y.mLoopCount);
                });
    }

    bool EquipmentRuntime::sameValues(const PlainEquipmentValues& a, const PlainEquipmentValues& b)
    {
        return std::tie(a.mActor, a.mSlots, a.mSelected, a.mLastGenerated)
            == std::tie(b.mActor, b.mSlots, b.mSelected, b.mLastGenerated)
            && a.mNpcStats == b.mNpcStats
            && std::equal(a.mObjects.begin(), a.mObjects.end(), b.mObjects.begin(), b.mObjects.end(), sameObject);
    }

    InventoryInstanceId EquipmentRuntime::ownedId(ESM::RefNum id)
    {
        return { id.mIndex, id.mContentFile };
    }

    size_t EquipmentRuntime::validateCommand(EquipmentCaller caller, const EquipmentCommand& command) const
    {
        const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
        if (mRestartActor || !id(command.mActor).isSet() || !id(command.mItem).isSet()
            || caller.mActor != command.mActor || command.mExpectedRevision != mWorld.getPtrRegistryRevision()
            || command.mSlot < 0 || command.mSlot >= InventoryStore::Slots
            || (command.mState != EquipmentRequestedState::Equipped
                && command.mState != EquipmentRequestedState::Unequipped))
            throw std::invalid_argument("Equipment command caller, identity, revision or state invalid");
        if (mConnected && (command.mExpectedRevision >= std::numeric_limits<size_t>::max() - 1
            || mWorld.getLastGeneratedRefNum().mContentFile != -1
            || mWorld.getLastGeneratedRefNum().mIndex == std::numeric_limits<uint32_t>::max()))
            throw std::invalid_argument("Session registry counter exhausted");
        const auto actorPtr = mWorld.getPtr(id(command.mActor));
        if (!actorPtr.hasLiveReference() || actorPtr.getCellRef().getRefNum() != id(command.mActor))
            throw std::invalid_argument("Equipment command actor registry identity mismatch");
        size_t actor = 0;
        for (; actor < mActors.size(); ++actor)
            if (mActors[actor] && sameReference(actorPtr, mActors[actor]->getPtr()))
                break;
        if (actor >= 2) throw std::invalid_argument("Equipment requires a current actor");
        validateCaller(actor, actorPtr);
        const auto item = mWorld.getPtr(id(command.mItem));
        if (!item.hasLiveReference() || item.getContainerStore() != &mInventories[actor])
            throw std::invalid_argument("Equipment command item ownership or lifetime mismatch");
        return actor;
    }

    PersistenceResult EquipmentRuntime::execute(EquipmentCaller caller, EquipmentCommand command, EquipmentFileSink& file,
        const EquipmentBindings& bindings, std::unique_ptr<const EquipmentSuccess>& output,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed || file.failedClosed())
        {
            mFailedClosed = true;
            return PersistenceResult::Uncertain;
        }
        const auto actor = validateCommand(caller, command);
        const auto actorPtr = mActors[actor]->getPtr();
        const ESM::RefNum itemId{ command.mItem.mIndex, command.mItem.mContentFile };
        const auto item = mWorld.getPtr(itemId);
        phase.set(Phase::Preparation);
        auto prepared = PreparedPlainEquipment::prepare(ContainerStoreResolution(mInventories[actor], actorPtr),
            item, itemId, static_cast<size_t>(command.mExpectedRevision),
            command.mState == EquipmentRequestedState::Equipped, preparationContext(actor), command.mSlot);
        const auto& result = prepared.result();
        auto revision = mWorld.getPtrRegistryRevision();
        for (const auto& effect : result.mEffects)
            if (effect.mKind == PlainEquipmentResult::EffectKind::RegisterSplit)
                ++revision; // Same stock counter semantics as commitEquipment.
        phase.set(Phase::Result);
        auto staged = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{ command,
            ownedId(result.mSlots[InventoryStore::Slot_Shirt]), ownedId(result.mSelected), ownedId(result.mLastGenerated), revision, result.mLuck, result.mSkipped });
        std::unique_ptr<const PlainEquipmentResult> internal;
        const auto outcome = commitEquipment(actor, actorPtr, std::move(prepared), file, bindings, internal, bytes, faults);
        if (outcome == PersistenceResult::Accepted)
        {
            phase.set(Phase::Publication);
            output.swap(staged);
        }
        phase.set(Phase::Retirement);
        return outcome;
    }
}
