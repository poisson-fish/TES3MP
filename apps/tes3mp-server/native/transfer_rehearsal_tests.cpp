#include "test_allocations.hpp"
#include "transfer_rehearsal.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <new>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/readerscache.hpp>

namespace MWWorld::Testing
{
    namespace
    {
        void require(bool condition, const char* message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }

        auto nodeState(const ConstPtr& item)
        {
            const auto& ref = item.getCellRef();
            const auto& data = item.getRefData();
            const auto& locals = data.getLocals();
            auto detached = data.copyForContainerTransfer();
            detached.getLocals() = {};
            ESM::ObjectState state;
            detached.write(state);
            std::vector<std::tuple<std::string, float, bool, uint64_t>> animations;
            for (const auto& animation : data.getAnimationState().mScriptedAnims)
                animations.emplace_back(animation.mGroup, animation.mTime, animation.mAbsolute, animation.mLoopCount);
            return std::tuple{ item.mRef, item.getReferenceLifetime(), item.mRef->mWorldModel, ref.getRefNum(),
                ref.getCount(false), ref.getSoul(), ref.getCharge(), ref.getChargeIntRemainder(),
                ref.getEnchantmentCharge(), ref.hasChanged(), ref.getPosition(), ref.getOwner(), data.hasChanged(),
                data.isEnabled(), data.getPosition(), state.mFlags, data.mPhysicsPostponed, data.getBaseNode(),
                data.getCustomData(), data.getLuaScripts(), locals.getScriptId(), locals.mShorts, locals.mLongs,
                locals.mFloats, locals.mShorts.data(), locals.mLongs.data(), locals.mFloats.data(),
                locals.mShorts.capacity(), locals.mLongs.capacity(), locals.mFloats.capacity(),
                data.getAnimationState().mScriptedAnims.data(), data.getAnimationState().mScriptedAnims.capacity(),
                animations, data.isDeletedByContentFile(), ref.getRefId(), ref.getGlobalVariable(), ref.getFaction(),
                ref.getFactionRank(), ref.getScale(), ref.getTeleport(), ref.getDoorDest(), ref.getDestCell(),
                ref.getLockLevel(), ref.getKey(), ref.getTrap() };
        }

        auto snapshot(const DisposableTransferRehearsal& fixture)
        {
            std::vector<decltype(nodeState({}))> nodes;
            std::vector<const void*> registryNodes;
            for (const auto& [id, ptr] : fixture.mModel.getPtrRegistryView())
            {
                nodes.push_back(nodeState(ptr));
                registryNodes.push_back(fixture.registryNode(id));
            }
            std::vector<const void*> source, destination, other;
            for (const auto& node : fixture.sourceStorage())
                source.push_back(&node);
            for (const auto& node : fixture.destinationStorage())
                destination.push_back(&node);
            for (const auto& node : fixture.otherStorage())
                other.push_back(&node);
            return std::tuple{ nodes, registryNodes, source, destination, other, fixture.mModel.snapshotPtrRegistry(),
                fixture.mSourceScripts.snapshot(), fixture.mDestinationScripts.snapshot(),
                fixture.scriptNodes(fixture.mSourceScripts), fixture.scriptNodes(fixture.mDestinationScripts),
                fixture.scriptCursor(fixture.mSourceScripts), fixture.scriptCursor(fixture.mDestinationScripts),
                fixture.mSource.getSelectedEnchantItem(), fixture.mDestination.getSelectedEnchantItem(),
                fixture.mOther.getSelectedEnchantItem(), fixture.cacheState(fixture.mSource),
                fixture.cacheState(fixture.mDestination), fixture.cacheState(fixture.mOther), fixture.mNotifications };
        }

        struct Failure
        {
        };
        struct Listener : ContainerStoreListener
        {
            int mCalls = 0;
            void itemAdded(const ConstPtr&, int) override { ++mCalls; }
            void itemRemoved(const ConstPtr&, int) override { ++mCalls; }
        };

        void checkAllocationHooks()
        {
            // Direct calls cannot be elided like new-expressions. Exercise all
            // eight allocation overloads, zero size and matching delete families.
            struct Route
            {
                void* (*mAllocate)(size_t);
                void (*mFree)(void*, size_t);
                bool mThrows;
                size_t mAlignment;
            };
            const std::array routes{
                Route{ [](size_t n) { return ::operator new(n); }, [](void* p, size_t) { ::operator delete(p); }, true,
                    alignof(std::max_align_t) },
                Route{ [](size_t n) { return ::operator new[](n); },
                    [](void* p, size_t n) { ::operator delete[](p, n); }, true, alignof(std::max_align_t) },
                Route{ [](size_t n) { return ::operator new(n, std::nothrow); },
                    [](void* p, size_t) { ::operator delete(p, std::nothrow); }, false, alignof(std::max_align_t) },
                Route{ [](size_t n) { return ::operator new[](n, std::nothrow); },
                    [](void* p, size_t) { ::operator delete[](p, std::nothrow); }, false, alignof(std::max_align_t) },
                Route{ [](size_t n) { return ::operator new(n, std::align_val_t{ 64 }); },
                    [](void* p, size_t) { ::operator delete(p, std::align_val_t{ 64 }); }, true, 64 },
                Route{ [](size_t n) { return ::operator new[](n, std::align_val_t{ 64 }); },
                    [](void* p, size_t n) { ::operator delete[](p, n, std::align_val_t{ 64 }); }, true, 64 },
                Route{ [](size_t n) { return ::operator new(n, std::align_val_t{ 64 }, std::nothrow); },
                    [](void* p, size_t) { ::operator delete(p, std::align_val_t{ 64 }, std::nothrow); }, false, 64 },
                Route{ [](size_t n) { return ::operator new[](n, std::align_val_t{ 64 }, std::nothrow); },
                    [](void* p, size_t) { ::operator delete[](p, std::align_val_t{ 64 }, std::nothrow); }, false, 64 }
            };
            for (const auto& route : routes)
                for (size_t size : { size_t{ 0 }, size_t{ 17 } })
                {
                    Allocations::Trace trace;
                    void* value;
                    bool aligned;
                    size_t outstanding;
                    {
                        Allocations::Observe observe(trace);
                        value = route.mAllocate(size);
                        aligned = value && reinterpret_cast<std::uintptr_t>(value) % route.mAlignment == 0;
                        outstanding = trace.mOutstanding;
                        route.mFree(value, size);
                    }
                    require(aligned && trace.mTotal == 1 && trace.mFailures == 0 && outstanding == 1
                            && trace.mOutstanding == 0 && trace.mPeakOutstanding == 1 && trace.mTrackingOverflow == 0,
                        "allocation hook missed a route or broke alignment/zero-size allocation");
                    bool threw = false;
                    {
                        Allocations::Observe observe(trace, 1);
                        value = nullptr;
                        try
                        {
                            value = route.mAllocate(size);
                        }
                        catch (const std::bad_alloc&)
                        {
                            threw = true;
                        }
                    }
                    require(!value && threw == route.mThrows && trace.mTotal == 1 && trace.mFailures == 1
                            && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                        "allocation hook missed failure or broke throwing/nothrow semantics");
                }
            // Track only this observation's blocks, including non-LIFO frees.
            auto* prior = ::operator new(17);
            Allocations::Trace trace;
            {
                Allocations::Observe observe(trace);
                auto* first = ::operator new(17);
                auto* second = ::operator new[](17);
                auto* third = ::operator new(17, std::align_val_t{ 64 });
                ::operator delete(prior);
                ::operator delete[](second);
                ::operator delete(first);
                ::operator delete(third, std::align_val_t{ 64 });
            }
            require(trace.mTotal == 3 && trace.mPeakOutstanding == 3 && trace.mOutstanding == 0
                    && trace.mTrackingOverflow == 0,
                "allocation tracking lost ownership or non-LIFO deallocation");
        }

        struct ConsumerCopyTrace
        {
            Allocations::Trace* mTrace = nullptr;
            size_t mStarted = 0, mCompleted = 0, mOrdinal = 0;
        };

        // Only the source consumer is replaced: prepareTransfer copies it after
        // preparing stock inventory/script/registry storage and iterator guards.
        // Force both std::function storage and an identifiable fallible copy-body
        // allocation without adding instrumentation to any production source.
        struct AllocatingConsumer
        {
            ConsumerCopyTrace& mCopies;
            int& mNotifications;
            std::array<std::byte, 128> mPadding{};
            std::unique_ptr<int> mValue;

            AllocatingConsumer(ConsumerCopyTrace& copies, int& notifications)
                : mCopies(copies)
                , mNotifications(notifications)
                , mValue(std::make_unique<int>(7))
            {
            }
            AllocatingConsumer(const AllocatingConsumer& other)
                : mCopies(other.mCopies)
                , mNotifications(other.mNotifications)
            {
                if (mCopies.mTrace)
                {
                    ++mCopies.mStarted;
                    mCopies.mOrdinal = mCopies.mTrace->mTotal + 1;
                }
                Allocations::InPhase phase(Allocations::Phase::ConsumerCopy);
                mValue = std::make_unique<int>(*other.mValue);
                if (mCopies.mTrace)
                    ++mCopies.mCompleted;
            }
            void operator()(const Ptr&) const { ++mNotifications; }
        };

        auto ownedNodes(const PreparedContainerTransfer& pair)
        {
            std::vector<ConstPtr> result;
            for (const auto* storage : { &pair.getSourceStorage(), &pair.getDestinationStorage() })
                for (const auto& node : *storage)
                    result.emplace_back(&node);
            return result;
        }

        void requireDiscarded(const std::vector<ConstPtr>& nodes)
        {
            for (const auto& node : nodes)
                require(!node.hasLiveReference(), "consumed pair retained an owned node after discard");
        }

        template <class Make, class Verify>
        Allocations::Trace checkPreparationAllocationFailures(
            DisposableTransferRehearsal& fixture, ConsumerCopyTrace& copies, Make make, Verify verify)
        {
            using namespace Allocations;
            const auto measure = [&](Trace& trace, size_t failAt = 0) {
                copies = { &trace };
                bool caught = false, complete = false;
                {
                    Observe observe(trace, failAt);
                    InPhase phase(Phase::Preparation);
                    try
                    {
                        // Observe preparation, partial unwinding and full discard.
                        // Never allocate snapshots/assertions/observers in here.
                        auto pair = make();
                        complete = pair.getResolutionCompleteness().isComplete();
                    }
                    catch (const std::bad_alloc&)
                    {
                        caught = true;
                    }
                }
                copies.mTrace = nullptr;
                require(trace.mTrackingOverflow == 0 && trace.mOutstanding == 0,
                    "preparation leaked an observed allocation or overflowed cleanup tracking");
                require(trace.mTotal == trace.allocations(Phase::Preparation) + trace.allocations(Phase::ConsumerCopy),
                    "preparation reached an unrelated allocation phase");
                require(failAt ? caught && !complete && trace.mFailures == 1 && trace.mTotal == failAt
                               : !caught && complete && trace.mFailures == 0,
                    "preparation allocation failure was missed/swallowed or allocated during cleanup");
            };
            const auto fresh = [&] {
                std::vector<ConstPtr> nodes;
                {
                    auto pair = make();
                    require(pair.getResolutionCompleteness().isComplete(), "fresh preparation incomplete");
                    const auto* bindings = &pair.getIteratorBindings();
                    nodes = ownedNodes(pair);
                    verify();
                    pair = fixture.rehearse(std::move(pair));
                    require(&pair.getIteratorBindings() == bindings && ownedNodes(pair) == nodes,
                        "fresh rehearsal replaced protected bindings or owned nodes");
                    verify();
                }
                requireDiscarded(nodes);
                verify();
            };
            Trace baseline;
            measure(baseline);
            const auto consumerOrdinal = copies.mOrdinal;
            require(copies.mStarted == 1 && copies.mCompleted == 1 && consumerOrdinal > 1
                    && consumerOrdinal < baseline.mTotal && baseline.allocations(Phase::ConsumerCopy) == 1
                    && baseline.mPeakOutstanding > 0,
                "preparation missed the final fallible consumer copy or subsequent validation");
            verify();
            fresh();
            Trace repeated;
            measure(repeated);
            require(repeated.mAllocations == baseline.mAllocations && repeated.mVisits == baseline.mVisits
                    && repeated.mPeakOutstanding == baseline.mPeakOutstanding && copies.mOrdinal == consumerOrdinal
                    && copies.mStarted == 1 && copies.mCompleted == 1,
                "fresh preparation changed allocation coverage or final consumer copy order");
            verify();
            for (size_t failAt = 1; failAt <= baseline.mTotal; ++failAt)
            {
                try
                {
                    Trace failure;
                    measure(failure, failAt);
                    require(
                        failure.mFailedPhase == (failAt == consumerOrdinal ? Phase::ConsumerCopy : Phase::Preparation),
                        "preparation failure moved to a different allocation phase/ordinal");
                    require(copies.mStarted == (failAt >= consumerOrdinal ? 1 : 0)
                            && copies.mCompleted == (failAt > consumerOrdinal ? 1 : 0)
                            && (!copies.mStarted || copies.mOrdinal == consumerOrdinal),
                        "preparation failed at the wrong final consumer copy boundary");
                    verify();
                    fresh();
                }
                catch (...)
                {
                    std::cerr << "Preparation allocation ordinal=" << failAt << '/' << baseline.mTotal
                              << " final-consumer=" << consumerOrdinal << '\n';
                    throw;
                }
            }
            return baseline;
        }

        template <class Make, class Verify>
        Allocations::Trace checkAllocationFailures(DisposableTransferRehearsal& fixture, Make make, Verify verify)
        {
            using namespace Allocations;
            // Preparation and allocating assertions are deliberately outside the
            // measured call. Every measured rehearsal has an empty observer.
            auto pair = make();
            const auto* bindings = &pair.getIteratorBindings();
            const auto nodes = ownedNodes(pair);
            Trace baseline;
            {
                Observe observe(baseline);
                pair = fixture.rehearse(std::move(pair));
            }
            if (baseline.allocations(Phase::Validation) == 0 || baseline.allocations(Phase::Revalidation) == 0
                || baseline.allocations(Phase::Exchange) != 0 || baseline.allocations(Phase::Rollback) != 0
                || baseline.allocations(Phase::Outside) != 0)
                std::cerr << "Allocation phases: validation=" << baseline.allocations(Phase::Validation)
                          << " setup=" << baseline.allocations(Phase::Setup)
                          << " exchange=" << baseline.allocations(Phase::Exchange)
                          << " rollback=" << baseline.allocations(Phase::Rollback)
                          << " revalidation=" << baseline.allocations(Phase::Revalidation)
                          << " outside=" << baseline.allocations(Phase::Outside) << '\n';
            require(baseline.mFailures == 0 && baseline.allocations(Phase::Validation) > 0
                    && baseline.allocations(Phase::Revalidation) > 0 && baseline.allocations(Phase::Exchange) == 0
                    && baseline.allocations(Phase::Rollback) == 0 && baseline.allocations(Phase::Outside) == 0,
                "rehearsal missed fallible phases or allocated during exchange/rollback");
            for (auto phase :
                { Phase::Validation, Phase::Setup, Phase::Exchange, Phase::Rollback, Phase::Revalidation })
                require(baseline.visits(phase) == 1, "rehearsal skipped or repeated a measured phase");
            require(&pair.getIteratorBindings() == bindings && ownedNodes(pair) == nodes,
                "measured rehearsal replaced pair bindings or owned nodes");
            verify();
            Trace repeated;
            {
                Observe observe(repeated);
                // Include successful returned-pair discard in the observation.
                fixture.rehearse(std::move(pair));
            }
            require(repeated.mAllocations == baseline.mAllocations && repeated.mVisits == baseline.mVisits,
                "repeat/discard allocated differently from first rehearsal");
            requireDiscarded(nodes);
            verify();

            for (size_t failAt = 1; failAt <= baseline.mTotal; ++failAt)
            {
                auto failedPair = make();
                const auto failedNodes = ownedNodes(failedPair);
                Trace failure;
                bool caught = false;
                {
                    Observe observe(failure, failAt);
                    try
                    {
                        fixture.rehearse(std::move(failedPair));
                    }
                    catch (const std::bad_alloc&)
                    {
                        caught = true;
                    }
                }
                require(caught && failure.mFailures == 1 && failure.mTotal == failAt,
                    "allocation failure was swallowed, missed, or allocated during unwind/discard");
                size_t offset = failAt;
                auto expectedPhase = Phase::Validation;
                for (auto phase : { Phase::Validation, Phase::Setup, Phase::Revalidation })
                {
                    if (offset <= baseline.allocations(phase))
                    {
                        expectedPhase = phase;
                        break;
                    }
                    offset -= baseline.allocations(phase);
                }
                require(failure.mFailedPhase == expectedPhase && failure.allocations(expectedPhase) == offset,
                    "allocation failure moved to a different phase/ordinal");
                const size_t exchanged = expectedPhase == Phase::Revalidation ? 1 : 0;
                require(failure.visits(Phase::Exchange) == exchanged && failure.visits(Phase::Rollback) == exchanged
                        && failure.visits(Phase::Revalidation) == exchanged && failure.allocations(Phase::Exchange) == 0
                        && failure.allocations(Phase::Rollback) == 0 && failure.allocations(Phase::Outside) == 0,
                    "allocation failure bypassed rollback or allocated in exchange/rollback/discard");
                requireDiscarded(failedNodes);
                verify();
                // A fresh pair must work on the very same stores/services after
                // every failure, including revalidation after completed rollback.
                fixture.rehearse(make());
                verify();
            }
            return baseline;
        }
    }

    enum class AllocationCheck
    {
        None,
        Rehearsal,
        Preparation
    };

    static void checkTransferRehearsalCases(const ESMStore& content, AllocationCheck allocationCheck)
    {
        using Rehearsal = DisposableTransferRehearsal;
        using Stage = Rehearsal::Stage;
        using Pair = PreparedContainerTransfer;
        MWClass::registerClasses();
        ESMStore store;
        const auto plainId = ESM::RefId::stringRefId("native_plain");
        const auto scriptedId = ESM::RefId::stringRefId("native_scripted");
        const auto scriptId = ESM::RefId::stringRefId("native_script");
        const auto ownerId = ESM::RefId::stringRefId("player");
        for (auto id : { plainId, scriptedId })
            store.insertStatic(*content.get<ESM::Miscellaneous>().find(id));
        store.insertStatic(*content.get<ESM::NPC>().find(ownerId));
        store.insertStatic(*content.get<ESM::Script>().find(scriptId));
        ESM::ReadersCache readers;
        Compiler::Extensions extensions;
        Compiler::registerExtensions(extensions);
        MWScript::CompilerContext compilerContext(MWScript::CompilerContext::Type_Full);
        compilerContext.setExtensions(&extensions);
        struct Scripts final : MWScript::ScriptManager
        {
            using MWScript::ScriptManager::ScriptManager;
            int mRuns = 0;
            bool run(const ESM::RefId&, Interpreter::Context&) override
            {
                ++mRuns;
                throw std::runtime_error("rehearsal executed script instructions");
            }
        } scripts(store, compilerContext, 1);
        ManualRef plain(store, plainId), scripted(store, scriptedId);
        size_t cases = 0;
        Allocations::Trace totals;
        const bool allocationFailures = allocationCheck != AllocationCheck::None;
        if (allocationFailures)
            checkAllocationHooks();

        // Unrelated registered state in an independent WorldModel must stay exact,
        // even while another fixture has partially exchanged its stock storage.
        Rehearsal live(store, readers, scripts, ownerId, false);
        live.mSource.add(scripted.getPtr(), 9, live.mSourceAdd);
        live.mDestination.add(plain.getPtr(), 11, live.mDestinationAdd);
        const auto liveBefore = snapshot(live);
        for (bool shared : { false, true })
            for (bool scriptedItem : { false, true })
                for (bool stack : { false, true })
                    for (int quantity : { 1, 4 })
                        for (int cursorPosition : { 0, 1, 99 })
                        {
                            ConsumerCopyTrace copies;
                            Rehearsal fixture(store, readers, scripts, ownerId, shared);
                            if (allocationCheck == AllocationCheck::Preparation)
                                fixture.mRemoval.mInventoryUpdated = AllocatingConsumer(copies, fixture.mNotifications);
                            auto& source = fixture.mSource;
                            auto& destination = fixture.mDestination;
                            const auto item
                                = *source.add(scriptedItem ? scripted.getPtr() : plain.getPtr(), 4, fixture.mSourceAdd);
                            if (stack)
                                destination.add(
                                    scriptedItem ? scripted.getPtr() : plain.getPtr(), 7, fixture.mDestinationAdd);
                            ManualRef dormantItem(store, plainId);
                            dormantItem.getPtr().getCellRef().setSoul(ESM::RefId::stringRefId("dormant_soul"));
                            const auto dormantIterator = source.add(dormantItem.getPtr(), 2, fixture.mSourceAdd);
                            const auto dormant = *dormantIterator;
                            require(dormant != item, "dormant fixture merged with transfer item");
                            dormant.getCellRef() = dormant.getCellRef().copyWithCount(0);
                            item.getRefData().onActivate();
                            item.getRefData().activate();
                            if (scriptedItem)
                                item.getRefData().getLocals().mLongs.at(0) = 27;
                            source.setSelectedEnchantItem(cursorPosition == 1 ? dormantIterator : source.begin());
                            destination.setSelectedEnchantItem(destination.begin());
                            if (cursorPosition == 1)
                            {
                                const auto selected = destination.add(dormantItem.getPtr(), 2, fixture.mDestinationAdd);
                                selected->getCellRef() = selected->getCellRef().copyWithCount(0);
                                destination.setSelectedEnchantItem(selected);
                            }
                            // Non-mutated supplied store resolves unrelated registry
                            // and script entries, including dormant selection.
                            const auto other = fixture.mOther.add(
                                scriptedItem ? scripted.getPtr() : plain.getPtr(), 3, fixture.mOtherAdd);
                            fixture.mOther.setSelectedEnchantItem(other);
                            other->getCellRef() = other->getCellRef().copyWithCount(0);
                            for (auto* service : { &fixture.mSourceScripts, &fixture.mDestinationScripts })
                            {
                                service->startIteration();
                                std::pair<ESM::RefId, Ptr> entry;
                                for (int i = 0; i < cursorPosition && service->getNext(entry); ++i)
                                {
                                }
                            }
                            Listener listener;
                            source.setContListener(&listener);
                            destination.setContListener(&listener);
                            source.getWeight();
                            destination.getWeight();
                            const std::array supplied{ ContainerStoreResolution(
                                fixture.mOther, fixture.mOtherOwner.getPtr()) };
                            const auto make = [&] {
                                return source.prepareTransfer(
                                    item, quantity, destination, fixture.mRemoval, fixture.mDestinationAdd, supplied);
                            };
                            const auto before = snapshot(fixture);
                            const auto unrelated = nodeState(*other);
                            const auto verifyOriginal = [&] {
                                require(snapshot(fixture) == before,
                                    "transfer changed original nodes/state/caches/cursors");
                                require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                    "transfer emitted notifications/scripts or changed unrelated live state");
                            };
                            if (allocationFailures)
                            {
                                Allocations::Trace trace;
                                try
                                {
                                    trace = allocationCheck == AllocationCheck::Preparation
                                        ? checkPreparationAllocationFailures(fixture, copies, make, verifyOriginal)
                                        : checkAllocationFailures(fixture, make, verifyOriginal);
                                }
                                catch (...)
                                {
                                    std::cerr << "Allocation fixture: shared=" << shared << " scripted=" << scriptedItem
                                              << " existing=" << stack << " quantity=" << quantity
                                              << " cursor=" << cursorPosition << '\n';
                                    throw;
                                }
                                ++cases;
                                totals.mTotal += trace.mTotal;
                                totals.mPeakOutstanding = std::max(totals.mPeakOutstanding, trace.mPeakOutstanding);
                                for (size_t i = 0; i < totals.mAllocations.size(); ++i)
                                    totals.mAllocations[i] += trace.mAllocations[i];
                                continue;
                            }
                            auto decision = make();
                            require(decision.getResolutionCompleteness().isComplete(), "rehearsal fixture incomplete");
                            const auto* bindings = &decision.getIteratorBindings();
                            const auto expectedRegistry = decision.getRelocation().mRegistry;
                            const auto expectedSourceScripts = decision.getRelocation().mSourceScripts;
                            const auto expectedDestinationScripts
                                = shared ? expectedSourceScripts : *decision.getRelocation().mDestinationScripts;
                            const auto expectedSource = decision.getRelocation().mSource;
                            const auto expectedDestination = decision.getRelocation().mDestination;
                            const auto expectedSourceSelection = decision.getRelocation().mSourceSelection;
                            const auto expectedDestinationSelection = decision.getRelocation().mDestinationSelection;
                            const auto destinationId = decision.getDestinationIdentity();
                            std::vector<Stage> stages;
                            auto restored = fixture.rehearse(std::move(decision), [&](Stage stage) {
                                stages.push_back(stage);
                                require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                    "partial rehearsal leaked effects or changed live state");
                                require(nodeState(*other) == unrelated, "rehearsal changed supplied store node");
                                if (stage != Stage::Registry)
                                    return;
                                require(fixture.mModel.snapshotPtrRegistry() == expectedRegistry,
                                    "installed registry/revision/counter differ from protected relocation");
                                require(fixture.mSourceScripts.snapshot() == expectedSourceScripts
                                        && fixture.mDestinationAdd.mLocalScripts->snapshot()
                                            == expectedDestinationScripts,
                                    "installed shared/distinct scripts or cursors differ");
                                const auto checkInventory = [&](const auto& storage, const auto& expected,
                                                                ContainerStore& target) {
                                    require(storage.size() == expected.size(), "installed raw membership differs");
                                    size_t i = 0;
                                    for (const auto& node : storage)
                                    {
                                        const auto& view = expected[i++];
                                        const auto id = view.mIdentity.isSet() ? view.mIdentity : destinationId;
                                        require(&node == view.mItem.mRef && node.mRef.getRefNum() == id
                                                && node.mWorldModel == &fixture.mModel
                                                && fixture.mModel.getPtr(id).mRef == &node
                                                && fixture.mModel.getPtr(id).mContainerStore == &target,
                                            "installation lost node identity, owner or registry association");
                                    }
                                };
                                checkInventory(fixture.sourceStorage(), expectedSource, source);
                                checkInventory(fixture.destinationStorage(), expectedDestination, destination);
                                require(source.count(item.getCellRef().getRefId()) == 4 - quantity
                                        && destination.count(item.getCellRef().getRefId())
                                            == (stack ? 7 : 0) + quantity,
                                    "rehearsal installed wrong transfer quantity");
                                const auto selected = [](const ContainerStore& container) {
                                    const auto it = container.getSelectedEnchantItem();
                                    return it == container.end() ? ConstPtr() : *it;
                                };
                                require(selected(source).mRef == expectedSourceSelection.mRef
                                        && selected(destination).mRef == expectedDestinationSelection.mRef,
                                    "rehearsal installed wrong selection/end");
                                source.getWeight();
                                destination.getWeight();
                            });
                            require(&restored.getIteratorBindings() == bindings,
                                "rehearsal replaced protected pair bindings");
                            verifyOriginal();
                            const std::vector<Stage> expectedStages = shared
                                ? std::vector{ Stage::Validated, Stage::Identities, Stage::SourceInventory,
                                      Stage::DestinationInventory, Stage::SourceScripts, Stage::Registry }
                                : std::vector{ Stage::Validated, Stage::Identities, Stage::SourceInventory,
                                      Stage::DestinationInventory, Stage::SourceScripts, Stage::DestinationScripts,
                                      Stage::Registry };
                            require(stages == expectedStages,
                                "rehearsal duplicated shared service or reordered distinct services");
                            // Rollback returns the exact reusable pair; every saved
                            // node/iterator guard must still pass on the second run.
                            restored = fixture.rehearse(std::move(restored));
                            verifyOriginal();
                            for (auto stop : expectedStages)
                            {
                                auto failedPair = make();
                                const auto owned = ConstPtr(&failedPair.getDestinationStorage().back());
                                bool failed = false;
                                try
                                {
                                    fixture.rehearse(std::move(failedPair), [&](Stage stage) {
                                        if (stage == stop)
                                            throw Failure{};
                                    });
                                }
                                catch (const Failure&)
                                {
                                    failed = true;
                                }
                                require(failed && !owned.hasLiveReference(), "failed rehearsal retained consumed pair");
                                verifyOriginal();
                            }
                            {
                                auto discarded = fixture.rehearse(make());
                            }
                            verifyOriginal();

                            const auto reject = [&](Pair pair, std::string_view reason) {
                                bool rejected = false;
                                const auto unchanged = snapshot(fixture);
                                try
                                {
                                    fixture.rehearse(std::move(pair), [&](Stage) {
                                        throw std::runtime_error("invalid pair reached exchange observer");
                                    });
                                }
                                catch (const std::invalid_argument& error)
                                {
                                    rejected = std::string_view(error.what()).find(reason) != std::string_view::npos;
                                }
                                require(rejected && snapshot(fixture) == unchanged,
                                    "invalid rehearsal mutated state or wrong rejection");
                            };
                            auto stale = make();
                            const auto original = item.getCellRef();
                            item.getCellRef().setCount(3);
                            reject(std::move(stale), "changed");
                            item.getCellRef() = original;
                            auto corrupted = make();
                            ++const_cast<Pair::ResolutionCompleteness&>(corrupted.getResolutionCompleteness())
                                  .mRegistryEntries;
                            reject(std::move(corrupted), "completeness changed");
                            corrupted = make();
                            const_cast<CellRef&>(corrupted.getSourceStorage().front().mRef).setCount(99);
                            reject(std::move(corrupted), "changed");
                            corrupted = make();
                            const_cast<Pair::IteratorBindings&>(corrupted.getIteratorBindings()).mCount++;
                            reject(std::move(corrupted), "changed");
                            corrupted = make();
                            ++const_cast<PtrRegistry::Snapshot&>(corrupted.getRegistryStorage().getBindings())
                                  .mRevision;
                            reject(std::move(corrupted), "changed");
                            if (shared && scriptedItem && stack && quantity == 4 && cursorPosition == 0)
                            {
                                // Same-address reconstruction with identical values
                                // cannot authorize the saved private iterators.
                                const auto reconstruct = [](auto& node) {
                                    auto replacement = std::move(node);
                                    std::destroy_at(&node);
                                    std::construct_at(&node, std::move(replacement));
                                };
                                corrupted = make();
                                auto& nodes = const_cast<Pair::MiscList&>(corrupted.getSourceStorage());
                                reconstruct(nodes.back());
                                reject(std::move(corrupted), "iterator node lifetimes changed");
                                corrupted = make();
                                auto& entries = const_cast<LocalScripts::PreparedStorage::Entries&>(
                                    corrupted.getSourceScriptStorage().getEntries());
                                reconstruct(entries.front());
                                reject(std::move(corrupted), "iterator node lifetimes changed");
                                stale = make();
                                const auto counter = fixture.mModel.getLastGeneratedRefNum();
                                auto changedCounter = counter;
                                ++changedCounter.mIndex;
                                fixture.mModel.setLastGeneratedRefNum(changedCounter);
                                reject(std::move(stale), "changed");
                                fixture.mModel.setLastGeneratedRefNum(counter);
                                stale = make();
                                std::pair<ESM::RefId, Ptr> entry;
                                require(fixture.mSourceScripts.getNext(entry), "stale cursor fixture empty");
                                reject(std::move(stale), "changed");
                                fixture.mSourceScripts.startIteration();
                                const std::array liveSupplied{ ContainerStoreResolution(
                                    live.mOther, live.mOtherOwner.getPtr()) };
                                auto foreign = live.mSource.prepareTransfer(*live.mSource.begin(), 1, live.mDestination,
                                    live.mRemoval, live.mDestinationAdd, liveSupplied);
                                reject(std::move(foreign), "context changed");
                                auto serviceMismatch = make();
                                fixture.mDestinationAdd.mLocalScripts = &live.mSourceScripts;
                                reject(std::move(serviceMismatch), "service mismatch");
                                fixture.mDestinationAdd.mLocalScripts = &fixture.mSourceScripts;
                                auto originalPair = make();
                                auto moved = std::move(originalPair);
                                reject(std::move(originalPair), "moved from");
                                moved = fixture.rehearse(std::move(moved));
                                verifyOriginal();
                            }
                            // Independent registry-only and script-only unresolved
                            // entries must never be followed or partially installed.
                            ManualRef unresolved(store, plainId);
                            fixture.mModel.registerPtr(unresolved.getPtr());
                            reject(make(), "complete resolution");
                            fixture.mModel.deregisterLiveCellRef(*unresolved.getPtr().mRef);
                            ManualRef unresolvedScript(store, scriptedId);
                            fixture.mSourceScripts.add(scriptId, unresolvedScript.getPtr(), scripts);
                            reject(make(), "complete resolution");
                            fixture.mSourceScripts.remove(unresolvedScript.getPtr());
                            require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                "rejections affected effects or unrelated live state");
                        }
        if (allocationFailures)
        {
            require(cases == 48, "allocation failure matrix lost a fixture combination");
            if (allocationCheck == AllocationCheck::Preparation)
            {
                require(totals.allocations(Allocations::Phase::ConsumerCopy) == cases,
                    "preparation matrix missed final consumer allocations");
                std::cout << "Allocation preparation: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " preparation=" << totals.allocations(Allocations::Phase::Preparation)
                          << " final-consumer-copy=" << totals.allocations(Allocations::Phase::ConsumerCopy)
                          << " peak-outstanding=" << totals.mPeakOutstanding << " remaining-after-cleanup=0\n";
                return;
            }
            require(totals.allocations(Allocations::Phase::Setup) > 0, "fallible script setup was never exercised");
            std::cout << "Allocation rehearsal: cases=" << cases << " individually-failed=" << totals.mTotal
                      << " validation=" << totals.allocations(Allocations::Phase::Validation)
                      << " setup=" << totals.allocations(Allocations::Phase::Setup)
                      << " revalidation=" << totals.allocations(Allocations::Phase::Revalidation)
                      << " exchange=" << totals.allocations(Allocations::Phase::Exchange)
                      << " rollback=" << totals.allocations(Allocations::Phase::Rollback) << '\n';
        }
    }

    void checkTransferRehearsal(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::None);
    }

    void checkTransferRehearsalAllocations(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Rehearsal);
    }

    void checkTransferPreparationAllocations(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Preparation);
    }
}
