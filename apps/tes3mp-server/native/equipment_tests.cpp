#include "equipment_tests.hpp"
#include "equipment_codec.hpp"
#include "equipment_command.hpp"
#include "equipment_file.hpp"
#include "test_allocations.hpp"

#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
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
#include <components/esm3/loadench.hpp>
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

        struct EquipmentScratch
        {
            std::filesystem::path mPath;
            explicit EquipmentScratch(const std::filesystem::path& path)
                : mPath(path)
            {
                require(std::filesystem::create_directory(mPath), "equipment scratch directory already exists");
            }
            ~EquipmentScratch()
            {
                std::error_code ignored;
                std::filesystem::remove_all(mPath, ignored);
            }
        };

        EquipmentBytes equipmentFileBytes(const std::filesystem::path& path)
        {
            EquipmentBytes bytes;
            FileFaults faults;
            require(readEquipmentFile(path, bytes, faults) == FileReadResult::Read,
                "equipment file reopen failed");
            return bytes;
        }

        void writeEquipmentInput(const std::filesystem::path& path, std::span<const char> bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.close();
            require(!stream.fail(), "equipment input fixture write failed");
        }
    }

    // Test-owned actors and true stock InventoryStores; no NPC custom-data or
    // Environment installation. Private access stages/installs only these stores.
    class PlainEquipmentFixture
    {
    public:
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
        ESMStore mStore;
        ESM::ReadersCache mReaders;
        WorldModel mWorld{ mStore, mReaders, 1 };
        LocalScripts mScripts{ mStore };
        std::array<std::unique_ptr<ManualRef>, 2> mActors;
        std::array<InventoryStore, 2> mInventories;
        std::array<Ptr, 2> mItems;
        // The fixture is the sole writer of each bound stock NPC stat context.
        // No NPC custom-data or global player is installed alongside it.
        std::array<std::shared_ptr<EquipmentNpcStats>, 2> mNpcStats;
        std::vector<std::string> mEvents;
        Listener mListener;

        struct ActorEffects
        {
            static constexpr size_t MaxPending = 64;
            Listener mListener;
            size_t mInventoryUpdates = 0;
            std::vector<ESM::RefNum> mNotifications;
        };
        std::array<ActorEffects, 2> mActorEffects;
        bool mFailedClosed = false;
        // Only construction can authorize restart. Consumption closes this mode;
        // an ordinary or uncertain fixture can never opt back into it.
        std::optional<size_t> mRestartActor;

        void bindEffects(size_t actor)
        {
            mInventories.at(actor).setInvListener(&mActorEffects.at(actor).mListener);
            mInventories.at(actor).setContListener(&mActorEffects.at(actor).mListener);
        }

        // Private borrowed relocation data never leaves this test-owned fixture.
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

            Installation(PreparedPlainEquipment prepared, InventoryStore& target)
                : mPrepared(std::move(prepared))
                , mShirt(target.end())
                , mSelected(target.end())
            {
            }
        };

        void validateCaller(size_t actor, const Ptr& caller) const
        {
            if (actor >= mActors.size() || !mActors[actor] || !caller.hasLiveReference())
                throw std::invalid_argument("Equipment trusted caller lifetime or actor changed");
            const auto expected = mActors[actor]->getPtr();
            if (caller != expected || caller.mCell != expected.mCell
                || caller.mContainerStore != expected.mContainerStore
                || caller.getReferenceLifetime() != expected.getReferenceLifetime())
                throw std::invalid_argument("Equipment trusted caller does not match actor");
        }

        struct RestartBindings
        {
            const PlainEquipmentFixture* mFixture;
            std::array<const InventoryStore*, 2> mStores;
            std::array<std::weak_ptr<const void>, 2> mLifetimes;
            std::array<std::shared_ptr<const void>, 2> mStorage;
            std::weak_ptr<const void> mScripts;
            PtrRegistry::Index mRegistry;
            size_t mRevision;
            ESM::RefNum mCounter, mSavedCounter;
            std::array<std::shared_ptr<const EquipmentNpcStats>, 2> mNpcStats;
            std::array<std::optional<EquipmentNpcStatsValues>, 2> mStatValues;
        };

        RestartBindings restartBindings(ESM::RefNum savedCounter) const
        {
            if (mFailedClosed || !mRestartActor || mWorld.mPtrRegistry.mIndex.size() > 132)
                throw std::invalid_argument("Equipment restart requires an explicit fresh bounded fixture");
            return { this, { &mInventories[0], &mInventories[1] },
                { mInventories[0].mResolutionLifetime, mInventories[1].mResolutionLifetime },
                { mInventories[0].mStorageIdentity, mInventories[1].mStorageIdentity },
                mScripts.lifetimeWitness(), mWorld.mPtrRegistry.mIndex, mWorld.getPtrRegistryRevision(),
                mWorld.getLastGeneratedRefNum(), savedCounter, { mNpcStats[0], mNpcStats[1] },
                { mNpcStats[0] ? std::optional{ mNpcStats[0]->values() } : std::nullopt,
                    mNpcStats[1] ? std::optional{ mNpcStats[1]->values() } : std::nullopt } };
        }

        static bool sameReference(const ConstPtr& a, const ConstPtr& b)
        {
            return a == b && a.mCell == b.mCell && a.mContainerStore == b.mContainerStore
                && a.getReferenceLifetime() == b.getReferenceLifetime();
        }

        void validateRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
            const RestartBindings& fresh) const
        {
            const auto valid = [](bool value) {
                if (!value)
                    throw std::invalid_argument("Equipment fresh restart binding, lifetime or registry changed");
            };
            valid(!mFailedClosed && mRestartActor && *mRestartActor == actor && fresh.mFixture == this);
            validateCaller(actor, caller);
            const auto trusted = envelope(caller.getCellRef().getRefNum());
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

        std::unique_ptr<RestartInstallation> stageRestart(size_t actor, const Ptr& caller,
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
            saved.validate(mStore, bindings.mEnvelope.mActor);
            if (fresh.mRegistry.size() + saved.mObjects.size() > 132)
                throw std::invalid_argument("Equipment restart registry bound exceeded");
            for (const auto& object : saved.mObjects)
                if (fresh.mRegistry.contains(object.mRef.mRefNum))
                    throw std::invalid_argument("Equipment restart identity collides with fresh fixture");

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

        static void checkRestartStaging()
        {
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture source;
                source.seedValues(actor);
                PlainEquipmentValues saved;
                source.prepare(actor, true).exportValues(source.preparationContext(actor), saved);
                PlainEquipmentFixture fresh(actor);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                const EquipmentBindings bindings{ e, fresh.mStore, ids };
                const auto witnesses = fresh.restartBindings(saved.mLastGenerated);
                auto input = std::make_unique<const RestoredPlainEquipment>(
                    RestoredPlainEquipment::restore(saved, fresh.mStore, saved.mActor));
                const auto before = fresh.snapshot();
                auto staged = fresh.stageRestart(actor, fresh.mActors[actor]->getPtr(), bindings, witnesses, input);
                require(!input && sameValues(*staged->mValues, saved), "restart staging lost owned values");
                for (const auto& object : saved.mObjects)
                {
                    const auto node = staged->mRegistry.at(object.mRef.mRefNum);
                    require(node.hasLiveReference() && !node.mRef->mWorldModel
                            && node.mContainerStore == &fresh.mInventories[actor]
                            && node.getCellRef().getCount(false) == object.mRef.mCount,
                        "restart staging lost exact detached identities/counts");
                }
                fresh.unchanged(before);
            }
            std::cout << "equipment fresh two-actor restart staging=2\n";
        }

        void installRestart(size_t actor, const Ptr& caller, const EquipmentBindings& bindings,
            std::unique_ptr<RestartInstallation>& staged, EquipmentBytes& accepted,
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
                auto& nodes = candidate.mLists.mClothes.mList;
                for (auto& node : nodes)
                    node.mWorldModel = &mWorld;
                nodes.swap(live.mLists.mClothes.mList);
                live.mStorageIdentity.swap(candidate.mStorageIdentity);
                live.mSlots[InventoryStore::Slot_Shirt] = staged->mShirt;
                live.mSelectedEnchantItem = staged->mSelected;
                live.mWeightUpToDate = live.mRechargingItemsUpToDate = false;
                live.mModified = true;
                auto& registry = mWorld.mPtrRegistry;
                registry.mIndex.swap(staged->mRegistry);
                // Format 1 saves the generation counter, not a registry epoch.
                // Invalidate old fresh-fixture preparations with one revision.
                registry.mRevision = staged->mFresh.mRevision + 1;
                registry.mLastGenerated = restored.mLastGenerated;
                mItems[actor] = staged->mItem;
                mNpcStats[actor].swap(staged->mNpcStats);
                mRestartActor.reset();
                phase.set(Phase::Publication);
                output.swap(staged->mValues);
                bytes.swap(accepted);
                phase.set(Phase::Retirement);
                staged.reset();
            };
            install();
        }

        FileReadResult restartEquipment(size_t actor, const Ptr& caller, const std::filesystem::path& path,
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

        static void checkRestartSuccess(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t cases = 0;
            std::unique_ptr<const PlainEquipmentValues> output;
            EquipmentBytes bytes;
            for (size_t actor = 0; actor < 2; ++actor)
                for (int variant = 0; variant < 7; ++variant)
                {
                    PlainEquipmentValues saved;
                    {
                        PlainEquipmentFixture source;
                        source.seedValues(actor);
                        if (variant == 1)
                        {
                            source.mInventories[actor].equip(InventoryStore::Slot_Shirt,
                                source.mInventories[actor].begin(), source.context(actor, actor));
                            source.mInventories[actor].mSelectedEnchantItem = source.mInventories[actor].begin();
                        }
                        source.prepare(actor, variant != 1).exportValues(source.preparationContext(actor), saved);
                    } // No source actors, store, services or nodes survive.
                    if (variant == 2)
                    {
                        saved.mObjects.clear();
                        saved.mShirt = saved.mSelected = {};
                    }
                    if (variant == 3)
                    {
                        saved.mObjects.resize(1);
                        saved.mObjects.front().mRef.mCount = 0;
                        saved.mShirt = {};
                        saved.mSelected = saved.mObjects.front().mRef.mRefNum;
                    }
                    if (variant == 4)
                    {
                        while (saved.mObjects.size() < PlainEquipmentValues::MaxItems)
                        {
                            auto object = saved.mObjects.front();
                            object.mRef.mRefNum = { static_cast<uint32_t>(20 + saved.mObjects.size()), -1 };
                            object.mRef.mCount = 0;
                            saved.mObjects.push_back(std::move(object));
                        }
                        saved.mLastGenerated = { 1000, -1 }; // Beyond all surviving IDs.
                        saved.mSelected = saved.mObjects.back().mRef.mRefNum;
                    }
                    if (variant == 5)
                        saved.mLastGenerated = { std::numeric_limits<uint32_t>::max(),
                            std::numeric_limits<int32_t>::min() };
                    if (variant == 6)
                        saved.mLastGenerated = { 0, -2 }; // Exact stock rollover, not a surviving-node maximum.
                    PlainEquipmentFixture fresh(actor);
                    fresh.seedValues(1 - actor);
                    if (variant == 5)
                        fresh.mWorld.mPtrRegistry.mRevision = std::numeric_limits<size_t>::max();
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const EquipmentBindings bindings{ e, fresh.mStore, ids };
                    const auto witnesses = fresh.restartBindings(saved.mLastGenerated);
                    const auto before = fresh.snapshot();
                    EquipmentFileSink file(scratch / "restart.bin");
                    FileFaults faults;
                    EquipmentBytes persisted;
                    require(file.write(saved, bindings, persisted, faults) == TestPersistenceResult::Accepted,
                        "equipment restart success file setup failed");
                    Allocations::Trace trace;
                    FileReadResult outcome;
                    {
                        Allocations::Observe observe(trace);
                        outcome = fresh.restartEquipment(actor, fresh.mActors[actor]->getPtr(), scratch / "restart.bin",
                            bindings, witnesses, output, bytes, faults);
                    }
                    require(outcome == FileReadResult::Read && output && sameValues(*output, saved)
                            && sameValues(fresh.installedValues(actor), saved) && bytes == persisted
                            && fresh.mWorld.getLastGeneratedRefNum() == saved.mLastGenerated
                            && fresh.mWorld.getPtrRegistryRevision() == before.mRegistry.mRevision + 1
                            && trace.allocations(Allocations::Phase::Installation) == 0
                            && trace.allocations(Allocations::Phase::Publication) == 0
                            && trace.allocations(Allocations::Phase::Retirement) == 0,
                        "equipment restart lost exact values/counters or allocated during installation/publication");
                    for (const auto& node : fresh.mInventories[actor].mLists.mClothes.mList)
                    {
                        const auto ptr = fresh.mWorld.getPtr(node.mRef.getRefNum());
                        require(ptr.hasLiveReference() && ptr.mRef == &node && node.mWorldModel == &fresh.mWorld
                                && ptr.mContainerStore == &fresh.mInventories[actor],
                            "equipment restart lost active/dormant registry binding");
                    }
                    require(fresh.mActorEffects[actor].mListener.mCalls == 0
                            && fresh.mActorEffects[actor].mListener.mRemovals.empty()
                            && fresh.mActorEffects[actor].mNotifications.empty()
                            && fresh.mActorEffects[actor].mInventoryUpdates == 0
                            && fresh.mScripts.snapshot() == before.mScripts && fresh.mEvents == before.mEvents,
                        "equipment restart replayed operation effects");
                    fresh.unchangedActor(before, 1 - actor);
                    for (const auto& [id, binding] : before.mRegistry.mEntries)
                        require(fresh.mWorld.snapshotPtrRegistry().mEntries.at(id) == binding,
                            "equipment restart changed retained registry mapping");
                    const auto installed = fresh.snapshot();
                    const auto* outputStorage = output.get();
                    const auto* byteStorage = bytes.data();
                    faults = {};
                    bool blocked = false;
                    try
                    {
                        fresh.restartEquipment(actor, fresh.mActors[actor]->getPtr(), scratch / "restart.bin",
                            bindings, witnesses, output, bytes, faults);
                    }
                    catch (const std::invalid_argument&)
                    {
                        blocked = true;
                    }
                    require(blocked && faults.mReads == 0 && output.get() == outputStorage
                            && sameValues(*output, saved) && bytes.data() == byteStorage && bytes == persisted,
                        "equipment restart reused consumed fresh authorization");
                    fresh.unchanged(installed);
                    ++cases;
                }
            require(output && !bytes.empty(), "equipment restart publication borrowed fixture storage");
            std::cout << "equipment fresh two-actor restart successes=" << cases << '\n';
        }

        static void checkRestartGuards()
        {
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (int test = 0; test < 38; ++test)
                {
                    PlainEquipmentFixture source;
                    source.seedValues(actor);
                    PlainEquipmentValues saved;
                    source.prepare(actor, true).exportValues(source.preparationContext(actor), saved);
                    PlainEquipmentFixture fresh(actor);
                    const auto ids = referenceIds(saved);
                    auto e = envelope(saved.mActor);
                    const ESMStore* content = &fresh.mStore;
                    auto caller = fresh.mActors[actor]->getPtr();
                    auto witnesses = fresh.restartBindings(saved.mLastGenerated);
                    auto input = std::make_unique<const RestoredPlainEquipment>(
                        RestoredPlainEquipment::restore(saved, fresh.mStore, saved.mActor));
                    EquipmentBytes accepted;
                    encodeEquipment(saved, { e, fresh.mStore, ids }, accepted);
                    auto staged = fresh.stageRestart(actor, caller, { e, fresh.mStore, ids }, witnesses, input);
                    // Reject after staging as well as before it: none of these
                    // witnesses authorize following a retired owner/store/node.
                    switch (test)
                    {
                        case 0: caller = fresh.mActors[1 - actor]->getPtr(); break;
                        case 1: caller.mContainerStore = &fresh.mInventories[actor]; break;
                        case 2: e.mRuntime += "-foreign"; break;
                        case 3: ++e.mContent[0]; break;
                        case 4: e.mActor = fresh.mActors[1 - actor]->getPtr().getCellRef().getRefNum(); break;
                        case 5: content = &source.mStore; break;
                        case 6: fresh.mFailedClosed = true; break;
                        case 7: fresh.mRestartActor.reset(); break;
                        case 8: fresh.mRestartActor = 1 - actor; break;
                        case 9: staged->mFresh.mFixture = &source; break;
                        case 10: staged->mFresh.mStores[actor] = &source.mInventories[actor]; break;
                        case 11: staged->mFresh.mLifetimes[actor].reset(); break;
                        case 12: staged->mFresh.mStorage[actor].reset(); break;
                        case 13: staged->mFresh.mScripts.reset(); break;
                        case 14: ++fresh.mWorld.mPtrRegistry.mRevision; break;
                        case 15: ++fresh.mWorld.mPtrRegistry.mLastGenerated.mIndex; break;
                        case 16: ++staged->mFresh.mSavedCounter.mIndex; break;
                        case 17: fresh.mInventories[actor].setInvListener(nullptr); break;
                        case 18: fresh.mInventories[actor].setContListener(&fresh.mListener); break;
                        case 19: fresh.mInventories[actor].mUpdatesEnabled = false; break;
                        case 20: fresh.mInventories[actor].mResolved = false; break;
                        case 21:
                            fresh.mInventories[actor].mSlots[InventoryStore::Slot_Shirt]
                                = fresh.mInventories[1 - actor].begin();
                            break;
                        case 22:
                            fresh.mInventories[actor].mSelectedEnchantItem = fresh.mInventories[1 - actor].begin();
                            break;
                        case 23: fresh.mActors[actor].reset(); break;
                        case 24:
                            fresh.mInventories[actor].~InventoryStore();
                            new (&fresh.mInventories[actor]) InventoryStore;
                            break;
                        case 25:
                            fresh.mScripts.~LocalScripts();
                            new (&fresh.mScripts) LocalScripts(fresh.mStore);
                            break;
                        case 26:
                        {
                            auto& nodes = fresh.mInventories[1 - actor].mLists.mClothes.mList;
                            nodes.front().mWorldModel = nullptr; // Leave a stale registry Ptr deliberately.
                            nodes.clear();
                            break;
                        }
                        case 27: staged->mShirt = fresh.mInventories[1 - actor].begin(); break;
                        case 28: staged->mSelected = fresh.mInventories[1 - actor].begin(); break;
                        case 29: staged->mRegistry.begin()->second = source.mItems[actor]; break;
                        case 30: accepted.back() ^= 1; break;
                        case 31: staged->mRestored.reset(); break;
                        case 32:
                            const_cast<ESM::Clothing*>(fresh.mStore.get<ESM::Clothing>().search(
                                saved.mObjects.front().mRef.mRefID))->mScript = ESM::RefId::stringRefId("unsupported");
                            break;
                        case 33:
                            const_cast<ESM::Clothing*>(fresh.mStore.get<ESM::Clothing>().search(
                                saved.mObjects.front().mRef.mRefID))->mEnchant = ESM::RefId::stringRefId("unsupported");
                            break;
                        case 34:
                            fresh.mInventories[actor].mSlots[InventoryStore::Slot_Helmet]
                                = fresh.mInventories[1 - actor].begin();
                            break;
                        case 35:
                            fresh.mInventories[actor].setPtr(fresh.mActors[1 - actor]->getPtr(), fresh.mWorld);
                            break;
                        case 36:
                            while (fresh.mWorld.mPtrRegistry.mIndex.size() <= 132)
                                fresh.mWorld.mPtrRegistry.mIndex.emplace(
                                    ESM::RefNum{ static_cast<uint32_t>(100 + fresh.mWorld.mPtrRegistry.mIndex.size()), -1 },
                                    fresh.mItems[1 - actor]);
                            break;
                        case 37:
                            staged->mRestored->installationCandidate(fresh.mStore, saved.mActor,
                                saved.mLastGenerated).mLists.mClothes.mList.front().mBase
                                = source.mStore.get<ESM::Clothing>().search(saved.mObjects.front().mRef.mRefID);
                            break;
                    }
                    const auto before = fresh.snapshot();
                    const auto* stagedStorage = staged.get();
                    const auto acceptedValue = accepted;
                    const auto* acceptedStorage = accepted.data();
                    auto output = std::make_unique<const PlainEquipmentValues>(saved);
                    const auto* outputStorage = output.get();
                    EquipmentBytes bytes{ 'o', 'l', 'd' };
                    const auto* bytesStorage = bytes.data();
                    bool caught = false;
                    try
                    {
                        fresh.installRestart(actor, caller, { e, *content, ids }, staged, accepted, output, bytes);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                    require(caught && staged.get() == stagedStorage && accepted == acceptedValue
                            && accepted.data() == acceptedStorage && output.get() == outputStorage
                            && sameValues(*output, saved) && bytes == EquipmentBytes{ 'o', 'l', 'd' }
                            && bytes.data() == bytesStorage,
                        "equipment restart stale installation changed prior state/output");
                    fresh.unchanged(before);
                    ++rejected;
                }

            for (size_t actor = 0; actor < 2; ++actor)
                for (int test = 0; test < 9; ++test)
                {
                    PlainEquipmentFixture source;
                    PlainEquipmentValues saved;
                    source.prepare(actor, true).exportValues(source.preparationContext(actor), saved);
                    PlainEquipmentFixture fresh(actor);
                    auto e = envelope(saved.mActor);
                    const auto ids = referenceIds(saved);
                    auto witnesses = fresh.restartBindings(saved.mLastGenerated);
                    const ESMStore* restoredContent = &fresh.mStore;
                    if (test == 0)
                    {
                        saved.mObjects.front().mRef.mRefNum = fresh.mItems[1 - actor].getCellRef().getRefNum();
                        saved.mShirt = saved.mObjects.front().mRef.mRefNum;
                    }
                    if (test == 1)
                        ++witnesses.mSavedCounter.mIndex;
                    if (test == 2)
                        restoredContent = &source.mStore;
                    if (test == 3)
                        saved.mActor = fresh.mActors[1 - actor]->getPtr().getCellRef().getRefNum();
                    auto input = std::make_unique<const RestoredPlainEquipment>(
                        RestoredPlainEquipment::restore(saved, *restoredContent, saved.mActor));
                    if (test == 4)
                        input.reset();
                    if (test == 5)
                        fresh.mRestartActor.reset();
                    if (test == 6)
                    {
                        PlainEquipmentFixture expired(actor);
                        witnesses = expired.restartBindings(saved.mLastGenerated);
                    }
                    if (test == 7)
                    {
                        saved.mObjects.clear();
                        saved.mShirt = saved.mSelected = {};
                        auto other = fresh.mItems[1 - actor];
                        fresh.mWorld.mPtrRegistry.mIndex.erase(other.getCellRef().getRefNum());
                        other.getCellRef().setRefNum({ 100, -1 });
                        fresh.mWorld.registerPtr(other);
                        // Covers this actor, but not the unrelated retained item.
                        saved.mLastGenerated = saved.mActor;
                        witnesses = fresh.restartBindings(saved.mLastGenerated);
                        input = std::make_unique<const RestoredPlainEquipment>(
                            RestoredPlainEquipment::restore(saved, fresh.mStore, saved.mActor));
                    }
                    if (test == 8)
                    {
                        ManualRef item(fresh.mStore, ESM::RefId::stringRefId("equipment_shirt"));
                        fresh.mInventories[actor].addNewStack(item.getPtr(), 1);
                    }
                    const auto before = fresh.snapshot();
                    const auto* inputStorage = input.get();
                    bool caught = false;
                    try
                    {
                        fresh.stageRestart(actor, fresh.mActors[actor]->getPtr(), { e, fresh.mStore, ids }, witnesses, input);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                    require(caught && input.get() == inputStorage, "equipment restart staging accepted invalid input");
                    if (input)
                    {
                        PlainEquipmentValues retained;
                        input->exportValues(retained);
                        require(sameValues(retained, saved), "restart rejection changed detached input values");
                    }
                    fresh.unchanged(before);
                    ++rejected;
                }
            std::cout << "equipment restart stale/binding/lifetime rejections=" << rejected << '\n';
        }

        static void checkRestartReadFailures(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture source;
                source.seedValues(actor);
                PlainEquipmentValues saved;
                source.prepare(actor, true).exportValues(source.preparationContext(actor), saved);
                PlainEquipmentFixture fresh(actor);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                const EquipmentBindings bindings{ e, fresh.mStore, ids };
                const auto witnesses = fresh.restartBindings(saved.mLastGenerated);
                EquipmentBytes persisted;
                encodeEquipment(saved, bindings, persisted);
                const auto path = scratch / "read.bin";
                auto output = std::make_unique<const PlainEquipmentValues>(fresh.installedValues(actor));
                const auto* outputStorage = output.get();
                const auto outputValue = *output;
                EquipmentBytes bytes{ 'o', 'l', 'd' };
                const auto* bytesStorage = bytes.data();
                const auto before = fresh.snapshot();
                for (int test = 0; test < 12; ++test)
                {
                    auto input = persisted;
                    FileFaults faults;
                    const std::array failures{ FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read,
                        FileFault::ReadEof, FileFault::ReadClose };
                    if (test < 5)
                        faults = { failures[test], 17 };
                    if (test == 5)
                        input.resize(input.size() - 1);
                    if (test == 6)
                        input.front() ^= 1;
                    if (test == 7)
                        input.push_back('x');
                    if (test == 8)
                        input.clear();
                    if (test == 9)
                    {
                        auto bad = saved;
                        bad.mActor = fresh.mActors[1 - actor]->getPtr().getCellRef().getRefNum();
                        const auto foreign = envelope(bad.mActor);
                        encodeEquipment(bad, { foreign, fresh.mStore, ids }, input);
                    }
                    if (test == 10)
                    {
                        auto bad = e;
                        bad.mRuntime += "-foreign";
                        encodeEquipment(saved, { bad, fresh.mStore, ids }, input);
                    }
                    if (test == 11)
                    {
                        auto bad = e;
                        ++bad.mContent[0];
                        encodeEquipment(saved, { bad, fresh.mStore, ids }, input);
                    }
                    writeEquipmentInput(path, input);
                    bool caught = false;
                    try
                    {
                        caught = fresh.restartEquipment(actor, fresh.mActors[actor]->getPtr(), path,
                            bindings, witnesses, output, bytes, faults) != FileReadResult::Read;
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                    require(caught && output.get() == outputStorage && sameValues(*output, outputValue)
                            && bytes.data() == bytesStorage && bytes == EquipmentBytes{ 'o', 'l', 'd' }
                            && equipmentFileBytes(path) == input && fresh.mRestartActor == actor,
                        "equipment restart read/decode failure changed prior state/output");
                    fresh.unchanged(before);
                    ++rejected;
                }
                writeEquipmentInput(path, persisted);
                FileFaults faults;
                require(fresh.restartEquipment(actor, fresh.mActors[actor]->getPtr(), path, bindings, witnesses,
                            output, bytes, faults) == FileReadResult::Read && sameValues(*output, saved),
                    "equipment restart read/decode failure prevented safe retry");
            }
            std::cout << "equipment restart read/decode rejections=" << rejected << '\n';
        }

        static void checkRestartAllocations(const std::filesystem::path& scratch)
        {
            using namespace Allocations;
            EquipmentScratch directory(scratch);
            size_t failures = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    PlainEquipmentFixture source;
                    source.seedValues(actor);
                    if (!equip)
                        source.mInventories[actor].equip(InventoryStore::Slot_Shirt,
                            source.mInventories[actor].begin(), source.context(actor, actor));
                    PlainEquipmentValues saved;
                    source.prepare(actor, equip).exportValues(source.preparationContext(actor), saved);
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const auto path = scratch / "allocations.bin";
                    EquipmentBytes persisted;
                    encodeEquipment(saved, { e, source.mStore, ids }, persisted);
                    writeEquipmentInput(path, persisted);
                    size_t allocations = 0;
                    // A successful run measures every fallible step. Each failure
                    // then gets its own explicit fresh fixture and prior outputs.
                    for (size_t fail = 0; fail <= allocations + 1; ++fail)
                    {
                        auto fresh = std::make_unique<PlainEquipmentFixture>(actor);
                        const EquipmentBindings bindings{ e, fresh->mStore, ids };
                        const auto witnesses = fresh->restartBindings(saved.mLastGenerated);
                        const auto before = fresh->snapshot();
                        auto output = std::make_unique<const PlainEquipmentValues>(fresh->installedValues(actor));
                        const auto* outputStorage = output.get();
                        const auto outputValue = *output;
                        EquipmentBytes bytes{ 'o', 'l', 'd' };
                        const auto* bytesStorage = bytes.data();
                        FileFaults faults;
                        Trace trace;
                        bool caught = false;
                        FileReadResult outcome = FileReadResult::Unavailable;
                        {
                            Observe observe(trace, fail);
                            try
                            {
                                outcome = fresh->restartEquipment(actor, fresh->mActors[actor]->getPtr(), path,
                                    bindings, witnesses, output, bytes, faults);
                            }
                            catch (const std::exception&)
                            {
                                caught = true;
                            }
                            if (!caught)
                            {
                                // Include cleanup of installed storage/publication
                                // in tracking; successful ownership is not a leak.
                                output.reset();
                                EquipmentBytes{}.swap(bytes);
                                fresh.reset();
                            }
                        }
                        if (fail == 0)
                            allocations = trace.mTotal;
                        require(trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                            "equipment restart allocation failure or cleanup leaked");
                        if (fail > 0 && fail <= allocations)
                        {
                            require(caught && trace.mFailures == 1 && output.get() == outputStorage
                                    && sameValues(*output, outputValue) && bytes.data() == bytesStorage
                                    && bytes == EquipmentBytes{ 'o', 'l', 'd' } && fresh->mRestartActor == actor
                                    && trace.visits(Phase::Installation) == 0 && trace.visits(Phase::Publication) == 0,
                                "equipment restart allocation rejection changed prior state/output");
                            fresh->unchanged(before);
                            ++failures;
                        }
                        else
                            require(!caught && outcome == FileReadResult::Read && trace.mTotal == allocations
                                    && trace.mFailures == 0 && trace.allocations(Phase::Installation) == 0
                                    && trace.allocations(Phase::Publication) == 0 && trace.allocations(Phase::Retirement) == 0,
                                "equipment restart failed after final allocation or allocated during installation");
                        require(equipmentFileBytes(path) == persisted, "equipment restart allocation changed file");
                    }
                    require(allocations > 0, "equipment restart allocation coverage missing");
                }
            std::cout << "equipment restart individually-failed allocations=" << failures << '\n';
        }

        std::unique_ptr<Installation> stageInstallation(size_t actor, const Ptr& caller, PreparedPlainEquipment input)
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
                throw std::invalid_argument("Equipment fixture registry/effect bound exceeded");
            auto& candidate = input.installationCandidate(context, live);
            // Test listeners have fully owned, stageable semantics. Unknown
            // callbacks (including real mechanics listeners) cannot be dropped
            // or invoked after durable acceptance and are rejected visibly.
            if (live.mInventoryListener != &effects.mListener || live.mListener != &effects.mListener)
                throw std::invalid_argument("Unsupported equipment effect listener");
            if (!mItems[actor].hasLiveReference() || mItems[actor].mContainerStore != &live)
                throw std::invalid_argument("Equipment fixture item lifetime or binding changed");

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
                if (node.getCellRef().getRefNum() == mItems[actor].getCellRef().getRefNum())
                    staged->mItem = node;
            }
            using Kind = PlainEquipmentResult::EffectKind;
            for (const auto& effect : result.mEffects)
            {
                if (effect.mActor != result.mActor)
                    throw std::invalid_argument("Equipment effect owner changed");
                if (staged->mEffects.mListener.mCalls == std::numeric_limits<int>::max()
                    || staged->mEffects.mInventoryUpdates == std::numeric_limits<size_t>::max())
                    throw std::invalid_argument("Equipment test effect counter exhausted");
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

        static void checkInstallationPreparation()
        {
            size_t cases = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    PlainEquipmentFixture f;
                    if (!equip)
                        f.mInventories[actor].equip(InventoryStore::Slot_Shirt,
                            f.mInventories[actor].begin(), f.context(actor, actor));
                    f.bindEffects(actor);
                    const auto before = f.snapshot();
                    const auto expected = f.prepare(actor, equip).result();
                    const auto caller = f.mActors[actor]->getPtr();
                    auto staged = f.stageInstallation(actor, caller, f.prepare(actor, equip));
                    require(*staged->mResult == expected && staged->mEffects.mListener.mCalls == (equip ? 2 : 1)
                            && staged->mEffects.mInventoryUpdates == (equip ? 2 : 0)
                            && f.mActorEffects[actor].mListener.mCalls == 0
                            && f.mActorEffects[actor].mInventoryUpdates == 0,
                        "equipment installation preparation lost effects or mutated live consumers");
                    require(staged->mRevision == before.mRegistry.mRevision + (equip ? 1 : 0)
                            && staged->mSaved.mLastGenerated == expected.mLastGenerated,
                        "equipment installation preparation inferred counters");
                    for (const auto& item : expected.mItems)
                    {
                        const auto node = staged->mRegistry.at(item.mIdentity);
                        require(node.getCellRef().getCount(false) == item.mCount
                                && node.mContainerStore == &f.mInventories[actor] && !node.mRef->mWorldModel,
                            "equipment staged registry lost membership or attached live service");
                    }
                    f.unchanged(before);
                    ++cases;
                }
            std::cout << "equipment actor-local installation/effect preparations=" << cases << '\n';
        }

        // Test target only, serialized synchronous access; no arbitrary sink or
        // callback may reenter between final validation and file acceptance.
        // Uncertainty poisons this whole fixture, even with another file adapter.
        TestPersistenceResult commitEquipment(size_t actor, const Ptr& caller, PreparedPlainEquipment input,
            EquipmentFileSink& file, const EquipmentBindings& bindings,
            std::unique_ptr<const PlainEquipmentResult>& output, EquipmentBytes& bytes, FileFaults& faults)
        {
            using namespace Allocations;
            InPhase phase(Phase::Validation);
            if (mFailedClosed || file.failedClosed())
            {
                mFailedClosed = true;
                return TestPersistenceResult::Uncertain;
            }
            if (mRestartActor)
                throw std::invalid_argument("Equipment fresh restart fixture is not installed");
            validateCaller(actor, caller);
            const auto trusted = envelope(caller.getCellRef().getRefNum());
            if (&bindings.mContent != &mStore || bindings.mEnvelope.mActor != trusted.mActor
                || bindings.mEnvelope.mRuntime != trusted.mRuntime || bindings.mEnvelope.mContent != trusted.mContent)
                throw std::invalid_argument("Equipment commit runtime/content/actor binding changed");
            auto staged = stageInstallation(actor, caller, std::move(input));
            phase.set(Phase::Revalidation);
            auto& live = mInventories[actor];
            auto& candidate = staged->mPrepared.installationCandidate(preparationContext(actor), live);
            EquipmentBytes encoded;
            phase.set(Phase::Persistence);
            const auto outcome = file.write(staged->mSaved, bindings, encoded, faults);
            if (outcome != TestPersistenceResult::Accepted)
            {
                mFailedClosed = outcome == TestPersistenceResult::Uncertain;
                return outcome;
            }

            // Acceptance is the last fallible call. All Ptrs, iterators, map
            // capacity, effects and owned publication storage already exist.
            const auto install = [&]() noexcept {
                phase.set(Phase::Installation);
                auto& nodes = candidate.mLists.mClothes.mList;
                for (auto& node : nodes)
                    node.mWorldModel = &mWorld;
                static_assert(noexcept(nodes.swap(live.mLists.mClothes.mList)));
                nodes.swap(live.mLists.mClothes.mList);
                live.mSlots[InventoryStore::Slot_Shirt] = staged->mShirt;
                live.mSelectedEnchantItem = staged->mSelected;
                live.mRechargingItems.clear();
                live.mWeightUpToDate = live.mRechargingItemsUpToDate = false;
                live.mModified = true;
                auto& registry = mWorld.mPtrRegistry;
                static_assert(noexcept(registry.mIndex.swap(staged->mRegistry)));
                registry.mIndex.swap(staged->mRegistry);
                registry.mRevision = staged->mRevision;
                registry.mLastGenerated = staged->mSaved.mLastGenerated;
                mItems[actor] = staged->mItem;
                mNpcStats[actor].swap(staged->mPrepared.installationNpcStats());
                mActorEffects[actor].mListener.mCalls = staged->mEffects.mListener.mCalls;
                mActorEffects[actor].mListener.mRemovals.swap(staged->mEffects.mListener.mRemovals);
                mActorEffects[actor].mInventoryUpdates = staged->mEffects.mInventoryUpdates;
                mActorEffects[actor].mNotifications.swap(staged->mEffects.mNotifications);
                // Old nodes may not deregister their replacements on teardown.
                for (auto& node : nodes)
                    node.mWorldModel = nullptr;
                phase.set(Phase::Publication);
                output.swap(staged->mResult);
                bytes.swap(encoded);
                phase.set(Phase::Retirement);
                staged.reset();
            };
            install();
            return TestPersistenceResult::Accepted;
        }

        PlainEquipmentValues installedValues(size_t actor) const
        {
            const auto& inventory = mInventories[actor];
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
                it->mData.write(object, Compiler::Locals{});
                object.mHasCustomState = false;
            }
            if (mNpcStats[actor])
                result.mNpcStats = mNpcStats[actor]->values();
            return result;
        }

        static void checkDurableSuccess(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t cases = 0;
            std::unique_ptr<const PlainEquipmentResult> output;
            EquipmentBytes bytes;
            for (int variant = 0; variant < 9; ++variant)
            {
                PlainEquipmentFixture f;
                for (size_t actor = 0; actor < 2; ++actor)
                {
                    auto& inventory = f.mInventories[actor];
                    f.seedValues(actor);
                    const bool equip = variant < 3 || variant >= 6;
                    if (variant == 0 || variant == 3 || variant == 7)
                        f.mItems[actor].getCellRef().setCount(actor == 0 ? 1 : -1);
                    if (!equip || variant == 8)
                        inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                    if (variant == 5)
                        std::next(inventory.begin())->getCellRef().setSoul(ESM::RefId::stringRefId("different_soul"));
                    if (variant == 8)
                    {
                        auto shirt = *f.mItems[actor].get<ESM::Clothing>()->mBase;
                        shirt.mId = ESM::RefId::stringRefId(
                            actor == 0 ? "durable_replacement_shirt_a" : "durable_replacement_shirt_b");
                        f.mStore.insertStatic(shirt);
                        ManualRef replacement(f.mStore, shirt.mId);
                        auto item = inventory.addNewStack(replacement.getPtr(), actor == 0 ? 2 : -2);
                        f.mWorld.registerPtr(*item);
                        f.mItems[actor] = *item;
                    }
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
                    // Keep every previously installed actor identity covered.
                    if (variant == 6)
                        f.mWorld.setLastGeneratedRefNum({ std::numeric_limits<uint32_t>::max(), -10 - int(actor) });
                    else if (variant == 7)
                        f.mWorld.setLastGeneratedRefNum(
                            { std::numeric_limits<uint32_t>::max(), std::numeric_limits<int32_t>::min() });
                    else
                        f.mWorld.setLastGeneratedRefNum({ 900, -10 - int(actor) });
                    f.mWorld.mPtrRegistry.mRevision = std::numeric_limits<size_t>::max();
                    f.bindEffects(actor);
                    const auto before = f.snapshot();
                    const auto oldItem = f.mItems[actor];
                    auto prepared = f.prepare(actor, equip);
                    const auto expected = prepared.result();
                    PlainEquipmentValues saved;
                    prepared.exportValues(f.preparationContext(actor), saved);
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const EquipmentBindings bindings{ e, f.mStore, ids };
                    const auto path = scratch / (actor == 0 ? "actor-a.bin" : "actor-b.bin");
                    EquipmentFileSink file(path);
                    FileFaults faults{ FileFault::None, 17 };
                    Allocations::Trace trace;
                    TestPersistenceResult outcome;
                    {
                        Allocations::Observe observe(trace);
                        outcome = f.commitEquipment(actor, f.mActors[actor]->getPtr(), std::move(prepared),
                            file, bindings, output, bytes, faults);
                    }
                    require(outcome == TestPersistenceResult::Accepted && output && *output == expected
                            && bytes == equipmentFileBytes(path) && !oldItem.hasLiveReference()
                            && trace.allocations(Allocations::Phase::Installation) == 0
                            && trace.allocations(Allocations::Phase::Publication) == 0
                            && trace.allocations(Allocations::Phase::Retirement) == 0,
                        "equipment durable success lost publication or allocated after acceptance");
                    PlainEquipmentValues decoded;
                    decodeEquipment(bytes, bindings, decoded);
                    require(sameValues(saved, decoded) && sameValues(saved, f.installedValues(actor)),
                        "equipment durable install differs from complete persisted values");
                    size_t splits = 0, updates = 0;
                    int calls = 0;
                    std::vector<std::pair<ESM::RefNum, int>> removals;
                    for (const auto& effect : expected.mEffects)
                    {
                        using Kind = PlainEquipmentResult::EffectKind;
                        splits += effect.mKind == Kind::RegisterSplit;
                        updates += effect.mKind == Kind::InventoryUpdated;
                        calls += effect.mKind == Kind::EquipmentChanged || effect.mKind == Kind::ItemRemoved;
                        if (effect.mKind == Kind::ItemRemoved)
                            removals.emplace_back(effect.mItem, effect.mCount);
                    }
                    require(f.mWorld.getPtrRegistryRevision() == before.mRegistry.mRevision + splits
                            && f.mWorld.getLastGeneratedRefNum() == expected.mLastGenerated
                            && f.mActorEffects[actor].mListener.mCalls == calls
                            && f.mActorEffects[actor].mInventoryUpdates == updates
                            && f.mActorEffects[actor].mListener.mRemovals == removals
                            && f.mActorEffects[actor].mNotifications == std::vector<ESM::RefNum>(updates, expected.mActor)
                            && f.mScripts.snapshot() == before.mScripts && f.mEvents == before.mEvents,
                        "equipment durable install lost exact counters or stock effects");
                    for (const auto& node : inventory.mLists.mClothes.mList)
                    {
                        const auto registered = f.mWorld.getPtr(node.mRef.getRefNum());
                        require(registered.hasLiveReference() && registered.mRef == &node
                                && registered.mContainerStore == &inventory && node.mWorldModel == &f.mWorld,
                            "equipment durable registry lost active/dormant membership");
                    }
                    f.unchangedActor(before, 1 - actor);
                    const auto registry = f.mWorld.snapshotPtrRegistry();
                    for (const auto& [id, binding] : before.mRegistry.mEntries)
                        if (binding.getContainer() != &inventory)
                            require(registry.mEntries.at(id) == binding, "equipment install changed unrelated mapping");
                    ++cases;
                }
            }
            require(output && !output->mItems.empty() && !bytes.empty(),
                "equipment owned publication did not survive fixture destruction");
            std::cout << "equipment durable two-actor successes=" << cases << '\n';
        }

        static void checkCommitGuards(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (int test = 0; test < 31; ++test)
                {
                    PlainEquipmentFixture f;
                    f.bindEffects(actor);
                    f.seedValues(actor);
                    auto& inventory = f.mInventories[actor];
                    if (test == 17)
                        inventory.setInvListener(&f.mListener);
                    if (test == 18)
                        inventory.setContListener(&f.mListener);
                    if (test == 19)
                        inventory.setInvListener(nullptr);
                    auto caller = f.mActors[actor]->getPtr();
                    auto prepared = f.prepare(actor, true);
                    auto prior = f.installedValues(actor);
                    const auto ids = referenceIds(prior);
                    auto e = envelope(prior.mActor);
                    ESMStore foreignContent;
                    const ESMStore* content = &f.mStore;
                    const auto path = scratch / "guard.bin";
                    EquipmentFileSink file(path);
                    FileFaults faults;
                    EquipmentBytes priorBytes;
                    require(file.write(prior, { e, f.mStore, ids }, priorBytes, faults)
                            == TestPersistenceResult::Accepted,
                        "equipment commit guard prior file setup failed");
                    std::unique_ptr<const PlainEquipmentResult> output
                        = std::make_unique<const PlainEquipmentResult>(prepared.result());
                    const auto* outputStorage = output.get();
                    const auto outputValue = *output;
                    EquipmentBytes bytes{ 'o', 'l', 'd' };
                    const auto bytesValue = bytes;
                    const auto* bytesStorage = bytes.data();
                    std::optional<InventoryStore> alternate;
                    switch (test)
                    {
                        case 0:
                            caller = f.mActors[1 - actor]->getPtr();
                            break;
                        case 1:
                            caller.mContainerStore = &f.mInventories[1 - actor];
                            break;
                        case 2:
                            f.mActors[actor].reset();
                            break;
                        case 3:
                            f.mActors[actor] = std::make_unique<ManualRef>(f.mStore,
                                ESM::RefId::stringRefId("equipment_actor"));
                            f.mActors[actor]->getPtr().getCellRef().setRefNum(prior.mActor);
                            f.mWorld.registerPtr(f.mActors[actor]->getPtr());
                            break;
                        case 4:
                            f.mItems[actor].getCellRef().setCount(2);
                            break;
                        case 5:
                            inventory.setSelectedEnchantItem(inventory.begin());
                            break;
                        case 6:
                            inventory.mSlots[InventoryStore::Slot_Shirt] = inventory.begin();
                            break;
                        case 7:
                            f.mWorld.registerPtr(f.mItems[actor]);
                            break;
                        case 8:
                            f.mWorld.setLastGeneratedRefNum({ 99, -3 });
                            break;
                        case 9:
                            inventory = InventoryStore();
                            break;
                        case 10:
                        {
                            auto& node = inventory.mLists.mClothes.mList.front();
                            const auto base = node.mBase;
                            const auto ref = node.mRef;
                            std::destroy_at(&node);
                            std::construct_at(&node, ESM::makeBlankCellRef(), base);
                            node.mRef = ref;
                            Ptr replacement(&node, nullptr);
                            replacement.mContainerStore = &inventory;
                            f.mWorld.registerPtr(replacement);
                            break;
                        }
                        case 11:
                            std::destroy_at(&f.mScripts);
                            std::construct_at(&f.mScripts, f.mStore);
                            break;
                        case 12:
                            content = &foreignContent;
                            break;
                        case 13:
                            e.mRuntime += "_foreign";
                            break;
                        case 14:
                            e.mContent[0] ^= 1;
                            break;
                        case 15:
                            e.mActor = f.mActors[1 - actor]->getPtr().getCellRef().getRefNum();
                            break;
                        case 16:
                            inventory.setInvListener(&f.mListener);
                            break;
                        case 20:
                            inventory.mUpdatesEnabled = false;
                            break;
                        case 21:
                            inventory.mSlots[InventoryStore::Slot_Robe] = inventory.begin();
                            break;
                        case 22:
                            const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase)->mScript
                                = ESM::RefId::stringRefId("unsupported_equipment_script");
                            break;
                        case 23:
                            const_cast<ESM::Clothing*>(f.mItems[actor].get<ESM::Clothing>()->mBase)->mEnchant
                                = ESM::RefId::stringRefId("unsupported_equipment_enchantment");
                            break;
                        case 24:
                        case 25:
                        {
                            alternate.emplace();
                            alternate->setPtr(caller, f.mWorld);
                            Misc::Rng::Generator rng{ 0 };
                            alternate->fill({}, {}, rng);
                            auto item = *alternate->addNewStack(f.mItems[actor], 2);
                            item.getCellRef().unsetRefNum();
                            f.mWorld.registerPtr(item);
                            prepared = PreparedPlainEquipment::prepare(ContainerStoreResolution(*alternate, caller),
                                item, item.getCellRef().getRefNum(), f.mWorld.getPtrRegistryRevision(), true,
                                f.preparationContext(actor));
                            if (test == 25)
                            {
                                alternate.reset();
                                alternate.emplace(); // Same address, new store lifetime.
                            }
                            break;
                        }
                        case 26:
                            const_cast<PlainEquipmentResult&>(prepared.result()).mEffects.front().mKind
                                = static_cast<PlainEquipmentResult::EffectKind>(999);
                            break;
                        case 27:
                            f.mActorEffects[actor].mListener.mCalls = std::numeric_limits<int>::max();
                            break;
                        case 28:
                            f.mActorEffects[actor].mNotifications.assign(ActorEffects::MaxPending, prior.mActor);
                            break;
                        case 29:
                            f.mActorEffects[actor].mListener.mRemovals.assign(
                                ActorEffects::MaxPending, { prior.mObjects.front().mRef.mRefNum, 1 });
                            break;
                        case 30:
                            f.mActorEffects[actor].mNotifications.resize(ActorEffects::MaxPending + 1);
                            break;
                    }
                    const auto before = f.snapshot();
                    faults = {};
                    bool failed = false;
                    Allocations::Trace trace;
                    {
                        Allocations::Observe observe(trace);
                        try
                        {
                            f.commitEquipment(actor, caller, std::move(prepared), file,
                                { e, *content, ids }, output, bytes, faults);
                        }
                        catch (const std::invalid_argument& error)
                        {
                            failed = !std::string_view(error.what()).empty();
                        }
                    }
                    if (!failed)
                        std::cerr << "equipment commit guard actor=" << actor << " case=" << test << '\n';
                    require(failed && output.get() == outputStorage && *output == outputValue
                            && bytes == bytesValue && bytes.data() == bytesStorage && faults.mWrites == 0
                            && trace.visits(Allocations::Phase::Persistence) == 0
                            && trace.visits(Allocations::Phase::Installation) == 0
                            && trace.visits(Allocations::Phase::Publication) == 0
                            && !file.failedClosed() && !f.mFailedClosed && equipmentFileBytes(path) == priorBytes,
                        "equipment commit stale/binding/effect rejection changed prior state or output");
                    f.unchanged(before);
                    ++rejected;
                }
            std::cout << "equipment commit stale/binding/lifetime/effect rejections=" << rejected << '\n';
        }

        static void checkCommitPersistence(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t safe = 0, uncertain = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                    for (auto failure : { FileFault::Create, FileFault::Write, FileFault::Flush, FileFault::Close,
                             FileFault::Replace, FileFault::ReplaceError, FileFault::AfterReplace, FileFault::Barrier,
                             FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read, FileFault::ReadEof,
                             FileFault::ReadClose })
                    {
                        PlainEquipmentFixture f;
                        f.seedValues(actor);
                        if (!equip)
                            f.mInventories[actor].equip(InventoryStore::Slot_Shirt,
                                f.mInventories[actor].begin(), f.context(actor, actor));
                        f.bindEffects(actor);
                        f.bindEffects(1 - actor);
                        auto prepared = f.prepare(actor, equip);
                        PlainEquipmentValues saved;
                        prepared.exportValues(f.preparationContext(actor), saved);
                        const auto prior = f.installedValues(actor);
                        const auto ids = referenceIds(saved);
                        const auto e = envelope(saved.mActor);
                        const EquipmentBindings bindings{ e, f.mStore, ids };
                        const auto path = scratch / "persistence.bin";
                        EquipmentFileSink file(path);
                        FileFaults faults;
                        EquipmentBytes priorBytes, newBytes;
                        require(file.write(prior, bindings, priorBytes, faults) == TestPersistenceResult::Accepted,
                            "equipment commit persistence prior file setup failed");
                        encodeEquipment(saved, bindings, newBytes);
                        require(priorBytes != newBytes, "equipment commit persistence needs distinct complete files");
                        EquipmentBytes bytes{ 'o', 'l', 'd' };
                        const auto bytesValue = bytes;
                        const auto* bytesStorage = bytes.data();
                        auto output = std::make_unique<const PlainEquipmentResult>(prepared.result());
                        const auto* outputStorage = output.get();
                        const auto outputValue = *output;
                        const auto before = f.snapshot();
                        const auto caller = f.mActors[actor]->getPtr();
                        faults = { failure, 17 };
                        Allocations::Trace trace;
                        TestPersistenceResult outcome;
                        {
                            Allocations::Observe observe(trace);
                            outcome = f.commitEquipment(actor, caller, std::move(prepared), file,
                                bindings, output, bytes, faults);
                        }
                        const bool poisoned = failure >= FileFault::ReplaceError;
                        const auto actual = equipmentFileBytes(path);
                        const auto unchanged = [&] {
                            require(output.get() == outputStorage && *output == outputValue
                                    && bytes == bytesValue && bytes.data() == bytesStorage,
                                "equipment commit persistence changed prior publication");
                            f.unchanged(before);
                        };
                        require(outcome == (poisoned ? TestPersistenceResult::Uncertain : TestPersistenceResult::Rejected)
                                && file.failedClosed() == poisoned && f.mFailedClosed == poisoned
                                && trace.visits(Allocations::Phase::Installation) == 0
                                && trace.visits(Allocations::Phase::Publication) == 0
                                && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                                && actual == (failure > FileFault::ReplaceError ? newBytes : priorBytes)
                                && !std::filesystem::exists(std::filesystem::path(path).concat(".tmp")),
                            "equipment commit persistence failure installed effects or left incomplete bytes");
                        unchanged();
                        if (poisoned)
                        {
                            // Both the used adapter and a fresh adapter are blocked;
                            // another actor cannot bypass fixture-wide uncertainty.
                            EquipmentFileSink fresh(path);
                            for (bool freshAdapter : { false, true })
                            {
                                auto other = f.prepare(1 - actor, true);
                                Allocations::Trace retry;
                                faults = {};
                                {
                                    Allocations::Observe observe(retry, 1);
                                    outcome = f.commitEquipment(1 - actor, f.mActors[1 - actor]->getPtr(),
                                        std::move(other), freshAdapter ? fresh : file, bindings, output, bytes, faults);
                                }
                                require(outcome == TestPersistenceResult::Uncertain && retry.mTotal == 0
                                        && faults.mWrites == 0 && equipmentFileBytes(path) == actual,
                                    "equipment uncertain fixture allowed a retry through another actor/sink");
                                unchanged();
                            }
                            std::unique_ptr<const RestoredPlainEquipment> restored;
                            require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Read,
                                "equipment uncertain file did not restore detached");
                            PlainEquipmentValues values;
                            restored->exportValues(values);
                            require(sameValues(values, failure == FileFault::ReplaceError ? prior : saved),
                                "equipment uncertain file did not contain complete prior/new values");
                            PlainEquipmentFixture recovered(actor);
                            const EquipmentBindings rebound{ e, recovered.mStore, ids };
                            const auto witnesses = recovered.restartBindings(values.mLastGenerated);
                            std::unique_ptr<const PlainEquipmentValues> recoveredOutput;
                            EquipmentBytes recoveredBytes;
                            const auto recoveredBefore = recovered.snapshot();
                            require(recovered.restartEquipment(actor, recovered.mActors[actor]->getPtr(), path,
                                        rebound, witnesses, recoveredOutput, recoveredBytes, faults) == FileReadResult::Read
                                    && sameValues(recovered.installedValues(actor), values)
                                    && sameValues(*recoveredOutput, values) && recoveredBytes == actual
                                    && recovered.mActorEffects[actor].mListener.mCalls == 0
                                    && recovered.mActorEffects[actor].mInventoryUpdates == 0,
                                "uncertain equipment commit did not install complete prior/new file in fresh fixture");
                            recovered.unchangedActor(recoveredBefore, 1 - actor);
                            bool blocked = false;
                            try
                            {
                                f.restartEquipment(actor, caller, path, bindings, witnesses, recoveredOutput,
                                    recoveredBytes, faults);
                            }
                            catch (const std::invalid_argument&)
                            {
                                blocked = true;
                            }
                            require(blocked && f.mFailedClosed, "equipment restart resumed uncertain old fixture");
                            unchanged();
                            ++uncertain;
                        }
                        else
                        {
                            faults = {};
                            require(f.commitEquipment(actor, caller, f.prepare(actor, equip), file,
                                        bindings, output, bytes, faults) == TestPersistenceResult::Accepted
                                    && sameValues(saved, f.installedValues(actor)),
                                "equipment safe persistence rejection prevented valid retry");
                            ++safe;
                        }
                    }
            std::cout << "equipment commit safe persistence failures=" << safe << " uncertain=" << uncertain << '\n';
        }

        static void checkCommitAllocations(const std::filesystem::path& scratch)
        {
            using namespace Allocations;
            EquipmentScratch directory(scratch);
            size_t failures = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                {
                    auto f = std::make_unique<PlainEquipmentFixture>();
                    f->seedValues(actor);
                    if (!equip)
                        f->mInventories[actor].equip(InventoryStore::Slot_Shirt,
                            f->mInventories[actor].begin(), f->context(actor, actor));
                    f->bindEffects(actor);
                    PlainEquipmentValues saved;
                    f->prepare(actor, equip).exportValues(f->preparationContext(actor), saved);
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const EquipmentBindings bindings{ e, f->mStore, ids };
                    const auto path = scratch / "allocations.bin";
                    EquipmentFileSink file(path);
                    EquipmentBytes priorBytes;
                    FileFaults faults;
                    require(file.write(f->installedValues(actor), bindings, priorBytes, faults)
                            == TestPersistenceResult::Accepted,
                        "equipment commit allocation prior file setup failed");
                    auto output = std::make_unique<const PlainEquipmentResult>(f->prepare(1 - actor, true).result());
                    const auto* outputStorage = output.get();
                    const auto outputValue = *output;
                    EquipmentBytes bytes{ 'o', 'l', 'd' };
                    const auto* bytesStorage = bytes.data();
                    const auto bytesValue = bytes;
                    const auto caller = f->mActors[actor]->getPtr();
                    const auto run = [&] {
                        InPhase phase(Phase::Preparation);
                        return f->commitEquipment(actor, caller, f->prepare(actor, equip),
                            file, bindings, output, bytes, faults);
                    };
                    faults = { FileFault::Create };
                    require(run() == TestPersistenceResult::Rejected, "equipment allocation warmup failed");
                    const auto before = f->snapshot();
                    Trace count;
                    TestPersistenceResult outcome;
                    {
                        Observe observe(count);
                        outcome = run();
                    }
                    require(outcome == TestPersistenceResult::Rejected && count.mTotal > 0 && count.mOutstanding == 0
                            && count.mTrackingOverflow == 0, "equipment commit allocation baseline leaked");
                    for (size_t fail = 1; fail <= count.mTotal; ++fail)
                    {
                        Trace trace;
                        bool rejected = false;
                        faults = {};
                        {
                            Observe observe(trace, fail);
                            try
                            {
                                run();
                            }
                            catch (const std::exception&)
                            {
                                rejected = true;
                            }
                        }
                        if (!rejected || trace.mFailures != 1 || trace.mOutstanding != 0)
                            std::cerr << "equipment commit allocation actor=" << actor << " equip=" << equip
                                      << " fail=" << fail << " injected=" << trace.mFailures
                                      << " outstanding=" << trace.mOutstanding << '\n';
                        require(rejected && trace.mFailures == 1 && trace.mOutstanding == 0
                                && trace.mTrackingOverflow == 0 && trace.visits(Phase::Installation) == 0
                                && trace.visits(Phase::Publication) == 0 && faults.mWrites == 0
                                && !file.failedClosed() && !f->mFailedClosed
                                && output.get() == outputStorage && *output == outputValue
                                && bytes.data() == bytesStorage && bytes == bytesValue
                                && equipmentFileBytes(path) == priorBytes,
                            "equipment commit allocation failure changed prior state/output or leaked");
                        f->unchanged(before);
                        ++failures;
                    }
                    Trace success;
                    faults = {};
                    {
                        Observe observe(success, count.mTotal + 1);
                        outcome = run();
                        output.reset();
                        EquipmentBytes{}.swap(bytes);
                        f.reset();
                    }
                    require(outcome == TestPersistenceResult::Accepted && success.mTotal == count.mTotal
                            && success.mFailures == 0 && success.mOutstanding == 0 && success.mTrackingOverflow == 0
                            && success.allocations(Phase::Installation) == 0
                            && success.allocations(Phase::Publication) == 0 && success.allocations(Phase::Retirement) == 0,
                        "equipment commit allocation retry grew, leaked or allocated after acceptance");
                }
            std::cout << "equipment commit allocation failures=" << failures << " remaining-after-cleanup=0\n";
        }

        explicit PlainEquipmentFixture(std::optional<size_t> restartActor = {})
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
                ContainerStoreResolution witness(inventory, actor);
            }
            if (restartActor)
            {
                auto& target = mInventories.at(*restartActor);
                mItems[*restartActor] = Ptr();
                target.mLists.mClothes.mList.clear();
                mRestartActor = restartActor;
                bindEffects(0);
                bindEffects(1);
            }
        }

        void enableLuck(bool seedRuntimeStats = true)
        {
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                ESM::Attribute attribute;
                attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
                mStore.insertStatic(attribute);
            }
            for (int i = 0; i < ESM::Skill::Length; ++i)
            {
                ESM::Skill skill;
                skill.blank();
                skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
                mStore.insertStatic(skill);
            }
            ESM::GameSetting multiplier;
            multiplier.mId = ESM::RefId::stringRefId("fNPCbaseMagickaMult");
            multiplier.mValue = ESM::Variant(2.f);
            mStore.insertStatic(multiplier);
            auto* npc = const_cast<ESM::NPC*>(mActors[0]->getPtr().get<ESM::NPC>()->mBase);
            npc->mNpdtType = ESM::NPC::NPC_DEFAULT;
            for (size_t i = 0; i < npc->mNpdt.mAttributes.size(); ++i)
                npc->mNpdt.mAttributes[i] = static_cast<unsigned char>(33 + i);
            for (size_t i = 0; i < npc->mNpdt.mSkills.size(); ++i)
                npc->mNpdt.mSkills[i] = static_cast<unsigned char>(10 + i);
            npc->mNpdt.mHealth = 120;
            npc->mNpdt.mMana = 71;
            npc->mNpdt.mFatigue = 155;
            npc->mNpdt.mLevel = 7;
            npc->mNpdt.mDisposition = 48;
            npc->mNpdt.mReputation = 12;
            ESM::MagicEffect magic;
            magic.blank();
            magic.mId = ESM::MagicEffect::FortifyAttribute;
            magic.mData.mFlags = ESM::MagicEffect::AppliedOnce | ESM::MagicEffect::TargetAttribute;
            mStore.insertStatic(magic);
            ESM::Enchantment enchantment;
            enchantment.blank();
            enchantment.mId = ESM::RefId::stringRefId("equipment_luck");
            enchantment.mData.mType = ESM::Enchantment::ConstantEffect;
            enchantment.mEffects.populate({ { ESM::MagicEffect::FortifyAttribute, {}, ESM::Attribute::Luck,
                ESM::RT_Self, 0, 0, 9, 9 } });
            mStore.insertStatic(enchantment);
            auto* shirt = const_cast<ESM::Clothing*>(mStore.get<ESM::Clothing>().find(ESM::RefId::stringRefId("equipment_shirt")));
            shirt->mEnchant = enchantment.mId;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                mNpcStats[actor] = std::make_shared<EquipmentNpcStats>(mActors[actor]->getPtr(), mStore);
                const auto& initialized = mNpcStats[actor]->stats();
                require(initialized.getMagicka().getBase() == 71 && initialized.getFatigue().getCurrent() == 155
                        && initialized.getHealth().getCurrent() == 120 && initialized.getLevel() == 7
                        && initialized.getBaseDisposition() == 48 && initialized.getReputation() == 12,
                    "Explicit NPC initialization lost NPDT dynamic or NPC-specific stats");
                for (int i = 0; i < ESM::Skill::Length; ++i)
                    require(initialized.getSkill(ESM::Skill::indexToRefId(i)).getBase() == 10 + i,
                        "Explicit NPC initialization lost stock skills");
                mNpcStats[actor]->mStats.setAttribute(ESM::Attribute::Luck, actor == 0 ? 40.f : 65.f);
                if (seedRuntimeStats)
                {
                    auto& stats = mNpcStats[actor]->mStats;
                    for (int i = 0; i < ESM::Attribute::Length; ++i)
                    {
                        const auto id = ESM::Attribute::indexToRefId(i);
                        if (id == ESM::Attribute::Luck)
                            continue;
                        auto attribute = stats.getAttribute(id);
                        attribute.setBase(50.f + 13.f * actor + i + 0.25f);
                        attribute.setModifier(1.5f + i);
                        attribute.damage(0.5f + actor);
                        stats.setAttribute(id, attribute, 2.f);
                    }
                    // Include above-maximum magicka and negative fatigue; these
                    // are valid stock dynamic values and must not be clamped.
                    stats.setHealth({ 120.f + actor, -7.f, 88.25f + actor });
                    stats.setMagicka({ 71.f + actor, 5.f, 220.5f + actor });
                    stats.setFatigue({ 155.f + actor, -3.f, -12.5f - actor });
                }
                bindEffects(actor);
            }
        }

        void checkLuck(size_t actor, bool equipped) const
        {
            const auto& stats = mNpcStats[actor]->mStats;
            const auto& luck = stats.getAttribute(ESM::Attribute::Luck);
            const auto expected = equipped ? 9.f : 0.f;
            require(luck.getModified() == (actor == 0 ? 40.f : 65.f) + expected
                    && stats.getMagicEffects().getOrDefault(
                        MWMechanics::EffectKey(ESM::MagicEffect::FortifyAttribute, ESM::Attribute::Luck)).getMagnitude() == expected,
                "Constant effect did not change the intended actor's gameplay attribute");
            const auto& spells = stats.getActiveSpells();
            require(std::distance(spells.begin(), spells.end()) == (equipped ? 1 : 0),
                "Constant effect applied more than once or survived removal");
            if (equipped)
                require(spells.begin()->getCaster() == mActors[actor]->getPtr().getCellRef().getRefNum()
                        && spells.begin()->getItem() == mInventories[actor].getSlot(InventoryStore::Slot_Shirt)->getCellRef().getRefNum()
                        && spells.begin()->getEffects().front().mMagnitude == 9,
                    "Constant effect ownership/item identity mismatch");
        }

        static void checkEnchanted(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            PlainEquipmentFixture f;
            f.enableLuck();
            size_t commits = 0;
            for (bool equip : { true, false, true })
                for (size_t actor = 0; actor < 2; ++actor)
                {
                    const auto before = f.snapshot();
                    auto expectedStats = f.mNpcStats[actor]->values();
                    expectedStats.mAttributes[7][1] = equip ? 9.f : 0.f;
                    {
                        auto prepared = f.prepareContinuation(actor, equip);
                        PlainEquipmentValues staged;
                        prepared.exportValues(f.preparationContext(actor), staged);
                        require(staged.mNpcStats == expectedStats,
                            "NPC preparation changed unrelated attributes or dynamic values");
                        f.unchanged(before);
                    }
                    const auto path = scratch / (actor == 0 ? "a.bin" : "b.bin");
                    EquipmentFileSink sink(path);
                    auto command = f.equipmentCommand(actor, equip);
                    const auto e = envelope(f.mActors[actor]->getPtr().getCellRef().getRefNum());
                    const auto ids = referenceIds(f.installedValues(actor));
                    const EquipmentBindings bindings{ e, f.mStore, ids };
                    std::unique_ptr<const EquipmentSuccess> output;
                    EquipmentBytes bytes;
                    FileFaults faults;
                    require(executeEquipment(f, { command.mActor }, command, sink, bindings, output, bytes, faults)
                            == TestPersistenceResult::Accepted && output && output->mCommand == command,
                        "Enchanted equipment command failed");
                    f.checkLuck(actor, equip);
                    require(f.mNpcStats[actor]->values() == expectedStats,
                        "NPC installation changed unrelated attributes or dynamic values");
                    require(output->mLuck == std::optional{ expectedStats.mAttributes[7] },
                        "Owned success lost Luck values");
                    f.unchangedActor(before, 1 - actor);
                    // A mechanics refresh on the same equipped item must not add its modifier again.
                    f.mNpcStats[actor]->mStats.getActiveSpells().updateConstantFortifyLuck(
                        f.mActors[actor]->getPtr(), f.mInventories[actor], f.mStore, f.mNpcStats[actor]->mStats);
                    f.checkLuck(actor, equip);
                    PlainEquipmentValues decoded;
                    decodeEquipment(bytes, bindings, decoded);
                    require(sameValues(decoded, f.installedValues(actor)), "Enchanted save lost gameplay state");
                    ++commits;
                }
            std::cout << "constant Fortify Luck: actor A 40->49->40->49, actor B 65->74->65->74; isolated commits="
                << commits << '\n';
        }

        TestPersistenceResult luckCommand(size_t actor, bool equip, const std::filesystem::path& path,
            std::unique_ptr<const EquipmentSuccess>& output, EquipmentBytes& bytes, FileFaults& faults)
        {
            EquipmentFileSink sink(path);
            const auto command = equipmentCommand(actor, equip);
            const auto e = envelope(mActors[actor]->getPtr().getCellRef().getRefNum());
            const auto ids = referenceIds(installedValues(actor));
            return executeEquipment(*this, { command.mActor }, command, sink, { e, mStore, ids }, output, bytes, faults);
        }

        static void checkEnchantedGuards(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            const auto path = scratch / "guards.bin";
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (int test = 0; test < 11; ++test)
                {
                    PlainEquipmentFixture f;
                    f.enableLuck();
                    std::unique_ptr<const EquipmentSuccess> output;
                    EquipmentBytes bytes;
                    FileFaults faults;
                    require(f.luckCommand(actor, true, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                        "Enchanted guard setup failed");
                    const auto priorBytes = bytes;
                    const auto* priorOutput = output.get();
                    const auto priorValue = *output;
                    const auto* priorStorage = bytes.data();
                    const auto e = envelope(f.mActors[actor]->getPtr().getCellRef().getRefNum());
                    const auto ids = referenceIds(f.installedValues(actor));
                    EquipmentFileSink sink(path);
                    auto command = f.equipmentCommand(actor, false);
                    auto caller = EquipmentCaller{ command.mActor };
                    auto prepared = f.prepare(actor, false);
                    auto* enchantment = const_cast<ESM::Enchantment*>(f.mStore.get<ESM::Enchantment>().find(
                        ESM::RefId::stringRefId("equipment_luck")));
                    switch (test)
                    {
                        case 0: caller.mActor = ownedId(f.mActors[1 - actor]->getPtr().getCellRef().getRefNum()); break;
                        case 1: --command.mExpectedRevision; break;
                        case 2: enchantment->mEffects.mList.front().mData.mMagnMax = 10; break;
                        case 3: enchantment->mEffects.mList.front().mData.mRange = ESM::RT_Target; break;
                        case 4: enchantment->mEffects.mList.front().mData.mAttribute = ESM::Attribute::Strength; break;
                        case 5: f.mNpcStats[actor]->mStats.setAttribute(ESM::Attribute::Luck, 80.f); break;
                        case 6: f.mNpcStats[actor]->mStats.getActiveSpells() = MWMechanics::ActiveSpells{}; break;
                        case 7: f.mNpcStats[actor]->mStats.getMagicEffects().add(
                            { ESM::MagicEffect::FortifyAttribute, ESM::Attribute::Luck }, MWMechanics::EffectParam(1)); break;
                        case 8: f.mNpcStats[actor] = f.mNpcStats[1 - actor]; break;
                        case 9: f.mNpcStats[actor]->mStats.setAttribute(ESM::Attribute::Speed, 83.f); break;
                        case 10: f.mNpcStats[actor]->mStats.setMagicka({ 79.f, 2.f, 11.f }); break;
                    }
                    const auto before = f.snapshot();
                    faults = {};
                    bool caught = false;
                    try
                    {
                        if (test >= 5)
                        {
                            std::unique_ptr<const PlainEquipmentResult> internal;
                            f.commitEquipment(actor, f.mActors[actor]->getPtr(), std::move(prepared), sink,
                                { e, f.mStore, ids }, internal, bytes, faults);
                        }
                        else
                            executeEquipment(f, caller, command, sink, { e, f.mStore, ids }, output, bytes, faults);
                    }
                    catch (const std::invalid_argument&) { caught = true; }
                    if (!caught)
                        std::cerr << "enchanted guard accepted actor=" << actor << " case=" << test << '\n';
                    require(caught && output.get() == priorOutput && *output == priorValue && bytes == priorBytes
                            && bytes.data() == priorStorage && faults.mWrites == 0 && equipmentFileBytes(path) == priorBytes,
                        "Enchanted validation failure changed publication/file");
                    f.unchanged(before);
                    ++rejected;
                }
            // Version 1 stays plain; the NPC format cannot lie about the consequence.
            PlainEquipmentFixture f;
            f.enableLuck();
            const auto good = f.installedValues(0);
            const auto e = envelope(good.mActor);
            const auto ids = referenceIds(good);
            EquipmentBytes encoded;
            encodeEquipment(good, { e, f.mStore, ids }, encoded);
            for (int test = 0; test < 6; ++test)
            {
                auto bad = good;
                if (test == 0) bad.mNpcStats.reset();
                if (test == 1) bad.mNpcStats->mAttributes[7][1] = 9;
                if (test == 2) bad.mNpcStats->mAttributes[7][0] = std::numeric_limits<float>::quiet_NaN();
                if (test == 3) bad.mNpcStats->mAttributes[0][2] = -1;
                if (test == 4) bad.mNpcStats->mDynamic[1][2] = std::numeric_limits<float>::infinity();
                if (test == 5) bad.mNpcStats->mDynamic[0][2] = 0;
                auto output = encoded;
                bool caught = false;
                try { encodeEquipment(bad, { e, f.mStore, ids }, output); }
                catch (const std::invalid_argument&) { caught = true; }
                require(caught && output == encoded, "Invalid NPC stat save was encoded");
                ++rejected;
            }
            for (int test = 0; test < 5; ++test)
            {
                auto bad = encoded;
                const std::string tag = test < 2 ? "FVER" : test == 2 ? "ATTR" : "DYNA";
                const auto it = std::search(bad.begin(), bad.end(), tag.begin(), tag.end());
                require(it != bad.end(), "Missing enchanted codec fixture field");
                const auto offset = static_cast<size_t>(it - bad.begin()) + 8;
                if (test < 2)
                    bad[offset] = test == 0 ? 1 : 2; // Reject both reinterpretation and retired Luck-only format.
                else
                {
                    const auto number = std::bit_cast<uint32_t>(test == 2 ? 9.f
                        : test == 3 ? std::numeric_limits<float>::infinity() : 0.f);
                    const auto fieldOffset = test == 2 ? 7 * 12 + 4 : test == 3 ? 12 + 8 : 8;
                    for (size_t byte = 0; byte < 4; ++byte)
                        bad[offset + fieldOffset + byte] = static_cast<char>(number >> (8 * byte));
                }
                auto output = good;
                const auto* storage = output.mObjects.data();
                bool caught = false;
                try { decodeEquipment(bad, { e, f.mStore, ids }, output); }
                catch (const std::invalid_argument&) { caught = true; }
                require(caught && sameValues(output, good) && output.mObjects.data() == storage,
                    "Enchanted version/consequence byte rejection changed output");
                ++rejected;
            }
            // With no equipped effect, ownership must come from the stat
            // context itself, never from an ActiveSpells caster as a proxy.
            for (int test = 0; test < 4; ++test)
            {
                PlainEquipmentFixture owner;
                owner.enableLuck();
                const auto actor = owner.mActors[0]->getPtr();
                if (test == 0)
                    owner.mNpcStats[0] = owner.mNpcStats[1];
                if (test == 1)
                    actor.getCellRef().setRefNum({ 900, -1 });
                if (test == 2)
                    const_cast<ESM::NPC*>(actor.get<ESM::NPC>()->mBase)->mNpdtType
                        = ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS;
                if (test == 3)
                    const_cast<ESM::GameSetting*>(owner.mStore.get<ESM::GameSetting>().find("fNPCbaseMagickaMult"))
                        ->mValue.setFloat(3.f);
                const auto before = owner.snapshot();
                bool caught = false;
                try { owner.mNpcStats[0]->validate(actor, owner.mStore); }
                catch (const std::invalid_argument&) { caught = true; }
                require(caught, "Unequipped NPC stat context accepted changed owner/content");
                owner.unchanged(before);
                ++rejected;
            }
            // Reuse restart staging/installation: no new recovery fixture or
            // rollback path for saved-base mismatch and stale dynamic stats.
            for (int test = 0; test < 3; ++test)
            {
                const bool wrongBase = test == 0;
                PlainEquipmentFixture fresh(0);
                fresh.enableLuck(false);
                auto saved = good;
                if (wrongBase)
                {
                    auto npc = *fresh.mActors[0]->getPtr().get<ESM::NPC>()->mBase;
                    npc.mId = ESM::RefId::stringRefId("other_equipment_npc");
                    fresh.mStore.insertStatic(npc);
                    saved.mNpcStats->mBase = npc.mId;
                }
                const auto savedIds = referenceIds(saved);
                const EquipmentBindings bindings{ e, fresh.mStore, savedIds };
                auto detached = std::make_unique<const RestoredPlainEquipment>(
                    RestoredPlainEquipment::restore(saved, fresh.mStore, saved.mActor));
                const auto witness = fresh.restartBindings(saved.mLastGenerated);
                std::unique_ptr<RestartInstallation> staged;
                auto output = std::make_unique<const PlainEquipmentValues>(good);
                const auto* outputStorage = output.get();
                EquipmentBytes accepted, bytes{ 'o', 'l', 'd' };
                encodeEquipment(saved, bindings, accepted);
                const auto acceptedValue = accepted;
                const auto* acceptedStorage = accepted.data();
                const auto* bytesStorage = bytes.data();
                if (!wrongBase)
                {
                    staged = fresh.stageRestart(0, fresh.mActors[0]->getPtr(), bindings, witness, detached);
                    if (test == 1)
                        fresh.mNpcStats[0]->mStats.setFatigue({ 100.f, 3.f, -4.f });
                    else
                        staged->mNpcStats = fresh.mNpcStats[1];
                }
                const auto before = fresh.snapshot();
                bool caught = false;
                try
                {
                    if (wrongBase)
                        fresh.stageRestart(0, fresh.mActors[0]->getPtr(), bindings, witness, detached);
                    else
                        fresh.installRestart(0, fresh.mActors[0]->getPtr(), bindings, staged, accepted, output, bytes);
                }
                catch (const std::invalid_argument&) { caught = true; }
                require(caught && (wrongBase ? static_cast<bool>(detached) : static_cast<bool>(staged))
                        && output.get() == outputStorage && sameValues(*output, good)
                        && accepted == acceptedValue && accepted.data() == acceptedStorage
                        && bytes == EquipmentBytes{ 'o', 'l', 'd' } && bytes.data() == bytesStorage,
                    "NPC restart rejection consumed input or changed publication");
                fresh.unchanged(before);
                ++rejected;
            }
            std::cout << "enchanted preparation/content/actor/stat/save rejections=" << rejected << '\n';
        }

        static void checkEnchantedDurability(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            const auto path = scratch / "durable.bin";
            size_t failures = 0, recoveries = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                    for (auto failure : { FileFault::Flush, FileFault::ReplaceError, FileFault::AfterReplace })
                    {
                        PlainEquipmentValues prior, proposed;
                        EquipmentBytes actual;
                        bool restoredEquipped = false;
                        {
                            PlainEquipmentFixture f;
                            f.enableLuck();
                            std::unique_ptr<const EquipmentSuccess> output;
                            EquipmentBytes bytes;
                            FileFaults faults;
                            // Ensure both possible recovered files are complete NPC stat states.
                            if (equip)
                            {
                                require(f.luckCommand(actor, true, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                                    "Enchanted durability setup equip failed");
                                faults = {};
                                require(f.luckCommand(actor, false, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                                    "Enchanted durability setup unequip failed");
                            }
                            else
                                require(f.luckCommand(actor, true, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                                    "Enchanted durability setup failed");
                            prior = f.installedValues(actor);
                            const auto command = f.equipmentCommand(actor, equip);
                            const auto item = f.mWorld.getPtr({ command.mItem.mIndex, command.mItem.mContentFile });
                            auto prepared = PreparedPlainEquipment::prepare(
                                ContainerStoreResolution(f.mInventories[actor], f.mActors[actor]->getPtr()), item,
                                item.getCellRef().getRefNum(), f.mWorld.getPtrRegistryRevision(), equip, f.preparationContext(actor));
                            prepared.exportValues(f.preparationContext(actor), proposed);
                            const auto before = f.snapshot();
                            const auto* outputStorage = output.get();
                            const auto outputValue = *output;
                            const auto* bytesStorage = bytes.data();
                            const auto priorBytes = bytes;
                            faults = { failure, 17 };
                            const auto outcome = f.luckCommand(actor, equip, path, output, bytes, faults);
                            require(outcome == (failure == FileFault::Flush ? TestPersistenceResult::Rejected : TestPersistenceResult::Uncertain)
                                    && output.get() == outputStorage && *output == outputValue
                                    && bytes.data() == bytesStorage && bytes == priorBytes,
                                "Failed enchanted durability published success");
                            f.unchanged(before);
                            f.checkLuck(actor, !equip);
                            require(!std::filesystem::exists(path.string() + ".tmp"), "Enchanted failure left staging file");
                            ++failures;
                            if (failure == FileFault::Flush)
                            {
                                require(equipmentFileBytes(path) == priorBytes, "Safe failure replaced enchanted save");
                                faults = {};
                                require(f.luckCommand(actor, equip, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                                    "Safe enchanted retry failed");
                                f.checkLuck(actor, equip);
                            }
                            else
                            {
                                for (size_t blockedActor = 0; blockedActor < 2; ++blockedActor)
                                {
                                    const auto blocked = f.equipmentCommand(blockedActor, blockedActor == actor ? equip : true);
                                    EquipmentFileSink freshSink(path);
                                    const auto e = envelope(f.mActors[blockedActor]->getPtr().getCellRef().getRefNum());
                                    const auto ids = referenceIds(f.installedValues(blockedActor));
                                    faults = {};
                                    require(executeEquipment(f, { blocked.mActor }, blocked, freshSink,
                                                { e, f.mStore, ids }, output, bytes, faults) == TestPersistenceResult::Uncertain
                                            && faults.mWrites == 0, "Uncertain enchanted fixture accepted another command");
                                }
                                f.unchanged(before);
                            }
                            actual = equipmentFileBytes(path);
                            restoredEquipped = failure == FileFault::ReplaceError ? !equip : equip;
                        } // Destroy all old actors, stats, preparations and sinks before recovery.
                        PlainEquipmentFixture fresh(actor);
                        fresh.enableLuck(false);
                        const auto expected = failure == FileFault::ReplaceError ? prior : proposed;
                        require(fresh.mNpcStats[actor]->values() != *expected.mNpcStats,
                            "Restart fixture accidentally contains the saved runtime stats");
                        const auto e = envelope(expected.mActor);
                        const auto ids = referenceIds(expected);
                        const EquipmentBindings bindings{ e, fresh.mStore, ids };
                        const auto witness = fresh.restartBindings(expected.mLastGenerated);
                        std::unique_ptr<const PlainEquipmentValues> restored;
                        EquipmentBytes bytes;
                        FileFaults faults;
                        const auto other = fresh.snapshot();
                        require(fresh.restartEquipment(actor, fresh.mActors[actor]->getPtr(), path, bindings, witness,
                                    restored, bytes, faults) == FileReadResult::Read
                                && sameValues(*restored, expected) && sameValues(fresh.installedValues(actor), expected)
                                && bytes == actual && fresh.mActorEffects[actor].mListener.mCalls == 0
                                && fresh.mActorEffects[actor].mNotifications.empty(),
                            "Enchanted restart changed durable consequence or replayed publication");
                        fresh.checkLuck(actor, restoredEquipped);
                        fresh.unchangedActor(other, 1 - actor);
                        std::unique_ptr<const EquipmentSuccess> output;
                        faults = {};
                        require(fresh.luckCommand(actor, !restoredEquipped, path, output, bytes, faults)
                                == TestPersistenceResult::Accepted, "Enchanted recovery continuation failed");
                        fresh.checkLuck(actor, !restoredEquipped);
                        ++recoveries;
                    }
            std::cout << "enchanted durability failures=" << failures << " fresh recoveries/continuations=" << recoveries << '\n';
        }

        static void checkEnchantedAllocations(const std::filesystem::path& scratch)
        {
            using namespace Allocations;
            EquipmentScratch directory(scratch);
            const auto path = scratch / "allocations.bin";
            size_t failures = 0, retries = 0;
            // Equip and unequip differ in effect insertion/removal; restart owns
            // a newly reconstructed stats object. Sweep each new path once.
            for (int mode = 0; mode < 3; ++mode)
            {
                size_t allocations = 0;
                for (size_t fail = 0; fail <= allocations + 1; ++fail)
                {
                    auto f = std::make_unique<PlainEquipmentFixture>();
                    f->enableLuck();
                    std::unique_ptr<const EquipmentSuccess> output;
                    EquipmentBytes bytes;
                    FileFaults faults;
                    require(f->luckCommand(0, true, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                        "Allocation fixture initial equip failed");
                    if (mode == 0)
                    {
                        faults = {};
                        require(f->luckCommand(0, false, path, output, bytes, faults) == TestPersistenceResult::Accepted,
                            "Allocation fixture initial unequip failed");
                    }
                    const auto saved = f->installedValues(0);
                    const auto persisted = equipmentFileBytes(path);
                    if (mode == 2)
                    {
                        f.reset();
                        f = std::make_unique<PlainEquipmentFixture>(0);
                        f->enableLuck(false);
                    }
                    const auto before = f->snapshot();
                    const auto* outputStorage = output.get();
                    const auto outputValue = *output;
                    const auto oldBytes = bytes;
                    const auto* bytesStorage = bytes.data();
                    const auto e = envelope(saved.mActor);
                    const auto ids = referenceIds(saved);
                    const EquipmentBindings bindings{ e, f->mStore, ids };
                    const auto fresh = mode == 2 ? std::optional(f->restartBindings(saved.mLastGenerated)) : std::nullopt;
                    std::unique_ptr<const PlainEquipmentValues> restored;
                    faults = {};
                    Trace trace;
                    bool caught = false;
                    {
                        Observe observe(trace, fail);
                        try
                        {
                            if (mode == 2)
                            {
                                if (f->restartEquipment(0, f->mActors[0]->getPtr(), path, bindings, *fresh,
                                        restored, bytes, faults) != FileReadResult::Read)
                                    throw std::runtime_error("Allocation restart I/O failure");
                            }
                            else if (f->luckCommand(0, mode == 0, path, output, bytes, faults) != TestPersistenceResult::Accepted)
                                throw std::runtime_error("Allocation command I/O failure");
                        }
                        catch (const std::exception&) { caught = true; }
                        if (!caught)
                        {
                            f->checkLuck(0, mode != 1);
                            if (mode == 2)
                                require(f->mNpcStats[0]->values() == *saved.mNpcStats,
                                    "Allocation recovery lost saved NPC stats");
                            output.reset();
                            restored.reset();
                            EquipmentBytes{}.swap(bytes);
                            f.reset();
                        }
                    }
                    if (fail == 0)
                        allocations = trace.mTotal;
                    require(trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                        "Enchanted allocation rejection/cleanup leaked owned state");
                    if (fail > 0 && fail <= allocations)
                    {
                        require(caught && trace.mFailures == 1 && !restored && output.get() == outputStorage
                                && *output == outputValue && bytes.data() == bytesStorage && bytes == oldBytes
                                && equipmentFileBytes(path) == persisted && trace.visits(Phase::Installation) == 0
                                && trace.visits(Phase::Publication) == 0,
                            "Enchanted allocation failure changed canonical state, file or success");
                        f->unchanged(before);
                        require(!std::filesystem::exists(path.string() + ".tmp"), "Allocation failure left temporary file");
                        ++failures;
                    }
                    else
                    {
                        require(!caught && trace.mFailures == 0 && trace.mTotal == allocations
                                && trace.allocations(Phase::Installation) == 0 && trace.allocations(Phase::Publication) == 0
                                && trace.allocations(Phase::Retirement) == 0,
                            "Enchanted success allocated after durable acceptance");
                        ++retries;
                    }
                }
            }
            std::cout << "enchanted individually failed allocations=" << failures
                << " successful end-of-sweep retries=" << retries << " remaining-after-cleanup=0\n";
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
            return { mStore, mWorld, mScripts, mActors[actor]->getPtr(), mActors[actor]->getPtr(), mNpcStats[actor] };
        }

        PreparedPlainEquipment prepare(size_t actor, bool equip)
        {
            return PreparedPlainEquipment::prepare(
                ContainerStoreResolution(mInventories[actor], mActors[actor]->getPtr()), mItems[actor],
                mItems[actor].getCellRef().getRefNum(), mWorld.getPtrRegistryRevision(), equip,
                preparationContext(actor));
        }

        std::string luckState(size_t actor) const
        {
            if (!mNpcStats[actor])
                return {};
            std::ostringstream stream(std::ios::binary);
            ESM::ESMWriter writer;
            writer.save(stream);
            writer.startRecord("LUCK");
            ESM::ActiveSpells active;
            mNpcStats[actor]->mStats.getActiveSpells().writeState(active);
            active.save(writer);
            for (const auto& [key, value] : mNpcStats[actor]->mStats.getMagicEffects())
            {
                writer.writeHNRefId("EFID", key.mId);
                writer.writeHNRefId("ARG_", key.mArg);
                writer.writeHNT("BASE", value.getBase());
                writer.writeHNT("MODI", value.getModifier());
            }
            writer.endRecord("LUCK");
            writer.close();
            return stream.str();
        }

        struct Snapshot
        {
            struct Node
            {
                const LiveCellRefBase* mAddress;
                ReferenceLifetime::Witness mLifetime;
                std::string mCell;
                RefData mData;
                const SceneUtil::PositionAttitudeTransform* mScene;
            };
            std::array<const EquipmentNpcStats*, 2> mNpcStorage{};
            std::array<EquipmentNpcStatsValues, 2> mNpcValues;
            std::array<std::string, 2> mLuckEffects;
            PtrRegistry::Snapshot mRegistry;
            LocalScripts::List mScripts;
            std::array<std::vector<Node>, 2> mNodes;
            std::array<std::vector<ContainerStoreIterator>, 2> mSlots;
            std::array<std::tuple<float, unsigned int, bool, bool, bool, bool, bool, bool>, 2> mMetadata;
            std::vector<ContainerStoreIterator> mSelections;
            std::vector<std::string> mEvents;
            int mListenerCalls;
            std::vector<std::pair<ESM::RefNum, int>> mListenerRemovals;
            std::array<std::pair<int, size_t>, 2> mActorEffects;
            std::array<std::vector<std::pair<ESM::RefNum, int>>, 2> mRemovals;
            std::array<std::vector<ESM::RefNum>, 2> mNotifications;
            std::array<std::pair<InventoryStoreListener*, ContainerStoreListener*>, 2> mListeners;
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
            result.mListenerRemovals = mListener.mRemovals;
            for (size_t i = 0; i < 2; ++i)
            {
                const auto& store = mInventories[i];
                result.mNpcStorage[i] = mNpcStats[i].get();
                result.mLuckEffects[i] = luckState(i);
                if (mNpcStats[i])
                    result.mNpcValues[i] = mNpcStats[i]->values();
                result.mActorEffects[i] = { mActorEffects[i].mListener.mCalls, mActorEffects[i].mInventoryUpdates };
                result.mRemovals[i] = mActorEffects[i].mListener.mRemovals;
                result.mNotifications[i] = mActorEffects[i].mNotifications;
                result.mListeners[i] = { store.mInventoryListener, store.mListener };
                result.mSlots[i] = store.mSlots;
                result.mMetadata[i] = { store.mCachedWeight, store.mSeed, store.mWeightUpToDate, store.mModified,
                    store.mResolved, store.mRechargingItemsUpToDate, store.mUpdatesEnabled, store.mFirstAutoEquip };
                result.mSelections.push_back(store.mSelectedEnchantItem);
                // RefData's stock copy constructor clears activation flags.
                // Keep snapshot nodes stable instead of copying on vector growth.
                result.mNodes[i].reserve(store.mLists.mClothes.mList.size());
                for (const auto& node : store.mLists.mClothes.mList)
                    result.mNodes[i].push_back({ &node, ConstPtr(&node, nullptr).getReferenceLifetime(),
                        cellBytes(node.mRef), node.mData.copyForContainerTransfer(), node.mData.getBaseNode() });
            }
            return result;
        }

        void unchanged(const Snapshot& before) const
        {
            require(mWorld.snapshotPtrRegistry() == before.mRegistry && mScripts.snapshot() == before.mScripts
                    && mEvents == before.mEvents && mListener.mCalls == before.mListenerCalls
                    && mListener.mRemovals == before.mListenerRemovals,
                "equipment rejection/preparation changed registry, exact counters, services or live effects");
            for (size_t i = 0; i < 2; ++i)
                unchangedActor(before, i);
        }

        void unchangedActor(const Snapshot& before, size_t i) const
        {
            const auto& store = mInventories[i];
            require(before.mNpcStorage[i] == mNpcStats[i].get() && before.mLuckEffects[i] == luckState(i)
                    && (!mNpcStats[i] || before.mNpcValues[i] == mNpcStats[i]->values()),
                "Equipment rejection changed canonical NPC stats or effects");
            require(before.mActorEffects[i]
                        == std::pair{ mActorEffects[i].mListener.mCalls, mActorEffects[i].mInventoryUpdates }
                    && before.mRemovals[i] == mActorEffects[i].mListener.mRemovals
                    && before.mNotifications[i] == mActorEffects[i].mNotifications
                    && before.mListeners[i] == std::pair{ store.mInventoryListener, store.mListener },
                "equipment rejection/preparation changed actor-local effects");
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
                require(&node == saved.mAddress && saved.mLifetime.isLive(&node)
                        && saved.mLifetime == ConstPtr(&node, nullptr).getReferenceLifetime()
                        && cellBytes(node.mRef) == saved.mCell
                        && node.mData.matchesContainerTransferState(saved.mData)
                        && node.mData.getBaseNode() == saved.mScene,
                    "equipment rejection/preparation changed node lifetime, values or scene binding");
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
                && a.mNpcStats == b.mNpcStats
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

        // Restart scenarios use only owned saved values after the source dies.
        static PlainEquipmentValues continuationSave(size_t actor, bool equipped, bool dormantSelection)
        {
            PlainEquipmentFixture source;
            source.seedValues(actor);
            auto& inventory = source.mInventories[actor];
            auto dormant = inventory.addNewStack(source.mItems[actor], 1);
            dormant->getCellRef().unsetRefNum();
            source.mWorld.registerPtr(*dormant);
            dormant->getCellRef() = dormant->getCellRef().copyWithCount(0);
            inventory.mSelectedEnchantItem = dormantSelection ? dormant : inventory.begin();
            if (!equipped)
                inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), source.context(actor, actor));
            PlainEquipmentValues saved;
            source.prepare(actor, equipped).exportValues(source.preparationContext(actor), saved);
            saved.mLastGenerated = { 900, -1 }; // Deliberately beyond surviving IDs.
            return saved;
        }

        static InventoryInstanceId ownedId(ESM::RefNum id) { return { id.mIndex, id.mContentFile }; }

        EquipmentCommand equipmentCommand(size_t actor, bool equip) const
        {
            const auto& inventory = mInventories[actor];
            const auto item = equip ? inventory.begin()
                : ConstContainerStoreIterator(inventory.mSlots[InventoryStore::Slot_Shirt]);
            require(item != inventory.end(), "equipment command requires a current active shirt");
            return { ownedId(mActors[actor]->getPtr().getCellRef().getRefNum()),
                ownedId(item->getCellRef().getRefNum()), mWorld.getPtrRegistryRevision(),
                equip ? EquipmentRequestedState::Equipped : EquipmentRequestedState::Unequipped };
        }

        TestPersistenceResult execute(EquipmentCaller caller, EquipmentCommand command, EquipmentFileSink& file,
            const EquipmentBindings& bindings, std::unique_ptr<const EquipmentSuccess>& output,
            EquipmentBytes& bytes, FileFaults& faults)
        {
            using namespace Allocations;
            InPhase phase(Phase::Validation);
            if (mFailedClosed || file.failedClosed())
            {
                mFailedClosed = true;
                return TestPersistenceResult::Uncertain;
            }
            const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
            if (mRestartActor || !id(command.mActor).isSet() || !id(command.mItem).isSet()
                || caller.mActor != command.mActor || command.mExpectedRevision != mWorld.getPtrRegistryRevision()
                || (command.mState != EquipmentRequestedState::Equipped
                    && command.mState != EquipmentRequestedState::Unequipped))
                throw std::invalid_argument("Equipment command caller, identity, revision or state invalid");
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

            phase.set(Phase::Preparation);
            auto prepared = PreparedPlainEquipment::prepare(ContainerStoreResolution(mInventories[actor], actorPtr),
                item, id(command.mItem), static_cast<size_t>(command.mExpectedRevision),
                command.mState == EquipmentRequestedState::Equipped, preparationContext(actor));
            const auto& result = prepared.result();
            auto revision = mWorld.getPtrRegistryRevision();
            for (const auto& effect : result.mEffects)
                if (effect.mKind == PlainEquipmentResult::EffectKind::RegisterSplit)
                    ++revision; // Same stock counter semantics as commitEquipment.
            phase.set(Phase::Result);
            auto staged = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{ command,
                ownedId(result.mShirt), ownedId(result.mSelected), ownedId(result.mLastGenerated), revision, result.mLuck });
            std::unique_ptr<const PlainEquipmentResult> internal;
            const auto outcome = commitEquipment(actor, actorPtr, std::move(prepared), file, bindings, internal, bytes, faults);
            if (outcome == TestPersistenceResult::Accepted)
            {
                phase.set(Phase::Publication);
                output.swap(staged);
            }
            phase.set(Phase::Retirement);
            return outcome;
        }

        void checkInstalledMembership(size_t actor, const PlainEquipmentValues& expected) const
        {
            require(sameValues(installedValues(actor), expected), "continuation lost exact installed values");
            for (const auto& node : mInventories[actor].mLists.mClothes.mList)
            {
                const auto ptr = mWorld.getPtr(node.mRef.getRefNum());
                require(ptr.hasLiveReference() && ptr.mRef == &node && node.mWorldModel == &mWorld
                        && ptr.mContainerStore == &mInventories[actor],
                    "continuation lost signed/active/dormant registry membership");
            }
        }

        void installContinuationFile(size_t actor, const PlainEquipmentValues& saved,
            const std::filesystem::path& path, const EquipmentBytes& expectedBytes)
        {
            const auto ids = referenceIds(saved);
            const auto e = envelope(saved.mActor);
            const EquipmentBindings bindings{ e, mStore, ids };
            const auto before = snapshot();
            const auto witnesses = restartBindings(saved.mLastGenerated);
            std::unique_ptr<const PlainEquipmentValues> output;
            EquipmentBytes bytes;
            FileFaults faults;
            require(restartEquipment(actor, mActors[actor]->getPtr(), path, bindings, witnesses, output, bytes, faults)
                        == FileReadResult::Read
                    && output && sameValues(*output, saved) && bytes == expectedBytes
                    && equipmentFileBytes(path) == expectedBytes && !mRestartActor && !mFailedClosed
                    && mWorld.getPtrRegistryRevision() == before.mRegistry.mRevision + 1
                    && mWorld.mPtrRegistry.mIndex.size() == before.mRegistry.mEntries.size() + saved.mObjects.size()
                    && mActorEffects[actor].mListener.mCalls == before.mActorEffects[actor].first
                    && mActorEffects[actor].mInventoryUpdates == before.mActorEffects[actor].second
                    && mActorEffects[actor].mListener.mRemovals == before.mRemovals[actor]
                    && mActorEffects[actor].mNotifications == before.mNotifications[actor]
                    && mScripts.snapshot() == before.mScripts && mEvents == before.mEvents,
                "continuation restart changed file/counters or replayed effects");
            checkInstalledMembership(actor, saved);
            unchangedActor(before, 1 - actor);
            for (const auto& [id, binding] : before.mRegistry.mEntries)
                require(mWorld.snapshotPtrRegistry().mEntries.at(id) == binding,
                    "continuation restart changed retained registry mapping");
        }

        EquipmentBytes startContinuation(size_t actor, const PlainEquipmentValues& saved,
            const std::filesystem::path& path)
        {
            seedValues(1 - actor);
            EquipmentBytes bytes;
            {
                EquipmentFileSink initial(path);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                FileFaults faults;
                require(initial.write(saved, { e, mStore, ids }, bytes, faults) == TestPersistenceResult::Accepted,
                    "continuation initial file setup failed");
            } // Every post-restart command uses another sink.
            installContinuationFile(actor, saved, path, bytes);
            return bytes;
        }

        PreparedPlainEquipment prepareContinuation(size_t actor, bool equip)
        {
            // Resolve a current member for each new operation. A restacked shirt
            // remains a registered dormant node, not a valid new equip request.
            auto& inventory = mInventories[actor];
            const auto item = equip ? inventory.begin() : inventory.mSlots[InventoryStore::Slot_Shirt];
            require(item != inventory.end(), "continuation requires a current active shirt");
            mItems[actor] = *item;
            return prepare(actor, equip);
        }

        void commitContinuation(size_t actor, bool equip, EquipmentFileSink& file,
            const std::filesystem::path& path, std::unique_ptr<const PlainEquipmentResult>& output,
            EquipmentBytes& bytes, size_t failAt = 0, std::unique_ptr<const EquipmentSuccess>* commandOutput = nullptr)
        {
            const auto anchor = mItems[actor];
            auto prepared = prepareContinuation(actor, equip);
            const auto oldItem = mItems[actor];
            // The command must resolve its own item even after this fixture's
            // retained anchor became dormant in a prior restack.
            if (commandOutput)
                mItems[actor] = anchor;
            const auto before = snapshot();
            const auto prior = installedValues(actor);
            const auto identity = oldItem.getCellRef().getRefNum();
            const auto count = oldItem.getCellRef().getCount(false);
            // An independent expected ledger for these homogeneous shirt cases;
            // this is not a second general equipment implementation.
            PlainEquipmentResult expected{ prior.mActor, equip ? identity : ESM::RefNum{}, prior.mSelected,
                prior.mLastGenerated, {}, {} };
            for (const auto& object : prior.mObjects)
                expected.mItems.push_back({ object.mRef.mRefNum, object.mRef.mRefID, object.mRef.mCount });
            auto original = std::find_if(expected.mItems.begin(), expected.mItems.end(),
                [&](const auto& item) { return item.mIdentity == identity; });
            using Kind = PlainEquipmentResult::EffectKind;
            size_t splits = 0, updates = 0;
            auto removals = before.mRemovals[actor];
            auto notifications = before.mNotifications[actor];
            int calls = 1;
            if (equip && std::abs(count) > 1)
            {
                splits = 1;
                updates = 2;
                calls = 2;
                if (++expected.mLastGenerated.mIndex == 0)
                    --expected.mLastGenerated.mContentFile;
                const int sign = count > 0 ? 1 : -1;
                original->mCount = sign;
                expected.mItems.push_back({ expected.mLastGenerated, original->mBase, count - sign });
                expected.mEffects = { { Kind::RegisterSplit, prior.mActor, expected.mLastGenerated, 0 },
                    { Kind::InventoryUpdated, prior.mActor, {}, 0 },
                    { Kind::ItemRemoved, prior.mActor, identity, std::abs(count) - 1 },
                    { Kind::InventoryUpdated, prior.mActor, {}, 0 } };
                removals.emplace_back(identity, std::abs(count) - 1);
                notifications.insert(notifications.end(), 2, prior.mActor);
            }
            else if (!equip)
            {
                auto remainder = std::find_if(expected.mItems.begin(), expected.mItems.end(),
                    [&](const auto& item) { return item.mIdentity != identity && item.mCount != 0; });
                if (remainder != expected.mItems.end())
                {
                    remainder->mCount += count;
                    original->mCount = 0;
                    expected.mEffects.push_back({ Kind::DeleteStackScript, prior.mActor, identity, 0 });
                }
                if (expected.mSelected == identity)
                    expected.mSelected = {};
            }
            expected.mEffects.push_back({ Kind::EquipmentChanged, prior.mActor, {}, 0 });
            require(prepared.result() == expected, "continuation differs from exact shirt transition/effect ledger");
            PlainEquipmentValues saved;
            prepared.exportValues(preparationContext(actor), saved);
            const auto ids = referenceIds(saved);
            const auto e = envelope(saved.mActor);
            const EquipmentBindings bindings{ e, mStore, ids };
            FileFaults faults{ FileFault::None, 17 };
            Allocations::Trace trace;
            TestPersistenceResult outcome;
            const auto command = equipmentCommand(actor, equip);
            {
                Allocations::Observe observe(trace, failAt);
                if (commandOutput)
                    outcome = executeEquipment(*this, { command.mActor }, command, file, bindings, *commandOutput, bytes, faults);
                else
                    outcome = commitEquipment(actor, mActors[actor]->getPtr(), std::move(prepared),
                        file, bindings, output, bytes, faults);
            }
            PlainEquipmentValues decoded;
            decodeEquipment(equipmentFileBytes(path), bindings, decoded);
            const EquipmentSuccess expectedSuccess{ command, ownedId(expected.mShirt), ownedId(expected.mSelected),
                ownedId(expected.mLastGenerated), static_cast<size_t>(before.mRegistry.mRevision + splits) };
            require(outcome == TestPersistenceResult::Accepted
                    && (commandOutput ? *commandOutput && **commandOutput == expectedSuccess : output && *output == expected)
                    && sameValues(saved, decoded) && bytes == equipmentFileBytes(path) && !oldItem.hasLiveReference()
                    && mWorld.getLastGeneratedRefNum() == expected.mLastGenerated
                    && mWorld.getPtrRegistryRevision() == before.mRegistry.mRevision + splits
                    && mWorld.mPtrRegistry.mIndex.size() == before.mRegistry.mEntries.size() + splits
                    && mActorEffects[actor].mListener.mCalls == before.mActorEffects[actor].first + calls
                    && mActorEffects[actor].mInventoryUpdates == before.mActorEffects[actor].second + updates
                    && mActorEffects[actor].mListener.mRemovals == removals
                    && mActorEffects[actor].mNotifications == notifications
                    && mScripts.snapshot() == before.mScripts && mEvents == before.mEvents
                    && trace.allocations(Allocations::Phase::Installation) == 0
                    && trace.allocations(Allocations::Phase::Publication) == 0
                    && trace.allocations(Allocations::Phase::Retirement) == 0
                    && (failAt == 0 || (trace.mFailures == 0 && trace.mTotal + 1 == failAt))
                    && trace.mTrackingOverflow == 0,
                "continuation lost durable values/counters or emitted effects beyond the new commit");
            checkInstalledMembership(actor, saved);
            unchangedActor(before, 1 - actor);
            for (const auto& [id, binding] : before.mRegistry.mEntries)
                if (binding.getContainer() != &mInventories[actor])
                    require(mWorld.snapshotPtrRegistry().mEntries.at(id) == binding,
                        "continuation changed unrelated actor registry mapping");
        }

        static void checkCommandGuards(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            enum class Guard
            {
                Caller, EmptyCaller, EmptyActor, EmptyItem, MissingActor, MissingItem, OtherActor, OtherItem,
                ActorAsItem, ItemAsActor, OldRevision, FutureRevision, EmptyRevision, DormantItem, InvalidState,
                WrongState, MissingActorMapping, MissingItemMapping, WrongActorMapping, WrongItemMapping,
                ExpiredItem, ActorBinding, RuntimeBinding, ContentBinding
            };
            size_t rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                    for (auto guard : { Guard::Caller, Guard::EmptyCaller, Guard::EmptyActor, Guard::EmptyItem,
                             Guard::MissingActor, Guard::MissingItem, Guard::OtherActor, Guard::OtherItem,
                             Guard::ActorAsItem, Guard::ItemAsActor, Guard::OldRevision, Guard::FutureRevision,
                             Guard::EmptyRevision, Guard::DormantItem, Guard::InvalidState, Guard::WrongState,
                             Guard::MissingActorMapping, Guard::MissingItemMapping, Guard::WrongActorMapping,
                             Guard::WrongItemMapping, Guard::ExpiredItem, Guard::ActorBinding,
                             Guard::RuntimeBinding, Guard::ContentBinding })
                    {
                        PlainEquipmentFixture f;
                        for (size_t i = 0; i < 2; ++i)
                            f.seedValues(i);
                        auto& inventory = f.mInventories[actor];
                        auto dormant = inventory.addNewStack(f.mItems[actor], 1);
                        dormant->getCellRef().unsetRefNum();
                        f.mWorld.registerPtr(*dormant);
                        dormant->getCellRef() = dormant->getCellRef().copyWithCount(0);
                        if (!equip)
                            inventory.equip(InventoryStore::Slot_Shirt, inventory.begin(), f.context(actor, actor));
                        f.bindEffects(0);
                        f.bindEffects(1);
                        // Fixture-owned lazy service witness is setup storage,
                        // not a retained allocation from the rejected command.
                        f.mScripts.lifetimeWitness();
                        const auto path = scratch / "guard.bin";
                        const auto otherPath = scratch / "guard-other.bin";
                        EquipmentBytes priorBytes, otherBytes;
                        for (size_t i : { actor, 1 - actor })
                        {
                            const auto saved = f.installedValues(i);
                            const auto ids = referenceIds(saved);
                            const auto e = envelope(saved.mActor);
                            EquipmentFileSink initial(i == actor ? path : otherPath);
                            FileFaults faults;
                            require(initial.write(saved, { e, f.mStore, ids }, i == actor ? priorBytes : otherBytes, faults)
                                    == TestPersistenceResult::Accepted,
                                "equipment command guard file setup failed");
                        }
                        const auto values = f.installedValues(actor);
                        const auto ids = referenceIds(values);
                        auto e = envelope(values.mActor);
                        auto command = f.equipmentCommand(actor, equip);
                        const auto original = command;
                        EquipmentCaller caller{ command.mActor };
                        const auto other = ownedId(f.mActors[1 - actor]->getPtr().getCellRef().getRefNum());
                        const auto itemId = f.mItems[actor].getCellRef().getRefNum();
                        switch (guard)
                        {
                            case Guard::Caller: caller.mActor = other; break;
                            case Guard::EmptyCaller: caller.mActor = {}; break;
                            case Guard::EmptyActor: command.mActor = caller.mActor = {}; break;
                            case Guard::EmptyItem: command.mItem = {}; break;
                            case Guard::MissingActor: command.mActor = caller.mActor = { 123456, -1 }; break;
                            case Guard::MissingItem: command.mItem = { 123456, -1 }; break;
                            case Guard::OtherActor: command.mActor = caller.mActor = other; break;
                            case Guard::OtherItem: command.mItem = ownedId(f.mItems[1 - actor].getCellRef().getRefNum()); break;
                            case Guard::ActorAsItem: command.mItem = command.mActor; break;
                            case Guard::ItemAsActor: command.mActor = caller.mActor = command.mItem; break;
                            case Guard::OldRevision: --command.mExpectedRevision; break;
                            case Guard::FutureRevision: ++command.mExpectedRevision; break;
                            case Guard::EmptyRevision: command.mExpectedRevision = 0; break;
                            case Guard::DormantItem: command.mItem = ownedId(dormant->getCellRef().getRefNum()); break;
                            case Guard::InvalidState: command.mState = static_cast<EquipmentRequestedState>(255); break;
                            case Guard::WrongState:
                                command.mState = equip ? EquipmentRequestedState::Unequipped : EquipmentRequestedState::Equipped;
                                break;
                            case Guard::MissingActorMapping: f.mWorld.mPtrRegistry.mIndex.erase(values.mActor); break;
                            case Guard::MissingItemMapping: f.mWorld.mPtrRegistry.mIndex.erase(itemId); break;
                            case Guard::WrongActorMapping:
                                f.mWorld.mPtrRegistry.mIndex.at(values.mActor) = f.mActors[1 - actor]->getPtr();
                                break;
                            case Guard::WrongItemMapping:
                                f.mWorld.mPtrRegistry.mIndex.at(itemId) = *dormant;
                                break;
                            case Guard::ExpiredItem:
                                // Retain an expired registry witness; never dereference it in command resolution.
                                inventory.mLists.mClothes.mList.front().mWorldModel = nullptr;
                                inventory.mLists.mClothes.mList.pop_front();
                                inventory.mSlots[InventoryStore::Slot_Shirt] = inventory.end();
                                break;
                            case Guard::ActorBinding: e.mActor = f.mActors[1 - actor]->getPtr().getCellRef().getRefNum(); break;
                            case Guard::RuntimeBinding: e.mRuntime += "-wrong"; break;
                            case Guard::ContentBinding: e.mContent[0] ^= 1; break;
                        }
                        auto output = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{ original, {}, {}, {}, 999 });
                        const auto* outputStorage = output.get();
                        const auto outputValue = *output;
                        auto bytes = priorBytes;
                        const auto* byteStorage = bytes.data();
                        const auto before = f.snapshot();
                        const auto anchors = f.mItems;
                        EquipmentFileSink file(path);
                        FileFaults faults;
                        Allocations::Trace trace;
                        bool caught = false;
                        {
                            Allocations::Observe observe(trace);
                            try
                            {
                                executeEquipment(f, caller, command, file, { e, f.mStore, ids }, output, bytes, faults);
                            }
                            catch (const std::invalid_argument& error)
                            {
                                caught = !std::string_view(error.what()).empty();
                            }
                        }
                        if (!caught || trace.visits(Allocations::Phase::Persistence) != 0 || trace.mOutstanding != 0)
                            std::cerr << "command guard actor=" << actor << " equip=" << equip
                                      << " case=" << static_cast<int>(guard) << " caught=" << caught
                                      << " persistence=" << trace.visits(Allocations::Phase::Persistence)
                                      << " outstanding=" << trace.mOutstanding << '\n';
                        require(caught && output.get() == outputStorage && *output == outputValue
                                && bytes.data() == byteStorage && bytes == priorBytes
                                && equipmentFileBytes(path) == priorBytes && equipmentFileBytes(otherPath) == otherBytes
                                && !std::filesystem::exists(std::filesystem::path(path).concat(".tmp"))
                                && !std::filesystem::exists(std::filesystem::path(otherPath).concat(".tmp"))
                                && faults.mWrites == 0 && !file.failedClosed() && !f.mFailedClosed
                                && trace.visits(Allocations::Phase::Persistence) == 0
                                && trace.visits(Allocations::Phase::Installation) == 0
                                && trace.visits(Allocations::Phase::Publication) == 0
                                && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                                && sameReference(f.mItems[0], anchors[0]) && sameReference(f.mItems[1], anchors[1]),
                            "equipment command rejection changed state, publication, files or effects");
                        f.unchanged(before);
                        ++rejected;
                    }
            std::cout << "equipment owned command caller/identity/revision/state/binding rejections=" << rejected << '\n';
        }

        static void checkCommand(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            std::unique_ptr<const EquipmentSuccess> success;
            EquipmentBytes bytes;
            size_t commits = 0;
            {
                PlainEquipmentFixture f;
                for (size_t actor = 0; actor < 2; ++actor)
                {
                    f.seedValues(actor);
                    f.bindEffects(actor);
                }
                std::unique_ptr<const PlainEquipmentResult> internal;
                for (size_t actor = 0; actor < 2; ++actor)
                {
                    const auto path = scratch / (actor == 0 ? "first.bin" : "second.bin");
                    EquipmentFileSink file(path);
                    for (bool equip : { true, false, true, false })
                    {
                        f.commitContinuation(actor, equip, file, path, internal, bytes, 0, &success);
                        ++commits;
                    }
                }
            }
            require(success && success->mCommand.mActor.mIndex != 0 && !bytes.empty(),
                "equipment command publication borrowed a destroyed fixture");
            std::cout << "equipment owned command isolated commits=" << commits << '\n';
        }

        static void checkRestartContinuation(const std::filesystem::path& scratch, bool commands = false)
        {
            EquipmentScratch directory(scratch);
            size_t commits = 0;
            std::unique_ptr<const PlainEquipmentResult> output;
            std::unique_ptr<const EquipmentSuccess> success;
            EquipmentBytes bytes;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equipped : { false, true })
                    for (bool dormant : { false, true })
                    {
                        auto saved = continuationSave(actor, equipped, dormant);
                        if (dormant)
                            saved.mLastGenerated = { std::numeric_limits<uint32_t>::max(), -2 };
                        PlainEquipmentFixture f(actor);
                        if (dormant)
                            f.mWorld.mPtrRegistry.mRevision = std::numeric_limits<size_t>::max() - 1;
                        const auto path = scratch / "continued.bin";
                        f.startContinuation(actor, saved, path);
                        EquipmentFileSink file(path);
                        for (bool equip : { !equipped, equipped, !equipped })
                        {
                            f.commitContinuation(actor, equip, file, path, output, bytes, 0, commands ? &success : nullptr);
                            ++commits;
                        }
                        // The same fixture can operate on its other actor; that
                        // actor's file is independent, not a two-player world save.
                        const auto otherPath = scratch / "other.bin";
                        EquipmentFileSink otherFile(otherPath);
                        const auto actorFile = equipmentFileBytes(path);
                        for (bool equip : { true, false })
                        {
                            f.commitContinuation(1 - actor, equip, otherFile, otherPath, output, bytes, 0, commands ? &success : nullptr);
                            require(equipmentFileBytes(path) == actorFile, "other actor overwrote restored actor file");
                            ++commits;
                        }
                    }
            require((commands ? success && success->mCommand.mActor.mIndex != 0 : output && !output->mItems.empty())
                    && !bytes.empty(), "continuation publication borrowed fixture");
            std::cout << "equipment post-restart isolated commits=" << commits << '\n';
        }

        size_t continuationAllocations(size_t actor, bool equip, const std::filesystem::path& path,
            const std::filesystem::path& otherPath, bool commands = false)
        {
            using namespace Allocations;
            auto proposal = prepareContinuation(actor, equip);
            const auto expected = proposal.result();
            const auto* proposalStorage = &proposal.result();
            PlainEquipmentValues saved;
            proposal.exportValues(preparationContext(actor), saved);
            const auto ids = referenceIds(saved);
            const auto e = envelope(saved.mActor);
            const EquipmentBindings bindings{ e, mStore, ids };
            EquipmentBytes priorBytes;
            FileFaults faults;
            {
                EquipmentFileSink initial(path);
                require(initial.write(installedValues(actor), bindings, priorBytes, faults)
                        == TestPersistenceResult::Accepted,
                    "continuation allocation prior file setup failed");
            }
            const auto otherBytes = equipmentFileBytes(otherPath);
            auto output = std::make_unique<const PlainEquipmentResult>(expected);
            const auto* outputStorage = output.get();
            const auto command = equipmentCommand(actor, equip);
            auto success = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{ command, {}, {}, {}, 999 });
            const auto* successStorage = success.get();
            const auto successValue = *success;
            EquipmentBytes bytes{ 'o', 'l', 'd' };
            const auto bytesValue = bytes;
            const auto* bytesStorage = bytes.data();
            const auto before = snapshot();
            const auto unchangedOutput = [&] {
                unchanged(before);
                require(&proposal.result() == proposalStorage && proposal.result() == expected
                        && output.get() == outputStorage && *output == expected
                        && success.get() == successStorage && *success == successValue
                        && bytes.data() == bytesStorage && bytes == bytesValue
                        && equipmentFileBytes(path) == priorBytes && equipmentFileBytes(otherPath) == otherBytes
                        && !std::filesystem::exists(std::filesystem::path(path).concat(".tmp"))
                        && !mFailedClosed,
                    "continuation allocation changed state, output storage/value or actor files");
            };
            // Count a discarded preparation, then assign into an existing owned
            // proposal under every failure. No failed assignment may consume it.
            Trace preparation;
            {
                Observe observe(preparation);
                InPhase phase(Phase::Preparation);
                prepareContinuation(actor, equip);
            }
            require(preparation.mTotal > 0 && preparation.mOutstanding == 0 && preparation.mTrackingOverflow == 0,
                "continuation preparation allocation baseline leaked");
            unchangedOutput();
            size_t failures = 0;
            // Command sweeps below already include preparation in every attempt.
            for (size_t fail = 1; !commands && fail <= preparation.mTotal; ++fail)
            {
                Trace trace;
                bool rejected = false;
                {
                    Observe observe(trace, fail);
                    InPhase phase(Phase::Preparation);
                    try
                    {
                        proposal = prepareContinuation(actor, equip);
                    }
                    catch (const std::exception&)
                    {
                        rejected = true;
                    }
                }
                require(rejected && trace.mFailures == 1 && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                    "continuation preparation allocation escaped rejection or leaked");
                unchangedOutput();
                ++failures;
            }
            proposal.validate(preparationContext(actor));
            require(prepareContinuation(actor, equip).result() == expected,
                "continuation preparation retry changed proposal");

            // A creation fault counts the full fallible commit path without
            // installing it. Every attempt uses a sink constructed after restart.
            const auto attempt = [&](Trace& trace, size_t fail, FileFault fault) {
                EquipmentFileSink fresh(path);
                auto prepared = prepareContinuation(actor, equip);
                faults = { fault, 17 };
                TestPersistenceResult outcome = TestPersistenceResult::Rejected;
                bool rejected = false;
                {
                    Observe observe(trace, fail);
                    try
                    {
                        outcome = commands
                            ? executeEquipment(*this, { command.mActor }, command, fresh, bindings, success, bytes, faults)
                            : commitEquipment(actor, mActors[actor]->getPtr(), std::move(prepared),
                                fresh, bindings, output, bytes, faults);
                    }
                    catch (const std::exception&)
                    {
                        rejected = true;
                    }
                }
                require(!fresh.failedClosed() && outcome == TestPersistenceResult::Rejected
                        && rejected == (fail != 0) && trace.mFailures == (fail != 0 ? 1 : 0)
                        && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                        && trace.visits(Phase::Installation) == 0 && trace.visits(Phase::Publication) == 0
                        && faults.mWrites == 0,
                    "continuation commit allocation escaped rejection, installed or leaked");
                unchangedOutput();
            };
            Trace count;
            attempt(count, 0, FileFault::Create);
            require(count.mTotal > 0 && count.allocations(Phase::Persistence) > 0,
                "continuation commit allocation baseline missed persistence staging");
            for (size_t fail = 1; fail <= count.mTotal; ++fail)
            {
                Trace trace;
                attempt(trace, fail, FileFault::None);
                ++failures;
            }
            EquipmentFileSink retry(path);
            // The ledger verifies this retry's complete values and only its new
            // effects. Arming the next allocation also covers the accepted tail.
            commitContinuation(actor, equip, retry, path, output, bytes, count.mTotal + 1, commands ? &success : nullptr);
            require(equipmentFileBytes(otherPath) == otherBytes,
                "continuation allocation retry changed the other actor file");
            return failures;
        }

        static void checkRestartContinuationAllocations(const std::filesystem::path& scratch, bool commands = false)
        {
            EquipmentScratch directory(scratch);
            size_t failures = 0, retries = 0;
            for (size_t restoredActor = 0; restoredActor < 2; ++restoredActor)
                for (size_t actor = 0; actor < 2; ++actor)
                    for (bool equip : { true, false })
                    {
                        PlainEquipmentFixture f(restoredActor);
                        const auto restoredPath = scratch / "restored.bin";
                        const auto otherPath = scratch / "other.bin";
                        f.startContinuation(restoredActor, continuationSave(restoredActor, false, true), restoredPath);
                        {
                            const auto other = f.installedValues(1 - restoredActor);
                            const auto ids = referenceIds(other);
                            const auto e = envelope(other.mActor);
                            EquipmentFileSink initial(otherPath);
                            EquipmentBytes bytes;
                            FileFaults faults;
                            require(initial.write(other, { e, f.mStore, ids }, bytes, faults)
                                    == TestPersistenceResult::Accepted,
                                "continuation allocation other actor file setup failed");
                        }
                        const auto& path = actor == restoredActor ? restoredPath : otherPath;
                        if (!equip)
                        {
                            EquipmentFileSink initial(path);
                            std::unique_ptr<const PlainEquipmentResult> output;
                            std::unique_ptr<const EquipmentSuccess> success;
                            EquipmentBytes bytes;
                            f.commitContinuation(actor, true, initial, path, output, bytes, 0, commands ? &success : nullptr);
                        }
                        failures += f.continuationAllocations(actor, equip, path,
                            actor == restoredActor ? otherPath : restoredPath, commands);
                        ++retries;
                    }
            std::cout << "equipment post-restart allocation failures=" << failures
                      << " remaining-after-failure=0 verified retries=" << retries << '\n';
        }

        static void checkRestartContinuationGuards(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t rejected = 0;
            for (size_t restoredActor = 0; restoredActor < 2; ++restoredActor)
                for (size_t actor = 0; actor < 2; ++actor)
                    for (int test = 0; test < 16; ++test)
                    {
                        PlainEquipmentFixture f(restoredActor);
                        f.startContinuation(restoredActor, continuationSave(restoredActor, false, true),
                            scratch / "restart.bin");
                        auto prepared = f.prepareContinuation(actor, true);
                        const auto prior = f.installedValues(actor);
                        const auto ids = referenceIds(prior);
                        auto e = envelope(prior.mActor);
                        ESMStore foreign;
                        const ESMStore* content = &f.mStore;
                        auto caller = f.mActors[actor]->getPtr();
                        auto& inventory = f.mInventories[actor];
                        const auto oldStorage = inventory.mStorageIdentity;
                        const auto oldSelection = inventory.mSelectedEnchantItem;
                        const auto oldCount = f.mItems[actor].getCellRef().getCount(false);
                        const auto path = scratch / "guard.bin";
                        EquipmentFileSink file(path);
                        EquipmentBytes bytes;
                        FileFaults faults;
                        require(file.write(prior, { e, f.mStore, ids }, bytes, faults) == TestPersistenceResult::Accepted,
                            "post-restart guard file setup failed");
                        auto output = std::make_unique<const PlainEquipmentResult>(prepared.result());
                        std::optional<InventoryStore> alternate;
                        switch (test)
                        {
                            case 0: caller = f.mActors[1 - actor]->getPtr(); break;
                            case 1: caller.mContainerStore = &inventory; break;
                            case 2:
                            {
                                ManualRef dead(f.mStore, ESM::RefId::stringRefId("equipment_actor"));
                                dead.getPtr().getCellRef().setRefNum(prior.mActor);
                                caller = dead.getPtr();
                                break;
                            }
                            case 3: e.mRuntime += "-foreign"; break;
                            case 4: e.mContent[0] ^= 1; break;
                            case 5: e.mActor = f.mActors[1 - actor]->getPtr().getCellRef().getRefNum(); break;
                            case 6: content = &foreign; break;
                            case 7:
                            {
                                // Same desired operation is possible again, but
                                // the old nodes/preparation no longer authorize it.
                                f.commitContinuation(actor, true, file, path, output, bytes);
                                f.commitContinuation(actor, false, file, path, output, bytes);
                                break;
                            }
                            case 8:
                            {
                                const auto otherPath = scratch / "other.bin";
                                EquipmentFileSink other(otherPath);
                                f.commitContinuation(1 - actor, true, other, otherPath, output, bytes);
                                break;
                            }
                            case 9:
                                std::destroy_at(&f.mScripts);
                                std::construct_at(&f.mScripts, f.mStore);
                                break;
                            case 10:
                            case 11:
                            {
                                alternate.emplace();
                                alternate->setPtr(caller, f.mWorld);
                                Misc::Rng::Generator rng{ 0 };
                                alternate->fill({}, {}, rng);
                                auto item = *alternate->addNewStack(f.mItems[actor], 2);
                                item.getCellRef().unsetRefNum();
                                f.mWorld.registerPtr(item);
                                prepared = PreparedPlainEquipment::prepare(ContainerStoreResolution(*alternate, caller),
                                    item, item.getCellRef().getRefNum(), f.mWorld.getPtrRegistryRevision(), true,
                                    f.preparationContext(actor));
                                if (test == 11)
                                {
                                    alternate.reset();
                                    alternate.emplace(); // Same address, expired lifetime.
                                }
                                break;
                            }
                            case 12: inventory.setInvListener(&f.mListener); break;
                            case 13: f.mItems[actor].getCellRef().setCount(2); break;
                            case 14: inventory.mSelectedEnchantItem = inventory.begin(); break;
                            case 15: inventory.mStorageIdentity = f.mInventories[1 - actor].mStorageIdentity; break;
                        }
                        const auto before = f.snapshot();
                        const auto priorBytes = equipmentFileBytes(path);
                        const auto* outputStorage = output.get();
                        const auto outputValue = *output;
                        const auto* byteStorage = bytes.data();
                        const auto byteValue = bytes;
                        faults = {};
                        Allocations::Trace trace;
                        bool failed = false;
                        {
                            Allocations::Observe observe(trace);
                            try
                            {
                                f.commitEquipment(actor, caller, std::move(prepared), file,
                                    { e, *content, ids }, output, bytes, faults);
                            }
                            catch (const std::invalid_argument& error)
                            {
                                failed = !std::string_view(error.what()).empty();
                            }
                        }
                        if (!failed)
                            std::cerr << "post-restart guard restored=" << restoredActor << " actor=" << actor
                                      << " case=" << test << '\n';
                        require(failed && output.get() == outputStorage && *output == outputValue
                                && bytes.data() == byteStorage && bytes == byteValue && equipmentFileBytes(path) == priorBytes
                                && faults.mWrites == 0 && trace.visits(Allocations::Phase::Persistence) == 0
                                && trace.visits(Allocations::Phase::Installation) == 0
                                && trace.visits(Allocations::Phase::Publication) == 0 && !file.failedClosed() && !f.mFailedClosed,
                            "post-restart guard changed state/output/file or poisoned safe retry");
                        f.unchanged(before);
                        // Restore intentional test mutations; expired services,
                        // nodes and preparations are replaced by current ones.
                        alternate.reset();
                        if (test == 12)
                            f.bindEffects(actor);
                        if (test == 13)
                            f.mItems[actor].getCellRef().setCount(oldCount);
                        if (test == 14)
                            inventory.mSelectedEnchantItem = oldSelection;
                        if (test == 15)
                            inventory.mStorageIdentity = oldStorage;
                        f.commitContinuation(actor, true, file, path, output, bytes);
                        ++rejected;
                    }
            std::cout << "equipment post-restart stale/caller/binding/lifetime rejections and retries=" << rejected << '\n';
        }

        static void checkRestartContinuationPersistence(const std::filesystem::path& scratch, bool commands = false)
        {
            EquipmentScratch directory(scratch);
            size_t safe = 0, uncertain = 0, blocked = 0;
            for (size_t restoredActor = 0; restoredActor < 2; ++restoredActor)
                for (size_t actor = 0; actor < 2; ++actor)
                    for (bool equip : { true, false })
                        for (auto failure : { FileFault::Create, FileFault::Write, FileFault::Flush, FileFault::Close,
                                 FileFault::Replace, FileFault::ReplaceError, FileFault::AfterReplace, FileFault::Barrier,
                                 FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read, FileFault::ReadEof, FileFault::ReadClose })
                        {
                            PlainEquipmentFixture f(restoredActor);
                            f.startContinuation(restoredActor, continuationSave(restoredActor, false, true),
                                scratch / "restart.bin");
                            const auto path = scratch / "persistence.bin";
                            const auto otherPath = scratch / "persistence-other.bin";
                            EquipmentBytes otherBytes;
                            {
                                const auto otherValues = f.installedValues(1 - actor);
                                const auto otherIds = referenceIds(otherValues);
                                const auto otherEnvelope = envelope(otherValues.mActor);
                                EquipmentFileSink initial(otherPath);
                                FileFaults faults;
                                require(initial.write(otherValues, { otherEnvelope, f.mStore, otherIds }, otherBytes, faults)
                                        == TestPersistenceResult::Accepted,
                                    "post-restart persistence other actor file setup failed");
                            }
                            EquipmentFileSink file(path);
                            EquipmentBytes bytes;
                            std::unique_ptr<const PlainEquipmentResult> output;
                            std::unique_ptr<const EquipmentSuccess> success;
                            if (!equip)
                                f.commitContinuation(actor, true, file, path, output, bytes, 0, commands ? &success : nullptr);
                            auto prepared = f.prepareContinuation(actor, equip);
                            PlainEquipmentValues saved;
                            prepared.exportValues(f.preparationContext(actor), saved);
                            const auto prior = f.installedValues(actor);
                            const auto ids = referenceIds(saved);
                            const auto e = envelope(prior.mActor);
                            const EquipmentBindings bindings{ e, f.mStore, ids };
                            FileFaults faults;
                            EquipmentBytes priorBytes, newBytes;
                            require(file.write(prior, bindings, priorBytes, faults) == TestPersistenceResult::Accepted,
                                "post-restart persistence prior file setup failed");
                            encodeEquipment(saved, bindings, newBytes);
                            require(priorBytes != newBytes, "post-restart persistence requires distinct files");
                            if (!output)
                                output = std::make_unique<const PlainEquipmentResult>(prepared.result());
                            if (!success)
                                success = std::make_unique<const EquipmentSuccess>();
                            const auto* successStorage = success.get();
                            const auto successValue = *success;
                            const auto command = f.equipmentCommand(actor, equip);
                            bytes = priorBytes;
                            const auto* outputStorage = output.get();
                            const auto outputValue = *output;
                            const auto* byteStorage = bytes.data();
                            const auto before = f.snapshot();
                            faults = { failure, 17 };
                            Allocations::Trace trace;
                            TestPersistenceResult outcome;
                            {
                                Allocations::Observe observe(trace);
                                outcome = commands
                                    ? executeEquipment(f, { command.mActor }, command, file, bindings, success, bytes, faults)
                                    : f.commitEquipment(actor, f.mActors[actor]->getPtr(), std::move(prepared),
                                        file, bindings, output, bytes, faults);
                            }
                            const bool poisoned = failure >= FileFault::ReplaceError;
                            const auto actual = equipmentFileBytes(path);
                            const auto unchanged = [&] {
                                f.unchanged(before);
                                require(output.get() == outputStorage && *output == outputValue
                                        && success.get() == successStorage && *success == successValue
                                        && bytes.data() == byteStorage && bytes == priorBytes && equipmentFileBytes(path) == actual
                                        && equipmentFileBytes(otherPath) == otherBytes
                                        && !std::filesystem::exists(std::filesystem::path(otherPath).concat(".tmp")),
                                    "post-restart failed commit changed prior publication/file");
                            };
                            require(outcome == (poisoned ? TestPersistenceResult::Uncertain : TestPersistenceResult::Rejected)
                                    && f.mFailedClosed == poisoned && file.failedClosed() == poisoned
                                    && faults.mReached == (failure == FileFault::Replace ? FileFault::Close
                                        : failure == FileFault::ReplaceError || failure == FileFault::AfterReplace
                                            ? FileFault::Replace : failure >= FileFault::ReadOpen ? FileFault::Read : failure)
                                    && trace.visits(Allocations::Phase::Installation) == 0
                                    && trace.visits(Allocations::Phase::Publication) == 0
                                    && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                                    && actual == (failure > FileFault::ReplaceError ? newBytes : priorBytes)
                                    && !std::filesystem::exists(std::filesystem::path(path).concat(".tmp")),
                                "post-restart persistence failure installed effects or left incomplete file");
                            unchanged();
                            if (!poisoned)
                            {
                                f.commitContinuation(actor, equip, file, path, output, bytes, 0, commands ? &success : nullptr);
                                require(sameValues(f.installedValues(actor), saved) && equipmentFileBytes(otherPath) == otherBytes,
                                    "safe retry changed intended values or other actor file");
                                ++safe;
                                continue;
                            }
                            for (size_t retryActor = 0; retryActor < 2; ++retryActor)
                                for (bool freshSink : { false, true })
                                {
                                    const bool retryEquip = !f.installedValues(retryActor).mShirt.isSet();
                                    auto retry = f.prepareContinuation(retryActor, retryEquip);
                                    const auto retryValues = f.installedValues(retryActor);
                                    const auto retryIds = referenceIds(retryValues);
                                    const auto retryEnvelope = envelope(retryValues.mActor);
                                    const auto retryCommand = f.equipmentCommand(retryActor, retryEquip);
                                    const auto retryPath = scratch / "fresh-sink.bin";
                                    EquipmentFileSink fresh(retryPath);
                                    faults = {};
                                    Allocations::Trace retryTrace;
                                    {
                                        Allocations::Observe observe(retryTrace, 1);
                                        outcome = commands
                                            ? executeEquipment(f, { retryCommand.mActor }, retryCommand, freshSink ? fresh : file,
                                                { retryEnvelope, f.mStore, retryIds }, success, bytes, faults)
                                            : f.commitEquipment(retryActor, f.mActors[retryActor]->getPtr(), std::move(retry),
                                                freshSink ? fresh : file, { retryEnvelope, f.mStore, retryIds }, output, bytes, faults);
                                    }
                                    require(outcome == TestPersistenceResult::Uncertain && retryTrace.mTotal == 0
                                            && faults.mWrites == 0 && !std::filesystem::exists(retryPath),
                                        "post-restart uncertainty bypassed fixture through valid actor/fresh sink");
                                    unchanged();
                                    ++blocked;
                                }
                            ++uncertain;
                        }
            std::cout << "equipment post-restart safe failures/retries=" << safe << " uncertain=" << uncertain
                      << " blocked actor/sink retries=" << blocked << '\n';
        }

        static void checkRestartContinuationRecovery(
            const std::filesystem::path& scratch, bool allocations = false, bool commands = false)
        {
            EquipmentScratch directory(scratch);
            size_t recoveredFiles = 0, commits = 0, allocationFailures = 0;
            auto output = std::make_unique<const PlainEquipmentResult>();
            std::unique_ptr<const EquipmentSuccess> success;
            EquipmentBytes bytes;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool equip : { true, false })
                    for (auto failure : { FileFault::ReplaceError, FileFault::AfterReplace, FileFault::Barrier,
                             FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read, FileFault::ReadEof, FileFault::ReadClose })
                    {
                        // Existing recovery coverage checks every uncertain I/O
                        // boundary. Allocation sweeps need its two complete file
                        // outcomes, not a duplicate sweep per equivalent fault.
                        if (allocations && failure != FileFault::ReplaceError && failure != FileFault::AfterReplace)
                            continue;
                        const auto path = scratch / "recovery.bin";
                        PlainEquipmentValues recovery;
                        EquipmentBytes acceptedFile;
                        {
                            PlainEquipmentFixture f(actor);
                            f.startContinuation(actor, continuationSave(actor, equip, true), path);
                            EquipmentFileSink file(path);
                            f.commitContinuation(actor, !equip, file, path, output, bytes, 0, commands ? &success : nullptr);
                            const auto prior = f.installedValues(actor);
                            const auto priorBytes = equipmentFileBytes(path);
                            const auto outputValue = *output;
                            const auto* outputStorage = output.get();
                            const auto* byteStorage = bytes.data();
                            const auto* successStorage = success.get();
                            const auto successValue = success ? *success : EquipmentSuccess{};
                            const auto command = f.equipmentCommand(actor, equip);
                            auto prepared = f.prepareContinuation(actor, equip);
                            PlainEquipmentValues saved;
                            prepared.exportValues(f.preparationContext(actor), saved);
                            const auto ids = referenceIds(saved);
                            const auto e = envelope(saved.mActor);
                            const EquipmentBindings bindings{ e, f.mStore, ids };
                            EquipmentBytes newBytes;
                            encodeEquipment(saved, bindings, newBytes);
                            require(priorBytes != newBytes && f.mActorEffects[actor].mListener.mCalls != 0,
                                "subsequent uncertain commit requires distinct state and prior committed effects");
                            const auto before = f.snapshot();
                            FileFaults faults{ failure, 17 };
                            const auto outcome = commands
                                ? executeEquipment(f, { command.mActor }, command, file, bindings, success, bytes, faults)
                                : f.commitEquipment(actor, f.mActors[actor]->getPtr(), std::move(prepared),
                                    file, bindings, output, bytes, faults);
                            require(outcome == TestPersistenceResult::Uncertain
                                    && f.mFailedClosed && file.failedClosed()
                                    && output.get() == outputStorage && *output == outputValue
                                    && success.get() == successStorage && (!success || *success == successValue)
                                    && bytes.data() == byteStorage && bytes == priorBytes,
                                "subsequent uncertain commit installed or published unaccepted operation");
                            f.unchanged(before);
                            acceptedFile = equipmentFileBytes(path);
                            recovery = failure == FileFault::ReplaceError ? prior : saved;
                            require(acceptedFile == (failure == FileFault::ReplaceError ? priorBytes : newBytes),
                                "subsequent uncertain commit left a partial actor-local file");
                            // An uncertain composition cannot reuse consumed restart
                            // authority even with bindings from another fresh fixture.
                            PlainEquipmentFixture fresh(actor);
                            std::unique_ptr<const PlainEquipmentValues> rejectedOutput;
                            EquipmentBytes rejectedBytes{ 'o', 'l', 'd' };
                            faults = {};
                            bool rejected = false;
                            try
                            {
                                f.restartEquipment(actor, f.mActors[actor]->getPtr(), path, bindings,
                                    fresh.restartBindings(recovery.mLastGenerated), rejectedOutput, rejectedBytes, faults);
                            }
                            catch (const std::invalid_argument& error)
                            {
                                rejected = !std::string_view(error.what()).empty();
                            }
                            require(rejected && !rejectedOutput && rejectedBytes == EquipmentBytes({ 'o', 'l', 'd' })
                                    && faults.mReads == 0 && equipmentFileBytes(path) == acceptedFile,
                                "uncertain fixture restarted in place");
                            f.unchanged(before);
                        } // All old live nodes, services, sinks and preparation are gone.

                        PlainEquipmentFixture recovered(actor);
                        recovered.seedValues(1 - actor);
                        // This actor belongs to the new fixture, not to the file.
                        recovered.mItems[1 - actor].getCellRef().setCount(actor == 0 ? -7 : 4);
                        recovered.mInventories[1 - actor].setSelectedEnchantItem(recovered.mInventories[1 - actor].begin());
                        recovered.installContinuationFile(actor, recovery, path, acceptedFile);
                        require(recovered.mActorEffects[actor].mListener.mCalls == 0
                                && recovered.mActorEffects[actor].mListener.mRemovals.empty()
                                && recovered.mActorEffects[actor].mInventoryUpdates == 0
                                && recovered.mActorEffects[actor].mNotifications.empty(),
                            "fresh recovery replayed previously committed or uncertain operation effects");
                        EquipmentFileSink freshSink(path);
                        const auto otherPath = scratch / "recovery-other.bin";
                        if (allocations)
                        {
                            const auto other = recovered.installedValues(1 - actor);
                            const auto ids = referenceIds(other);
                            const auto e = envelope(other.mActor);
                            EquipmentFileSink initial(otherPath);
                            EquipmentBytes priorBytes;
                            FileFaults faults;
                            require(initial.write(other, { e, recovered.mStore, ids }, priorBytes, faults)
                                    == TestPersistenceResult::Accepted,
                                "recovery allocation other actor file setup failed");
                        }
                        const bool nextEquip = !recovery.mShirt.isSet();
                        for (bool next : { nextEquip, !nextEquip })
                        {
                            if (allocations)
                                allocationFailures += recovered.continuationAllocations(actor, next, path, otherPath, commands);
                            else
                                recovered.commitContinuation(actor, next, freshSink, path, output, bytes, 0, commands ? &success : nullptr);
                            ++commits;
                        }
                        const auto actorBytes = equipmentFileBytes(path);
                        EquipmentFileSink otherSink(otherPath);
                        for (bool next : { true, false })
                        {
                            if (allocations)
                                allocationFailures += recovered.continuationAllocations(1 - actor, next, otherPath, path, commands);
                            else
                                recovered.commitContinuation(1 - actor, next, otherSink, otherPath, output, bytes, 0, commands ? &success : nullptr);
                            require(equipmentFileBytes(path) == actorBytes, "recovery continuation crossed actor files");
                            ++commits;
                        }
                        ++recoveredFiles;
                    }
            require((commands ? success && success->mCommand.mActor.mIndex != 0 : output && !output->mItems.empty())
                    && !bytes.empty(), "recovery publication borrowed destroyed owner");
            std::cout << "equipment subsequent uncertain fresh recoveries=" << recoveredFiles
                      << " isolated continuation commits=" << commits
                      << " allocation failures=" << allocationFailures << '\n';
        }

        void rejectContinuationBoundary(size_t actor, bool equip, std::string_view diagnostic,
            const std::filesystem::path& path, const std::filesystem::path& otherPath, bool commands = false)
        {
            const auto prior = installedValues(actor);
            const auto ids = referenceIds(prior);
            const auto e = envelope(prior.mActor);
            auto output = std::make_unique<const PlainEquipmentResult>(PlainEquipmentResult{
                prior.mActor, prior.mShirt, prior.mSelected, prior.mLastGenerated, {}, {} });
            const auto* outputStorage = output.get();
            const auto outputValue = *output;
            auto success = std::make_unique<const EquipmentSuccess>();
            const auto* successStorage = success.get();
            const auto successValue = *success;
            auto bytes = equipmentFileBytes(path);
            const auto bytesValue = bytes;
            const auto* bytesStorage = bytes.data();
            const auto otherBytes = equipmentFileBytes(otherPath);
            const auto before = snapshot();
            EquipmentFileSink fresh(path);
            FileFaults faults;
            Allocations::Trace trace;
            bool rejected = false;
            {
                Allocations::Observe observe(trace);
                try
                {
                    if (commands)
                    {
                        const auto command = equipmentCommand(actor, equip);
                        executeEquipment(*this, { command.mActor }, command, fresh, { e, mStore, ids }, success, bytes, faults);
                    }
                    else
                        commitEquipment(actor, mActors[actor]->getPtr(), prepareContinuation(actor, equip),
                            fresh, { e, mStore, ids }, output, bytes, faults);
                }
                catch (const std::invalid_argument& error)
                {
                    rejected = std::string_view(error.what()).find(diagnostic) != std::string_view::npos;
                }
            }
            require(rejected && !mFailedClosed && !fresh.failedClosed() && faults.mWrites == 0
                    && trace.visits(Allocations::Phase::Persistence) == 0
                    && trace.visits(Allocations::Phase::Installation) == 0
                    && trace.visits(Allocations::Phase::Publication) == 0
                    && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                    && output.get() == outputStorage && *output == outputValue
                    && success.get() == successStorage && *success == successValue
                    && bytes.data() == bytesStorage && bytes == bytesValue
                    && equipmentFileBytes(path) == bytesValue && equipmentFileBytes(otherPath) == otherBytes
                    && !std::filesystem::exists(std::filesystem::path(path).concat(".tmp")),
                "continuation boundary failed without a diagnostic or changed output/files");
            unchanged(before); // Includes dormant members, exact IDs and both actors' effects.
            checkInstalledMembership(actor, prior);
        }

        static void checkRestartContinuationBoundaries(const std::filesystem::path& scratch, bool commands = false)
        {
            EquipmentScratch directory(scratch);
            size_t restarts = 0, recoveries = 0, rejected = 0, commits = 0;
            std::unique_ptr<const EquipmentSuccess> success;
            for (size_t actor = 0; actor < 2; ++actor)
                for (bool capacity : { true, false })
                    for (bool split : { true, false })
                        for (auto fault : { FileFault::None, FileFault::ReplaceError, FileFault::AfterReplace })
                        {
                            auto saved = continuationSave(actor, false, true);
                            saved.mShirt = {};
                            for (auto& object : saved.mObjects)
                                object.mRef.mCount = 0;
                            saved.mObjects.front().mRef.mCount = (actor == 0 ? 1 : -1) * (split ? 3 : 1);
                            saved.mSelected = saved.mObjects.back().mRef.mRefNum;
                            if (capacity)
                            {
                                while (saved.mObjects.size() < PreparedPlainEquipment::MaxItems)
                                {
                                    auto dormant = saved.mObjects.back();
                                    dormant.mRef.mRefNum = { ++saved.mLastGenerated.mIndex, -1 };
                                    saved.mObjects.push_back(std::move(dormant));
                                }
                            }
                            else
                                saved.mLastGenerated = { std::numeric_limits<uint32_t>::max() - (split ? 1u : 0u),
                                    std::numeric_limits<int32_t>::min() };
                            const auto path = scratch / "boundary.bin";
                            const auto otherPath = scratch / "boundary-other.bin";
                            EquipmentBytes accepted;
                            PlainEquipmentValues recovery;
                            {
                                PlainEquipmentFixture f(actor);
                                f.startContinuation(actor, saved, path);
                                EquipmentFileSink file(path);
                                std::unique_ptr<const PlainEquipmentResult> output;
                                EquipmentBytes bytes;
                                if (fault == FileFault::None)
                                {
                                    f.commitContinuation(actor, true, file, path, output, bytes, 0, commands ? &success : nullptr);
                                    recovery = f.installedValues(actor);
                                    accepted = bytes;
                                    ++commits;
                                }
                                else
                                {
                                    auto prepared = f.prepareContinuation(actor, true);
                                    PlainEquipmentValues next;
                                    prepared.exportValues(f.preparationContext(actor), next);
                                    const auto ids = referenceIds(next);
                                    const auto e = envelope(next.mActor);
                                    const EquipmentBindings bindings{ e, f.mStore, ids };
                                    EquipmentBytes newBytes;
                                    encodeEquipment(next, bindings, newBytes);
                                    const auto priorBytes = equipmentFileBytes(path);
                                    output = std::make_unique<const PlainEquipmentResult>(prepared.result());
                                    const auto* outputStorage = output.get();
                                    const auto outputValue = *output;
                                    bytes = priorBytes;
                                    const auto* byteStorage = bytes.data();
                                    const auto before = f.snapshot();
                                    FileFaults faults{ fault, 17 };
                                    success = std::make_unique<const EquipmentSuccess>();
                                    const auto* successStorage = success.get();
                                    const auto successValue = *success;
                                    const auto command = f.equipmentCommand(actor, true);
                                    const auto outcome = commands
                                        ? executeEquipment(f, { command.mActor }, command, file, bindings, success, bytes, faults)
                                        : f.commitEquipment(actor, f.mActors[actor]->getPtr(), std::move(prepared),
                                            file, bindings, output, bytes, faults);
                                    require(outcome == TestPersistenceResult::Uncertain
                                            && f.mFailedClosed && file.failedClosed() && output.get() == outputStorage
                                            && *output == outputValue && bytes.data() == byteStorage && bytes == priorBytes
                                            && success.get() == successStorage && *success == successValue,
                                        "boundary uncertain commit installed or published");
                                    f.unchanged(before);
                                    recovery = fault == FileFault::ReplaceError ? saved : next;
                                    accepted = equipmentFileBytes(path);
                                    require(accepted == (fault == FileFault::ReplaceError ? priorBytes : newBytes),
                                        "boundary recovery did not retain a complete prior/new file");
                                    ++recoveries;
                                }
                            } // Recover only owned values into another explicitly fresh fixture.
                            PlainEquipmentFixture f(actor);
                            f.seedValues(1 - actor);
                            // Its unrelated actor needs no new ID, even when the
                            // restored shared generation counter is exhausted.
                            f.mItems[1 - actor].getCellRef().setCount(actor == 0 ? -1 : 1);
                            f.installContinuationFile(actor, recovery, path, accepted);
                            ++restarts;
                            {
                                const auto other = f.installedValues(1 - actor);
                                const auto ids = referenceIds(other);
                                const auto e = envelope(other.mActor);
                                EquipmentFileSink initial(otherPath);
                                EquipmentBytes bytes;
                                FileFaults faults;
                                require(initial.write(other, { e, f.mStore, ids }, bytes, faults)
                                        == TestPersistenceResult::Accepted,
                                    "boundary other actor file setup failed");
                            }
                            EquipmentFileSink fresh(path);
                            std::unique_ptr<const PlainEquipmentResult> output;
                            EquipmentBytes bytes;
                            // Prior-file recovery still has the last permitted
                            // split available; new-file recovery already used it.
                            if (!recovery.mShirt.isSet())
                            {
                                f.commitContinuation(actor, true, fresh, path, output, bytes, 0, commands ? &success : nullptr);
                                ++commits;
                            }
                            if (capacity && split)
                            {
                                require(f.installedValues(actor).mObjects.size() == PlainEquipmentValues::MaxItems
                                        && f.mWorld.mPtrRegistry.mIndex.size() == PlainEquipmentValues::MaxItems + 3,
                                    "64-node split did not preserve the complete 65-node saved membership");
                                // Even unequip without a split remains outside the
                                // preparation bound. Do not prune dormant nodes.
                                for (bool equip : { false, true })
                                {
                                    f.rejectContinuationBoundary(actor, equip, "bounded plain clothing", path, otherPath, commands);
                                    ++rejected;
                                }
                            }
                            else
                            {
                                f.commitContinuation(actor, false, fresh, path, output, bytes, 0, commands ? &success : nullptr);
                                ++commits;
                                if (!capacity && split)
                                {
                                    require(f.mWorld.getLastGeneratedRefNum()
                                            == ESM::RefNum{ std::numeric_limits<uint32_t>::max(),
                                                std::numeric_limits<int32_t>::min() },
                                        "last split did not consume exactly the final generation");
                                    f.rejectContinuationBoundary(actor, true, "counter exhausted", path, otherPath, commands);
                                    ++rejected;
                                }
                                else
                                {
                                    const auto counter = f.mWorld.getLastGeneratedRefNum();
                                    const auto nodes = f.installedValues(actor).mObjects.size();
                                    f.commitContinuation(actor, true, fresh, path, output, bytes, 0, commands ? &success : nullptr);
                                    f.commitContinuation(actor, false, fresh, path, output, bytes, 0, commands ? &success : nullptr);
                                    commits += 2;
                                    require(f.mWorld.getLastGeneratedRefNum() == counter
                                            && f.installedValues(actor).mObjects.size() == nodes
                                            && (!capacity || nodes == PreparedPlainEquipment::MaxItems),
                                        "no-split continuation consumed generation or changed raw membership");
                                }
                            }
                            const auto actorFile = equipmentFileBytes(path);
                            EquipmentFileSink other(otherPath);
                            for (bool equip : { true, false })
                            {
                                f.commitContinuation(1 - actor, equip, other, otherPath, output, bytes, 0, commands ? &success : nullptr);
                                require(equipmentFileBytes(path) == actorFile,
                                    "boundary continuation changed restored actor file");
                                ++commits;
                            }
                        }
            std::cout << "equipment boundary fresh restarts=" << restarts << " prior/new recoveries=" << recoveries
                      << " visible rejections=" << rejected << " isolated commits=" << commits << '\n';
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
            if (values.mNpcStats)
                ids.push_back(values.mNpcStats->mBase);
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

        static void checkFileWrite(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            PlainEquipmentFixture f;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                f.seedValues(actor);
                auto prepared = f.prepare(actor, true);
                const auto effects = prepared.result();
                const auto before = f.snapshot();
                PlainEquipmentValues saved;
                prepared.exportValues(f.preparationContext(actor), saved);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                const EquipmentBindings bindings{ e, f.mStore, ids };
                const auto path = scratch / (actor == 0 ? "actor-a.bin" : "actor-b.bin");
                EquipmentFileSink file(path);
                EquipmentBytes output{ 'o', 'l', 'd' }, expected;
                encodeEquipment(saved, bindings, expected);
                FileFaults faults{ FileFault::None, 17 };
                require(file.write(saved, bindings, output, faults) == TestPersistenceResult::Accepted
                        && !file.failedClosed() && output == expected && equipmentFileBytes(path) == expected
                        && faults.mWrites > 1 && faults.mReads > 1,
                    "equipment file did not persist complete encoded values with short I/O");
                PlainEquipmentValues decoded;
                decodeEquipment(output, bindings, decoded);
                require(sameValues(saved, decoded), "equipment file write lost supported values");
                const auto* storage = output.data();
                auto invalid = saved;
                invalid.mActor = f.mActors[1 - actor]->getPtr().getCellRef().getRefNum();
                faults = {};
                f.reject([&] { file.write(invalid, bindings, output, faults); }, "owner");
                require(output == expected && output.data() == storage && faults.mWrites == 0
                        && equipmentFileBytes(path) == expected && prepared.result() == effects,
                    "equipment binding rejection touched file/output/effects");
                f.unchanged(before);
            }
            std::cout << "equipment bound file writes=2\n";
        }

        static void checkRestore(bool codec = false, const std::filesystem::path& scratch = {})
        {
            std::optional<EquipmentScratch> directory;
            if (!scratch.empty())
                directory.emplace(scratch);
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
                    std::unique_ptr<const RestoredPlainEquipment> fromFile;
                    if (codec)
                    {
                        // Exercise stock clamping/omissions, including fields
                        // whose door/lock switch is off and negative anim time.
                        auto& object = saved.mObjects[0];
                        object.mRef.mScale = actor == 0 ? 0.125f : 8.f;
                        object.mRef.mTeleport = false;
                        object.mRef.mIsLocked = false;
                        object.mAnimationState.mScriptedAnims[0].mTime = -3.5f;
                        if (directory)
                        {
                            auto shirt = *f.mItems[actor].get<ESM::Clothing>()->mBase;
                            shirt.mId = ESM::RefId::stringRefId("equipment_file_second_shirt");
                            f.mStore.insertStatic(shirt);
                            object.mRef.mRefID = shirt.mId;
                        }
                        const auto expected = saved;
                        const auto ids = referenceIds(saved);
                        const auto e = envelope(saved.mActor);
                        const EquipmentBindings bindings{ e, f.mStore, ids };
                        encodeEquipment(saved, bindings, bytes);
                        decodeEquipment(bytes, bindings, saved); // Replace a nonempty output.
                        require(sameValues(saved, expected), "equipment byte codec lost supported values");
                        if (directory)
                        {
                            const auto path = scratch / "equipment.bin";
                            EquipmentFileSink file(path);
                            EquipmentBytes committed;
                            FileFaults faults{ FileFault::None, 17 };
                            require(file.write(saved, bindings, committed, faults) == TestPersistenceResult::Accepted
                                    && committed == bytes,
                                "equipment restart fixture write failed");
                            faults = { FileFault::None, 13 };
                            require(restartEquipmentFile(path, bindings, fromFile, faults) == FileReadResult::Read
                                    && faults.mReads > 1,
                                "equipment detached file restart failed");
                            PlainEquipmentValues fileValues;
                            fromFile->exportValues(fileValues);
                            require(sameValues(fileValues, expected), "equipment file restart lost complete values");
                        }
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
                    if (fromFile)
                    {
                        fromFile->exportValues(independent);
                        require(sameValues(again, independent), "file restart borrowed mutable input values");
                        EquipmentBytes reencoded;
                        const auto ids = referenceIds(independent);
                        const auto e = envelope(independent.mActor);
                        encodeEquipment(independent, { e, f.mStore, ids }, reencoded);
                        require(reencoded == bytes, "equipment file restart re-encoding changed saved bytes");
                    }
                    f.unchanged(before);
                    ++roundTrips;
                }
            std::cout << "equipment " << (directory ? "file/detached" : codec ? "byte/detached" : "detached")
                      << " round trips=" << roundTrips
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

        static void checkFileGuards(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            size_t safe = 0, uncertain = 0, rejected = 0;
            for (size_t actor = 0; actor < 2; ++actor)
            {
                PlainEquipmentFixture f;
                f.seedValues(actor);
                for (auto& inventory : f.mInventories)
                {
                    inventory.setInvListener(&f.mListener);
                    inventory.setContListener(&f.mListener);
                    inventory.setSelectedEnchantItem(inventory.begin());
                }
                auto prepared = f.prepare(actor, true);
                const auto effects = prepared.result();
                const auto* effectStorage = prepared.result().mEffects.data();
                const auto before = f.snapshot();
                PlainEquipmentValues saved, oldValues;
                prepared.exportValues(f.preparationContext(actor), saved);
                f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), oldValues);
                const auto ids = referenceIds(saved);
                const auto e = envelope(saved.mActor);
                const EquipmentBindings bindings{ e, f.mStore, ids };
                auto prior = saved;
                prior.mObjects[0].mRef.mGlobalVariable += "_prior";
                EquipmentBytes priorBytes, newBytes;
                encodeEquipment(prior, bindings, priorBytes);
                encodeEquipment(saved, bindings, newBytes);
                require(priorBytes != newBytes, "file guards lost distinct prior/new bytes");
                const auto path = scratch / "committed.bin";
                const auto input = scratch / "external.bin";
                auto temporary = path;
                temporary += ".tmp";
                const auto seed = [&] {
                    EquipmentFileSink file(path);
                    FileFaults faults;
                    EquipmentBytes bytes;
                    require(file.write(prior, bindings, bytes, faults) == TestPersistenceResult::Accepted,
                        "equipment prior file setup failed");
                };
                seed();
                EquipmentBytes output{ 'o', 'l', 'd' };
                const auto outputValue = output;
                const auto* outputStorage = output.data();
                std::unique_ptr<const RestoredPlainEquipment> restored = std::make_unique<RestoredPlainEquipment>(
                    RestoredPlainEquipment::restore(oldValues, f.mStore, oldValues.mActor));
                const auto* restoredOutput = restored.get();
                const auto* restoredStorage = restored->mState.get();
                const auto unchanged = [&] {
                    require(output == outputValue && output.data() == outputStorage
                            && restored.get() == restoredOutput && restored->mState.get() == restoredStorage
                            && prepared.result() == effects && prepared.result().mEffects.data() == effectStorage,
                        "equipment file failure changed output storage/value or captured effects");
                    PlainEquipmentValues retained;
                    restored->exportValues(retained);
                    require(sameValues(retained, oldValues), "equipment file failure changed detached nodes");
                    f.unchanged(before);
                };
                EquipmentFileSink file(path);
                for (auto failure :
                    { FileFault::Create, FileFault::Write, FileFault::Flush, FileFault::Close, FileFault::Replace })
                {
                    FileFaults faults{ failure, 17 };
                    require(file.write(saved, bindings, output, faults) == TestPersistenceResult::Rejected
                            && !file.failedClosed() && equipmentFileBytes(path) == priorBytes
                            && !std::filesystem::exists(temporary),
                        "pre-replacement equipment failure changed committed bytes or poisoned sink");
                    require(failure != FileFault::Write || faults.mWrites == 1, "equipment short write seam missed");
                    unchanged();
                    ++safe;
                }
                writeEquipmentInput(temporary, priorBytes);
                FileFaults faults;
                require(file.write(saved, bindings, output, faults) == TestPersistenceResult::Rejected
                        && equipmentFileBytes(temporary) == priorBytes && equipmentFileBytes(path) == priorBytes,
                    "equipment exclusive staging touched another writer's file");
                std::filesystem::remove(temporary);
                unchanged();
                ++safe;
                for (auto failure : { FileFault::ReplaceError, FileFault::AfterReplace, FileFault::Barrier,
                         FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read, FileFault::ReadEof,
                         FileFault::ReadClose })
                {
                    seed();
                    EquipmentFileSink uncertainFile(path);
                    faults = { failure, 17 };
                    require(uncertainFile.write(saved, bindings, output, faults) == TestPersistenceResult::Uncertain
                            && uncertainFile.failedClosed() && !std::filesystem::exists(temporary),
                        "uncertain equipment replacement emitted acceptance or leaked staging");
                    const auto bytes = equipmentFileBytes(path);
                    require(bytes == (failure == FileFault::ReplaceError ? priorBytes : newBytes),
                        "uncertain equipment replacement left a partial file");
                    // A poisoned adapter cannot even allocate/encode on retry.
                    faults = {};
                    Allocations::Trace trace;
                    TestPersistenceResult blocked;
                    {
                        Allocations::Observe observe(trace, 1);
                        blocked = uncertainFile.write(saved, bindings, output, faults);
                    }
                    require(blocked == TestPersistenceResult::Uncertain && trace.mTotal == 0
                            && faults.mWrites == 0 && faults.mReads == 0 && equipmentFileBytes(path) == bytes,
                        "uncertain equipment sink allowed an in-place retry");
                    unchanged();
                    std::unique_ptr<const RestoredPlainEquipment> recovered;
                    require(restartEquipmentFile(path, bindings, recovered, faults) == FileReadResult::Read,
                        "uncertain equipment file could not restart detached");
                    PlainEquipmentValues recoveredValues;
                    recovered->exportValues(recoveredValues);
                    EquipmentBytes reencoded;
                    encodeEquipment(recoveredValues, bindings, reencoded);
                    require(reencoded == bytes, "uncertain restart did not preserve complete prior/new state");
                    ++uncertain;
                }
                seed();
                for (auto failure : { FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read,
                         FileFault::ReadEof, FileFault::ReadClose })
                {
                    faults = { failure, 17 };
                    require(readEquipmentFile(path, output, faults) == FileReadResult::Unavailable,
                        "equipment failed read published bytes");
                    faults = { failure, 17 };
                    require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Unavailable,
                        "equipment failed read published a restart");
                    unchanged();
                    ++rejected;
                }
                const auto rejectInput = [&](std::span<const char> bytes, const EquipmentBindings& expected) {
                    writeEquipmentInput(input, bytes);
                    faults = { FileFault::None, 17 };
                    bool failed = false;
                    try
                    {
                        restartEquipmentFile(input, expected, restored, faults);
                    }
                    catch (const std::invalid_argument&)
                    {
                        failed = true;
                    }
                    require(failed, "equipment file accepted malformed or mismatched input");
                    unchanged();
                    require(equipmentFileBytes(path) == priorBytes, "equipment input failure changed committed file");
                    ++rejected;
                };
                for (size_t length : { size_t{ 0 }, size_t{ 1 }, size_t{ 15 }, size_t{ 16 },
                         fieldAt(newBytes, ESM::fourCC("ACTR")).mData + 3, newBytes.size() / 2, newBytes.size() - 1 })
                    rejectInput(std::span(newBytes).first(length), bindings);
                for (int corruption = 0; corruption < 10; ++corruption)
                {
                    auto bad = newBytes;
                    switch (corruption)
                    {
                        case 0:
                            bad.push_back('!');
                            break;
                        case 1:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("FVER")).mData, 4);
                            break;
                        case 2:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("SIZE")).mData, 66);
                            break;
                        case 3:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("LGEN")).mData, 1);
                            break;
                        case 4:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("SHRT")).mData, oldValues.mActor.mIndex);
                            break;
                        case 5:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("SELE")).mData + 4, 0);
                            break;
                        case 6:
                            bad[fieldAt(bad, ESM::fourCC("NAME")).mData + 1] = '!';
                            break;
                        case 7:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("NAM9")).mData, 0x80000000);
                            break;
                        case 8:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("FLAG")).mData, 8);
                            break;
                        case 9:
                            putNumber(bad, fieldAt(bad, ESM::fourCC("XSCL")).mData,
                                std::bit_cast<uint32_t>(1.75f));
                            break;
                    }
                    rejectInput(bad, bindings);
                }
                for (int mismatch = 0; mismatch < 5; ++mismatch)
                {
                    auto foreign = e;
                    auto foreignIds = ids;
                    ESMStore absentContent;
                    if (mismatch == 0)
                        foreign.mRuntime += "_other";
                    if (mismatch == 1)
                        foreign.mContent[0] ^= 1;
                    if (mismatch == 2)
                        foreign.mActor = oldValues.mActor;
                    if (mismatch == 3)
                        foreignIds.clear();
                    rejectInput(newBytes, { foreign, mismatch == 4 ? absentContent : f.mStore, foreignIds });
                }
                // Sparse over-bound file: both paths must reject size before even
                // the first C++ allocation or data read on the opened handle.
                writeEquipmentInput(input, {});
                std::filesystem::resize_file(input, MaxEquipmentBytes + 1);
                for (bool restart : { false, true })
                {
                    faults = {};
                    Allocations::Trace trace;
                    FileReadResult result;
                    {
                        Allocations::Observe observe(trace, 1);
                        result = restart ? restartEquipmentFile(input, bindings, restored, faults)
                                         : readEquipmentFile(input, output, faults);
                    }
                    require(result == FileReadResult::TooLarge && trace.mTotal == 0 && faults.mReads == 0,
                        "equipment oversized file allocated/read before checking size");
                    unchanged();
                    ++rejected;
                }
                std::filesystem::remove(input);
                for (const auto& unavailable : { input, scratch })
                {
                    faults = {};
                    require(readEquipmentFile(unavailable, output, faults) == FileReadResult::Unavailable,
                        "equipment read accepted absent/nonregular file");
                    faults = {};
                    require(restartEquipmentFile(unavailable, bindings, restored, faults) == FileReadResult::Unavailable,
                        "equipment restart accepted absent/nonregular file");
                    unchanged();
                    ++rejected;
                }
                require(equipmentFileBytes(path) == priorBytes, "equipment read guards changed committed bytes");
                // All safe rejections leave the original adapter usable.
                faults = {};
                require(file.write(saved, bindings, output, faults) == TestPersistenceResult::Accepted
                        && output == newBytes && equipmentFileBytes(path) == newBytes,
                    "equipment retry after safe rejection did not persist exact bytes");
                faults = {};
                require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Read,
                    "equipment retry after read rejection failed");
                PlainEquipmentValues retried;
                restored->exportValues(retried);
                require(sameValues(saved, retried) && prepared.result() == effects,
                    "equipment retry lost values or changed captured effects");
                f.unchanged(before);
            }
            std::cout << "equipment file safe-rejections=" << safe << " uncertain=" << uncertain
                      << " read/input-rejections=" << rejected << '\n';
        }

        static void checkFileBounds(const std::filesystem::path& scratch)
        {
            EquipmentScratch directory(scratch);
            const auto path = scratch / "equipment.bin";
            PlainEquipmentValues expected;
            EquipmentEnvelope e;
            std::vector<ESM::RefId> ids;
            EquipmentBytes committed;
            {
                PlainEquipmentFixture source;
                source.seedValues(1);
                auto prepared = source.prepare(1, true);
                prepared.exportValues(source.preparationContext(1), expected);
                e = envelope(expected.mActor);
                ids = referenceIds(expected);
                EquipmentFileSink file(path);
                FileFaults faults;
                require(file.write(expected, { e, source.mStore, ids }, committed, faults)
                        == TestPersistenceResult::Accepted,
                    "equipment lifetime fixture write failed");
            }
            PlainEquipmentFixture replacement;
            const auto before = replacement.snapshot();
            const EquipmentBindings bindings{ e, replacement.mStore, ids };
            std::unique_ptr<const RestoredPlainEquipment> restored;
            FileFaults faults;
            require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Read,
                "equipment restart depended on destroyed source actors/content");
            std::filesystem::remove(path);
            PlainEquipmentValues actual;
            restored->exportValues(actual);
            EquipmentBytes reencoded;
            encodeEquipment(actual, bindings, reencoded);
            require(sameValues(expected, actual) && reencoded == committed,
                "equipment restored output borrowed source/file storage");

            // Valid equipment larger than transfer's 8 MiB cap must traverse
            // write, post-replacement verification, read, decode and restore.
            auto object = expected.mObjects[0];
            object.mRef.mCount = -1;
            auto animation = object.mAnimationState.mScriptedAnims[0];
            animation.mGroup.assign(PlainEquipmentValues::MaxText, 'a');
            object.mAnimationState.mScriptedAnims.assign(PlainEquipmentValues::MaxAnimations, animation);
            expected.mObjects.assign(9, object);
            for (size_t i = 0; i < expected.mObjects.size(); ++i)
                expected.mObjects[i].mRef.mRefNum = { static_cast<uint32_t>(100 + i), -1 };
            expected.mObjects.back().mRef.mCount = 0;
            expected.mShirt = expected.mObjects.front().mRef.mRefNum;
            expected.mSelected = expected.mObjects.back().mRef.mRefNum;
            expected.mLastGenerated = { 900, -2 };
            EquipmentFileSink file(path);
            faults = {};
            require(file.write(expected, bindings, committed, faults) == TestPersistenceResult::Accepted
                    && committed.size() > 8 * 1024 * 1024 && committed.size() <= MaxEquipmentBytes,
                "equipment file reused transfer's smaller bound");
            faults = {};
            require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Read,
                "large equipment file restart failed");
            restored->exportValues(actual);
            encodeEquipment(actual, bindings, reencoded);
            require(sameValues(expected, actual) && reencoded == committed,
                "large equipment file lost animations, signed/dormant values or exact counters");
            const size_t largeBytes = committed.size();
            expected.mObjects.clear();
            expected.mShirt = {};
            expected.mSelected = {};
            faults = {};
            require(file.write(expected, bindings, committed, faults) == TestPersistenceResult::Accepted,
                "empty equipment file write failed");
            faults = {};
            require(restartEquipmentFile(path, bindings, restored, faults) == FileReadResult::Read,
                "empty equipment file restart failed");
            restored->exportValues(actual);
            require(sameValues(expected, actual), "empty equipment file lost actor or saved counter");
            replacement.unchanged(before);
            std::cout << "equipment file bounds/lifetime cases=3 large-file-bytes=" << largeBytes << '\n';
        }

        static void checkFileAllocations(const std::filesystem::path& scratch)
        {
            using namespace Allocations;
            EquipmentScratch directory(scratch);
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
                    const auto before = f.snapshot();
                    PlainEquipmentValues saved, oldValues;
                    prepared.exportValues(f.preparationContext(actor), saved);
                    f.prepare(1 - actor, true).exportValues(f.preparationContext(1 - actor), oldValues);
                    const auto ids = referenceIds(saved);
                    const auto e = envelope(saved.mActor);
                    const EquipmentBindings bindings{ e, f.mStore, ids };
                    const auto path = scratch / "equipment.bin";
                    auto temporary = path;
                    temporary += ".tmp";
                    auto prior = saved;
                    prior.mObjects[0].mRef.mGlobalVariable += "_prior";
                    EquipmentBytes priorBytes, newBytes;
                    encodeEquipment(prior, bindings, priorBytes);
                    encodeEquipment(saved, bindings, newBytes);
                    const auto seed = [&] {
                        EquipmentFileSink initial(path);
                        EquipmentBytes bytes;
                        FileFaults faults;
                        require(initial.write(prior, bindings, bytes, faults) == TestPersistenceResult::Accepted,
                            "equipment allocation prior setup failed");
                    };
                    seed();
                    EquipmentFileSink file(path);
                    FileFaults faults;
                    EquipmentBytes output{ 'o', 'l', 'd' };
                    const auto outputValue = output;
                    const auto* outputStorage = output.data();
                    std::unique_ptr<const RestoredPlainEquipment> restored = std::make_unique<RestoredPlainEquipment>(
                        RestoredPlainEquipment::restore(oldValues, f.mStore, oldValues.mActor));
                    const auto* restoredOutput = restored.get();
                    const auto* restoredStorage = restored->mState.get();
                    for (int operation = 0; operation < 5; ++operation)
                    {
                        const auto run = [&](EquipmentBytes& bytes, std::unique_ptr<const RestoredPlainEquipment>& owner) {
                            if (operation == 0)
                            {
                                EquipmentFileSink construction(path);
                                return true;
                            }
                            if (operation == 1)
                                return file.write(saved, bindings, bytes, faults) == TestPersistenceResult::Accepted;
                            if (operation == 2)
                                return readEquipmentFile(path, bytes, faults) == FileReadResult::Read;
                            if (operation == 3)
                                return restartEquipmentFile(path, bindings, owner, faults) == FileReadResult::Read;
                            // Stage re-export/re-encoding too, then publish both
                            // complete owned outputs without a fallible step.
                            std::unique_ptr<const RestoredPlainEquipment> staged;
                            if (restartEquipmentFile(path, bindings, staged, faults) != FileReadResult::Read)
                                return false;
                            PlainEquipmentValues values;
                            staged->exportValues(values);
                            EquipmentBytes encoded;
                            encodeEquipment(values, bindings, encoded);
                            bytes.swap(encoded);
                            owner = std::move(staged);
                            return true;
                        };
                        {
                            EquipmentBytes bytes;
                            std::unique_ptr<const RestoredPlainEquipment> owner;
                            faults = {};
                            require(run(bytes, owner), "equipment file allocation warmup failed");
                        }
                        seed();
                        Trace count;
                        bool accepted = false;
                        faults = {};
                        {
                            Observe observe(count);
                            EquipmentBytes bytes;
                            std::unique_ptr<const RestoredPlainEquipment> owner;
                            accepted = run(bytes, owner);
                        }
                        require(accepted && count.mTotal > 0 && count.mOutstanding == 0 && count.mTrackingOverflow == 0,
                            "equipment file allocation baseline missed work or leaked");
                        seed();
                        for (size_t fail = 1; fail <= count.mTotal; ++fail)
                        {
                            Trace trace;
                            bool failed = false;
                            accepted = false;
                            faults = {};
                            {
                                Observe observe(trace, fail);
                                try
                                {
                                    accepted = run(output, restored);
                                }
                                catch (const std::exception&)
                                {
                                    failed = true;
                                }
                            }
                            if (!failed || accepted || trace.mFailures != 1 || trace.mOutstanding != 0)
                                std::cerr << "equipment file allocation operation=" << operation << " fail=" << fail
                                          << " rejected=" << failed << " accepted=" << accepted
                                          << " injected=" << trace.mFailures << " outstanding=" << trace.mOutstanding << '\n';
                            require(failed && !accepted && trace.mFailures == 1 && trace.mOutstanding == 0
                                    && trace.mTrackingOverflow == 0 && faults.mWrites == 0 && !file.failedClosed()
                                    && output == outputValue && output.data() == outputStorage
                                    && restored.get() == restoredOutput && restored->mState.get() == restoredStorage
                                    && equipmentFileBytes(path) == priorBytes && !std::filesystem::exists(temporary)
                                    && prepared.result() == effects && prepared.result().mEffects.data() == effectStorage,
                                "equipment file allocation failure changed prior bytes/output/effects");
                            PlainEquipmentValues retained;
                            restored->exportValues(retained);
                            require(sameValues(retained, oldValues), "equipment allocation failure changed owned nodes");
                            f.unchanged(before);
                            ++failures;
                        }
                        // Fail just beyond the measured operation: success must
                        // neither allocate after replacement nor grow on retry.
                        Trace retry;
                        faults = {};
                        {
                            Observe observe(retry, count.mTotal + 1);
                            EquipmentBytes bytes;
                            std::unique_ptr<const RestoredPlainEquipment> owner;
                            accepted = run(bytes, owner);
                        }
                        require(accepted && retry.mFailures == 0 && retry.mTotal == count.mTotal
                                && retry.mOutstanding == 0 && retry.mTrackingOverflow == 0
                                && equipmentFileBytes(path) == (operation == 1 ? newBytes : priorBytes),
                            "equipment file deterministic retry failed or leaked");
                        seed();
                    }
                    require(prepared.result() == effects, "equipment file allocation checks changed effect intents");
                    f.unchanged(before);
                }
            std::cout << "equipment file allocation failures=" << failures << " remaining-after-cleanup=0\n";
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

    TestPersistenceResult executeEquipment(PlainEquipmentFixture& fixture, EquipmentCaller caller,
        EquipmentCommand command, EquipmentFileSink& file, const EquipmentBindings& bindings,
        std::unique_ptr<const EquipmentSuccess>& output, std::vector<char>& bytes, FileFaults& faults)
    {
        return fixture.execute(caller, command, file, bindings, output, bytes, faults);
    }

    void checkPlainEquipment(std::string_view filter, const std::filesystem::path& scratch)
    {
        if (filter == "inventory-equipment-enchanted-allocations")
        {
            PlainEquipmentFixture::checkEnchantedAllocations(scratch);
            return;
        }
        if (filter == "inventory-equipment-enchanted-guards")
        {
            PlainEquipmentFixture::checkEnchantedGuards(scratch);
            return;
        }
        if (filter == "inventory-equipment-enchanted-durability")
        {
            PlainEquipmentFixture::checkEnchantedDurability(scratch);
            return;
        }
        if (filter == "inventory-equipment-enchanted")
        {
            PlainEquipmentFixture::checkEnchanted(scratch);
            return;
        }
        if (filter == "inventory-equipment-command")
        {
            PlainEquipmentFixture::checkCommand(scratch);
            return;
        }
        if (filter == "inventory-equipment-command-guards")
        {
            PlainEquipmentFixture::checkCommandGuards(scratch);
            return;
        }
        if (filter == "inventory-equipment-command-boundaries")
        {
            PlainEquipmentFixture::checkRestartContinuationBoundaries(scratch, true);
            return;
        }
        if (filter == "inventory-equipment-command-restart")
        {
            PlainEquipmentFixture::checkRestartContinuation(scratch, true);
            return;
        }
        if (filter == "inventory-equipment-command-allocations")
        {
            PlainEquipmentFixture::checkRestartContinuationAllocations(scratch, true);
            return;
        }
        if (filter == "inventory-equipment-command-persistence")
        {
            PlainEquipmentFixture::checkRestartContinuationPersistence(scratch, true);
            return;
        }
        if (filter == "inventory-equipment-command-recovery")
        {
            PlainEquipmentFixture::checkRestartContinuationRecovery(scratch, false, true);
            return;
        }
        if (filter == "inventory-equipment-command-recovery-allocations")
        {
            PlainEquipmentFixture::checkRestartContinuationRecovery(scratch, true, true);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-boundaries")
        {
            PlainEquipmentFixture::checkRestartContinuationBoundaries(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-allocations")
        {
            PlainEquipmentFixture::checkRestartContinuationAllocations(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-recovery-allocations")
        {
            PlainEquipmentFixture::checkRestartContinuationRecovery(scratch, true);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-recovery")
        {
            PlainEquipmentFixture::checkRestartContinuationRecovery(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-guards")
        {
            PlainEquipmentFixture::checkRestartContinuationGuards(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation-persistence")
        {
            PlainEquipmentFixture::checkRestartContinuationPersistence(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-continuation")
        {
            PlainEquipmentFixture::checkRestartContinuation(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-staging")
        {
            PlainEquipmentFixture::checkRestartStaging();
            return;
        }
        if (filter == "inventory-equipment-restart")
        {
            PlainEquipmentFixture::checkRestartSuccess(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-guards")
        {
            PlainEquipmentFixture::checkRestartGuards();
            return;
        }
        if (filter == "inventory-equipment-restart-read")
        {
            PlainEquipmentFixture::checkRestartReadFailures(scratch);
            return;
        }
        if (filter == "inventory-equipment-restart-allocations")
        {
            PlainEquipmentFixture::checkRestartAllocations(scratch);
            return;
        }
        if (filter == "inventory-equipment-commit-guards")
        {
            PlainEquipmentFixture::checkCommitGuards(scratch);
            return;
        }
        if (filter == "inventory-equipment-commit-persistence")
        {
            PlainEquipmentFixture::checkCommitPersistence(scratch);
            return;
        }
        if (filter == "inventory-equipment-commit-allocations")
        {
            PlainEquipmentFixture::checkCommitAllocations(scratch);
            return;
        }
        if (filter == "inventory-equipment-commit")
        {
            PlainEquipmentFixture::checkDurableSuccess(scratch);
            return;
        }
        if (filter == "inventory-equipment-installation")
        {
            PlainEquipmentFixture::checkInstallationPreparation();
            return;
        }
        if (filter == "inventory-equipment-file-bounds")
        {
            PlainEquipmentFixture::checkFileBounds(scratch);
            return;
        }
        if (filter == "inventory-equipment-file-allocations")
        {
            PlainEquipmentFixture::checkFileAllocations(scratch);
            return;
        }
        if (filter == "inventory-equipment-file-guards")
        {
            PlainEquipmentFixture::checkFileGuards(scratch);
            return;
        }
        if (filter == "inventory-equipment-file-restart")
        {
            PlainEquipmentFixture::checkRestore(true, scratch);
            return;
        }
        if (filter == "inventory-equipment-file-write")
        {
            PlainEquipmentFixture::checkFileWrite(scratch);
            return;
        }
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
