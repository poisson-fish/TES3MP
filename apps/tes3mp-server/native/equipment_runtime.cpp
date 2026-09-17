#include "equipment_runtime.hpp"
#include "runtime_phases.hpp"
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadnpc.hpp>
#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    EquipmentRuntime::EquipmentRuntime(const ESMStore& content, WorldModel& world, LocalScripts& scripts,
        std::string runtime, std::array<unsigned char, 32> contentIdentity,
        const std::array<EquipmentActorBinding, 2>& actors,
        std::shared_ptr<const EquipmentScriptLocals> locals, MWBase::ScriptManager* declarations,
        std::optional<size_t> restartActor, bool connected)
        : mStore(content), mWorld(world), mScripts(scripts), mRuntime(std::move(runtime)), mContent(contentIdentity)
        , mScriptLocals(std::move(locals)), mConnected(connected)
    {
        if (&world.mStore != &content || !scripts.usesStore(content) || mRuntime.empty()
            || mRuntime.size() > 128 || mRuntime.find('\0') != std::string::npos
            || std::none_of(mContent.begin(), mContent.end(), [](auto byte) { return byte != 0; })
            || (restartActor && (connected ? *restartActor != 2 : *restartActor >= 2)))
            throw std::invalid_argument("Equipment runtime content/service/startup binding mismatch");
        if (connected && (!world.mPtrRegistry.mIndex.empty() || !scripts.snapshot().mEntries.empty()))
            throw std::invalid_argument("Connected runtime requires exclusive fresh registry and script services");
        // Validate all trusted startup records before constructing registered nodes.
        for (const auto& actor : actors)
        {
            content.get<ESM::NPC>().find(actor.mBase);
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
        // Bind lazy service identity during trusted startup, never during a
        // rejectable command/recovery allocation observation.
        scripts.lifetimeWitness();
        MWClass::registerClasses();
        for (size_t i = 0; i < actors.size(); ++i)
        {
            mActors[i] = std::make_unique<ManualRef>(content, actors[i].mBase);
            const Ptr actor = mActors[i]->getPtr();
            world.registerPtr(actor);
            auto& inventory = mInventories[i];
            inventory.setPtr(actor, world);
            Misc::Rng::Generator rng{ 0 };
            inventory.fill({}, {}, rng);
            ManualRef item(content, actors[i].mShirt);
            mItems[i] = *inventory.addNewStack(item.getPtr(), actors[i].mCount);
            world.registerPtr(mItems[i]);
            ContainerStoreResolution witness(inventory, actor);
            const auto script = content.get<ESM::Clothing>().find(actors[i].mShirt)->mScript;
            if (!script.empty())
                mItems[i].getRefData().setLocals(*content.get<ESM::Script>().find(script), *declarations);
            if (actors[i].mNpcStats)
                mNpcStats[i] = std::make_shared<EquipmentNpcStats>(actor, content);
            bindEffects(i);
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
        if (actor >= mActors.size() || !mActors[actor] || !caller.hasLiveReference())
            throw std::invalid_argument("Equipment trusted caller lifetime or actor changed");
        const auto expected = mActors[actor]->getPtr();
        if (caller != expected || caller.mCell != expected.mCell
            || caller.mContainerStore != expected.mContainerStore
            || caller.getReferenceLifetime() != expected.getReferenceLifetime())
            throw std::invalid_argument("Equipment trusted caller does not match actor");
    }

    EquipmentRuntime::RestartBindings EquipmentRuntime::restartBindings(ESM::RefNum savedCounter) const
    {
        if (mFailedClosed || !mRestartActor || mWorld.mPtrRegistry.mIndex.size() > 132)
            throw std::invalid_argument("Equipment restart requires an explicit fresh bounded runtime");
        return { this, { &mInventories[0], &mInventories[1] },
            { mInventories[0].mResolutionLifetime, mInventories[1].mResolutionLifetime },
            { mInventories[0].mStorageIdentity, mInventories[1].mStorageIdentity },
            mScripts.lifetimeWitness(), mWorld.mPtrRegistry.mIndex, mWorld.getPtrRegistryRevision(),
            mWorld.getLastGeneratedRefNum(), savedCounter, { mNpcStats[0], mNpcStats[1] },
            { mNpcStats[0] ? std::optional{ mNpcStats[0]->values() } : std::nullopt,
                mNpcStats[1] ? std::optional{ mNpcStats[1]->values() } : std::nullopt }, mScriptLocals };
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
        valid(registry.mIndex.size() <= 132 && registry.mIndex.size() == fresh.mRegistry.size()
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
        if (fresh.mRegistry.size() + saved.mObjects.size() > 132)
            throw std::invalid_argument("Equipment restart registry bound exceeded");
        for (const auto& object : saved.mObjects)
            if (fresh.mRegistry.contains(object.mRef.mRefNum))
                throw std::invalid_argument("Equipment restart identity collides with fresh runtime");

        phase.set(Phase::Setup);
        auto& live = mInventories[actor];
        auto staged = std::make_unique<RestartInstallation>(fresh, live);
        staged->mRegistry = fresh.mRegistry;
        for (auto it = candidate.mLists.mClothes.mList.begin(); it != candidate.mLists.mClothes.mList.end(); ++it)
        {
            Ptr node(&*it, nullptr);
            node.mContainerStore = &live;
            const auto id = it->mRef.getRefNum();
            staged->mRegistry.emplace(id, node);
            if (id == saved.mShirt)
                staged->mShirt = ContainerStoreIterator(&live, it);
            if (id == saved.mSelected)
                staged->mSelected = ContainerStoreIterator(&live, it);
            if (staged->mItem.isEmpty() && it->mRef.getCount(false) != 0)
                staged->mItem = node;
        }
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
        auto shirt = live.end(), selected = live.end();
        Ptr first;
        for (auto it = candidate.mLists.mClothes.mList.begin(); it != candidate.mLists.mClothes.mList.end(); ++it)
        {
            Ptr node(&*it, nullptr);
            node.mContainerStore = &live;
            const auto id = it->mRef.getRefNum();
            if (!sameReference(node, staged->mRegistry.at(id)))
                throw std::invalid_argument("Equipment restart node relocation changed");
            if (id == restored.mShirt)
                shirt = ContainerStoreIterator(&live, it);
            if (id == restored.mSelected)
                selected = ContainerStoreIterator(&live, it);
            if (first.isEmpty() && it->mRef.getCount(false) != 0)
                first = node;
        }
        if (shirt != staged->mShirt || selected != staged->mSelected || !sameReference(first, staged->mItem))
            throw std::invalid_argument("Equipment restart slot, selection or item relocation changed");
        validateRestart(actor, caller, bindings, staged->mFresh);

        // All reading, decoding, allocation, validation and relocation is
        // complete. No equip/removal callbacks are replayed on restart.
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            installInventory(actor, candidate, staged->mShirt, staged->mSelected, staged->mItem, staged->mNpcStats, true);
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

    std::unique_ptr<EquipmentRuntime::Installation> EquipmentRuntime::stageInstallation(size_t actor, const Ptr& caller, PreparedPlainEquipment input)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        validateCaller(actor, caller);
        const auto context = preparationContext(actor);
        auto& live = mInventories[actor];
        const auto& effects = mActorEffects[actor];
        if (mWorld.mPtrRegistry.mIndex.size() > 2 * PlainEquipmentValues::MaxItems + 2
            || effects.mNotifications.size() > ActorEffects::MaxPending
            || effects.mListener.mRemovals.size() > ActorEffects::MaxPending)
            throw std::invalid_argument("Equipment runtime registry/effect bound exceeded");
        auto& candidate = input.installationCandidate(context, live);
        // Runtime listeners have fully owned, stageable semantics. Unknown
        // callbacks (including real mechanics listeners) cannot be dropped
        // or invoked after durable acceptance and are rejected visibly.
        if (live.mInventoryListener != &effects.mListener || live.mListener != &effects.mListener)
            throw std::invalid_argument("Unsupported equipment effect listener");
        if (!mItems[actor].isEmpty() && (!mItems[actor].hasLiveReference() || mItems[actor].mContainerStore != &live))
            throw std::invalid_argument("Equipment runtime item lifetime or binding changed");

        phase.set(Phase::Setup);
        auto staged = std::make_unique<Installation>(std::move(input), live);
        staged->mPrepared.exportValues(context, staged->mSaved);
        staged->mRegistry = mWorld.mPtrRegistry.mIndex;
        staged->mRevision = mWorld.getPtrRegistryRevision();
        staged->mEffects = effects;
        staged->mShirt = staged->mSelected = live.end();
        const auto& result = staged->mPrepared.result();
        for (auto it = candidate.mLists.mClothes.mList.begin(); it != candidate.mLists.mClothes.mList.end(); ++it)
        {
            Ptr node(&*it, nullptr); // Allocate lifetime witnesses before persistence.
            node.mContainerStore = &live;
            const auto id = it->mRef.getRefNum();
            staged->mRegistry.insert_or_assign(id, node);
            if (id == result.mShirt)
                staged->mShirt = ContainerStoreIterator(&live, it);
            if (id == result.mSelected)
                staged->mSelected = ContainerStoreIterator(&live, it);
            if (mItems[actor].isEmpty() ? staged->mItem.isEmpty()
                : node.getCellRef().getRefNum() == mItems[actor].getCellRef().getRefNum())
                staged->mItem = node;
        }
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
        const auto& inventory = mInventories[actor];
        if (inventory.mLists.mClothes.mList.size() > PlainEquipmentValues::MaxItems)
            throw std::invalid_argument("Equipment inventory export bound exceeded");
        PlainEquipmentValues result;
        result.mActor = mActors[actor]->getPtr().getCellRef().getRefNum();
        result.mLastGenerated = mWorld.getLastGeneratedRefNum();
        for (auto it = inventory.mLists.mClothes.mList.begin(); it != inventory.mLists.mClothes.mList.end(); ++it)
        {
            const auto position = ConstContainerStoreIterator(&inventory, it);
            if (position == inventory.mSlots[InventoryStore::Slot_Shirt])
                result.mShirt = it->mRef.getRefNum();
            if (position == inventory.mSelectedEnchantItem)
                result.mSelected = it->mRef.getRefNum();
            auto& object = result.mObjects.emplace_back();
            object.blank();
            it->mRef.writeState(object);
            it->mData.write(object, equipmentDeclarations(mStore, it->mBase->mScript, mScriptLocals.get()));
            object.mHasCustomState = false;
        }
        if (mNpcStats[actor])
            result.mNpcStats = mNpcStats[actor]->values();
        return result;
    }

    PlainEquipmentContext EquipmentRuntime::preparationContext(size_t actor) const
    {
        return { mStore, mWorld, mScripts, mActors[actor]->getPtr(), mActors[actor]->getPtr(), mNpcStats[actor], mScriptLocals };
    }

    auto EquipmentRuntime::cellValues(const ESM::CellRef& ref)
    {
        return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
            ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
            ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
            ref.mTrap, ref.mReferenceBlocked, ref.mPos);
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
        return std::tie(a.mActor, a.mShirt, a.mSelected, a.mLastGenerated)
            == std::tie(b.mActor, b.mShirt, b.mSelected, b.mLastGenerated)
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
            command.mState == EquipmentRequestedState::Equipped, preparationContext(actor));
        const auto& result = prepared.result();
        auto revision = mWorld.getPtrRegistryRevision();
        for (const auto& effect : result.mEffects)
            if (effect.mKind == PlainEquipmentResult::EffectKind::RegisterSplit)
                ++revision; // Same stock counter semantics as commitEquipment.
        phase.set(Phase::Result);
        auto staged = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{ command,
            ownedId(result.mShirt), ownedId(result.mSelected), ownedId(result.mLastGenerated), revision, result.mLuck, result.mSkipped });
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
