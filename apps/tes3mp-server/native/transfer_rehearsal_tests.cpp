#include "test_allocations.hpp"
#include "transfer_rehearsal.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <new>
#include <optional>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/compiler/locals.hpp>
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
            ref.writeState(state);
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
                ref.getLockLevel(), ref.getKey(), ref.getTrap(), state.mRef.mIsLocked, state.mRef.mReferenceBlocked,
                ref.getGlobalVariable().data(), ref.getGlobalVariable().capacity() };
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

        auto localOutputState(const ESM::Locals& locals)
        {
            std::vector<std::pair<const char*, size_t>> strings;
            for (const auto& [name, value] : locals.mVariables)
                strings.emplace_back(name.data(), name.capacity());
            return std::tuple{ locals.mVariables, locals.mVariables.data(), locals.mVariables.capacity(), strings };
        }

        auto animationValues(const ESM::AnimationState& state)
        {
            std::vector<std::tuple<std::string, float, bool, uint64_t>> result;
            for (const auto& animation : state.mScriptedAnims)
                result.emplace_back(animation.mGroup, animation.mTime, animation.mAbsolute, animation.mLoopCount);
            return result;
        }

        auto cellRefValues(const ESM::CellRef& r)
        {
            return std::tuple{ r.mRefNum, r.mRefID, r.mScale, r.mOwner, r.mGlobalVariable, r.mSoul, r.mFaction,
                r.mFactionRank, r.mChargeInt, r.mChargeIntRemainder, r.mEnchantmentCharge, r.mCount, r.mTeleport,
                r.mDoorDest, r.mDestCell, r.mLockLevel, r.mIsLocked, r.mKey, r.mTrap, r.mReferenceBlocked, r.mPos };
        }

        auto objectOutputState(const ESM::ObjectState& state)
        {
            const auto& r = state.mRef;
            std::vector<std::tuple<int32_t, std::string, size_t>> lua;
            for (const auto& script : state.mLuaScripts.mScripts)
                lua.emplace_back(script.mScriptId, script.mData, script.mTimers.size());
            std::vector<std::pair<const char*, size_t>> strings;
            for (const auto& animation : state.mAnimationState.mScriptedAnims)
                strings.emplace_back(animation.mGroup.data(), animation.mGroup.capacity());
            return std::tuple{ cellRefValues(r), r.mGlobalVariable.data(), r.mGlobalVariable.capacity(),
                r.mDestCell.data(), r.mDestCell.capacity(), localOutputState(state.mLocals), lua,
                state.mLuaScripts.mScripts.data(), state.mLuaScripts.mScripts.capacity(), state.mPosition,
                animationValues(state.mAnimationState), state.mAnimationState.mScriptedAnims.data(),
                state.mAnimationState.mScriptedAnims.capacity(), strings, state.mActorIdConverter, state.mVersion,
                state.mFlags, state.mHasLocals, state.mEnabled, state.mHasCustomState };
        }

        ESM::ObjectState outputSentinel()
        {
            ESM::ObjectState state;
            state.blank();
            state.mRef.mCount = 73;
            state.mRef.mGlobalVariable = "unrelated caller cellref sentinel";
            state.mRef.mDestCell = "unrelated caller destination cell sentinel";
            state.mLocals.mVariables.emplace_back("existing caller local sentinel", ESM::Variant(91.f));
            state.mLuaScripts.mScripts.push_back({ 12, "unrelated caller Lua sentinel", {} });
            state.mFlags = 93;
            state.mHasLocals = 7;
            state.mEnabled = 5;
            state.mPosition = { { 91.f, 92.f, 93.f }, { 0.25f, 0.5f, 0.75f } };
            state.mAnimationState.mScriptedAnims.emplace_back();
            state.mAnimationState.mScriptedAnims.back().mGroup = "existing caller animation sentinel";
            return state;
        }

        // Test-only composition over const owned nodes, never registry diagnostics
        // or borrowed resolved objects. Full current validation precedes all reads.
        void serializePair(const DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
            const Compiler::Locals& declarations, std::vector<ESM::ObjectState>& output)
        {
            if (!fixture.mSource.validateTransfer(pair, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd)
                    .isComplete())
                throw std::invalid_argument("Serialization requires complete resolution");
            std::vector<ESM::ObjectState> staged;
            for (const auto* storage : { &pair.getSourceStorage(), &pair.getDestinationStorage() })
                for (const auto& node : *storage)
                {
                    staged.emplace_back();
                    staged.back().blank();
                    node.mData.write(staged.back(), declarations);
                }
            output.swap(staged);
        }

        auto pairOutputState(const std::vector<ESM::ObjectState>& output)
        {
            std::vector<decltype(objectOutputState(outputSentinel()))> states;
            for (const auto& state : output)
                states.push_back(objectOutputState(state));
            return std::tuple{ output.data(), output.capacity(), states };
        }

        // Owned test output only: these associations never become node RefNums.
        // No borrowed Ptr, registry key to follow, or production persistence API.
        struct SerializedInventory
        {
            std::vector<ESM::ObjectState> mObjects;
            std::vector<ESM::RefNum> mProposedIdentities;

            void swap(SerializedInventory& other) noexcept
            {
                static_assert(noexcept(mObjects.swap(other.mObjects)));
                static_assert(noexcept(mProposedIdentities.swap(other.mProposedIdentities)));
                mObjects.swap(other.mObjects);
                mProposedIdentities.swap(other.mProposedIdentities);
            }
        };

        struct SerializedPair
        {
            SerializedInventory mSource, mDestination;
        };

        void serializePair(const DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
            const Compiler::Locals& declarations, SerializedPair& output)
        {
            if (!fixture.mSource.validateTransfer(pair, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd)
                    .isComplete())
                throw std::invalid_argument("ObjectState serialization requires complete resolution");
            SerializedPair staged;
            const auto serialize = [&](const auto& storage, const auto& views, SerializedInventory& inventory) {
                inventory.mObjects.reserve(storage.size());
                inventory.mProposedIdentities.reserve(storage.size());
                size_t i = 0;
                for (const auto& node : storage)
                {
                    const auto identity = views.at(i++).mIdentity;
                    inventory.mProposedIdentities.push_back(
                        identity.isSet() ? identity : pair.getDestinationIdentity());
                    inventory.mObjects.emplace_back();
                    auto& object = inventory.mObjects.back();
                    object.blank();
                    node.mRef.writeState(object);
                    node.mData.write(object, declarations);
                    // Only plain/initialized MWScript MISC without custom/Lua state
                    // passes preparation. No class-specific state is staged here.
                    object.mHasCustomState = false;
                }
            };
            serialize(pair.getSourceStorage(), pair.getRelocation().mSource, staged.mSource);
            serialize(pair.getDestinationStorage(), pair.getRelocation().mDestination, staged.mDestination);
            output.mSource.swap(staged.mSource);
            output.mDestination.swap(staged.mDestination);
        }

        auto pairOutputState(const SerializedPair& output)
        {
            const auto inventory = [](const SerializedInventory& value) {
                return std::tuple{ pairOutputState(value.mObjects), value.mProposedIdentities,
                    value.mProposedIdentities.data(), value.mProposedIdentities.capacity() };
            };
            return std::tuple{ inventory(output.mSource), inventory(output.mDestination) };
        }

        template <bool ObjectStates>
        auto pairOutputSentinel()
        {
            if constexpr (ObjectStates)
            {
                SerializedPair result;
                result.mSource.mObjects.push_back(outputSentinel());
                result.mDestination.mObjects = { outputSentinel(), outputSentinel() };
                result.mSource.mProposedIdentities.push_back({ 701, -1 });
                result.mDestination.mProposedIdentities = { { 702, -1 }, { 703, -1 } };
                return result;
            }
            else
                return std::vector<ESM::ObjectState>{ outputSentinel() };
        }

        // Synthetic TES3 fields, including fields unusual on MISC, establish that
        // the serializer preserves the complete CellRef rather than a selected subset.
        ESM::CellRef decoratedCellRef(const CellRef& ref)
        {
            ESM::ObjectState state;
            ref.writeState(state);
            auto& r = state.mRef;
            r.mScale = 1.25f;
            r.mOwner = ESM::RefId::stringRefId("serialization_owner");
            r.mGlobalVariable = "serialization_global_with_allocating_name";
            if (r.mSoul.empty())
                r.mSoul = ESM::RefId::stringRefId("serialization_soul");
            r.mFaction = ESM::RefId::stringRefId("serialization_faction");
            r.mFactionRank = 3;
            r.mChargeInt = 61;
            r.mChargeIntRemainder = 0.375f;
            r.mEnchantmentCharge = 12.5f;
            r.mTeleport = true;
            r.mDoorDest = { { 11.f, -12.f, 13.f }, { 0.25f, -0.5f, 0.75f } };
            r.mDestCell = "Serialization destination with allocating name";
            r.mLockLevel = 42;
            r.mIsLocked = true;
            r.mKey = ESM::RefId::stringRefId("serialization_key");
            r.mTrap = ESM::RefId::stringRefId("serialization_trap");
            r.mReferenceBlocked = 1;
            r.mPos = { { -31.f, 32.f, 33.f }, { -0.125f, 0.5f, 1.25f } };
            return r;
        }

        template <class MakeOutput, class Write, class Snapshot, class Check, class Verify>
        size_t checkSerializationAllocations(
            MakeOutput makeOutput, Write write, Snapshot capture, Check check, Verify verify)
        {
            const auto fresh = [&] {
                auto output = makeOutput();
                write(output);
                check(output);
                verify();
            };
            const auto measure = [&](size_t failAt) {
                std::optional output{ makeOutput() };
                const auto before = capture(*output);
                Allocations::Trace trace;
                bool caught = false;
                {
                    Allocations::Observe observe(trace, failAt);
                    try
                    {
                        write(*output);
                        // Include successful output destruction in leak coverage.
                        if (!failAt)
                            output.reset();
                    }
                    catch (const std::bad_alloc&)
                    {
                        caught = true;
                    }
                }
                require(trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                    "serialization leaked an observed allocation or overflowed tracking");
                require(failAt ? caught && trace.mFailures == 1 && trace.mTotal == failAt
                               : !caught && trace.mFailures == 0 && trace.mTotal > 0,
                    "serialization missed/swallowed allocation failure or allocated during cleanup");
                if (failAt)
                    require(output && capture(*output) == before, "failed serialization changed caller output/storage");
                verify();
                fresh();
                return trace.mTotal;
            };
            const auto count = measure(0);
            require(measure(0) == count, "serialization allocation coverage changed on repeat");
            for (size_t failAt = 1; failAt <= count; ++failAt)
            {
                try
                {
                    measure(failAt);
                }
                catch (...)
                {
                    std::cerr << "Serialization allocation ordinal=" << failAt << '/' << count << '\n';
                    throw;
                }
            }
            return count;
        }

        void checkSerializedData(const RefData& data, const ESM::ObjectState& output, size_t prefix = 0)
        {
            const auto& locals = data.getLocals();
            const bool scripted = !locals.getScriptId().empty();
            const auto& values = output.mLocals.mVariables;
            require(output.mHasLocals == static_cast<unsigned char>(scripted)
                    && values.size() == prefix + (scripted ? 6 : 0),
                "serialized locals presence/size mismatch");
            if (scripted)
            {
                const std::array<std::string_view, 6> names{ "onpcadd", "serialization_short_variable", "counter",
                    "serialization_long_variable", "ratio", "serialization_float_variable" };
                for (size_t i = 0; i < names.size(); ++i)
                {
                    const auto& [name, value] = values[prefix + i];
                    require(name == names[i] && value.getType() == (i < 4 ? ESM::VT_Int : ESM::VT_Float),
                        "serialized local name/order/type mismatch");
                    require(i < 2   ? value.getInteger() == locals.mShorts[i]
                            : i < 4 ? value.getInteger() == locals.mLongs[i - 2]
                                    : value.getFloat() == locals.mFloats[i - 4],
                        "serialized local value mismatch");
                }
            }
            auto flags = data.copyForContainerTransfer();
            flags.getLocals() = {};
            ESM::ObjectState stock;
            stock.blank();
            flags.write(stock); // Stock uninitialized-locals wrapper also needs no Environment.
            require(output.mFlags == stock.mFlags && output.mEnabled == static_cast<unsigned char>(data.isEnabled())
                    && output.mPosition == data.getPosition()
                    && animationValues(output.mAnimationState) == animationValues(data.getAnimationState()),
                "serialized flags/enabled/position/animation mismatch");
        }

        void checkSerializedRef(const CellRef& ref, const ESM::CellRef& output)
        {
            require(output.mRefNum == ref.getRefNum() && output.mRefID == ref.getRefId()
                    && output.mCount == ref.getCount(false) && output.mSoul == ref.getSoul()
                    && output.mChargeInt == ref.getCharge() && output.mChargeIntRemainder == ref.getChargeIntRemainder()
                    && output.mEnchantmentCharge == ref.getEnchantmentCharge() && output.mOwner == ref.getOwner()
                    && output.mGlobalVariable == ref.getGlobalVariable() && output.mFaction == ref.getFaction()
                    && output.mFactionRank == ref.getFactionRank() && output.mScale == ref.getScale()
                    && output.mTeleport == ref.getTeleport() && output.mDoorDest == ref.getDoorDest()
                    && ESM::RefId::stringRefId(output.mDestCell) == ref.getDestCell()
                    && output.mLockLevel == ref.getLockLevel() && output.mIsLocked == ref.isLocked()
                    && output.mKey == ref.getKey() && output.mTrap == ref.getTrap() && output.mReferenceBlocked == 1
                    && output.mPos == ref.getPosition(),
                "serialized CellRef field mismatch");
        }

        void checkPairOutput(const PreparedContainerTransfer& pair, const std::vector<ESM::ObjectState>& output)
        {
            const auto nodes = ownedNodes(pair);
            require(output.size() == nodes.size(), "serialization omitted dormant owned nodes");
            for (size_t i = 0; i < nodes.size(); ++i)
                checkSerializedData(nodes[i].getRefData(), output[i]);
        }

        void checkPairOutput(const PreparedContainerTransfer& pair, const SerializedPair& output)
        {
            const auto check = [&](const auto& storage, const auto& views, const SerializedInventory& inventory) {
                require(inventory.mObjects.size() == storage.size() && views.size() == storage.size()
                        && inventory.mProposedIdentities.size() == storage.size(),
                    "ObjectState serialization lost membership/identity metadata or dormant nodes");
                size_t i = 0;
                for (const auto& node : storage)
                {
                    const auto& state = inventory.mObjects[i];
                    const auto id = views[i].mIdentity;
                    require(inventory.mProposedIdentities[i] == (id.isSet() ? id : pair.getDestinationIdentity())
                            && inventory.mProposedIdentities[i].isSet() && !node.mRef.getRefNum().isSet()
                            && !state.mRef.mRefNum.isSet() && node.mWorldModel == nullptr,
                        "ObjectState serialization mixed detached values and proposed identities");
                    checkSerializedRef(node.mRef, state.mRef);
                    checkSerializedData(node.mData, state);
                    require(state.mLuaScripts.mScripts.empty() && !state.mHasCustomState
                            && state.mActorIdConverter == nullptr && state.mVersion == ESM::DefaultFormatVersion,
                        "ObjectState serialization introduced unsupported state");
                    ++i;
                }
            };
            check(pair.getSourceStorage(), pair.getRelocation().mSource, output.mSource);
            check(pair.getDestinationStorage(), pair.getRelocation().mDestination, output.mDestination);
        }

        size_t checkCellRefSerialization()
        {
            auto raw = decoratedCellRef(CellRef(ESM::makeBlankCellRef()));
            raw.mRefNum = { 51, 2 };
            CellRef ref(raw);
            ref.setEnchantmentCharge(10.5f); // Preserve an already changed input as well.
            raw.mEnchantmentCharge = 10.5f;
            const auto expected = cellRefValues(raw);
            const auto* global = ref.getGlobalVariable().data();
            const auto verify = [&] {
                ESM::ObjectState current;
                ref.writeState(current);
                require(cellRefValues(current.mRef) == expected && ref.hasChanged()
                        && ref.getGlobalVariable().data() == global,
                    "CellRef serialization changed input fields/storage/change tracking");
            };
            size_t allocations = 0;
            for (int count : { -4, 0, 7 })
            {
                const auto counted = ref.copyWithCount(count);
                allocations += checkSerializationAllocations(
                    outputSentinel, [&](auto& output) { counted.writeState(output); }, objectOutputState,
                    [&](const auto& output) {
                        auto fields = raw;
                        fields.mCount = count;
                        require(cellRefValues(output.mRef) == cellRefValues(fields), "CellRef copy lost a field");
                        auto sentinel = outputSentinel();
                        const auto& unchanged = output;
                        require(unchanged.mLocals.mVariables == sentinel.mLocals.mVariables
                                && unchanged.mFlags == sentinel.mFlags && unchanged.mEnabled == sentinel.mEnabled
                                && unchanged.mHasLocals == sentinel.mHasLocals
                                && unchanged.mPosition == sentinel.mPosition
                                && animationValues(unchanged.mAnimationState)
                                    == animationValues(sentinel.mAnimationState)
                                && unchanged.mLuaScripts.mScripts.front().mData
                                    == sentinel.mLuaScripts.mScripts.front().mData
                                && unchanged.mHasCustomState == sentinel.mHasCustomState
                                && unchanged.mActorIdConverter == sentinel.mActorIdConverter
                                && unchanged.mVersion == sentinel.mVersion,
                            "CellRef serialization changed unrelated ObjectState fields");
                    },
                    verify);
            }
            auto output = outputSentinel();
            // A successful direct call must also preserve unrelated caller storage.
            const auto locals = localOutputState(output.mLocals);
            const auto* animation = output.mAnimationState.mScriptedAnims.data();
            const auto* lua = output.mLuaScripts.mScripts.data();
            ref.writeState(output);
            require(localOutputState(output.mLocals) == locals
                    && output.mAnimationState.mScriptedAnims.data() == animation
                    && output.mLuaScripts.mScripts.data() == lua,
                "CellRef serialization replaced unrelated storage");
            // TES4 has no serialization implementation here; preserve both no-ops.
            const auto before = objectOutputState(output);
            for (const CellRef& other : { CellRef(ESM4::Reference{}), CellRef(ESM4::ActorCharacter{}) })
                other.writeState(output);
            require(objectOutputState(output) == before, "TES4 CellRef serialization changed caller output");
            return allocations;
        }

        template <bool ObjectStates = false, class Verify>
        size_t checkPairSerialization(DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
            const Compiler::Locals& declarations, Verify verifyOriginal, bool checkMalformed)
        {
            const auto nodes = ownedNodes(pair);
            std::vector<decltype(nodeState({}))> before;
            for (const auto& node : nodes)
                before.push_back(nodeState(node));
            const auto* bindings = &pair.getIteratorBindings();
            const auto resolution = pair.getResolutionCompleteness();
            const auto verify = [&] {
                verifyOriginal();
                require(ownedNodes(pair) == nodes && &pair.getIteratorBindings() == bindings
                        && pair.getResolutionCompleteness() == resolution,
                    "serialization changed protected bindings/resolution/owned nodes");
                for (size_t i = 0; i < nodes.size(); ++i)
                    require(nodeState(nodes[i]) == before[i], "serialization changed detached value/storage/identity");
            };
            const auto makeOutput = [] { return pairOutputSentinel<ObjectStates>(); };
            const auto write = [&](auto& output) { serializePair(fixture, pair, declarations, output); };
            const auto check = [&](const auto& output) { checkPairOutput(pair, output); };
            // A saved completeness flag is insufficient: even an all-resolved
            // report must pass the complete current pair validator.
            auto rejectedOutput = makeOutput();
            const auto savedOutputState = pairOutputState(rejectedOutput);
            auto& report
                = const_cast<PreparedContainerTransfer::ResolutionCompleteness&>(pair.getResolutionCompleteness());
            ++report.mRegistryEntries;
            bool rejected = false;
            try
            {
                write(rejectedOutput);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            --report.mRegistryEntries;
            require(rejected && pairOutputState(rejectedOutput) == savedOutputState,
                "serialization skipped complete pair validation or changed rejected output");
            verify();
            write(rejectedOutput);
            check(rejectedOutput);
            verify();
            size_t allocations = checkSerializationAllocations(
                makeOutput, write, [](const auto& output) { return pairOutputState(output); }, check, verify);
            if (!checkMalformed)
                return allocations;

            const auto& data = pair.getSourceItem().getRefData();
            const auto makeLocals = [] { return outputSentinel().mLocals; };
            allocations += checkSerializationAllocations(
                makeLocals, [&](auto& output) { data.getLocals().write(output, declarations); }, localOutputState,
                [&](const auto& output) {
                    auto state = outputSentinel();
                    data.write(state, declarations);
                    require(output.mVariables == state.mLocals.mVariables, "direct locals append mismatch");
                },
                verify);
            allocations += checkSerializationAllocations(
                outputSentinel, [&](auto& output) { data.write(output, declarations); }, objectOutputState,
                [&](const auto& output) {
                    checkSerializedData(data, output, 1);
                    auto expected = outputSentinel();
                    require(output.mFlags == 7
                            && output.mLocals.mVariables.front() == expected.mLocals.mVariables.front()
                            && output.mRef.mCount == 73 && output.mRef.mGlobalVariable == expected.mRef.mGlobalVariable
                            && output.mLuaScripts.mScripts.front().mData == expected.mLuaScripts.mScripts.front().mData
                            && output.mHasCustomState == expected.mHasCustomState,
                        "RefData serialization replaced unrelated caller fields");
                },
                verify);

            // Initialized scripts with no declarations still write mHasLocals;
            // uninitialized plain references leave pre-existing locals alone.
            auto empty = data.copyForContainerTransfer();
            empty.getLocals().mShorts.clear();
            empty.getLocals().mLongs.clear();
            empty.getLocals().mFloats.clear();
            Compiler::Locals noDeclarations;
            auto emptyOutput = outputSentinel();
            const auto existingLocals = emptyOutput.mLocals.mVariables;
            require(empty.getLocals().write(emptyOutput.mLocals, noDeclarations), "empty initialized locals lost");
            empty.write(emptyOutput, noDeclarations);
            require(emptyOutput.mHasLocals == 1 && emptyOutput.mLocals.mVariables == existingLocals,
                "empty initialized script changed append semantics");
            RefData plain;
            require(!plain.getLocals().write(emptyOutput.mLocals, declarations), "plain locals initialized by write");
            const auto untouchedLocals = localOutputState(emptyOutput.mLocals);
            plain.write(emptyOutput, declarations);
            require(emptyOutput.mHasLocals == 0 && localOutputState(emptyOutput.mLocals) == untouchedLocals,
                "plain RefData changed caller locals");

            const auto reject = [&](const RefData& malformed, const Compiler::Locals& decl) {
                const auto saved = malformed.copyForContainerTransfer();
                auto output = outputSentinel();
                const auto original = objectOutputState(output);
                for (bool refData : { false, true })
                {
                    bool caught = false;
                    try
                    {
                        if (refData)
                            malformed.write(output, decl);
                        else
                            malformed.getLocals().write(output.mLocals, decl);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                    require(caught && objectOutputState(output) == original
                            && malformed.matchesContainerTransferState(saved),
                        "malformed locals shape accepted or changed caller/input");
                    verify();
                    auto fresh = makeOutput();
                    write(fresh);
                    check(fresh);
                    verify();
                }
                if (&malformed == &data)
                {
                    auto aggregate = makeOutput();
                    const auto savedOutput = pairOutputState(aggregate);
                    bool caught = false;
                    Allocations::Trace trace;
                    {
                        Allocations::Observe observe(trace);
                        try
                        {
                            serializePair(fixture, pair, decl, aggregate);
                        }
                        catch (const std::invalid_argument&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && pairOutputState(aggregate) == savedOutput && trace.mOutstanding == 0
                            && trace.mTrackingOverflow == 0,
                        "malformed declarations leaked staging or partially published pair output");
                    verify();
                    write(aggregate);
                    check(aggregate);
                    verify();
                }
            };
            for (char type : { 's', 'l', 'f' })
            {
                for (int shape = 0; shape < 4; ++shape)
                {
                    Compiler::Locals malformed;
                    for (char current : { 's', 'l', 'f' })
                    {
                        const auto& names = declarations.get(current);
                        for (size_t i = 0; i < names.size(); ++i)
                        {
                            if (type == current && shape == 0 && i == 1)
                                continue;
                            malformed.declare(current,
                                type == current && i == 1 && shape >= 2
                                    ? (shape == 2 ? std::string_view{} : std::string_view(names[0]))
                                    : names[i]);
                        }
                        if (type == current && shape == 1)
                            malformed.declare(current, "unexpected_extra_declaration");
                    }
                    reject(data, malformed);
                }
                for (bool extra : { false, true })
                {
                    auto malformed = data.copyForContainerTransfer();
                    const auto resize = [&](auto& values) { values.resize(values.size() + (extra ? 1 : -1)); };
                    if (type == 's')
                        resize(malformed.getLocals().mShorts);
                    else if (type == 'l')
                        resize(malformed.getLocals().mLongs);
                    else
                        resize(malformed.getLocals().mFloats);
                    reject(malformed, declarations);
                }
            }
            // Cross-type duplicate names are ambiguous even with matching counts.
            Compiler::Locals duplicate;
            for (char type : { 's', 'l', 'f' })
                for (const auto& name : declarations.get(type))
                    duplicate.declare(type, name == "counter" ? "onpcadd" : name);
            reject(data, duplicate);

            return allocations;
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
        Preparation,
        Serialization,
        ObjectState
    };

    static void checkTransferRehearsalCases(const ESMStore& content, AllocationCheck allocationCheck)
    {
        using Rehearsal = DisposableTransferRehearsal;
        using Stage = Rehearsal::Stage;
        using Pair = PreparedContainerTransfer;
        const bool serialization
            = allocationCheck == AllocationCheck::Serialization || allocationCheck == AllocationCheck::ObjectState;
        MWClass::registerClasses();
        ESMStore store;
        const auto plainId = ESM::RefId::stringRefId("native_plain");
        const auto scriptedId = ESM::RefId::stringRefId("native_scripted");
        const auto scriptId = ESM::RefId::stringRefId("native_script");
        const auto ownerId = ESM::RefId::stringRefId("player");
        for (auto id : { plainId, scriptedId })
            store.insertStatic(*content.get<ESM::Miscellaneous>().find(id));
        store.insertStatic(*content.get<ESM::NPC>().find(ownerId));
        auto script = *content.get<ESM::Script>().find(scriptId);
        if (serialization)
            script.mScriptText
                = "begin native_script\nshort OnPCAdd\nshort serialization_short_variable\n"
                  "long counter\nlong serialization_long_variable\n"
                  "float ratio\nfloat serialization_float_variable\nend native_script\n";
        store.insertStatic(script);
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
        if (allocationCheck == AllocationCheck::ObjectState)
            totals.mTotal += checkCellRefSerialization();

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
                            Ptr destinationDormant;
                            if (cursorPosition == 1)
                            {
                                const auto selected = destination.add(dormantItem.getPtr(), 2, fixture.mDestinationAdd);
                                selected->getCellRef() = selected->getCellRef().copyWithCount(0);
                                destination.setSelectedEnchantItem(selected);
                                destinationDormant = *selected;
                            }
                            // Non-mutated supplied store resolves unrelated registry
                            // and script entries, including dormant selection.
                            const auto other = fixture.mOther.add(
                                scriptedItem ? scripted.getPtr() : plain.getPtr(), 3, fixture.mOtherAdd);
                            fixture.mOther.setSelectedEnchantItem(other);
                            other->getCellRef() = other->getCellRef().copyWithCount(0);
                            if (serialization)
                            {
                                const auto decorate = [&](const Ptr& ptr) {
                                    if (allocationCheck == AllocationCheck::ObjectState)
                                        ptr.getCellRef() = CellRef(decoratedCellRef(ptr.getCellRef()));
                                    auto& data = ptr.getRefData();
                                    data.disable();
                                    data.mPhysicsPostponed = true;
                                    data.setPosition({ { 11.5f, -2.25f, 300.f }, { 0.125f, -0.5f, 1.75f } });
                                    data.onActivate();
                                    data.activate();
                                    auto& locals = data.getLocals();
                                    if (!locals.getScriptId().empty())
                                    {
                                        locals.mShorts = { 1, -19 };
                                        locals.mLongs = { 27, -123456 };
                                        locals.mFloats = { -1.25f, 6.5f };
                                    }
                                    auto& animations = data.getAnimationState().mScriptedAnims;
                                    animations.resize(2);
                                    animations[0].mGroup = "serialization_animation_with_allocating_name";
                                    animations[0].mTime = 3.75f;
                                    animations[0].mAbsolute = true;
                                    animations[0].mLoopCount = 0x100000001ull;
                                    animations[1].mGroup = "idle";
                                    animations[1].mTime = -0.5f;
                                    animations[1].mLoopCount = 3;
                                };
                                decorate(item);
                                decorate(dormant);
                                for (auto it = destination.begin(); it != destination.end(); ++it)
                                    decorate(*it);
                                if (!destinationDormant.isEmpty())
                                    decorate(destinationDormant);
                            }
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
                            if (serialization)
                            {
                                const auto run = [&]<bool ObjectStates>() {
                                    const auto& declarations = scripts.getLocals(scriptId);
                                    std::vector<ConstPtr> nodes;
                                    auto output = pairOutputSentinel<ObjectStates>();
                                    std::optional<decltype(pairOutputState(output))> retained;
                                    {
                                        auto pair = make();
                                        nodes = ownedNodes(pair);
                                        require(pair.getSourceItem().getCellRef().getCount(false) == 4 - quantity
                                                && pair.getDestinationItem().getCellRef().getCount(false)
                                                    == (!scriptedItem && stack ? 7 + quantity : quantity),
                                            "serialization fixture lost full/partial removal or destination count");
                                        const bool malformed
                                            = !shared && scriptedItem && !stack && quantity == 1 && cursorPosition == 0;
                                        totals.mTotal += checkPairSerialization<ObjectStates>(
                                            fixture, pair, declarations, verifyOriginal, malformed);
                                        // Rehearsal must retain the same read-only owned values
                                        // and protected bindings for another serialization.
                                        pair = fixture.rehearse(std::move(pair));
                                        serializePair(fixture, pair, declarations, output);
                                        checkPairOutput(pair, output);
                                        require(
                                            ownedNodes(pair) == nodes, "serialization/rehearsal replaced owned nodes");
                                        retained = pairOutputState(output);
                                    }
                                    requireDiscarded(nodes);
                                    // Owned values and proposed identities outlive the pair.
                                    require(pairOutputState(output) == *retained, "discard invalidated owned output");
                                    verifyOriginal();
                                    auto incomplete = source.prepareTransfer(
                                        item, quantity, destination, fixture.mRemoval, fixture.mDestinationAdd);
                                    require(!incomplete.getResolutionCompleteness().isComplete(),
                                        "serialization incomplete fixture resolved extra objects");
                                    const auto saved = pairOutputState(output);
                                    bool caught = false;
                                    try
                                    {
                                        serializePair(fixture, incomplete, declarations, output);
                                    }
                                    catch (const std::invalid_argument&)
                                    {
                                        caught = true;
                                    }
                                    require(caught && pairOutputState(output) == saved,
                                        "incomplete serialization accepted or changed caller output");
                                    verifyOriginal();
                                    auto fresh = make();
                                    serializePair(fixture, fresh, declarations, output);
                                    checkPairOutput(fresh, output);
                                    verifyOriginal();
                                };
                                if (allocationCheck == AllocationCheck::ObjectState)
                                    run.template operator()<true>();
                                else
                                    run.template operator()<false>();
                                ++cases;
                                continue;
                            }
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
            if (serialization)
            {
                std::cout << (allocationCheck == AllocationCheck::ObjectState ? "ObjectState" : "RefData")
                          << " serialization: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " remaining-after-cleanup=0 malformed-shapes=19 direct-rejections=38"
                             " malformed-pair-declarations=13 incomplete-pairs=48 corrupted-pairs=48\n";
                return;
            }
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

    void checkTransferSerialization(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Serialization);
    }

    void checkTransferObjectState(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::ObjectState);
    }
}
