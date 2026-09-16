#include "inventory_transfer_command.hpp"
#include "test_allocations.hpp"
#include "transfer_file_sink.hpp"
#include "transfer_rehearsal.hpp"
#include "transfer_save_codec.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>

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

        // Bitwise float witnesses include NaNs and signed zero. Also witness the
        // allocating string payload of deliberately malformed Variant values.
        auto namedLocalState(const ESM::Locals& locals)
        {
            std::vector<std::tuple<std::string, ESM::VarType, int32_t, uint32_t, std::string, const char*, size_t,
                const char*, size_t>>
                values;
            for (const auto& [name, value] : locals.mVariables)
            {
                const auto type = value.getType();
                const bool integer = type == ESM::VT_Int || type == ESM::VT_Short || type == ESM::VT_Long;
                const bool string = type == ESM::VT_String;
                values.emplace_back(name, type, integer ? value.getInteger() : 0,
                    type == ESM::VT_Float ? std::bit_cast<uint32_t>(value.getFloat()) : 0,
                    string ? value.getString() : std::string{}, name.data(), name.capacity(),
                    string ? value.getString().data() : nullptr, string ? value.getString().capacity() : 0);
            }
            return std::tuple{ values, locals.mVariables.data(), locals.mVariables.capacity() };
        }

        auto localOutputState(const ESM::Locals& locals)
        {
            return namedLocalState(locals);
        }

        auto positionBits(const ESM::Position& position)
        {
            std::array<uint32_t, 6> result;
            for (int i = 0; i < 3; ++i)
            {
                result[i] = std::bit_cast<uint32_t>(position.pos[i]);
                result[i + 3] = std::bit_cast<uint32_t>(position.rot[i]);
            }
            return result;
        }

        auto animationValues(const ESM::AnimationState& state)
        {
            std::vector<std::tuple<std::string, uint32_t, bool, uint64_t>> result;
            for (const auto& animation : state.mScriptedAnims)
                result.emplace_back(animation.mGroup, std::bit_cast<uint32_t>(animation.mTime), animation.mAbsolute,
                    animation.mLoopCount);
            return result;
        }

        auto cellRefValues(const ESM::CellRef& r)
        {
            return std::tuple{ r.mRefNum, r.mRefID, std::bit_cast<uint32_t>(r.mScale), r.mOwner, r.mGlobalVariable,
                r.mSoul, r.mFaction, r.mFactionRank, r.mChargeInt, std::bit_cast<uint32_t>(r.mChargeIntRemainder),
                std::bit_cast<uint32_t>(r.mEnchantmentCharge), r.mCount, r.mTeleport, positionBits(r.mDoorDest),
                r.mDestCell, r.mLockLevel, r.mIsLocked, r.mKey, r.mTrap, r.mReferenceBlocked, positionBits(r.mPos) };
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
                state.mLuaScripts.mScripts.data(), state.mLuaScripts.mScripts.capacity(), positionBits(state.mPosition),
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

        void serializePair(const RestoredPair& pair, const Compiler::Locals& declarations, SerializedPair& output)
        {
            SerializedPair staged;
            staged.mRestart = pair.mRestart;
            staged.mScripts = pair.mScripts;
            serializeInventory(
                pair.mSource.mNodes, [&](size_t i) { return pair.mSource.mProposedIdentities.at(i); }, declarations,
                staged.mSource);
            serializeInventory(
                pair.mDestination.mNodes, [&](size_t i) { return pair.mDestination.mProposedIdentities.at(i); },
                declarations, staged.mDestination);
            output.swap(staged);
        }

        auto scriptOutputState(const TransferScriptMetadata& value)
        {
            return std::tuple{ value, value.mOther.data(), value.mOther.capacity(),
                value.mServices[0].mEntries.data(), value.mServices[0].mEntries.capacity(),
                value.mServices[1].mEntries.data(), value.mServices[1].mEntries.capacity() };
        }

        auto pairOutputState(const SerializedPair& output)
        {
            const auto inventory = [](const SerializedInventory& value) {
                return std::tuple{ pairOutputState(value.mObjects), value.mProposedIdentities,
                    value.mProposedIdentities.data(), value.mProposedIdentities.capacity() };
            };
            return std::tuple{ inventory(output.mSource), inventory(output.mDestination), output.mRestart,
                scriptOutputState(output.mScripts) };
        }

        template <bool ObjectStates>
        auto pairOutputSentinel()
        {
            if constexpr (ObjectStates)
            {
                SerializedPair result;
                result.mRestart = { 0x123456789ull, { 999, -1 } };
                result.mScripts.mShared = false;
                result.mScripts.mServices[1].mEntries.push_back({ { 999, -1 }, ESM::RefId{} });
                result.mScripts.mServices[1].mCursor = 1;
                result.mScripts.mOther.push_back({ { 888, -1 }, ESM::RefId{}, true });
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
                if (failAt)
                {
                    // Retry on the preserved caller as well as a fresh instance.
                    write(*output);
                    check(*output);
                    verify();
                }
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

        auto localValues(const MWScript::Locals& locals)
        {
            std::vector<uint32_t> floats;
            for (float value : locals.mFloats)
                floats.push_back(std::bit_cast<uint32_t>(value));
            return std::tuple{ locals.getScriptId(), locals.mShorts, locals.mLongs, floats };
        }

        auto localStorage(const MWScript::Locals& locals)
        {
            return std::tuple{ localValues(locals), locals.mShorts.data(), locals.mShorts.capacity(),
                locals.mLongs.data(), locals.mLongs.capacity(), locals.mFloats.data(), locals.mFloats.capacity() };
        }

        auto declarationState(const Compiler::Locals& declarations)
        {
            const auto capture = [&](char type) {
                const auto& names = declarations.get(type);
                std::vector<std::pair<const char*, size_t>> strings;
                for (const auto& name : names)
                    strings.emplace_back(name.data(), name.capacity());
                return std::tuple{ names, names.data(), names.capacity(), strings };
            };
            return std::tuple{ capture('s'), capture('l'), capture('f') };
        }

        auto contentState(const RestoreContent& content)
        {
            const auto string = [](const std::string& s) { return std::tuple{ s, s.data(), s.capacity() }; };
            const auto base = [&](const ESM::Miscellaneous& b) {
                return std::tuple{ &b, b.mId, b.mScript, std::bit_cast<uint32_t>(b.mData.mWeight), b.mData.mValue,
                    b.mData.mFlags, b.mRecordFlags, string(b.mName), string(b.mModel), string(b.mIcon) };
            };
            std::vector<decltype(base(*content.mBases.front()))> bases;
            std::vector<const ESM::Miscellaneous*> bindings;
            for (const auto* record : content.mBases)
            {
                bindings.push_back(record);
                if (record)
                    bases.push_back(base(*record));
            }
            const auto& script = content.mScript;
            std::vector<decltype(string(script.mScriptText))> names;
            for (const auto& name : script.mVarNames)
                names.push_back(string(name));
            return std::tuple{ content.mBases.data(), bindings, bases, &script, script.mId, script.mRecordFlags,
                script.mNumShorts, script.mNumLongs, script.mNumFloats, names, script.mVarNames.data(),
                script.mVarNames.capacity(), script.mScriptData, script.mScriptData.data(),
                script.mScriptData.capacity(), string(script.mScriptText), declarationState(content.mDeclarations) };
        }

        template <class Verify>
        size_t checkLocalRestore(const MWScript::Locals& configured, const ESM::Locals& input,
            const Compiler::Locals& declarations, Verify verifyOriginal, bool malformed, size_t& rejections)
        {
            const auto inputBefore = namedLocalState(input);
            const auto configuredBefore = localStorage(configured);
            const auto declarationsBefore = declarationState(declarations);
            const auto verify = [&] {
                require(namedLocalState(input) == inputBefore && localStorage(configured) == configuredBefore
                        && declarationState(declarations) == declarationsBefore,
                    "local restoration changed input/declaration values or storage");
                verifyOriginal();
            };
            const auto roundTrip
                = [&](const MWScript::Locals& restored, const ESM::Locals& expected, const Compiler::Locals& decl) {
                      ESM::Locals output;
                      require(restored.write(output, decl), "restored locals lost configured state");
                      // Canonical write order is independent of the input order.
                      auto ordered = expected.mVariables;
                      auto actual = output.mVariables;
                      const auto byName = [](const auto& a, const auto& b) { return a.first < b.first; };
                      std::sort(ordered.begin(), ordered.end(), byName);
                      std::sort(actual.begin(), actual.end(), byName);
                      require(actual == ordered, "named local read/write lost names/types/values");
                      auto fresh = configured;
                      fresh.mShorts.resize(restored.mShorts.size());
                      fresh.mLongs.resize(restored.mLongs.size());
                      fresh.mFloats.resize(restored.mFloats.size());
                      fresh.read(output, decl);
                      require(localValues(fresh) == localValues(restored), "local write/read lost restored values");
                  };
            const auto make = [&] {
                auto locals = configured;
                std::fill(locals.mShorts.begin(), locals.mShorts.end(), 71);
                std::fill(locals.mLongs.begin(), locals.mLongs.end(), 829);
                std::fill(locals.mFloats.begin(), locals.mFloats.end(), 93.5f);
                return locals;
            };
            size_t allocations = checkSerializationAllocations(
                make, [&](auto& locals) { locals.read(input, declarations); }, localStorage,
                [&](const auto& locals) { roundTrip(locals, input, declarations); }, verify);
            // Exercise each write allocation after restoration, with a preserved
            // caller prefix and fresh read/write recovery after every failure.
            auto restored = make();
            restored.read(input, declarations);
            const auto restoredBefore = localStorage(restored);
            ESM::Locals canonical;
            restored.write(canonical, declarations);
            allocations += checkSerializationAllocations([] { return outputSentinel().mLocals; },
                [&](auto& output) { restored.write(output, declarations); }, namedLocalState,
                [&](const auto& output) {
                    auto expected = outputSentinel().mLocals.mVariables;
                    expected.insert(expected.end(), canonical.mVariables.begin(), canonical.mVariables.end());
                    require(output.mVariables == expected, "restored local write changed append semantics");
                    roundTrip(restored, input, declarations);
                },
                [&] {
                    require(localStorage(restored) == restoredBefore, "write changed restored locals/storage");
                    verify();
                });
            if (!malformed)
                return allocations;

            const auto reject = [&](MWScript::Locals target, const ESM::Locals& bad, const Compiler::Locals& decl) {
                const auto targetBefore = localStorage(target);
                const auto badBefore = namedLocalState(bad);
                const auto declBefore = declarationState(decl);
                Allocations::Trace trace;
                bool caught = false;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        target.read(bad, decl);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                }
                require(caught && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                        && localStorage(target) == targetBefore && namedLocalState(bad) == badBefore
                        && declarationState(decl) == declBefore,
                    "malformed local restoration accepted, leaked, or changed input/target storage");
                ++rejections;
                // Repair only deliberately malformed target shapes/configuration;
                // valid targets retry directly with the good input.
                if (target.getScriptId().empty())
                {
                    ESM::Locals unwritten;
                    require(!target.write(unwritten, declarations), "rejection initialized locals");
                    target = make();
                }
                target.mShorts.resize(configured.mShorts.size());
                target.mLongs.resize(configured.mLongs.size());
                target.mFloats.resize(configured.mFloats.size());
                target.read(input, declarations);
                roundTrip(target, input, declarations);
                auto fresh = make();
                fresh.read(input, declarations);
                roundTrip(fresh, input, declarations);
                verify();
            };
            reject({}, input, declarations);
            for (char type : { 's', 'l', 'f' })
            {
                for (int shape = 0; shape < 5; ++shape)
                {
                    Compiler::Locals bad;
                    for (char current : { 's', 'l', 'f' })
                    {
                        const auto& names = declarations.get(current);
                        for (size_t i = 0; i < names.size(); ++i)
                        {
                            if (current == type && shape == 0 && i == 1)
                                continue;
                            bad.declare(current,
                                current == type && i == 1 && shape >= 2
                                    ? (shape == 2          ? std::string{}
                                              : shape == 3 ? names[0]
                                                           : std::string("bad\0name", 8))
                                    : names[i]);
                        }
                        if (current == type && shape == 1)
                            bad.declare(current, "unexpected_extra_declaration");
                    }
                    reject(make(), input, bad);
                }
                for (bool extra : { false, true })
                {
                    auto target = make();
                    const auto resize = [&](auto& values) { values.resize(values.size() + (extra ? 1 : -1)); };
                    if (type == 's')
                        resize(target.mShorts);
                    else if (type == 'l')
                        resize(target.mLongs);
                    else
                        resize(target.mFloats);
                    reject(target, input, declarations);
                }
            }
            Compiler::Locals duplicate;
            for (char type : { 's', 'l', 'f' })
                for (const auto& name : declarations.get(type))
                    duplicate.declare(type, name == "counter" ? "onpcadd" : name);
            reject(make(), input, duplicate);

            for (size_t i = 0; i < input.mVariables.size(); ++i)
            {
                for (int shape = 0; shape < 6; ++shape)
                {
                    auto bad = input;
                    auto& name = bad.mVariables[i].first;
                    if (shape == 0)
                        bad.mVariables.erase(bad.mVariables.begin() + i);
                    else if (shape == 1)
                        bad.mVariables.push_back(bad.mVariables[i]);
                    else if (shape == 2)
                        name.clear();
                    else if (shape == 3)
                        name = "unknown_allocating_local_name";
                    else if (shape == 4)
                        name = input.mVariables[(i + 1) % input.mVariables.size()].first;
                    else
                        name[0] = 'X';
                    reject(make(), bad, declarations);
                }
                for (auto type : { ESM::VT_Unknown, ESM::VT_None, ESM::VT_Short, ESM::VT_Long, ESM::VT_String,
                         ESM::VT_Int, ESM::VT_Float })
                {
                    if (type == input.mVariables[i].second.getType())
                        continue;
                    auto bad = input;
                    auto& value = bad.mVariables[i].second;
                    value.setType(type);
                    if (type == ESM::VT_String)
                        value.setString("malformed allocating variant string");
                    if (type == ESM::VT_Float)
                        value.setFloat(1.5f);
                    reject(make(), bad, declarations);
                }
            }
            for (int value :
                { -32769, 32768, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max() })
                for (size_t index : { 0, 1 })
                {
                    auto bad = input;
                    bad.mVariables[index].second.setInteger(value);
                    reject(make(), bad, declarations);
                }
            for (float value : { std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN() })
                for (size_t index : { 4, 5 })
                {
                    auto bad = input;
                    bad.mVariables[index].second.setFloat(value);
                    reject(make(), bad, declarations);
                }

            // Numeric endpoints, subnormals, signed zero, and arbitrary named order.
            for (int sample = 0; sample < 3; ++sample)
            {
                auto expected = make();
                expected.mShorts = { -32768, 32767 };
                expected.mLongs = { std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max() };
                expected.mFloats = sample == 0
                    ? std::vector<float>{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max() }
                    : sample == 1 ? std::vector<float>{ -0.f, 0.f }
                                  : std::vector<float>{ -std::numeric_limits<float>::denorm_min(),
                                        std::numeric_limits<float>::denorm_min() };
                ESM::Locals values;
                expected.write(values, declarations);
                std::reverse(values.mVariables.begin(), values.mVariables.end());
                allocations += checkLocalRestore(configured, values, declarations, verifyOriginal, false, rejections);
                auto target = make();
                target.read(values, declarations);
                require(localValues(target) == localValues(expected), "local restoration lost numeric endpoint bits");
            }
            // Empty configured scripts and each individual vector shape are valid.
            for (char only : { ' ', 's', 'l', 'f' })
            {
                auto target = make();
                Compiler::Locals decl;
                for (const auto& name : only == ' ' ? std::vector<std::string>{} : declarations.get(only))
                    decl.declare(only, name);
                if (only != 's')
                    target.mShorts.clear();
                if (only != 'l')
                    target.mLongs.clear();
                if (only != 'f')
                    target.mFloats.clear();
                ESM::Locals values;
                target.write(values, decl);
                if (only != ' ')
                    allocations += checkLocalRestore(target, values, decl, verifyOriginal, false, rejections);
                else
                {
                    Allocations::Trace trace;
                    {
                        Allocations::Observe observe(trace);
                        target.read(values, decl);
                    }
                    require(trace.mTotal == 0, "empty locals restoration allocated");
                }
                roundTrip(target, values, decl);
            }
            verify();
            return allocations;
        }

        auto restoredState(const std::unique_ptr<const RestoredPair>& pair)
        {
            const auto inventory = [](const RestoredInventory& value) {
                std::vector<decltype(nodeState({}))> nodes;
                std::vector<const ESM::Miscellaneous*> bases;
                std::vector<std::pair<const char*, size_t>> strings;
                std::vector<std::tuple<const LiveCellRefBase*, const CellStore*, const ContainerStore*,
                    ReferenceLifetime::Witness>> views;
                for (const auto& view : value.mViews)
                    views.emplace_back(view.mRef, view.mCell, view.mContainerStore, view.getReferenceLifetime());
                for (const auto& node : value.mNodes)
                {
                    nodes.push_back(nodeState(ConstPtr(&node)));
                    bases.push_back(node.mBase);
                    for (const auto& animation : node.mData.getAnimationState().mScriptedAnims)
                        strings.emplace_back(animation.mGroup.data(), animation.mGroup.capacity());
                }
                return std::tuple{ nodes, bases, strings, value.mProposedIdentities, value.mProposedIdentities.data(),
                    value.mProposedIdentities.capacity(), views, value.mViews.data(), value.mViews.capacity() };
            };
            return std::tuple{ pair.get(), inventory(pair->mSource), inventory(pair->mDestination), pair->mRestart,
                scriptOutputState(pair->mScripts) };
        }

        RestoredPair expectedRestoration(const PreparedContainerTransfer& pair, bool serialized = true)
        {
            RestoredPair result;
            const auto& registry = pair.getRelocation().mRegistry;
            result.mRestart = { registry.mRevision, registry.mLastGenerated };
            const auto copy = [&](const auto& storage, const auto& views, RestoredInventory& inventory) {
                size_t i = 0;
                for (const auto& node : storage)
                {
                    auto& expected = inventory.mNodes.emplace_back(ESM::makeBlankCellRef(), node.mBase);
                    expected.mRef = node.mRef;
                    expected.mData = node.mData.copyForContainerTransfer();
                    // This transient engine flag is deliberately absent from ObjectState.
                    if (serialized)
                        expected.mData.mPhysicsPostponed = false;
                    const auto id = views[i++].mIdentity;
                    inventory.mProposedIdentities.push_back(id.isSet() ? id : pair.getDestinationIdentity());
                }
            };
            copy(pair.getSourceStorage(), pair.getRelocation().mSource, result.mSource);
            copy(pair.getDestinationStorage(), pair.getRelocation().mDestination, result.mDestination);
            return result;
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
                    && CellRef(output).getDestCell() == ref.getDestCell() && output.mLockLevel == ref.getLockLevel()
                    && output.mIsLocked == ref.isLocked() && output.mKey == ref.getKey()
                    && output.mTrap == ref.getTrap() && output.mReferenceBlocked == 1
                    && output.mPos == ref.getPosition(),
                "serialized CellRef field mismatch");
        }

        void checkRestored(const RestoredPair& restored, const RestoredPair& expected)
        {
            require(restored.mRestart == expected.mRestart, "restoration lost accepted revision/generation counter");
            const auto check = [](const RestoredInventory& actual, const RestoredInventory& wanted) {
                require(actual.mProposedIdentities == wanted.mProposedIdentities
                        && actual.mNodes.size() == wanted.mNodes.size(),
                    "restoration lost membership/associations");
                auto next = wanted.mNodes.begin();
                for (const auto& node : actual.mNodes)
                {
                    const auto& value = *next++;
                    require(&node != &value && node.mBase == value.mBase && node.mWorldModel == nullptr
                            && !node.mRef.getRefNum().isSet() && !node.mData.getBaseNode()
                            && !node.mData.getCustomData() && !node.mData.getLuaScripts(),
                        "restoration shared nodes, assigned identities or installed unsupported state");
                    // Compare actual engine values against a pre-destruction copy
                    // of the prepared result, independent of the restored serializer.
                    require(node.mData.matchesContainerTransferState(value.mData)
                            && localValues(node.mData.getLocals()) == localValues(value.mData.getLocals())
                            && positionBits(node.mData.getPosition()) == positionBits(value.mData.getPosition())
                            && animationValues(node.mData.getAnimationState())
                                == animationValues(value.mData.getAnimationState()),
                        "restored RefData differs from expected prepared result");
                    ESM::ObjectState ref;
                    value.mRef.writeState(ref);
                    checkSerializedRef(node.mRef, ref.mRef);
                    ESM::ObjectState actualRef;
                    node.mRef.writeState(actualRef);
                    require(cellRefValues(actualRef.mRef) == cellRefValues(ref.mRef),
                        "restored CellRef lost hidden fields or float bits");
                }
            };
            check(restored.mSource, expected.mSource);
            check(restored.mDestination, expected.mDestination);
        }

        void checkSavedValues(const SerializedPair& actual, const SerializedPair& expected)
        {
            require(actual.mRestart == expected.mRestart, "save/restore/save changed restart metadata");
            require(actual.mScripts == expected.mScripts, "save/restore/save changed script metadata");
            const auto check = [](const SerializedInventory& a, const SerializedInventory& b) {
                require(a.mProposedIdentities == b.mProposedIdentities && a.mObjects.size() == b.mObjects.size(),
                    "save/restore/save lost membership or identities");
                for (size_t i = 0; i < a.mObjects.size(); ++i)
                {
                    const auto& x = a.mObjects[i];
                    const auto& y = b.mObjects[i];
                    require(cellRefValues(x.mRef) == cellRefValues(y.mRef) && x.mHasLocals == y.mHasLocals
                            && x.mLocals.mVariables == y.mLocals.mVariables && x.mFlags == y.mFlags
                            && x.mEnabled == y.mEnabled && positionBits(x.mPosition) == positionBits(y.mPosition)
                            && animationValues(x.mAnimationState) == animationValues(y.mAnimationState)
                            && x.mVersion == y.mVersion && x.mActorIdConverter == nullptr && !x.mHasCustomState
                            && x.mLuaScripts.mScripts.empty(),
                        "save/restore/save changed owned engine values");
                }
            };
            check(actual.mSource, expected.mSource);
            check(actual.mDestination, expected.mDestination);
        }

        auto invalidRestartValues(TransferRestartMetadata valid)
        {
            auto values = std::vector<TransferRestartMetadata>{ { 0, valid.mLastGenerated },
                { valid.mRevision, {} }, { valid.mRevision, { 1, -1 } },
                { valid.mRevision, { valid.mLastGenerated.mIndex, 0 } },
                { valid.mRevision, { valid.mLastGenerated.mIndex, -2 } },
                { valid.mRevision, { UINT32_MAX, INT32_MIN } } };
            if constexpr (sizeof(size_t) < sizeof(uint64_t))
                values.push_back({ static_cast<uint64_t>(std::numeric_limits<size_t>::max()) + 1,
                    valid.mLastGenerated });
            return values;
        }

        template <class Verify>
        size_t checkInventoryRestore(const SerializedPair& input, const RestoreContent& content,
            const RestoredPair& expected, Verify verifyOriginal, bool malformed, size_t& rejections)
        {
            const auto inputBefore = pairOutputState(input);
            const auto contentBefore = contentState(content);
            const auto verify = [&] {
                require(pairOutputState(input) == inputBefore && contentState(content) == contentBefore,
                    "restoration changed caller input or declarations/storage");
                verifyOriginal();
            };
            const auto make = [&] {
                std::unique_ptr<const RestoredPair> result;
                restorePair(input, content, result);
                return result;
            };
            Allocations::Trace validation;
            {
                Allocations::Observe observe(validation);
                validateRestore(input, content);
            }
            require(validation.mTotal == 0, "valid inventory validation allocated before staging");
            const auto check = [&](const auto& restored) {
                checkRestored(*restored, expected);
                SerializedPair saved;
                serializePair(*restored, content.mDeclarations, saved);
                checkSavedValues(saved, input);
                // The second save must itself be independently restorable.
                std::unique_ptr<const RestoredPair> again;
                restorePair(saved, content, again);
                checkRestored(*again, expected);
            };
            size_t allocations = checkSerializationAllocations(
                make, [&](auto& output) { restorePair(input, content, output); }, restoredState, check, verify);
            auto restored = make();
            const auto restoredBefore = restoredState(restored);
            allocations += checkSerializationAllocations([] { return pairOutputSentinel<true>(); },
                [&](auto& output) { serializePair(*restored, content.mDeclarations, output); },
                [](const auto& output) { return pairOutputState(output); },
                [&](const auto& output) { checkSavedValues(output, input); },
                [&] {
                    require(restoredState(restored) == restoredBefore, "reserialization changed restored storage");
                    verify();
                });
            if (!malformed)
                return allocations;

            auto reordered = input;
            for (auto* inventory : { &reordered.mSource, &reordered.mDestination })
                for (auto& object : inventory->mObjects)
                    std::reverse(object.mLocals.mVariables.begin(), object.mLocals.mVariables.end());
            const auto reorderedBefore = pairOutputState(reordered);
            allocations += checkSerializationAllocations(
                make, [&](auto& output) { restorePair(reordered, content, output); }, restoredState, check,
                [&] {
                    require(pairOutputState(reordered) == reorderedBefore, "restoration reordered caller locals");
                    verify();
                });

            // Configured scripts with no variables must remain configured even
            // though their vectors are indistinguishable from plain empty locals.
            auto emptyLocals = input;
            for (auto* inventory : { &emptyLocals.mSource, &emptyLocals.mDestination })
                for (auto& object : inventory->mObjects)
                    object.mLocals.mVariables.clear();
            Compiler::Locals noDeclarations;
            const RestoreContent emptyContent{ content.mBases, content.mScript, noDeclarations };
            const auto emptyBefore = pairOutputState(emptyLocals);
            allocations += checkSerializationAllocations(
                make, [&](auto& output) { restorePair(emptyLocals, emptyContent, output); }, restoredState,
                [&](const auto& output) {
                    SerializedPair saved;
                    serializePair(*output, noDeclarations, saved);
                    checkSavedValues(saved, emptyLocals);
                    std::unique_ptr<const RestoredPair> again;
                    restorePair(saved, emptyContent, again);
                    SerializedPair second;
                    serializePair(*again, noDeclarations, second);
                    checkSavedValues(second, emptyLocals);
                },
                [&] {
                    require(pairOutputState(emptyLocals) == emptyBefore, "restoration changed empty-script input");
                    verify();
                });

            const auto reject = [&](const SerializedPair& bad, const RestoreContent& supplied) {
                auto output = make();
                const auto before = restoredState(output);
                const auto saved = pairOutputState(bad);
                const auto suppliedBefore = contentState(supplied);
                Allocations::Trace validationTrace;
                {
                    Allocations::Observe observe(validationTrace);
                    try
                    {
                        validateRestore(bad, supplied);
                    }
                    catch (const std::invalid_argument&)
                    {
                    }
                }
                Allocations::Trace trace;
                bool caught = false;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        restorePair(bad, supplied, output);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                }
                require(caught && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                        && trace.mTotal == validationTrace.mTotal && validationTrace.mOutstanding == 0,
                    "malformed inventory accepted or leaked staged restoration");
                require(restoredState(output) == before && pairOutputState(bad) == saved
                        && contentState(supplied) == suppliedBefore,
                    "malformed restoration changed input, prior output or storage");
                for (size_t ordinal = 1; ordinal <= trace.mTotal; ++ordinal)
                {
                    Allocations::Trace failure;
                    caught = false;
                    {
                        Allocations::Observe observe(failure, ordinal);
                        try
                        {
                            restorePair(bad, supplied, output);
                        }
                        catch (const std::exception&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && failure.mFailures == 1 && failure.mOutstanding == 0
                            && failure.mTrackingOverflow == 0 && restoredState(output) == before
                            && pairOutputState(bad) == saved && contentState(supplied) == suppliedBefore,
                        "malformed restore allocation failure changed caller state or leaked");
                    check(make());
                    verify();
                }
                allocations += trace.mTotal;
                verify();
                restorePair(input, content, output);
                check(output);
                check(make());
                ++rejections;
            };
            const auto mutate = [&](auto change) {
                auto bad = input;
                change(bad);
                reject(bad, content);
            };
            for (auto restart : invalidRestartValues(input.mRestart))
                mutate([&](auto& bad) { bad.mRestart = restart; });
            // Exercise errors on both sides, including the final destination node.
            for (bool destination : { false, true })
            {
                const auto object = [&](auto change) {
                    mutate([&](auto& bad) { change((destination ? bad.mDestination : bad.mSource).mObjects.back()); });
                };
                mutate(
                    [&](auto& bad) { (destination ? bad.mDestination : bad.mSource).mProposedIdentities.pop_back(); });
                mutate(
                    [&](auto& bad) { (destination ? bad.mDestination : bad.mSource).mProposedIdentities.back() = {}; });
                mutate([&](auto& bad) {
                    (destination ? bad.mDestination : bad.mSource).mProposedIdentities.back().mContentFile = -2;
                });
                mutate([&](auto& bad) {
                    (destination ? bad.mDestination : bad.mSource).mProposedIdentities.back()
                        = { bad.mRestart.mLastGenerated.mIndex + 1, -1 };
                });
                object([](auto& s) { s.mRef.mRefNum = { 19, -1 }; });
                object([](auto& s) { s.mRef.mRefID = {}; });
                object([](auto& s) { s.mRef.mRefID = ESM::RefId::generated(19); });
                object([](auto& s) { s.mRef.mRefID = ESM::RefId::stringRefId("not_supplied"); });
                object([](auto& s) { s.mRef.mRefID = ESM::RefId::stringRefId("gold_001"); });
                object([](auto& s) { s.mRef.mCount = std::numeric_limits<int32_t>::min(); });
                object([](auto& s) { s.mRef.mScale = 0; });
                object([](auto& s) { s.mRef.mScale = -1; });
                object([](auto& s) { s.mRef.mChargeInt = -2; });
                object([](auto& s) { s.mRef.mEnchantmentCharge = -2; });
                object([](auto& s) { s.mRef.mGlobalVariable = std::string("bad\0name", 8); });
                object([](auto& s) { s.mRef.mDestCell = std::string(4097, 'x'); });
                object([](auto& s) { s.mRef.mOwner = ESM::RefId::stringRefId(std::string("bad\0id", 6)); });
                object([](auto& s) { s.mRef.mSoul = ESM::RefId::generated(17); });
                object([](auto& s) { s.mHasLocals = 2; });
                object([](auto& s) { s.mEnabled = 2; });
                object([](auto& s) { s.mFlags = 8; });
                object([](auto& s) { s.mVersion = ESM::DefaultFormatVersion + 1; });
                object([](auto& s) { s.mHasCustomState = true; });
                object([](auto& s) { s.mActorIdConverter = reinterpret_cast<ESM::ActorIdConverter*>(1); });
                object([](auto& s) { s.mLuaScripts.mScripts.push_back({ 17, "unsupported", {} }); });
                object([](auto& s) { s.mAnimationState.mScriptedAnims.front().mGroup.clear(); });
                object([](auto& s) { s.mAnimationState.mScriptedAnims.front().mGroup = std::string("bad\0name", 8); });
                object([](auto& s) { s.mAnimationState.mScriptedAnims.resize(257); });
                object([](auto& s) { s.mLocals.mVariables.resize(1025); });
                for (float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity() })
                {
                    object([&](auto& s) { s.mRef.mScale = value; });
                    object([&](auto& s) { s.mRef.mChargeIntRemainder = value; });
                    object([&](auto& s) { s.mRef.mEnchantmentCharge = value; });
                    object([&](auto& s) { s.mAnimationState.mScriptedAnims.back().mTime = value; });
                    for (int axis = 0; axis < 3; ++axis)
                        for (bool rotation : { false, true })
                            for (int position = 0; position < 3; ++position)
                                object([&](auto& s) {
                                    auto& p = position == 0 ? s.mRef.mPos
                                        : position == 1     ? s.mRef.mDoorDest
                                                            : s.mPosition;
                                    (rotation ? p.rot : p.pos)[axis] = value;
                                });
                }
            }
            mutate([](auto& bad) {
                bad.mDestination.mProposedIdentities.back() = bad.mSource.mProposedIdentities.front();
            });
            mutate([](auto& bad) { bad.mSource.mProposedIdentities.back() = bad.mSource.mProposedIdentities.front(); });
            mutate([](auto& bad) {
                const size_t originalSize = bad.mDestination.mObjects.size();
                bad.mDestination.mObjects.resize(1025);
                for (size_t i = originalSize; i < bad.mDestination.mObjects.size(); ++i)
                    bad.mDestination.mObjects[i].blank();
                bad.mDestination.mProposedIdentities.resize(1025);
            });
            // This fixture has a scripted destination and a plain dormant source.
            const auto local = [&](auto change) {
                mutate([&](auto& bad) { change(bad.mDestination.mObjects.back().mLocals.mVariables); });
            };
            mutate([](auto& bad) { bad.mDestination.mObjects.back().mHasLocals = 0; });
            mutate([](auto& bad) { bad.mSource.mObjects.back().mHasLocals = 1; });
            mutate([](auto& bad) {
                bad.mSource.mObjects.back().mLocals.mVariables.emplace_back("unexpected", ESM::Variant(1));
            });
            local([](auto& v) { v.pop_back(); });
            local([](auto& v) { v.push_back(v.front()); });
            local([](auto& v) { v.front().first.clear(); });
            local([](auto& v) { v.front().first = "unknown"; });
            local([](auto& v) { v.front().first = v.back().first; });
            local([](auto& v) { v.front().first = std::string("bad\0name", 8); });
            local([](auto& v) { v.front().second.setInteger(32768); });
            local([](auto& v) { v.front().second.setInteger(-32769); });
            local([](auto& v) { v.front().second = ESM::Variant(1.f); });
            local([](auto& v) { v.back().second = ESM::Variant(1); });
            local([](auto& v) { v.back().second = ESM::Variant(std::numeric_limits<float>::quiet_NaN()); });
            for (auto type : { ESM::VT_None, ESM::VT_Short, ESM::VT_Long, ESM::VT_String })
                local([&](auto& v) { v.front().second.setType(type); });
            for (int shape = 0; shape < 4; ++shape)
            {
                Compiler::Locals badDeclarations;
                for (char type : { 's', 'l', 'f' })
                    for (const auto& name : content.mDeclarations.get(type))
                    {
                        if (type == 's' && name == content.mDeclarations.get('s').front())
                        {
                            if (shape == 0)
                                continue;
                            badDeclarations.declare(type,
                                shape == 1       ? std::string{}
                                    : shape == 2 ? content.mDeclarations.get('l').front()
                                                 : std::string("bad\0name", 8));
                        }
                        else
                            badDeclarations.declare(type, name);
                    }
                reject(input, { content.mBases, content.mScript, badDeclarations });
            }
            auto bases = std::vector<const ESM::Miscellaneous*>(content.mBases.begin(), content.mBases.end());
            bases.push_back(bases.front());
            reject(input, { bases, content.mScript, content.mDeclarations });
            bases.back() = nullptr;
            reject(input, { bases, content.mScript, content.mDeclarations });
            reject(input, { {}, content.mScript, content.mDeclarations });
            auto badScript = content.mScript;
            badScript.mId = ESM::RefId::stringRefId("wrong_script");
            reject(input, { content.mBases, badScript, content.mDeclarations });
            auto gold = *content.mBases.front();
            gold.mId = ESM::RefId::stringRefId("gold_100");
            bases = { &gold };
            reject(input, { bases, content.mScript, content.mDeclarations });
            return allocations;
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
            const auto& registry = pair.getRelocation().mRegistry;
            require(output.mRestart == TransferRestartMetadata{ registry.mRevision, registry.mLastGenerated },
                "serialization lost complete prepared registry restart metadata");
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

        auto byteState(const TransferSaveBytes& bytes)
        {
            return std::tuple{ bytes, bytes.data(), bytes.capacity() };
        }

        template <class Verify>
        size_t checkCodecSave(const SerializedPair& input, const TransferSaveBytes& bytes, const SaveBindings& bindings,
            const RestoredPair& expected, Verify verifyOriginal, bool malformed, size_t& rejections)
        {
            const auto inputBefore = pairOutputState(input);
            const auto bytesBefore = byteState(bytes);
            const auto contentBefore = contentState(bindings.mContent);
            const auto envelopeBefore = bindings.mEnvelope;
            const auto verify = [&] {
                require(pairOutputState(input) == inputBefore && byteState(bytes) == bytesBefore
                        && contentState(bindings.mContent) == contentBefore && bindings.mEnvelope == envelopeBefore,
                    "codec changed input/storage/content/envelope");
                verifyOriginal();
            };
            const auto check = [&](const SerializedPair& decoded) {
                checkSavedValues(decoded, input);
                std::unique_ptr<const RestoredPair> restored;
                restorePair(decoded, bindings.mContent, restored);
                checkRestored(*restored, expected);
                SerializedPair saved;
                serializePair(*restored, bindings.mContent.mDeclarations, saved);
                checkSavedValues(saved, input);
                TransferSaveBytes again;
                encodeTransferSave(saved, bindings, again);
                require(again == bytes, "restored save changed canonical bytes");
            };
            size_t allocations = checkSerializationAllocations([] { return TransferSaveBytes(57, 'x'); },
                [&](auto& output) { encodeTransferSave(input, bindings, output); }, byteState,
                [&](const auto& output) { require(output == bytes, "encoding changed accepted bytes"); }, verify);
            allocations += checkSerializationAllocations([] { return pairOutputSentinel<true>(); },
                [&](auto& output) { decodeTransferSave(bytes, bindings, output); },
                [](const auto& output) { return pairOutputState(output); }, check, verify);
            if (!malformed)
                return allocations;

            // Preserve high 32 revision bits and the inclusive counter boundary.
            // Values are carried verbatim even when no saved node owns that ID.
            for (uint64_t revision : { uint64_t{ 1 }, uint64_t{ std::numeric_limits<size_t>::max() } })
            {
                auto boundary = input;
                boundary.mRestart = { revision, { UINT32_MAX, -1 } };
                TransferSaveBytes encoded;
                encodeTransferSave(boundary, bindings, encoded);
                SerializedPair decoded;
                decodeTransferSave(encoded, bindings, decoded);
                checkSavedValues(decoded, boundary);
                std::unique_ptr<const RestoredPair> restored;
                restorePair(decoded, bindings.mContent, restored);
                SerializedPair saved;
                serializePair(*restored, bindings.mContent.mDeclarations, saved);
                checkSavedValues(saved, boundary);
                TransferSaveBytes again;
                encodeTransferSave(saved, bindings, again);
                require(again == encoded, "restart metadata boundary lost through detached save");
                verify();
            }

            const auto reject = [&](std::span<const char> bad, const SaveBindings& supplied, bool preflight = false) {
                auto output = pairOutputSentinel<true>();
                const auto before = pairOutputState(output);
                const TransferSaveBytes retained(bad.begin(), bad.end());
                Allocations::Trace trace;
                bool caught = false;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        decodeTransferSave(bad, supplied, output);
                    }
                    catch (const std::exception&)
                    {
                        caught = true;
                    }
                }
                require(caught && pairOutputState(output) == before
                        && std::equal(bad.begin(), bad.end(), retained.begin()) && trace.mOutstanding == 0
                        && trace.mTrackingOverflow == 0,
                    "malformed codec input accepted, leaked or changed prior output");
                // Only std::invalid_argument's diagnostic may allocate before
                // preflight rejects. ESMReader alone would allocate 50 KiB.
                require(!preflight || trace.mTotal <= 1, "unbounded input reached engine allocation");
                ++rejections;
                verify();
                // Rejection diagnostics and late semantic/canonical checks can
                // allocate too. Fail each observed ordinal on those paths with
                // fresh retries, then retry on the unchanged prior output.
                for (size_t ordinal = 1; ordinal <= trace.mTotal; ++ordinal)
                {
                    Allocations::Trace failure;
                    caught = false;
                    {
                        Allocations::Observe observe(failure, ordinal);
                        try
                        {
                            decodeTransferSave(bad, supplied, output);
                        }
                        catch (const std::exception&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && failure.mFailures == 1 && failure.mOutstanding == 0
                            && failure.mTrackingOverflow == 0 && pairOutputState(output) == before,
                        "malformed decode allocation failure changed output or leaked");
                    verify();
                    auto retry = pairOutputSentinel<true>();
                    decodeTransferSave(bytes, bindings, retry);
                    check(retry);
                }
                allocations += trace.mTotal;
                decodeTransferSave(bytes, bindings, output);
                check(output);
                verify();
            };
            for (size_t size = 0; size < bytes.size(); ++size)
                reject(std::span(bytes).first(size), bindings, true);
            auto bad = bytes;
            bad.push_back(0);
            reject(bad, bindings, true);
            bad.assign(MaxTransferSaveBytes + 1, 0);
            reject(bad, bindings, true);

            const auto number = [](const TransferSaveBytes& data, size_t offset) {
                uint32_t value = 0;
                for (size_t i = 0; i < 4; ++i)
                    value |= static_cast<uint32_t>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
                return value;
            };
            const auto put = [](TransferSaveBytes& data, size_t offset, uint32_t value) {
                for (size_t i = 0; i < 4; ++i)
                    data.at(offset + i) = static_cast<char>(value >> (8 * i));
            };
            struct Field
            {
                std::string mTag;
                size_t mRecord, mHeader, mSize;
            };
            std::vector<Field> fields;
            std::vector<size_t> records;
            for (size_t record = 0; record < bytes.size();)
            {
                records.push_back(record);
                const auto end = record + 16 + number(bytes, record + 4);
                for (size_t sub = record + 16; sub < end;)
                {
                    const auto size = number(bytes, sub + 4);
                    fields.push_back({ std::string(bytes.data() + sub, 4), record, sub, size });
                    sub += 8 + size;
                }
                record = end;
            }
            for (const auto record : records)
            {
                bad = bytes;
                put(bad, record + 4, UINT32_MAX);
                reject(bad, bindings, true);
                bad = bytes;
                put(bad, record + 8, 1);
                reject(bad, bindings, true);
            }
            for (const auto& field : fields)
            {
                bad = bytes;
                put(bad, field.mHeader + 4, UINT32_MAX);
                reject(bad, bindings, true);
                bad = bytes;
                bad[field.mHeader] = '?';
                reject(bad, bindings, true);
            }
            const auto find = [&](std::string_view tag) -> const Field& {
                const auto it
                    = std::find_if(fields.begin(), fields.end(), [&](const auto& f) { return f.mTag == tag; });
                if (it == fields.end())
                    throw std::runtime_error("missing codec mutation field: " + std::string(tag));
                return *it;
            };
            const auto change = [&](std::string_view tag, uint32_t value, bool early = false) {
                bad = bytes;
                put(bad, find(tag).mHeader + 8, value);
                reject(bad, bindings, early);
            };
            for (auto tag : { "FORM", "FVER", "SOWN", "DOWN", "INIT" })
                change(tag, 999999, true);
            change("FVER", 1, true); // No version-1 migration or inferred metadata.
            change("FVER", 2, true); // Version 2 did not persist script service state.
            bad = bytes;
            bad.erase(bad.begin() + find("RREV").mHeader, bad.begin() + find("LGEN").mHeader + 16);
            put(bad, find("RREV").mRecord + 4, number(bytes, find("RREV").mRecord + 4) - 32);
            put(bad, find("FVER").mHeader + 8, 1);
            reject(bad, bindings, true); // Actual old layout without either field.
            for (const auto restart : invalidRestartValues(input.mRestart))
            {
                bad = bytes;
                const auto revision = find("RREV").mHeader + 8;
                const auto counter = find("LGEN").mHeader + 8;
                put(bad, revision, static_cast<uint32_t>(restart.mRevision));
                put(bad, revision + 4, static_cast<uint32_t>(restart.mRevision >> 32));
                put(bad, counter, restart.mLastGenerated.mIndex);
                put(bad, counter + 4, std::bit_cast<uint32_t>(restart.mLastGenerated.mContentFile));
                reject(bad, bindings, true);
            }
            // Strict framing: missing, short, long, duplicate and reordered
            // metadata must fail before ESMReader allocation or output publication.
            for (const auto tag : { "RREV", "LGEN" })
                for (const size_t size : { size_t{ 0 }, size_t{ 4 }, size_t{ 12 } })
                {
                    const auto& field = find(tag);
                    bad = bytes;
                    bad.erase(bad.begin() + field.mHeader + 8, bad.begin() + field.mHeader + 8 + field.mSize);
                    bad.insert(bad.begin() + field.mHeader + 8, size, 0);
                    put(bad, field.mHeader + 4, static_cast<uint32_t>(size));
                    put(bad, field.mRecord + 4,
                        number(bytes, field.mRecord + 4) - static_cast<uint32_t>(field.mSize)
                            + static_cast<uint32_t>(size));
                    reject(bad, bindings, true);
                }
            for (const auto tag : { "RREV", "LGEN" })
            {
                const auto& field = find(tag);
                bad = bytes;
                bad.erase(bad.begin() + field.mHeader, bad.begin() + field.mHeader + 8 + field.mSize);
                put(bad, field.mRecord + 4,
                    number(bytes, field.mRecord + 4) - static_cast<uint32_t>(8 + field.mSize));
                reject(bad, bindings, true);
            }
            bad = bytes;
            std::swap_ranges(bad.begin() + find("RREV").mHeader, bad.begin() + find("RREV").mHeader + 16,
                bad.begin() + find("LGEN").mHeader);
            reject(bad, bindings, true);
            bad = bytes;
            put(bad, find("HEDR").mHeader + 16, UINT32_MAX); // Nested author length.
            reject(bad, bindings, true);
            change("SIZE", 1025, true);
            change("SIZE", 0, true);
            change("XSAV", 0x7f800000); // Nonfinite scale.
            change("NAM9", 0x80000000); // Unsupported signed count.
            change("FLAG", UINT32_MAX);
            change("XTIM", 0x7fc00000);
            change("POS_", 0x7f800000);
            change("XPOS", 0x7f800000);
            bad = bytes;
            const auto& local = find("LOCA");
            put(bad, local.mHeader + 8 + local.mSize + 8, INT32_MAX);
            reject(bad, bindings); // Short local outside its declared range.
            for (auto tag : { "HCUS", "HLOC", "ENAB", "ABST" })
            {
                bad = bytes;
                bad[find(tag).mHeader + 8] = 2;
                reject(bad, bindings, true);
            }
            bad = bytes;
            bad[find("NAME").mHeader + 9] = '?'; // Unknown RefId must never intern.
            reject(bad, bindings, true);
            bad = bytes;
            bad[find("NAME").mHeader + 8] = static_cast<char>(ESM::RefIdType::SizedString);
            reject(bad, bindings, true);
            const auto firstId = find("IDEN").mHeader + 8;
            bad = bytes;
            put(bad, firstId, input.mRestart.mLastGenerated.mIndex + 1);
            put(bad, firstId + 4, UINT32_MAX);
            reject(bad, bindings, true);
            for (const auto tag : { "SOWN", "DOWN", "INIT" })
            {
                auto envelope = bindings.mEnvelope;
                auto& id = std::string_view(tag) == "SOWN" ? envelope.mSourceOwner
                    : std::string_view(tag) == "DOWN"     ? envelope.mDestinationOwner
                                                         : envelope.mInitiator;
                id = { input.mRestart.mLastGenerated.mIndex + 1, -1 };
                bad = bytes;
                put(bad, find(tag).mHeader + 8, id.mIndex);
                put(bad, find(tag).mHeader + 12, UINT32_MAX);
                reject(bad, { envelope, bindings.mContent, bindings.mReferenceIds }, true);
            }
            for (uint32_t value : { 0u, bindings.mEnvelope.mSourceOwner.mIndex })
            {
                bad = bytes;
                put(bad, firstId, value);
                put(bad, firstId + 4, UINT32_MAX);
                reject(bad, bindings, true);
            }
            bad = bytes;
            put(bad, firstId + 4, UINT32_MAX - 1);
            reject(bad, bindings, true);
            const auto secondId = std::find_if(fields.begin(), fields.end(),
                [&](const auto& f) { return f.mTag == "IDEN" && f.mHeader + 8 != firstId; });
            require(secondId != fields.end(), "codec duplicate fixture needs two nodes");
            bad = bytes;
            std::copy_n(bytes.begin() + firstId, 8, bad.begin() + secondId->mHeader + 8);
            reject(bad, bindings, true);
            bad = bytes;
            bad[find("IDEN").mRecord + 3] = 'D';
            reject(bad, bindings, true);

            const auto append = [&](std::string_view tag, size_t count) {
                const auto& field = find(tag);
                bad = bytes;
                const size_t end = field.mRecord + 16 + number(bytes, field.mRecord + 4);
                TransferSaveBytes extra;
                for (size_t i = 0; i < count; ++i)
                    extra.insert(
                        extra.end(), bytes.begin() + field.mHeader, bytes.begin() + field.mHeader + 8 + field.mSize);
                bad.insert(bad.begin() + end, extra.begin(), extra.end());
                put(bad, field.mRecord + 4, number(bytes, field.mRecord + 4) + static_cast<uint32_t>(extra.size()));
            };
            append("LOCA", 1025);
            reject(bad, bindings, true);
            append("ANIS", 257);
            reject(bad, bindings, true);
            append("XSAV", 1); // Well-framed duplicate extension.
            reject(bad, bindings);
            for (const auto tag : { "RREV", "LGEN" })
            {
                append(tag, 1);
                reject(bad, bindings, true);
            }
            append("ANIS", MaxTransferObjectBytes / (find("ANIS").mSize + 8) + 1);
            reject(bad, bindings, true);
            const auto& text = find("XDST");
            bad = bytes;
            bad.insert(bad.begin() + text.mHeader + 8 + text.mSize, 4097, 'a');
            put(bad, text.mHeader + 4, static_cast<uint32_t>(text.mSize + 4097));
            put(bad, text.mRecord + 4, number(bytes, text.mRecord + 4) + 4097);
            reject(bad, bindings, true);
            for (int mode = 0; mode < 6; ++mode)
            {
                auto envelope = bindings.mEnvelope;
                if (mode == 0)
                    envelope.mRuntime += "-foreign";
                if (mode == 1)
                    ++envelope.mContent[0];
                if (mode == 2)
                    ++envelope.mSourceOwner.mIndex;
                if (mode == 3)
                    ++envelope.mDestinationOwner.mIndex;
                if (mode == 4)
                    ++envelope.mInitiator.mIndex;
                if (mode == 5)
                    std::swap(envelope.mSourceOwner, envelope.mDestinationOwner);
                reject(bytes, { envelope, bindings.mContent, bindings.mReferenceIds }, true);
            }
            auto missing = bindings.mContent;
            missing.mBases = missing.mBases.first(1);
            reject(bytes, { bindings.mEnvelope, missing, bindings.mReferenceIds });
            Compiler::Locals noDeclarations;
            RestoreContent noLocals{ bindings.mContent.mBases, bindings.mContent.mScript, noDeclarations };
            reject(bytes, { bindings.mEnvelope, noLocals, bindings.mReferenceIds });
            reject(bytes, { bindings.mEnvelope, bindings.mContent, bindings.mReferenceIds.first(1) }, true);

            const auto invalidRestarts = invalidRestartValues(input.mRestart);
            for (size_t mode = 0; mode < 21 + invalidRestarts.size(); ++mode)
            {
                auto invalid = input;
                auto envelope = bindings.mEnvelope;
                const SaveBindings supplied{ envelope, bindings.mContent, bindings.mReferenceIds };
                auto& inventory = invalid.mSource;
                auto& object = inventory.mObjects.front();
                if (mode == 0)
                    inventory.mProposedIdentities.pop_back();
                if (mode == 1)
                    inventory.mProposedIdentities[0] = invalid.mDestination.mProposedIdentities[0];
                if (mode == 2)
                    inventory.mProposedIdentities[0] = {};
                if (mode == 3)
                    inventory.mProposedIdentities[0] = bindings.mEnvelope.mSourceOwner;
                if (mode == 4)
                    object.mRef.mRefNum = { 99, -1 };
                if (mode == 5)
                    object.mRef.mRefID = bindings.mContent.mScript.mId;
                if (mode == 6)
                    object.mHasLocals = 2;
                if (mode == 7)
                    object.mVersion = 37;
                if (mode == 8)
                    object.mHasCustomState = true;
                if (mode == 9)
                    object.mLuaScripts.mScripts.push_back({ 1, "unsupported", {} });
                if (mode == 10)
                    object.mActorIdConverter = reinterpret_cast<ESM::ActorIdConverter*>(uintptr_t{ 1 });
                if (mode == 11)
                    object.mRef.mGlobalVariable.assign(4097, 'x');
                if (mode == 12)
                    object.mRef.mCount = INT32_MIN;
                if (mode == 13)
                    object.mRef.mScale = std::numeric_limits<float>::infinity();
                if (mode == 14)
                    object.mAnimationState.mScriptedAnims.resize(257);
                if (mode == 15)
                    inventory.mObjects.resize(1025);
                if (mode == 16)
                    object.mLocals.mVariables.front().first = "missing_local_binding";
                if (mode == 17)
                {
                    // Valid per-field sizes, but beyond the encoded object cap.
                    object.mAnimationState.mScriptedAnims.resize(256);
                    for (auto& animation : object.mAnimationState.mScriptedAnims)
                        animation.mGroup.assign(4096, 'a');
                }
                if (mode >= 18 && mode < 18 + invalidRestarts.size())
                    invalid.mRestart = invalidRestarts[mode - 18];
                if (mode >= 18 + invalidRestarts.size())
                {
                    const auto owner = mode - 18 - invalidRestarts.size();
                    auto& id = owner == 0 ? envelope.mSourceOwner
                        : owner == 1     ? envelope.mDestinationOwner
                                         : envelope.mInitiator;
                    id = { input.mRestart.mLastGenerated.mIndex + 1, -1 };
                }
                const auto retained = pairOutputState(invalid);
                TransferSaveBytes output(57, 'x');
                const auto before = byteState(output);
                bool caught = false;
                Allocations::Trace trace;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        encodeTransferSave(invalid, supplied, output);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                }
                require(caught && byteState(output) == before && pairOutputState(invalid) == retained
                        && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                    "invalid encoder input accepted, leaked or changed input/output");
                for (size_t ordinal = 1; ordinal <= trace.mTotal; ++ordinal)
                {
                    Allocations::Trace failure;
                    caught = false;
                    {
                        Allocations::Observe observe(failure, ordinal);
                        try
                        {
                            encodeTransferSave(invalid, supplied, output);
                        }
                        catch (const std::exception&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && failure.mFailures == 1 && failure.mOutstanding == 0
                            && failure.mTrackingOverflow == 0 && byteState(output) == before
                            && pairOutputState(invalid) == retained,
                        "invalid encoding allocation failure changed output or leaked");
                    TransferSaveBytes retry;
                    encodeTransferSave(input, bindings, retry);
                    require(retry == bytes, "encoding allocation retry failed");
                    verify();
                }
                allocations += trace.mTotal;
                ++rejections;
                encodeTransferSave(input, bindings, output);
                require(output == bytes, "encoding retry failed");
                verify();
            }
            return allocations;
        }

        struct FileEvidence
        {
            size_t mRejected = 0, mUncertain = 0, mMalformed = 0, mAllocations = 0;
        };

        TransferSaveBytes fileBytes(const std::filesystem::path& path)
        {
            TransferSaveBytes result;
            FileFaults faults;
            require(readTransferFile(path, result, faults) == FileReadResult::Read, "file reopen failed");
            return result;
        }

        void loadFile(
            const std::filesystem::path& path, const SaveBindings& bindings, SerializedPair& output, FileFaults& faults)
        {
            TransferSaveBytes bytes;
            if (readTransferFile(path, bytes, faults) != FileReadResult::Read)
                throw Failure{};
            decodeTransferSave(bytes, bindings, output);
        }

        void saveFixture(const DisposableTransferRehearsal& fixture, const Compiler::Locals& declarations,
            SerializedPair& output)
        {
            output.mRestart = { fixture.mModel.getPtrRegistryRevision(), fixture.mModel.getLastGeneratedRefNum() };
            const auto save = [&](const auto& storage, SerializedInventory& inventory) {
                auto it = storage.begin();
                serializeInventory(storage, [&](size_t) { return (it++)->mRef.getRefNum(); }, declarations, inventory);
                for (auto& object : inventory.mObjects)
                    object.mRef.mRefNum = {};
            };
            save(fixture.sourceStorage(), output.mSource);
            save(fixture.destinationStorage(), output.mDestination);
            output.mScripts = {};
            output.mScripts.mShared = fixture.mDestinationAdd.mLocalScripts == &fixture.mSourceScripts;
            for (const auto& node : fixture.otherStorage())
                output.mScripts.mOther.push_back({ node.mRef.getRefNum(), node.mRef.getRefId(),
                    !node.mData.getLocals().getScriptId().empty() });
            for (size_t side = 0; side < (output.mScripts.mShared ? 1u : 2u); ++side)
            {
                const auto list = (side == 0 ? fixture.mSourceScripts : fixture.mDestinationScripts).snapshot();
                auto& service = output.mScripts.mServices[side];
                service.mCursor = list.mCursor;
                for (const auto& entry : list.mEntries)
                    for (const auto* storage : { &fixture.sourceStorage(), &fixture.destinationStorage(),
                             &fixture.otherStorage() })
                        for (const auto& node : *storage)
                            if (entry.references(&node.mRef))
                                service.mEntries.push_back({ node.mRef.getRefNum(), entry.getScript() });
                require(service.mEntries.size() == list.mEntries.size(), "fixture save omitted script bindings");
            }
        }

        struct CommandEvidence
        {
            size_t mRejected = 0, mUncertain = 0, mAllocations = 0, mResultFailures = 0, mRepeated = 0;
        };

        template <class Make, class Verify, class Unrelated>
        size_t checkCommandCase(std::unique_ptr<DisposableTransferRehearsal>& owner, Make make,
            Verify verifyOriginal, Unrelated verifyUnrelated, const RestoreContent& content,
            const std::filesystem::path& scratch, const Ptr& item, int quantity, size_t failAt,
            size_t allocationCount, FileFault fault, CommandEvidence& evidence)
        {
            using namespace Allocations;
            auto& fixture = *owner;
            const auto path = scratch / "inventory.bin";
            const auto temporary = scratch / "inventory.bin.tmp";
            const auto ownedId = [](ESM::RefNum id) { return InventoryInstanceId{ id.mIndex, id.mContentFile }; };
            const auto engineId = [](InventoryInstanceId id) { return ESM::RefNum{ id.mIndex, id.mContentFile }; };
            SaveEnvelope envelope{ "OpenMW-0.51.0-test-inventory-runtime-1", { 1, 7, 19 },
                fixture.mSourceOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationAdd.mPlayer.getCellRef().getRefNum() };
            const std::array referenceIds{ content.mBases[0]->mId, content.mBases[1]->mId,
                ESM::RefId::stringRefId("serialization_owner"), ESM::RefId::stringRefId("serialization_soul"),
                ESM::RefId::stringRefId("serialization_faction"), ESM::RefId::stringRefId("serialization_key"),
                ESM::RefId::stringRefId("serialization_trap"), ESM::RefId::stringRefId("dormant_soul") };
            const SaveBindings bindings{ envelope, content, referenceIds };
            InventoryTransferCommand command{ ownedId(envelope.mSourceOwner), ownedId(envelope.mDestinationOwner),
                ownedId(envelope.mInitiator), ownedId(item.getCellRef().getRefNum()), quantity,
                fixture.mModel.getPtrRegistryRevision() };
            auto output = std::make_unique<const InventoryTransferSuccess>(InventoryTransferSuccess{});
            const auto* originalOutput = output.get();
            const auto originalValue = *output;
            const auto contentBefore = contentState(content);
            const auto unrelated = nodeState(*fixture.mOther.getSelectedEnchantItem());
            const int notificationsBefore = fixture.mNotifications;
            SerializedPair prior, expectedSave;
            saveFixture(fixture, content.mDeclarations, prior);
            InventoryTransferSuccess expectedResult;
            {
                auto pair = make();
                serializePair(fixture, pair, content.mDeclarations, expectedSave);
                expectedResult = { command, ownedId(pair.getDestinationIdentity()),
                    pair.getSourceItem().getCellRef().getCount(false),
                    pair.getDestinationItem().getCellRef().getCount(false), pair.getRelocation().mRegistry.mRevision };
            }
            TransferSaveBytes priorBytes, expectedBytes;
            encodeTransferSave(prior, bindings, priorBytes);
            encodeTransferSave(expectedSave, bindings, expectedBytes);
            TransferFileSink file(path);
            FileFaults faults;
            require(file.write(priorBytes, faults) == TestPersistenceResult::Accepted, "command prior file setup failed");
            const auto verifyOutput = [&] {
                require(output.get() == originalOutput && *output == originalValue,
                    "command rejection changed caller result value/storage");
            };
            const auto verify = [&] {
                verifyOutput();
                verifyOriginal();
                require(contentState(content) == contentBefore && fileBytes(path) == priorBytes
                        && !std::filesystem::exists(temporary),
                    "command safe rejection changed content/file or leaked staging");
            };
            const auto execute = [&] { return executeInventoryTransfer(fixture, command, bindings, file, faults, output); };
            if (!failAt && fault == FileFault::None)
            {
                const auto reject = [&](InventoryTransferCommand bad, const SaveBindings& supplied) {
                    const auto before = snapshot(fixture);
                    faults = {};
                    bool rejected = false;
                    try
                    {
                        executeInventoryTransfer(fixture, bad, supplied, file, faults, output);
                    }
                    catch (const std::invalid_argument&)
                    {
                        rejected = true;
                    }
                    require(rejected && faults.mReached == FileFault::None && faults.mWrites == 0
                            && faults.mReads == 0 && snapshot(fixture) == before && !fixture.failedClosed()
                            && !file.failedClosed() && fileBytes(path) == priorBytes,
                        "invalid command/binding reached I/O, accepted or changed fixture");
                    verifyOutput();
                    ++evidence.mRejected;
                };
                for (auto member : { &InventoryTransferCommand::mSourceOwner, &InventoryTransferCommand::mDestinationOwner,
                         &InventoryTransferCommand::mInitiator, &InventoryTransferCommand::mItem })
                    for (InventoryInstanceId badId : { InventoryInstanceId{}, InventoryInstanceId{ 1, -2 },
                             InventoryInstanceId{ 999999, -1 }, command.mItem })
                    {
                        auto bad = command;
                        bad.*member = badId;
                        if (bad != command)
                            reject(bad, bindings);
                    }
                for (int count : { 0, -1, std::numeric_limits<int>::min(), 5, std::numeric_limits<int>::max() })
                {
                    auto bad = command;
                    bad.mQuantity = count;
                    reject(bad, bindings);
                }
                for (auto revision : { command.mExpectedRevision - 1, std::numeric_limits<uint64_t>::max() })
                {
                    auto bad = command;
                    bad.mExpectedRevision = revision;
                    reject(bad, bindings);
                }
                auto bad = command;
                bad.mDestinationOwner = bad.mSourceOwner;
                reject(bad, bindings);
                bad = command;
                std::swap(bad.mSourceOwner, bad.mDestinationOwner);
                reject(bad, bindings);
                for (const auto* node : { &fixture.otherStorage().front(), &fixture.sourceStorage().back() })
                {
                    bad = command;
                    bad.mItem = ownedId(node->mRef.getRefNum());
                    reject(bad, bindings); // Foreign ownership and dormant zero count.
                }
                for (auto member : { &SaveEnvelope::mSourceOwner, &SaveEnvelope::mDestinationOwner,
                         &SaveEnvelope::mInitiator })
                {
                    auto wrong = envelope;
                    wrong.*member = { 999999, -1 };
                    reject(command, { wrong, content, referenceIds });
                }
                auto wrong = envelope;
                wrong.mRuntime.clear();
                reject(command, { wrong, content, referenceIds });
                wrong = envelope;
                wrong.mContent.fill(0);
                reject(command, { wrong, content, referenceIds });
                reject(command, { envelope, content, {} });
                reject(command, { envelope, { {}, content.mScript, content.mDeclarations }, referenceIds });
                const auto originalOwner = fixture.mRemoval.mContainer;
                fixture.mRemoval.mContainer = fixture.mOtherOwner.getPtr();
                reject(command, bindings);
                fixture.mRemoval.mContainer = originalOwner;
                const auto originalPlayer = fixture.mDestinationAdd.mPlayer;
                fixture.mDestinationAdd.mPlayer = fixture.mOtherOwner.getPtr();
                reject(command, bindings);
                fixture.mDestinationAdd.mPlayer = originalPlayer;
                const auto originalService = fixture.mDestinationAdd.mLocalScripts;
                fixture.mDestinationAdd.mLocalScripts = nullptr;
                reject(command, bindings);
                fixture.mDestinationAdd.mLocalScripts = originalService;
                Ptr expired;
                {
                    ManualRef actor(fixture.mDestinationAdd.mStore, fixture.mSourceOwner.getPtr().getCellRef().getRefId());
                    expired = actor.getPtr();
                }
                require(!expired.hasLiveReference(), "command expired context retained its lifetime");
                fixture.mDestinationAdd.mPlayer = expired;
                reject(command, bindings);
                fixture.mDestinationAdd.mPlayer = originalPlayer;
                // Extra registry-only and script-only nodes cannot be silently
                // omitted from the protected resolution/install set.
                {
                    ManualRef unresolved(fixture.mDestinationAdd.mStore, content.mBases[0]->mId);
                    fixture.mModel.registerPtr(unresolved.getPtr());
                    bad = command;
                    bad.mExpectedRevision = fixture.mModel.getPtrRegistryRevision();
                    reject(bad, bindings);
                    fixture.mModel.deregisterLiveCellRef(*unresolved.getPtr().mRef);
                }
                command.mExpectedRevision = fixture.mModel.getPtrRegistryRevision();
                {
                    ManualRef unresolved(fixture.mDestinationAdd.mStore, content.mBases[1]->mId);
                    fixture.mSourceScripts.add(
                        content.mScript.mId, unresolved.getPtr(), *fixture.mDestinationAdd.mScriptManager);
                    reject(command, bindings);
                    fixture.mSourceScripts.remove(unresolved.getPtr());
                }
                // Re-registration at the same address invalidates the intent.
                fixture.mModel.registerPtr(fixture.mSourceOwner.getPtr());
                reject(command, bindings); // Same address, new registry revision.
                command.mExpectedRevision = fixture.mModel.getPtrRegistryRevision();
                // The deliberate registry edits advanced its revision/counter.
                // Recompute the expected pair from the preserved inventory.
                {
                    auto pair = make();
                    expectedResult = { command, ownedId(pair.getDestinationIdentity()),
                        pair.getSourceItem().getCellRef().getCount(false),
                        pair.getDestinationItem().getCellRef().getCount(false), pair.getRelocation().mRegistry.mRevision };
                    serializePair(fixture, pair, content.mDeclarations, expectedSave);
                    encodeTransferSave(expectedSave, bindings, expectedBytes);
                }
                for (auto failure : { FileFault::Create, FileFault::Write, FileFault::Flush, FileFault::Close,
                         FileFault::Replace })
                {
                    faults = { failure, 17 };
                    const auto before = snapshot(fixture);
                    require(!execute() && snapshot(fixture) == before && !fixture.failedClosed() && !file.failedClosed(),
                        "safe command file failure accepted or changed fixture");
                    verifyOutput();
                    require(fileBytes(path) == priorBytes && !std::filesystem::exists(temporary),
                        "safe command file failure changed file or leaked staging");
                    ++evidence.mRejected;
                }
                {
                    std::ofstream stream(temporary, std::ios::binary);
                    stream.write(priorBytes.data(), static_cast<std::streamsize>(priorBytes.size()));
                }
                const auto beforeForeignStaging = snapshot(fixture);
                faults = {};
                require(!execute() && snapshot(fixture) == beforeForeignStaging && !fixture.failedClosed()
                        && !file.failedClosed() && fileBytes(temporary) == priorBytes && fileBytes(path) == priorBytes,
                    "command overwrote foreign staging");
                verifyOutput();
                std::filesystem::remove(temporary);
                ++evidence.mRejected;
            }

            faults = { fault, 17 };
            Trace measured;
            bool committed = false, allocationFailed = false, uncertain = false;
            {
                Observe observe(measured, failAt);
                try
                {
                    committed = execute();
                }
                catch (const std::bad_alloc&)
                {
                    allocationFailed = true;
                }
                catch (const TestDurabilityUncertain&)
                {
                    uncertain = true;
                }
                if (committed && failAt)
                {
                    output.reset();
                    owner.reset();
                }
            }
            require(measured.mTrackingOverflow == 0 && measured.allocations(Phase::Installation) == 0
                    && measured.allocations(Phase::Retirement) == 0 && measured.allocations(Phase::Publication) == 0,
                "command allocated after acceptance or overflowed tracking");
            if (fault != FileFault::None)
            {
                require(uncertain && !committed && !allocationFailed && fixture.failedClosed() && file.failedClosed()
                        && measured.mOutstanding == 0 && measured.visits(Phase::Installation) == 0
                        && measured.visits(Phase::Publication) == 0,
                    "uncertain command installed or published success");
                verifyOriginal();
                verifyOutput();
                const auto coherent = fileBytes(path);
                require(coherent == (fault == FileFault::ReplaceError ? priorBytes : expectedBytes),
                    "uncertain command left incoherent file");
                TransferFileSink freshSink(path);
                for (auto* nextSink : { &file, &freshSink })
                {
                    faults = {};
                    Trace closed;
                    bool blocked = false;
                    {
                        Observe observe(closed, 1);
                        try
                        {
                            executeInventoryTransfer(fixture, command, bindings, *nextSink, faults, output);
                        }
                        catch (const TestDurabilityUncertain&)
                        {
                            blocked = true;
                        }
                    }
                    require(blocked && closed.mTotal == 0 && faults.mReached == FileFault::None,
                        "uncertain command retried with old/new sink");
                    verifyOriginal();
                    verifyOutput();
                }
                ++evidence.mUncertain;
                owner.reset();
            }
            else if (failAt && failAt <= allocationCount)
            {
                require(allocationFailed && !committed && !uncertain && measured.mFailures == 1
                        && measured.mTotal == failAt && measured.mOutstanding == 0
                        && measured.visits(Phase::Installation) == 0 && measured.visits(Phase::Publication) == 0
                        && faults.mReached == FileFault::None,
                    "command missed allocation failure or reached acceptance");
                evidence.mResultFailures += measured.mFailedPhase == Phase::Result;
                verify();
                Trace retry;
                faults = { FileFault::None, 17 };
                {
                    Observe observe(retry, allocationCount + 1);
                    committed = execute();
                    output.reset();
                    owner.reset();
                }
                require(committed && retry.mFailures == 0 && retry.mOutstanding == 0 && retry.mTrackingOverflow == 0
                        && retry.visits(Phase::Publication) == 1 && retry.allocations(Phase::Publication) == 0,
                    "command allocation retry/publication/cleanup failed");
                require(fileBytes(path) == expectedBytes && !std::filesystem::exists(temporary),
                    "command retry persisted wrong bytes or leaked staging");
                verifyUnrelated();
                return measured.mTotal;
            }
            else
            {
                require(committed && !uncertain && !allocationFailed && measured.mFailures == 0
                        && measured.allocations(Phase::Result) == 1 && measured.visits(Phase::Installation) == 1
                        && measured.visits(Phase::Retirement) == 1 && measured.visits(Phase::Publication) == 1
                        && faults.mWrites > 1 && faults.mReads > 1,
                    "command missed result preparation, installation, publication or short I/O");
                if (failAt)
                {
                    require(measured.mTotal == allocationCount && measured.mOutstanding == 0,
                        "command successful cleanup leaked or allocated beyond measured boundary");
                    verifyUnrelated();
                    return measured.mTotal;
                }
                require(output.get() != originalOutput && *output == expectedResult
                        && output->mRevision == fixture.mModel.getPtrRegistryRevision()
                        && output->mRevision > command.mExpectedRevision,
                    "command published wrong owned result/revision");
                const auto destination = fixture.mModel.getPtr(engineId(output->mDestinationItem));
                require(destination.hasLiveReference() && destination.getContainerStore() == &fixture.mDestination
                        && destination.getCellRef().getCount(false) == output->mDestinationCount
                        && nodeState(*fixture.mOther.getSelectedEnchantItem()) == unrelated
                        && fixture.mNotifications == notificationsBefore,
                    "command result missed installed destination, changed other owner or emitted notifications");
                SerializedPair installed;
                saveFixture(fixture, content.mDeclarations, installed);
                checkSavedValues(installed, expectedSave);
                const auto installedBefore = snapshot(fixture);
                const auto* success = output.get();
                // Both the identical retry and another intent from the old
                // revision reject before I/O. This is not durable deduplication.
                for (int count : { quantity, quantity == 1 ? 4 : 1 })
                {
                    auto repeated = command;
                    repeated.mQuantity = count;
                    bool rejected = false;
                    faults = {};
                    try
                    {
                        executeInventoryTransfer(fixture, repeated, bindings, file, faults, output);
                    }
                    catch (const std::invalid_argument&)
                    {
                        rejected = true;
                    }
                    require(rejected && output.get() == success && *output == expectedResult
                            && snapshot(fixture) == installedBefore && faults.mReached == FileFault::None,
                        "repeated/stale command changed installed state/result or wrote again");
                    ++evidence.mRepeated;
                }
                owner.reset();
                require(*output == expectedResult, "success result borrowed disposed fixture state");
            }
            verifyUnrelated();
            require(contentState(content) == contentBefore && !std::filesystem::exists(temporary),
                "command changed content or leaked staging");
            SerializedPair decoded;
            FileFaults fresh;
            loadFile(path, bindings, decoded, fresh);
            const auto& coherentSave = fault == FileFault::ReplaceError ? prior : expectedSave;
            checkSavedValues(decoded, coherentSave);
            std::unique_ptr<const RestoredPair> restored;
            restorePair(decoded, content, restored);
            SerializedPair savedAgain;
            serializePair(*restored, content.mDeclarations, savedAgain);
            checkSavedValues(savedAgain, coherentSave);
            TransferSaveBytes again;
            encodeTransferSave(savedAgain, bindings, again);
            require(again == fileBytes(path) && again == (fault == FileFault::ReplaceError ? priorBytes : expectedBytes),
                "command/reopen/decode/detached restore/save changed persisted bytes");
            return measured.mTotal;
        }

        void checkFileReads(const std::filesystem::path& path, const TransferSaveBytes& bytes,
            const SerializedPair& saved, const SaveBindings& bindings, FileEvidence& evidence)
        {
            // A separate malformed-input path cannot damage the accepted save.
            auto badPath = path;
            badPath += ".input";
            const auto writeInput = [&](std::span<const char> input) {
                std::ofstream stream(badPath, std::ios::binary | std::ios::trunc);
                stream.write(input.data(), static_cast<std::streamsize>(input.size()));
                stream.close();
                require(!stream.fail(), "malformed file setup failed");
            };
            const auto verify = [&] { require(fileBytes(path) == bytes, "file read changed accepted bytes"); };
            {
                Allocations::Trace measured;
                {
                    Allocations::Observe observe(measured);
                    TransferFileSink fresh(path);
                }
                require(measured.mTotal > 0 && measured.mOutstanding == 0 && measured.mTrackingOverflow == 0,
                    "file sink constructor allocation coverage/cleanup missing");
                for (size_t ordinal = 1; ordinal <= measured.mTotal; ++ordinal)
                {
                    Allocations::Trace trace;
                    bool caught = false;
                    {
                        Allocations::Observe observe(trace, ordinal);
                        try
                        {
                            TransferFileSink fresh(path);
                        }
                        catch (const std::bad_alloc&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && trace.mFailures == 1 && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0,
                        "file sink constructor missed allocation failure or leaked");
                    TransferFileSink retry(path);
                    verify();
                    ++evidence.mAllocations;
                }
            }
            evidence.mAllocations += checkSerializationAllocations([] { return TransferSaveBytes(57, 'x'); },
                [&](auto& output) {
                    FileFaults faults;
                    require(readTransferFile(path, output, faults) == FileReadResult::Read, "bounded read failed");
                },
                byteState, [&](const auto& output) { require(output == bytes, "read changed bytes"); }, verify);
            evidence.mAllocations += checkSerializationAllocations([] { return pairOutputSentinel<true>(); },
                [&](auto& output) {
                    FileFaults faults;
                    loadFile(path, bindings, output, faults);
                },
                [](const auto& output) { return pairOutputState(output); },
                [&](const auto& output) { checkSavedValues(output, saved); }, verify);

            const auto reject = [&](const SaveBindings& supplied, FileFault fault = FileFault::None) {
                auto output = pairOutputSentinel<true>();
                const auto before = pairOutputState(output);
                Allocations::Trace measured;
                bool caught = false;
                {
                    Allocations::Observe observe(measured);
                    try
                    {
                        FileFaults faults{ fault, 17 };
                        loadFile(badPath, supplied, output, faults);
                    }
                    catch (...)
                    {
                        caught = true;
                    }
                }
                require(caught && pairOutputState(output) == before && measured.mOutstanding == 0
                        && measured.mTrackingOverflow == 0,
                    "invalid file changed prior decode output/storage or leaked");
                // Fault every observed allocation, including the rejection path.
                for (size_t ordinal = 1; ordinal <= measured.mTotal; ++ordinal)
                {
                    Allocations::Trace trace;
                    bool rejected = false;
                    {
                        Allocations::Observe observe(trace, ordinal);
                        try
                        {
                            FileFaults faults{ fault, 17 };
                            loadFile(badPath, supplied, output, faults);
                        }
                        catch (...)
                        {
                            rejected = true;
                        }
                    }
                    require(rejected && trace.mFailures == 1 && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                            && pairOutputState(output) == before,
                        "invalid-file allocation failure published output or leaked");
                    ++evidence.mAllocations;
                }
                FileFaults healthy;
                loadFile(path, bindings, output, healthy);
                checkSavedValues(output, saved);
                verify();
                ++evidence.mMalformed;
            };
            for (const size_t length :
                { size_t{ 0 }, size_t{ 1 }, size_t{ 15 }, size_t{ 16 }, bytes.size() / 2, bytes.size() - 1 })
            {
                writeInput(std::span(bytes).first(length));
                reject(bindings);
            }
            for (const char* tag : { "TES3", "FORM", "FVER", "RUNT", "CONT", "SOWN", "DOWN", "INIT" })
            {
                auto malformed = bytes;
                const auto found = std::search(malformed.begin(), malformed.end(), tag, tag + 4);
                require(found != malformed.end(), "file corruption field missing");
                // Corrupt the record name for TES3, the first payload byte otherwise.
                *(found + (std::string_view(tag) == "TES3" ? 0 : 8)) ^= 0x40;
                writeInput(malformed);
                reject(bindings);
            }
            for (const char* tag : { "FVER", "RREV", "LGEN" })
            {
                auto malformed = bytes;
                const auto found = std::search(malformed.begin(), malformed.end(), tag, tag + 4);
                require(found != malformed.end(), "file restart metadata field missing");
                const bool version = std::string_view(tag) == "FVER";
                std::fill_n(found + 8, version ? 4 : 8, 0);
                if (version)
                    *(found + 8) = 1;
                writeInput(malformed);
                reject(bindings);
            }
            auto trailing = bytes;
            trailing.push_back('x');
            writeInput(trailing);
            reject(bindings);
            writeInput(bytes);
            for (int mismatch = 0; mismatch < 5; ++mismatch)
            {
                auto envelope = bindings.mEnvelope;
                if (mismatch == 0)
                    envelope.mRuntime += "-foreign";
                else if (mismatch == 1)
                    ++envelope.mContent[0];
                else if (mismatch == 2)
                    ++envelope.mSourceOwner.mIndex;
                else if (mismatch == 3)
                    ++envelope.mDestinationOwner.mIndex;
                else
                    ++envelope.mInitiator.mIndex;
                reject({ envelope, bindings.mContent, bindings.mReferenceIds });
            }
            for (const auto fault :
                { FileFault::ReadOpen, FileFault::ReadSize, FileFault::Read, FileFault::ReadEof, FileFault::ReadClose })
            {
                TransferSaveBytes output(57, 'x');
                const auto before = byteState(output);
                FileFaults faults{ fault, 17 };
                require(readTransferFile(badPath, output, faults) == FileReadResult::Unavailable
                        && byteState(output) == before,
                    "failed read changed prior byte output/storage");
                reject(bindings, fault);
            }
            std::filesystem::resize_file(badPath, MaxTransferSaveBytes + 1);
            TransferSaveBytes sentinel(57, 'x');
            const auto before = byteState(sentinel);
            Allocations::Trace bounded;
            FileReadResult result;
            {
                Allocations::Observe observe(bounded, 1);
                FileFaults faults;
                result = readTransferFile(badPath, sentinel, faults);
            }
            require(result == FileReadResult::TooLarge && bounded.mTotal == 0 && byteState(sentinel) == before,
                "oversized file allocated before bound or changed prior output");
            reject(bindings);
            std::filesystem::remove(badPath);
            reject(bindings); // Absent file is not an empty successful restore.
            std::filesystem::create_directory(badPath);
            reject(bindings);
            std::filesystem::remove(badPath);
            verify();
        }

        template <class Make, class Verify, class Unrelated>
        size_t checkFileCommitCase(std::unique_ptr<DisposableTransferRehearsal>& owner, Make make,
            Verify verifyOriginal, Unrelated verifyUnrelated, const RestoreContent& content,
            const std::filesystem::path& scratch, size_t failAt, size_t allocationCount, FileFault fault,
            bool exhaustive, FileEvidence& evidence)
        {
            using namespace Allocations;
            auto& fixture = *owner;
            const auto path = scratch / "inventory.bin";
            const auto temporary = scratch / "inventory.bin.tmp";
            SaveEnvelope envelope{ "OpenMW-0.51.0-test-inventory-runtime-1", { 1, 7, 19 },
                fixture.mSourceOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationAdd.mPlayer.getCellRef().getRefNum() };
            const std::array referenceIds{ content.mBases[0]->mId, content.mBases[1]->mId,
                ESM::RefId::stringRefId("serialization_owner"), ESM::RefId::stringRefId("serialization_soul"),
                ESM::RefId::stringRefId("serialization_faction"), ESM::RefId::stringRefId("serialization_key"),
                ESM::RefId::stringRefId("serialization_trap"), ESM::RefId::stringRefId("dormant_soul") };
            const SaveBindings bindings{ envelope, content, referenceIds };
            SerializedPair prior, expectedSave;
            const auto saveInstalled = [&](SerializedPair& output) {
                saveFixture(fixture, content.mDeclarations, output);
            };
            saveInstalled(prior);
            auto pair = make();
            serializePair(fixture, pair, content.mDeclarations, expectedSave);
            const auto expected = expectedRestoration(pair);
            const auto expectedRegistry = pair.getRelocation().mRegistry;
            const auto expectedScripts = pair.getRelocation().mSourceScripts;
            const auto expectedDestinationScripts = pair.getRelocation().mDestinationScripts.value_or(expectedScripts);
            TransferSaveBytes priorBytes, expectedBytes;
            encodeTransferSave(prior, bindings, priorBytes);
            encodeTransferSave(expectedSave, bindings, expectedBytes);
            require(priorBytes != expectedBytes, "file test lost distinct prior/new states");
            TransferFileSink file(path);
            FileFaults seed;
            const auto seeded = file.write(priorBytes, seed);
            if (seeded != TestPersistenceResult::Accepted)
                std::cerr << "Prior file setup: stage=" << static_cast<int>(seed.mReached)
                          << " replace-error=" << seed.mReplaceError << '\n';
            require(seeded == TestPersistenceResult::Accepted, "prior file setup failed");
            const auto inputBefore = pairOutputState(expectedSave);
            const auto contentBefore = contentState(content);
            const auto verify = [&] {
                require(pairOutputState(expectedSave) == inputBefore && contentState(content) == contentBefore,
                    "file sink changed supplied save/content storage");
                verifyOriginal();
                require(fileBytes(path) == priorBytes && !std::filesystem::exists(temporary),
                    "safe file rejection changed prior bytes or leaked staging");
            };
            FileFaults faults{ fault, 17 };
            int accepted = 0, calls = 0;
            const DisposableTransferRehearsal::TestDurableSink sink = [&](const SerializedPair& saved) {
                ++calls;
                TransferSaveBytes bytes;
                encodeTransferSave(saved, bindings, bytes);
                const auto result = file.write(bytes, faults);
                accepted += result == TestPersistenceResult::Accepted;
                return result;
            };
            if (!failAt && fault == FileFault::None)
            {
                TransferSaveBytes oversized(MaxTransferSaveBytes + 1, 'x');
                Trace bounds;
                TestPersistenceResult emptyResult, oversizedResult;
                {
                    Observe observe(bounds, 1);
                    emptyResult = file.write({}, faults);
                    oversizedResult = file.write(oversized, faults);
                }
                require(emptyResult == TestPersistenceResult::Rejected
                        && oversizedResult == TestPersistenceResult::Rejected && bounds.mTotal == 0
                        && faults.mWrites == 0 && faults.mReads == 0,
                    "file sink touched I/O or allocated before byte bounds");
                verify();
                evidence.mRejected += 2;
                for (auto failure :
                    { FileFault::Create, FileFault::Write, FileFault::Flush, FileFault::Close, FileFault::Replace })
                {
                    faults = { failure, 17 };
                    require(!fixture.commitDurably(make(), content.mDeclarations, sink) && accepted == 0
                            && !fixture.failedClosed() && !file.failedClosed(),
                        "safe file rejection accepted or poisoned fixture");
                    require(failure != FileFault::Write || faults.mWrites == 1, "partial write seam missed");
                    verify();
                    ++evidence.mRejected;
                }
                // A foreign/stale staging file must never be truncated/deleted.
                {
                    std::ofstream stream(temporary, std::ios::binary);
                    stream.write(priorBytes.data(), static_cast<std::streamsize>(priorBytes.size()));
                }
                faults = {};
                require(!fixture.commitDurably(make(), content.mDeclarations, sink)
                        && fileBytes(temporary) == priorBytes && fileBytes(path) == priorBytes,
                    "exclusive staging overwrote another file");
                std::filesystem::remove(temporary);
                verify();
                ++evidence.mRejected;
                // Invalid preparation cannot reach encode/write, preserving all guards.
                auto invalid = make();
                ++const_cast<PreparedContainerTransfer::IteratorBindings&>(invalid.getIteratorBindings()).mCount;
                const int priorCalls = calls;
                bool rejected = false;
                try
                {
                    fixture.commitDurably(std::move(invalid), content.mDeclarations, sink);
                }
                catch (const std::invalid_argument&)
                {
                    rejected = true;
                }
                require(rejected && calls == priorCalls, "protected iterator mismatch reached file sink");
                verify();
            }
            // Include preparation, encoding, accepted installation and retirement.
            // The earlier pair remains a stale witness, never followed on success.
            calls = accepted = 0;
            faults = { fault, 17 };
            Trace measured;
            bool committed = false, allocationFailed = false, uncertain = false;
            {
                Observe observe(measured, failAt);
                InPhase phase(Phase::Preparation);
                try
                {
                    committed = fixture.commitDurably(make(), content.mDeclarations, sink);
                }
                catch (const std::bad_alloc&)
                {
                    allocationFailed = true;
                }
                catch (const TestDurabilityUncertain&)
                {
                    uncertain = true;
                }
                if (committed && failAt)
                    owner.reset();
            }
            require(measured.mTrackingOverflow == 0 && measured.allocations(Phase::Installation) == 0
                    && measured.allocations(Phase::Retirement) == 0,
                "file commit tracking overflow or post-acceptance allocation");
            if (fault != FileFault::None)
            {
                require(uncertain && !committed && !allocationFailed && accepted == 0 && calls == 1
                        && fixture.failedClosed() && file.failedClosed() && measured.mOutstanding == 0
                        && measured.visits(Phase::Installation) == 0,
                    "post-replacement uncertainty did not fail closed");
                verifyOriginal();
                require(fileBytes(path) == (fault == FileFault::ReplaceError ? priorBytes : expectedBytes)
                        && !std::filesystem::exists(temporary),
                    "uncertain replacement left incoherent bytes/staging");
                faults = {};
                require(file.write(priorBytes, faults) == TestPersistenceResult::Uncertain && faults.mWrites == 0
                        && faults.mReads == 0,
                    "uncertain sink permitted subsequent writes");
                for (int operation = 0; operation < 3; ++operation)
                {
                    bool blocked = false;
                    try
                    {
                        if (operation == 0)
                            fixture.commitDurably(make(), content.mDeclarations, sink);
                        else if (operation == 1)
                            fixture.commit(make(), content.mDeclarations, [](const SerializedPair&) { return true; });
                        else
                            fixture.rehearse(make());
                    }
                    catch (const TestDurabilityUncertain&)
                    {
                        blocked = true;
                    }
                    require(blocked && calls == 1 && accepted == 0, "uncertain fixture allowed mutation/retry");
                    verifyOriginal();
                }
                ++evidence.mUncertain;
                owner.reset(); // Recovery is fresh detached decode only.
            }
            else if (failAt && failAt <= allocationCount)
            {
                require(allocationFailed && !committed && !uncertain && accepted == 0 && measured.mFailures == 1
                        && measured.mTotal == failAt && measured.mOutstanding == 0
                        && measured.visits(Phase::Installation) == 0 && !file.failedClosed(),
                    "file commit missed allocation failure or partially accepted");
                verify();
                Trace retry;
                faults = { FileFault::None, 17 };
                {
                    Observe observe(retry, allocationCount + 1);
                    committed = fixture.commitDurably(make(), content.mDeclarations, sink);
                    owner.reset();
                }
                require(committed && accepted == 1 && retry.mFailures == 0 && retry.mOutstanding == 0
                        && retry.mTrackingOverflow == 0 && retry.allocations(Phase::Installation) == 0
                        && retry.allocations(Phase::Retirement) == 0,
                    "file commit retry/cleanup failed");
                verifyUnrelated();
                require(fileBytes(path) == expectedBytes, "retry persisted wrong bytes");
                return measured.mTotal;
            }
            else
            {
                require(committed && !uncertain && !allocationFailed && accepted == 1 && calls == 1
                        && measured.mFailures == 0 && faults.mWrites > 1 && faults.mReads > 1
                        && measured.visits(Phase::Installation) == 1 && measured.visits(Phase::Retirement) == 1,
                    "file acceptance/install/short I/O boundary failed");
                if (failAt)
                {
                    require(measured.mTotal == allocationCount && measured.mOutstanding == 0,
                        "accepted file commit allocated beyond measured boundary or leaked");
                    verifyUnrelated();
                    return measured.mTotal;
                }
                // Independent preparations own different nodes/registrations.
                // Compare semantic bindings, then resolve every installed node.
                const auto registry = fixture.mModel.snapshotPtrRegistry();
                require(registry.mRevision == expectedRegistry.mRevision
                        && registry.mLastGenerated == expectedRegistry.mLastGenerated
                        && registry.mEntries.size() == expectedRegistry.mEntries.size(),
                    "file commit installed wrong registry metadata");
                for (const auto& [id, binding] : expectedRegistry.mEntries)
                {
                    const auto& actual = registry.mEntries.at(id);
                    require(actual.getCell() == binding.getCell() && actual.getContainer() == binding.getContainer(),
                        "file commit installed wrong registry ownership");
                }
                for (const auto* storage : { &fixture.sourceStorage(), &fixture.destinationStorage() })
                    for (const auto& node : *storage)
                        require(fixture.mModel.getPtr(node.mRef.getRefNum()).mRef == &node
                                && node.mWorldModel == &fixture.mModel,
                            "file commit registered a different node");
                const auto checkScripts = [](const auto& actual, const auto& wanted) {
                    require(actual.mCursor == wanted.mCursor && actual.mEntries.size() == wanted.mEntries.size(),
                        "file commit installed wrong script membership/cursor");
                    for (size_t i = 0; i < actual.mEntries.size(); ++i)
                        require(actual.mEntries[i].getScript() == wanted.mEntries[i].getScript()
                                && actual.mEntries[i].getCell() == wanted.mEntries[i].getCell()
                                && actual.mEntries[i].getContainer() == wanted.mEntries[i].getContainer(),
                            "file commit installed wrong script order/ownership");
                };
                checkScripts(fixture.mSourceScripts.snapshot(), expectedScripts);
                checkScripts(fixture.mDestinationAdd.mLocalScripts->snapshot(), expectedDestinationScripts);
                SerializedPair installed;
                saveInstalled(installed);
                checkSavedValues(installed, expectedSave);
                owner.reset();
            }
            verifyUnrelated();
            // Fresh handle and fresh owned state after fixture destruction. This
            // does not authorize live installation, even when outcome was uncertain.
            SerializedPair decoded;
            FileFaults fresh;
            loadFile(path, bindings, decoded, fresh);
            const auto& coherentSave = fault == FileFault::ReplaceError ? prior : expectedSave;
            const auto& coherentBytes = fault == FileFault::ReplaceError ? priorBytes : expectedBytes;
            checkSavedValues(decoded, coherentSave);
            std::unique_ptr<const RestoredPair> restored;
            restorePair(decoded, content, restored);
            if (fault != FileFault::ReplaceError)
                checkRestored(*restored, expected);
            SerializedPair savedAgain;
            serializePair(*restored, content.mDeclarations, savedAgain);
            checkSavedValues(savedAgain, coherentSave);
            TransferSaveBytes again;
            encodeTransferSave(savedAgain, bindings, again);
            require(again == coherentBytes && fileBytes(path) == coherentBytes && !std::filesystem::exists(temporary),
                "reopen/decode/detached restore/save lost accepted bytes or leaked staging");
            if (exhaustive && fault == FileFault::None)
                checkFileReads(path, expectedBytes, expectedSave, bindings, evidence);
            return measured.mTotal;
        }

        template <class Make, class Incomplete, class Verify, class Unrelated>
        size_t checkCommitCase(std::unique_ptr<DisposableTransferRehearsal>& owner, Make make, Incomplete incomplete,
            Verify verifyOriginal, Unrelated verifyUnrelated, const RestoreContent& content, size_t failAt,
            size_t allocationCount, bool codec, bool malformed, size_t& codecAllocations, size_t& codecRejections)
        {
            using namespace Allocations;
            using Pair = PreparedContainerTransfer;
            using Fixture = DisposableTransferRehearsal;
            auto& fixture = *owner;
            auto persisted = std::make_unique<SerializedPair>(pairOutputSentinel<true>());
            const auto priorOutput = pairOutputState(*persisted);
            std::optional<TransferSaveBytes> encoded{ TransferSaveBytes(57, 'x') };
            const auto priorBytes = byteState(*encoded);
            SaveEnvelope envelope{ "OpenMW-0.51.0-test-inventory-runtime-1", { 1, 7, 19 },
                fixture.mSourceOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationOwner.getPtr().getCellRef().getRefNum(),
                fixture.mDestinationAdd.mPlayer.getCellRef().getRefNum() };
            const std::array referenceIds{ content.mBases[0]->mId, content.mBases[1]->mId,
                ESM::RefId::stringRefId("serialization_owner"), ESM::RefId::stringRefId("serialization_soul"),
                ESM::RefId::stringRefId("serialization_faction"), ESM::RefId::stringRefId("serialization_key"),
                ESM::RefId::stringRefId("serialization_trap"), ESM::RefId::stringRefId("dormant_soul") };
            const SaveBindings saveBindings{ envelope, content, referenceIds };
            int calls = 0, accepted = 0;
            // Stage a full independent sink copy. A failed copy, false return or
            // throw never publishes an acceptance, just like a synchronous sink.
            bool accept = failAt != 0;
            const Fixture::TestSink sink = [&](const SerializedPair& saved) {
                ++calls;
                auto staged = std::make_unique<SerializedPair>(saved);
                TransferSaveBytes stagedBytes;
                if (codec)
                    encodeTransferSave(saved, saveBindings, stagedBytes);
                if (!accept)
                    return false;
                persisted.swap(staged);
                if (codec)
                    encoded->swap(stagedBytes);
                ++accepted;
                return true;
            };
            Trace measured;
            bool caught = false, committed = false;
            {
                Observe observe(measured, failAt);
                InPhase phase(Phase::Preparation);
                try
                {
                    committed = fixture.commit(make(), content.mDeclarations, sink);
                }
                catch (const std::bad_alloc&)
                {
                    caught = true;
                }
                if (committed)
                {
                    // The ordinal immediately beyond all pre-acceptance work
                    // must never fire, even during complete successful cleanup.
                    persisted.reset();
                    encoded.reset();
                    owner.reset();
                }
            }
            require(measured.mOutstanding == 0 && measured.mTrackingOverflow == 0,
                "commit leaked an observed allocation or overflowed cleanup tracking");
            require(measured.allocations(Phase::Installation) == 0 && measured.allocations(Phase::Retirement) == 0,
                "accepted commit allocated during installation/retirement");
            if (failAt > allocationCount && allocationCount)
            {
                require(committed && !caught && calls == 1 && accepted == 1 && measured.mFailures == 0
                        && measured.mTotal == allocationCount && measured.visits(Phase::Installation) == 1
                        && measured.visits(Phase::Retirement) == 1,
                    "post-acceptance allocation boundary or fixture cleanup failed");
                verifyUnrelated();
                return measured.mTotal;
            }
            require(!committed && accepted == 0 && pairOutputState(*persisted) == priorOutput
                    && byteState(*encoded) == priorBytes,
                "failed commit accepted or changed prior sink output");
            require(failAt ? caught && measured.mFailures == 1 && measured.mTotal == failAt
                           : !caught && calls == 1 && measured.mFailures == 0,
                "commit missed an allocation failure or allocated during unwinding");
            require(measured.visits(Phase::Installation) == 0 && measured.visits(Phase::Retirement) == 0,
                "rejected commit reached installation");
            verifyOriginal();
            if (failAt)
            {
                // Retry on these exact preserved stores/services, accept, then
                // destroy all newly installed state inside allocation tracking.
                calls = accepted = 0;
                Trace retry;
                {
                    Observe observe(retry, allocationCount + 1);
                    InPhase phase(Phase::Preparation);
                    committed = fixture.commit(make(), content.mDeclarations, sink);
                    persisted.reset();
                    encoded.reset();
                    owner.reset();
                }
                require(committed && calls == 1 && accepted == 1 && retry.mTotal == allocationCount
                        && retry.mFailures == 0 && retry.mOutstanding == 0 && retry.mTrackingOverflow == 0
                        && retry.allocations(Phase::Installation) == 0 && retry.allocations(Phase::Retirement) == 0
                        && retry.visits(Phase::Installation) == 1 && retry.visits(Phase::Retirement) == 1,
                    "successful commit retry or complete cleanup failed");
                verifyUnrelated();
                return measured.mTotal;
            }
            require(measured.allocations(Phase::Preparation) > 0 && measured.allocations(Phase::Validation) > 0
                    && measured.allocations(Phase::Revalidation) > 0 && measured.allocations(Phase::Persistence) > 0,
                "commit allocation coverage missed preparation/validation/save/sink");

            const auto reject = [&](Pair pair) {
                const auto before = snapshot(fixture);
                const auto nodes = ownedNodes(pair);
                bool rejected = false, called = false;
                try
                {
                    fixture.commit(std::move(pair), content.mDeclarations, [&](const SerializedPair&) {
                        called = true;
                        return true;
                    });
                }
                catch (const std::invalid_argument&)
                {
                    rejected = true;
                }
                require(rejected && !called && snapshot(fixture) == before,
                    "invalid commit reached sink or changed fixture");
                requireDiscarded(nodes);
            };
            reject(incomplete());
            auto invalid = make();
            ++const_cast<Pair::ResolutionCompleteness&>(invalid.getResolutionCompleteness()).mRegistryEntries;
            reject(std::move(invalid));
            invalid = make();
            ++const_cast<Pair::IteratorBindings&>(invalid.getIteratorBindings()).mCount;
            reject(std::move(invalid));
            invalid = make();
            ++const_cast<PtrRegistry::Snapshot&>(invalid.getRegistryStorage().getBindings()).mRevision;
            reject(std::move(invalid));
            invalid = make();
            ++const_cast<PtrRegistry::Snapshot&>(invalid.getRegistryStorage().getBindings()).mLastGenerated.mIndex;
            reject(std::move(invalid));
            invalid = make();
            const_cast<CellRef&>(invalid.getSourceStorage().front().mRef).setCount(99);
            reject(std::move(invalid));
            const auto reconstruct = [](auto& node) {
                auto replacement = std::move(node);
                std::destroy_at(&node);
                std::construct_at(&node, std::move(replacement));
            };
            invalid = make();
            reconstruct(const_cast<Pair::MiscList&>(invalid.getSourceStorage()).back());
            reject(std::move(invalid));
            invalid = make();
            if (!invalid.getSourceScriptStorage().getEntries().empty())
            {
                reconstruct(
                    const_cast<LocalScripts::PreparedStorage::Entries&>(invalid.getSourceScriptStorage().getEntries())
                        .front());
                reject(std::move(invalid));
            }
            // Mutate current witnesses, not saved raw addresses/iterators.
            invalid = make();
            auto& original = const_cast<CellRef&>(fixture.sourceStorage().front().mRef);
            const auto originalRef = original;
            original.setCount(3);
            reject(std::move(invalid));
            original = originalRef;
            invalid = make();
            const auto counter = fixture.mModel.getLastGeneratedRefNum();
            auto changedCounter = counter;
            ++changedCounter.mIndex;
            fixture.mModel.setLastGeneratedRefNum(changedCounter);
            reject(std::move(invalid));
            fixture.mModel.setLastGeneratedRefNum(counter);
            verifyOriginal();

            for (int mode : { 0, 1, 2 })
            {
                auto pair = make();
                const auto nodes = ownedNodes(pair);
                const auto* bindings = &pair.getIteratorBindings();
                const auto* contexts = &pair.getContextBindings();
                const auto* collection = &pair.getResolvedStoreBindings();
                const auto* completeness = &pair.getResolutionCompleteness();
                bool reached = false, thrown = false;
                try
                {
                    require(
                        !fixture.commit(std::move(pair), content.mDeclarations,
                            [&](const SerializedPair& saved) {
                                reached = true;
                                verifyOriginal(); // Every current fixture byte/storage is still unchanged at the sink.
                                require(completeness->mIterators == bindings && completeness->mContexts == contexts
                                        && completeness->mResolvedStores == collection,
                                    "commit replaced protected bindings before sink");
                                std::unique_ptr<const RestoredPair> restored;
                                restorePair(saved, content, restored);
                                if (mode == 1)
                                    throw Failure{};
                                if (mode == 2)
                                    throw std::bad_alloc();
                                return false;
                            }),
                        "declining sink committed");
                }
                catch (const Failure&)
                {
                    thrown = mode == 1;
                }
                catch (const std::bad_alloc&)
                {
                    thrown = mode == 2;
                }
                require(reached && (mode == 0 || thrown), "commit swallowed sink exception");
                requireDiscarded(nodes);
                verifyOriginal();
                // The ordinary rehearsal remains usable after each sink failure.
                fixture.rehearse(make());
                verifyOriginal();
            }

            auto pair = make();
            auto stale = make(); // Retains original lifetime/storage witnesses through retirement.
            const auto expected = expectedRestoration(pair, false);
            const auto restoreExpected = expectedRestoration(pair);
            SerializedPair expectedSave;
            serializePair(fixture, pair, content.mDeclarations, expectedSave);
            const auto expectedRegistry = pair.getRelocation().mRegistry;
            const auto expectedScripts = pair.getRelocation().mSourceScripts;
            const auto expectedDestinationScripts = pair.getRelocation().mDestinationScripts.value_or(expectedScripts);
            const auto sourceSelection = pair.getRelocation().mSourceSelection;
            const auto destinationSelection = pair.getRelocation().mDestinationSelection;
            const auto sourceCursor = pair.getSourceScriptStorage().getCursor();
            const auto destinationCursor = pair.getDestinationScriptStorage().getCursor();
            const auto nodes = ownedNodes(pair);
            std::vector<ConstPtr> oldNodes;
            for (const auto* list : { &fixture.sourceStorage(), &fixture.destinationStorage() })
                for (const auto& node : *list)
                    oldNodes.emplace_back(&node);
            const auto otherBefore = fixture.cacheState(fixture.mOther);
            const auto sourceCache = fixture.cacheState(fixture.mSource),
                       destinationCache = fixture.cacheState(fixture.mDestination);
            const auto otherState = nodeState(ConstPtr(&fixture.otherStorage().front()));
            const int notifications = fixture.mNotifications;
            calls = accepted = 0;
            accept = true;
            require(fixture.commit(std::move(pair), content.mDeclarations,
                        [&](const SerializedPair& saved) {
                            verifyOriginal();
                            checkSavedValues(saved, expectedSave);
                            return sink(saved);
                        }),
                "accepting sink did not commit");
            require(calls == 1 && accepted == 1, "sink acceptance repeated");
            requireDiscarded(oldNodes);
            require(fixture.mModel.snapshotPtrRegistry() == expectedRegistry
                    && fixture.mSourceScripts.snapshot() == expectedScripts
                    && fixture.mDestinationAdd.mLocalScripts->snapshot() == expectedDestinationScripts
                    && fixture.scriptCursor(fixture.mSourceScripts) == sourceCursor
                    && fixture.scriptCursor(*fixture.mDestinationAdd.mLocalScripts) == destinationCursor,
                "committed registry/scripts/cursors differ from intended engine result");
            const auto checkInventory = [&](const auto& storage, const auto& wanted, const ContainerStore& target) {
                require(storage.size() == wanted.mNodes.size(), "commit changed raw membership");
                auto next = wanted.mNodes.begin();
                size_t i = 0;
                for (const auto& node : storage)
                {
                    const auto& value = *next++;
                    const auto id = wanted.mProposedIdentities[i++];
                    require(node.mBase == value.mBase && node.mRef.getRefNum() == id
                            && node.mWorldModel == &fixture.mModel && fixture.mModel.getPtr(id).mRef == &node
                            && fixture.mModel.getPtr(id).mContainerStore == &target
                            && node.mData.matchesContainerTransferState(value.mData)
                            && localValues(node.mData.getLocals()) == localValues(value.mData.getLocals()),
                        "commit lost owned engine values or registry ownership");
                }
            };
            checkInventory(fixture.sourceStorage(), expected.mSource, fixture.mSource);
            checkInventory(fixture.destinationStorage(), expected.mDestination, fixture.mDestination);
            const auto selected = [](const ContainerStore& store) {
                const auto it = store.getSelectedEnchantItem();
                return it == store.end() ? ConstPtr() : *it;
            };
            require(
                selected(fixture.mSource) == sourceSelection && selected(fixture.mDestination) == destinationSelection,
                "commit lost selection/end/dormant position");
            const auto checkCache = [&](const ContainerStore& store, const auto& before) {
                const auto after = fixture.cacheState(store);
                require(!std::get<1>(after) && !std::get<2>(after) && std::get<3>(after)
                        && std::get<4>(after) == std::get<4>(before) && std::get<5>(after) == std::get<5>(before)
                        && std::get<6>(after).empty() && std::get<9>(after) == std::get<9>(before)
                        && std::get<10>(after) == std::get<10>(before),
                    "commit lost storage/seed/listener/resolution or retained stale caches");
            };
            checkCache(fixture.mSource, sourceCache);
            checkCache(fixture.mDestination, destinationCache);
            require(fixture.cacheState(fixture.mOther) == otherBefore
                    && nodeState(ConstPtr(&fixture.otherStorage().front())) == otherState
                    && fixture.mNotifications == notifications,
                "commit changed supplied store or emitted notification");
            verifyUnrelated();
            // Save the actual installed engine state in the same detached format.
            SerializedPair installed;
            saveFixture(fixture, content.mDeclarations, installed);
            checkSavedValues(installed, *persisted);
            std::unique_ptr<const RestoredPair> restored;
            restorePair(*persisted, content, restored);
            checkRestored(*restored, restoreExpected);
            SerializedPair resaved;
            serializePair(*restored, content.mDeclarations, resaved);
            checkSavedValues(resaved, installed);
            if (codec)
            {
                const auto installedBefore = snapshot(fixture);
                codecAllocations += checkCodecSave(
                    *persisted, *encoded, saveBindings, restoreExpected,
                    [&] {
                        require(snapshot(fixture) == installedBefore, "codec changed installed fixture");
                        verifyUnrelated();
                    },
                    malformed, codecRejections);
            }
            // A moved-from pair cannot even be read to collect owned witnesses.
            bool reused = false;
            const auto committedBefore = snapshot(fixture);
            try
            {
                fixture.commit(std::move(pair), content.mDeclarations, sink);
            }
            catch (const std::invalid_argument&)
            {
                reused = true;
            }
            require(reused && calls == 1 && snapshot(fixture) == committedBefore,
                "consumed pair reuse reached sink or changed committed state");
            // No saved reference/iterator may be followed after old-node destruction.
            bool staleRejected = false;
            try
            {
                fixture.commit(std::move(stale), content.mDeclarations, sink);
            }
            catch (const std::invalid_argument&)
            {
                staleRejected = true;
            }
            require(staleRejected && calls == 1 && snapshot(fixture) == committedBefore,
                "retired witness accepted or changed committed state");
            owner.reset();
            requireDiscarded(nodes);
            if (codec)
            {
                // Decode after both the consumed prepared pair and committed
                // fixture are gone: only owned bytes and explicit content remain.
                SerializedPair decoded;
                decodeTransferSave(*encoded, saveBindings, decoded);
                restored.reset();
                restorePair(decoded, content, restored);
                checkRestored(*restored, restoreExpected);
                serializePair(*restored, content.mDeclarations, resaved);
                checkSavedValues(resaved, installed);
            }
            verifyUnrelated();
            return measured.mTotal;
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

    namespace
    {
        // Independent wire construction for malformed script metadata. Keep the
        // accepted inventory prefix intact; bypass the encoder's semantic checks.
        TransferSaveBytes replaceScriptRecord(const TransferSaveBytes& bytes, const TransferScriptMetadata& scripts)
        {
            const auto number = [&](size_t offset) {
                uint32_t result = 0;
                for (size_t i = 0; i < 4; ++i)
                    result |= static_cast<uint32_t>(static_cast<unsigned char>(bytes.at(offset + i))) << (8 * i);
                return result;
            };
            size_t record = 0;
            while (std::string_view(bytes.data() + record, 4) != "SCRP")
                record += 16 + number(record + 4);
            TransferSaveBytes result(bytes.begin(), bytes.begin() + record);
            const auto put = [&](uint32_t value) {
                for (size_t i = 0; i < 4; ++i)
                    result.push_back(static_cast<char>(value >> (8 * i)));
            };
            const auto tag = [&](std::string_view value) { result.insert(result.end(), value.begin(), value.end()); };
            const auto field = [&](std::string_view name, std::initializer_list<uint32_t> values) {
                tag(name);
                put(static_cast<uint32_t>(values.size() * 4));
                for (const auto value : values)
                    put(value);
            };
            const auto id = [&](std::string_view name, ESM::RefId value) {
                tag(name);
                const auto text = value.empty() ? std::string_view{} : value.getRefIdString();
                put(static_cast<uint32_t>(1 + text.size()));
                result.push_back(
                    static_cast<char>(value.empty() ? ESM::RefIdType::Empty : ESM::RefIdType::UnsizedString));
                tag(text);
            };
            tag("SCRP");
            put(0);
            put(0);
            put(0);
            field("SMAP", { scripts.mShared ? 0u : 1u });
            field("OCNT", { static_cast<uint32_t>(scripts.mOther.size()) });
            for (const auto& item : scripts.mOther)
            {
                field("BIND", { item.mIdentity.mIndex, std::bit_cast<uint32_t>(item.mIdentity.mContentFile),
                                  static_cast<uint32_t>(item.mConfigured) });
                id("BASE", item.mBase);
            }
            // Also emit an illegally populated shared second service for rejection.
            const bool second = !scripts.mShared || !scripts.mServices[1].mEntries.empty()
                || scripts.mServices[1].mCursor != 0;
            for (size_t side = 0; side < (second ? 2u : 1u); ++side)
            {
                const auto& service = scripts.mServices[side];
                field("SERV",
                    { static_cast<uint32_t>(service.mEntries.size()), static_cast<uint32_t>(service.mCursor) });
                for (const auto& entry : service.mEntries)
                {
                    field("SREF", { entry.mIdentity.mIndex, std::bit_cast<uint32_t>(entry.mIdentity.mContentFile) });
                    id("SCPT", entry.mScript);
                }
            }
            const auto size = static_cast<uint32_t>(result.size() - record - 16);
            for (size_t i = 0; i < 4; ++i)
                result[record + 4 + i] = static_cast<char>(size >> (8 * i));
            return result;
        }

        template <class Make, class Verify>
        size_t checkScriptMetadataCase(std::unique_ptr<DisposableTransferRehearsal>& fixture, Make make,
            Verify verifyOriginal, const RestoreContent& content, size_t& rejections)
        {
            SaveEnvelope envelope{ "OpenMW-0.51.0-test-inventory-runtime-1", { 1, 7, 19 },
                fixture->mSourceOwner.getPtr().getCellRef().getRefNum(),
                fixture->mDestinationOwner.getPtr().getCellRef().getRefNum(),
                fixture->mDestinationOwner.getPtr().getCellRef().getRefNum() };
            const std::array referenceIds{ content.mBases[0]->mId, content.mBases[1]->mId,
                ESM::RefId::stringRefId("serialization_owner"), ESM::RefId::stringRefId("serialization_soul"),
                ESM::RefId::stringRefId("serialization_faction"), ESM::RefId::stringRefId("serialization_key"),
                ESM::RefId::stringRefId("serialization_trap"), ESM::RefId::stringRefId("dormant_soul") };
            const SaveBindings bindings{ envelope, content, referenceIds };
            SerializedPair saved;
            size_t allocations = 0;
            {
                const auto prepared = make();
                const auto check = [&](const SerializedPair& value) {
                    checkPairOutput(prepared, value);
                    require(value.mScripts.mShared
                            == (fixture->mDestinationAdd.mLocalScripts == &fixture->mSourceScripts),
                        "lost shared/distinct script association");
                    for (size_t side = 0; side < (value.mScripts.mShared ? 1u : 2u); ++side)
                    {
                        const auto& storage
                            = side == 0 ? prepared.getSourceScriptStorage() : prepared.getDestinationScriptStorage();
                        const auto& service = value.mScripts.mServices[side];
                        require(service.mEntries.size() == storage.getEntries().size(),
                            "lost complete script membership");
                        auto node = storage.getEntries().begin();
                        for (const auto& entry : service.mEntries)
                        {
                            require(node->getScript() == entry.mScript
                                    && prepared.getRegistryStorage().getItem(entry.mIdentity) == node->getItem(),
                                "lost ordered registration identity/script");
                            ++node;
                        }
                        node = storage.getEntries().begin();
                        std::advance(node, service.mCursor);
                        require((node == storage.getEntries().end() ? nullptr : &*node) == storage.getCursor(),
                            "lost prepared script cursor");
                    }
                    size_t unregistered = 0, dormant = 0;
                    for (const auto* inventory : { &value.mSource, &value.mDestination })
                        for (size_t i = 0; i < inventory->mObjects.size(); ++i)
                        {
                            const auto& state = inventory->mObjects[i];
                            bool registered = false;
                            for (const auto& service : value.mScripts.mServices)
                                for (const auto& entry : service.mEntries)
                                    registered |= entry.mIdentity == inventory->mProposedIdentities[i];
                            unregistered += state.mHasLocals && !registered;
                            dormant += state.mRef.mCount == 0 && registered;
                        }
                    require(unregistered >= 2 && dormant >= 1, "configured/unregistered or dormant coverage missing");
                    require(value.mScripts.mOther.size() == fixture->otherStorage().size(),
                        "lost other-store bindings");
                };
                MWWorld::Testing::serializePair(*fixture, prepared, content.mDeclarations, saved);
                check(saved);
                allocations += checkSerializationAllocations([] { return pairOutputSentinel<true>(); },
                    [&](auto& output) {
                        MWWorld::Testing::serializePair(*fixture, prepared, content.mDeclarations, output);
                    },
                    [](const auto& output) { return pairOutputState(output); }, check, verifyOriginal);
            }
            TransferSaveBytes bytes;
            encodeTransferSave(saved, bindings, bytes);
            require(replaceScriptRecord(bytes, saved.mScripts) == bytes, "independent script wire layout differs");
            const auto retained = pairOutputState(saved);
            const auto retainedBytes = byteState(bytes);
            const auto verify = [&] {
                require(pairOutputState(saved) == retained && byteState(bytes) == retainedBytes,
                    "script codec changed caller input/storage");
                if (fixture)
                    verifyOriginal();
            };
            const auto checkDecoded = [&](const SerializedPair& output) {
                checkSavedValues(output, saved);
                std::unique_ptr<const RestoredPair> restored;
                restorePair(output, content, restored);
                require(restored->mScripts == saved.mScripts, "detached restore lost script metadata");
                SerializedPair again;
                serializePair(*restored, content.mDeclarations, again);
                checkSavedValues(again, saved);
            };
            allocations += checkSerializationAllocations([] { return TransferSaveBytes(71, 'x'); },
                [&](auto& output) { encodeTransferSave(saved, bindings, output); }, byteState,
                [&](const auto& output) { require(output == bytes, "script encoding changed"); }, verify);

            const auto reject = [&](const SerializedPair& bad, const TransferSaveBytes* raw = nullptr) {
                const auto badBefore = pairOutputState(bad);
                const auto wire = raw ? *raw : replaceScriptRecord(bytes, bad.mScripts);
                // Each rejection must happen before engine/staging allocation;
                // only the diagnostic itself may allocate. Fail it as well.
                for (bool decode : { false, true })
                {
                    if (raw && !decode)
                        continue;
                    auto output = pairOutputSentinel<true>();
                    auto encoded = TransferSaveBytes(71, 'x');
                    const auto outputBefore = pairOutputState(output);
                    const auto encodedBefore = byteState(encoded);
                    for (size_t failAt : { size_t{ 0 }, size_t{ 1 } })
                    {
                        bool caught = false;
                        Allocations::Trace trace;
                        {
                            Allocations::Observe observe(trace, failAt);
                            try
                            {
                                if (decode)
                                    decodeTransferSave(wire, bindings, output);
                                else
                                    encodeTransferSave(bad, bindings, encoded);
                            }
                            catch (const std::exception&)
                            {
                                caught = true;
                            }
                        }
                        require(caught && trace.mTotal <= 1 && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                                && pairOutputState(output) == outputBefore && byteState(encoded) == encodedBefore
                                && pairOutputState(bad) == badBefore,
                            "malformed script metadata allocated, leaked or changed output/input");
                        verify();
                        ++rejections;
                        allocations += trace.mFailures;
                    }
                    decodeTransferSave(bytes, bindings, output);
                    checkDecoded(output);
                    encodeTransferSave(saved, bindings, encoded);
                    require(encoded == bytes, "script rejection prevented healthy retry");
                }
            };
            const auto mutate = [&](auto edit) { auto bad = saved; edit(bad.mScripts); reject(bad); };
            mutate([](auto& s) { s.mServices[0].mCursor = s.mServices[0].mEntries.size() + 1; });
            mutate([](auto& s) { s.mServices[1].mCursor = SIZE_MAX; });
            mutate([](auto& s) { s.mServices[0].mEntries.push_back(s.mServices[0].mEntries.front()); });
            mutate([](auto& s) { s.mServices[1].mEntries.push_back(s.mServices[0].mEntries.front()); });
            mutate([](auto& s) { s.mServices[0].mEntries.resize(MaxTransferScriptEntries + 1); });
            for (const auto id : { ESM::RefNum{}, ESM::RefNum{ 1, -2 },
                     ESM::RefNum{ saved.mRestart.mLastGenerated.mIndex + 1, -1 }, envelope.mSourceOwner })
                mutate([&](auto& s) { s.mServices[0].mEntries.front().mIdentity = id; });
            for (const auto id : { ESM::RefId{}, content.mBases[0]->mId, ESM::RefId::stringRefId("missing_script") })
                mutate([&](auto& s) { s.mServices[0].mEntries.front().mScript = id; });
            mutate([](auto& s) { s.mOther.push_back(s.mOther.front()); });
            mutate([](auto& s) { s.mOther.resize(MaxTransferInventoryItems + 1); });
            mutate([&](auto& s) { s.mOther.front().mIdentity = saved.mSource.mProposedIdentities.front(); });
            mutate([&](auto& s) { s.mOther.front().mIdentity = envelope.mSourceOwner; });
            mutate([](auto& s) { s.mOther.front().mIdentity = {}; });
            mutate([&](auto& s) { s.mOther.front().mIdentity = { saved.mRestart.mLastGenerated.mIndex + 1, -1 }; });
            mutate([](auto& s) { s.mOther.front().mBase = {}; });
            mutate([](auto& s) { s.mOther.front().mBase = ESM::RefId::stringRefId("missing_base"); });
            if (saved.mScripts.mOther.front().mConfigured)
            {
                mutate([](auto& s) { s.mOther.clear(); });
                mutate([](auto& s) { s.mOther.front().mConfigured = false; });
                mutate([&](auto& s) { s.mOther.front().mBase = content.mBases[0]->mId; });
            }
            if (!saved.mScripts.mShared)
                mutate([](auto& s) {
                    s.mServices[1].mEntries.push_back(s.mServices[0].mEntries.front());
                    s.mServices[0].mEntries.erase(s.mServices[0].mEntries.begin());
                    s.mServices[0].mCursor = 0;
                });

            // Exercise raw scalar domains that owned bools/bounded vectors cannot
            // express, as well as hostile count/cursor values before allocation.
            const auto number = [&](size_t offset) {
                uint32_t value = 0;
                for (size_t i = 0; i < 4; ++i)
                    value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes.at(offset + i))) << (8 * i);
                return value;
            };
            size_t record = 0;
            while (std::string_view(bytes.data() + record, 4) != "SCRP")
                record += 16 + number(record + 4);
            for (const auto& [tag, offset, value] : {
                     std::tuple{ "SMAP", 0, 2u }, { "OCNT", 0, 1025u }, { "BIND", 8, 2u },
                     { "SERV", 0, static_cast<uint32_t>(MaxTransferScriptEntries + 1) }, { "SERV", 4, UINT32_MAX } })
            {
                size_t field = record + 16;
                while (std::string_view(bytes.data() + field, 4) != tag)
                    field += 8 + number(field + 4);
                auto malformed = bytes;
                for (size_t i = 0; i < 4; ++i)
                    malformed.at(field + 8 + offset + i) = static_cast<char>(value >> (8 * i));
                reject(saved, &malformed);
            }

            verifyOriginal();
            const auto old = fixture->restartBindings();
            fixture.reset();
            for (const auto& owner : old.mOwners)
                require(!owner.hasLiveReference(), "script test retained original fixture lifetime");
            allocations += checkSerializationAllocations([] { return pairOutputSentinel<true>(); },
                [&](auto& output) { decodeTransferSave(bytes, bindings, output); },
                [](const auto& output) { return pairOutputState(output); }, checkDecoded, verify);
            SerializedPair decoded;
            decodeTransferSave(bytes, bindings, decoded);
            checkDecoded(decoded);
            return allocations;
        }

        size_t checkRestartScriptStorage(std::unique_ptr<DisposableTransferRehearsal>& fixture,
            const SerializedPair& decoded, const std::unique_ptr<const RestoredPair>& restored,
            const RestoreContent& content, const SaveEnvelope& envelope,
            const DisposableTransferRehearsal::RestartScriptBindings& old, size_t& rejections)
        {
            using Fixture = DisposableTransferRehearsal;
            using Candidate = Fixture::RestartScripts;
            const auto fresh = fixture->restartScriptBindings();
            std::unique_ptr<const Fixture::RestartRegistry> registry;
            fixture->prepareRestartRegistry(decoded, *restored, envelope, fresh.mRegistry, registry);
            const auto fixtureBefore = snapshot(*fixture);
            const auto nodesBefore = restoredState(restored);
            const auto decodedBefore = pairOutputState(decoded);
            const auto contentBefore = contentState(content);
            const auto registryBefore = registry->getBindings();
            size_t unregistered = 0, dormant = 0;
            for (const auto* inventory : { &decoded.mSource, &decoded.mDestination })
                for (size_t i = 0; i < inventory->mObjects.size(); ++i)
                {
                    const auto id = inventory->mProposedIdentities[i];
                    bool registered = false;
                    for (const auto& service : decoded.mScripts.mServices)
                        for (const auto& entry : service.mEntries)
                            registered |= entry.mIdentity == id;
                    unregistered += inventory->mObjects[i].mHasLocals && !registered;
                    dormant += inventory->mObjects[i].mRef.mCount == 0 && registered;
                }
            require(unregistered >= 2 && dormant >= 1, "fresh script fixture lacks unregistered/dormant coverage");
            const auto verify = [&] {
                require(snapshot(*fixture) == fixtureBefore && restoredState(restored) == nodesBefore
                        && pairOutputState(decoded) == decodedBefore && contentState(content) == contentBefore
                        && registry->getBindings() == registryBefore && !fixture->failedClosed(),
                    "script reconstruction changed fixture, inputs, content or registry");
            };
            const auto check = [&](const auto& output, const SerializedPair& saved, const auto& index) {
                require(output->getRestart() == saved.mRestart, "script reconstruction rebuilt restart counters");
                const auto& source = output->getSourceStorage();
                const auto& destination = output->getDestinationStorage();
                require((&source == &destination) == saved.mScripts.mShared, "lost shared/distinct service storage");
                for (size_t side = 0; side < (saved.mScripts.mShared ? 1u : 2u); ++side)
                {
                    const auto& storage = side == 0 ? source : destination;
                    const auto& expected = saved.mScripts.mServices[side];
                    require(storage.getEntries().size() == expected.mEntries.size(), "lost exact script membership");
                    auto it = storage.getEntries().begin();
                    const void* cursor = nullptr;
                    for (size_t i = 0; i < expected.mEntries.size(); ++i, ++it)
                    {
                        const auto& registration = expected.mEntries[i];
                        const auto item = index->getItem(registration.mIdentity);
                        require(it->getScript() == registration.mScript && it->getItem() == item
                                && it->getItem().getReferenceLifetime() == item.getReferenceLifetime()
                                && it->getContainer() == item.mContainerStore && !it->getCell()
                                && it->references(&item.getCellRef()),
                            "script storage lost ordered identity, base or ownership");
                        if (i == expected.mCursor)
                            cursor = &*it;
                    }
                    require(storage.getCursor() == cursor, "script storage lost begin/middle/end cursor");
                }
            };
            const auto state = [](const auto& output) {
                const auto storage = [](const auto& list) {
                    std::vector<std::tuple<const void*, ConstPtr, ReferenceLifetime::Witness, ESM::RefId,
                        const CellStore*, const ContainerStore*>> entries;
                    for (const auto& entry : list.getEntries())
                        entries.emplace_back(&entry, entry.getItem(), entry.getItem().getReferenceLifetime(),
                            entry.getScript(), entry.getCell(), entry.getContainer());
                    return std::tuple{ &list, list.getCursor(), entries };
                };
                return std::tuple{ output.get(), output->getRestart(), storage(output->getSourceStorage()),
                    storage(output->getDestinationStorage()) };
            };
            const auto prepare = [&](auto& output) {
                fixture->prepareRestartScripts(decoded, *restored, content, envelope, *registry, fresh, output);
            };
            const auto make = [&] { std::unique_ptr<const Candidate> result; prepare(result); return result; };
            size_t allocations = checkSerializationAllocations(make, prepare, state,
                [&](const auto& output) { check(output, decoded, registry); }, verify);
            {
                auto output = make();
                Allocations::Trace trace;
                {
                    Allocations::Observe observe(trace, allocations + 1);
                    prepare(output);
                    output.reset();
                }
                require(trace.mTotal == allocations && trace.mFailures == 0 && trace.mOutstanding == 0
                        && trace.allocations(Allocations::Phase::Validation) == 0
                        && trace.allocations(Allocations::Phase::Publication) == 0,
                    "script validation/publication/retirement allocated");
            }
            auto output = make();
            const auto retry = [&] { prepare(output); check(output, decoded, registry); verify(); };
            const auto reject = [&](auto attempt, bool retryNow = true) {
                const auto prior = state(output);
                const auto before = snapshot(*fixture);
                const auto inputs = restoredState(restored);
                Allocations::Trace trace;
                size_t diagnosticAllocations = 0;
                for (size_t failAt = 0; failAt <= diagnosticAllocations; ++failAt)
                {
                    bool caught = false;
                    {
                        Allocations::Observe observe(trace, failAt);
                        try { attempt(output); }
                        catch (const std::invalid_argument&) { caught = failAt == 0; }
                        catch (const std::bad_alloc&) { caught = failAt == 1; }
                    }
                    const bool unchanged = state(output) == prior && snapshot(*fixture) == before
                        && restoredState(restored) == inputs && pairOutputState(decoded) == decodedBefore;
                    if (failAt == 0)
                        diagnosticAllocations = trace.mTotal;
                    if (!caught || trace.mTotal > 1 || trace.mOutstanding != 0 || trace.mTrackingOverflow != 0
                        || trace.allocations(Allocations::Phase::Preparation) != 0
                        || trace.allocations(Allocations::Phase::Publication) != 0 || !unchanged)
                    {
                        std::cerr << "Script rejection=" << rejections << " fail-at=" << failAt
                                  << " caught=" << caught << " allocations=" << trace.mTotal
                                  << " outstanding=" << trace.mOutstanding << " unchanged=" << unchanged << '\n';
                        throw std::runtime_error("script rejection staged, leaked, missed allocation failure or changed caller state");
                    }
                    allocations += trace.mFailures;
                    ++rejections;
                }
                if (retryNow)
                    retry();
            };
            const auto badBinding = [&](auto edit) {
                auto bad = fresh;
                edit(bad);
                const auto priorServices = bad.mServices;
                const auto priorLists = bad.mOriginal;
                const auto priorOther = bad.mRegistry.mOther;
                reject([&](auto& target) {
                    fixture->prepareRestartScripts(decoded, *restored, content, envelope, *registry, bad, target);
                });
                require(bad.mServices == priorServices && bad.mOriginal == priorLists && bad.mRegistry.mOther == priorOther,
                    "script rejection mutated explicit bindings");
            };
            badBinding([&](auto& b) { b = old; });
            for (size_t role = 0; role < 3; ++role)
            {
                badBinding([&](auto& b) { b.mServices[role] = nullptr; });
                badBinding([&](auto& b) { b.mServices[role] = &fixture->mDestinationScripts; b.mLifetimes[role] = {}; });
                badBinding([&](auto& b) { b.mLifetimes[role] = old.mLifetimes[role]; });
                badBinding([&](auto& b) { b.mOriginal[role].mCursor = SIZE_MAX; });
                badBinding([&](auto& b) { b.mOriginal[role].mEntries.resize(MaxTransferScriptEntries + 1); });
                badBinding([&](auto& b) { b.mRegistry.mOwners[role] = {}; });
                badBinding([&](auto& b) { b.mRegistry.mStores[role] = nullptr; });
                badBinding([&](auto& b) { b.mRegistry.mLifetimes[role] = old.mRegistry.mLifetimes[role]; });
                badBinding([&](auto& b) { b.mRegistry.mStorage[role] = old.mRegistry.mStorage[role]; });
            }
            badBinding([](auto& b) { b.mRegistry.mOther.clear(); });
            badBinding([](auto& b) { b.mRegistry.mOther.push_back(b.mRegistry.mOther.front()); });
            badBinding([](auto& b) { b.mRegistry.mOther.resize(MaxTransferInventoryItems + 1); });
            badBinding([](auto& b) { ++b.mRegistry.mOther.front().first.mIndex; });
            badBinding([&](auto& b) { b.mRegistry.mOther.front().second = old.mRegistry.mOther.front().second; });
            badBinding([&](auto& b) { b.mRegistry.mOther.front().second.mContainerStore = &fixture->mSource; });
            const auto malformed = [&](auto edit) {
                auto bad = decoded;
                std::unique_ptr<const RestoredPair> detached;
                restorePair(decoded, content, detached);
                edit(bad, const_cast<RestoredPair&>(*detached));
                const auto before = pairOutputState(bad);
                const auto nodes = restoredState(detached);
                reject([&](auto& target) {
                    fixture->prepareRestartScripts(bad, *detached, content, envelope, *registry, fresh, target);
                });
                require(pairOutputState(bad) == before && restoredState(detached) == nodes,
                    "malformed script reconstruction changed detached inputs");
            };
            const auto metadata = [&](auto edit) {
                // Use the original restored nodes so these checks exercise script
                // metadata validation, independently of registry pointer mismatches.
                auto bad = decoded;
                edit(bad.mScripts);
                auto& carried = const_cast<RestoredPair&>(*restored).mScripts;
                TransferScriptMetadata originalMetadata;
                std::swap(carried, originalMetadata);
                carried = bad.mScripts;
                const auto before = pairOutputState(bad);
                reject([&](auto& target) {
                    fixture->prepareRestartScripts(bad, *restored, content, envelope, *registry, fresh, target);
                }, false);
                require(pairOutputState(bad) == before, "script rejection changed malformed metadata");
                std::swap(carried, originalMetadata);
                retry();
            };
            metadata([](auto& s) { s.mShared = !s.mShared; });
            metadata([](auto& s) { s.mServices[0].mCursor = SIZE_MAX; });
            metadata([](auto& s) { s.mServices[1].mCursor = SIZE_MAX; });
            metadata([](auto& s) { s.mServices[0].mEntries.push_back(s.mServices[0].mEntries.front()); });
            metadata([](auto& s) { s.mServices[1].mEntries.push_back(s.mServices[0].mEntries.front()); });
            metadata([](auto& s) { s.mServices[0].mEntries.resize(MaxTransferScriptEntries + 1); });
            metadata([](auto& s) { s.mServices[0].mEntries.front().mScript = {}; });
            metadata([](auto& s) { s.mServices[0].mEntries.front().mIdentity = {}; });
            metadata([&](auto& s) { s.mServices[0].mEntries.front().mIdentity = envelope.mSourceOwner; });
            metadata([](auto& s) { s.mOther.clear(); });
            metadata([](auto& s) { s.mOther.push_back(s.mOther.front()); });
            metadata([](auto& s) { s.mOther.resize(MaxTransferInventoryItems + 1); });
            metadata([](auto& s) { s.mOther.front().mBase = {}; });
            metadata([](auto& s) { s.mOther.front().mConfigured = !s.mOther.front().mConfigured; });
            malformed([](auto&, auto& r) { r.mScripts.mShared = !r.mScripts.mShared; });
            malformed([](auto&, auto& r) { ++r.mRestart.mRevision; });
            malformed([](auto&, auto& r) { r.mSource.mViews.front() = {}; });
            malformed([&](auto&, auto& r) { r.mSource.mViews.front().mContainerStore = &fixture->mSource; });
            malformed([](auto&, auto& r) {
                r.mSource.mNodes.pop_front();
                r.mSource.mProposedIdentities.erase(r.mSource.mProposedIdentities.begin());
                r.mSource.mViews.erase(r.mSource.mViews.begin());
            });
            malformed([](auto&, auto& r) { r.mSource.mProposedIdentities.front() = {}; });
            // A second restore has identical values but different nodes/lifetimes.
            malformed([](auto&, auto&) {});
            const auto wrongRegistry = [&](const auto& bad) {
                reject([&](auto& target) {
                    fixture->prepareRestartScripts(decoded, *restored, content, envelope, bad, fresh, target);
                });
            };
            wrongRegistry(Fixture::RestartRegistry{});
            {
                std::unique_ptr<const RestoredPair> temporary;
                restorePair(decoded, content, temporary);
                std::unique_ptr<const Fixture::RestartRegistry> stale;
                fixture->prepareRestartRegistry(decoded, *temporary, envelope, fresh.mRegistry, stale);
                temporary.reset();
                wrongRegistry(*stale);
            }
            for (bool counter : { false, true })
            {
                auto& value = const_cast<PtrRegistry::Snapshot&>(registry->getBindings());
                const auto prior = value;
                if (counter) ++value.mLastGenerated.mIndex;
                else ++value.mRevision;
                reject([&](auto& target) {
                    fixture->prepareRestartScripts(decoded, *restored, content, envelope, *registry, fresh, target);
                }, false);
                value = prior;
                retry();
            }
            // Mutate actual bound base/configured state, including unregistered
            // nodes, while preserving the same validated registry/lifetime.
            for (const auto* inventory : { &restored->mSource, &restored->mDestination })
                for (const auto& view : inventory->mViews)
                {
                    auto* node = const_cast<LiveCellRef<ESM::Miscellaneous>*>(view.get<ESM::Miscellaneous>());
                    const auto* base = node->mBase;
                    node->mBase = nullptr;
                    // snapshot helpers require a valid base for some checks.
                    bool caught = false;
                    const auto prior = state(output);
                    try { prepare(output); }
                    catch (const std::invalid_argument&) { caught = true; }
                    node->mBase = base;
                    require(caught && state(output) == prior, "null restored base accepted or changed output");
                    ++rejections;
                    retry();
                    auto locals = std::move(node->mData.getLocals());
                    node->mData.getLocals() = locals;
                    node->mData.getLocals().mFloats.push_back(1.f);
                    reject(prepare, false);
                    node->mData.getLocals() = std::move(locals);
                    retry();
                }
            {
                auto item = fresh.mRegistry.mOther.front().second;
                auto* node = item.get<ESM::Miscellaneous>();
                const auto* base = node->mBase;
                node->mBase = content.mBases[base == content.mBases[0] ? 1 : 0];
                reject(prepare, false);
                node->mBase = base;
                retry();
                const auto originalService = fixture->mDestinationAdd.mLocalScripts;
                fixture->mDestinationAdd.mLocalScripts = nullptr;
                reject(prepare, false);
                fixture->mDestinationAdd.mLocalScripts = originalService;
                retry();
            }
            // Service order/cursor witnesses detect changes after capture even
            // though all incoming registrations would be detached.
            if (!fixture->mSourceScripts.snapshot().mEntries.empty())
            {
                fixture->mSourceScripts.startIteration();
                reject(prepare, false);
                std::pair<ESM::RefId, Ptr> ignored;
                while (fixture->mSourceScripts.getNext(ignored)) {}
                retry();
            }
            // Empty services remain distinct; clearing registrations does not
            // clear configured locals. Also preserve exhausted saved counters.
            for (int empty = 0; empty < 4; ++empty)
            {
                auto saved = decoded;
                saved.mRestart = { std::numeric_limits<size_t>::max(), { UINT32_MAX, -1 } };
                for (size_t side = 0; side < 2; ++side)
                {
                    auto& service = saved.mScripts.mServices[side];
                    if (empty & (1 << side)) service.mEntries.clear();
                    else std::reverse(service.mEntries.begin(), service.mEntries.end());
                    service.mCursor = service.mEntries.size();
                }
                std::unique_ptr<const RestoredPair> nodes;
                restorePair(saved, content, nodes);
                std::unique_ptr<const Fixture::RestartRegistry> index;
                fixture->prepareRestartRegistry(saved, *nodes, envelope, fresh.mRegistry, index);
                std::unique_ptr<const Candidate> candidate;
                fixture->prepareRestartScripts(saved, *nodes, content, envelope, *index, fresh, candidate);
                check(candidate, saved, index);
                index.reset(); // Candidate owns storage, not a registry reference.
                candidate->getDestinationStorage();
                nodes.reset();
                bool caught = false;
                try { candidate->getSourceStorage(); }
                catch (const std::invalid_argument&) { caught = true; }
                require(caught, "empty/unregistered service candidate followed destroyed inventory nodes");
                ++rejections;
            }
            verify();
            // Replacement at the exact same service address expires weak bindings.
            // Stock construction initializes an empty cursor without allocation.
            auto* service = &fixture->mSourceScripts;
            const auto* retained = output.get();
            std::destroy_at(service);
            std::construct_at(service, fixture->mSourceAdd.mStore);
            bool caught = false;
            try { prepare(output); }
            catch (const std::invalid_argument&) { caught = true; }
            require(caught && output.get() == retained && restoredState(restored) == nodesBefore
                    && pairOutputState(decoded) == decodedBefore,
                "same-address service replacement accepted stale bindings or changed inputs/output");
            ++rejections;
            const auto rebound = fixture->restartScriptBindings();
            fixture->prepareRestartScripts(decoded, *restored, content, envelope, *registry, rebound, output);
            check(output, decoded, registry);
            // A live owner cannot witness the separately destroyed stock store.
            std::destroy_at(&fixture->mSource);
            std::construct_at(&fixture->mSource);
            caught = false;
            try { output->getSourceStorage(); }
            catch (const std::invalid_argument&) { caught = true; }
            require(caught, "script candidate followed same-address store replacement");
            ++rejections;
            fixture->mSource.setPtr(rebound.mRegistry.mOwners[0], fixture->mModel);
            Misc::Rng::Generator prng{ 0 };
            fixture->mSource.fill({}, {}, prng);
            const auto rebuilt = fixture->restartScriptBindings();
            fixture->prepareRestartRegistry(decoded, *restored, envelope, rebuilt.mRegistry, registry);
            fixture->prepareRestartScripts(decoded, *restored, content, envelope, *registry, rebuilt, output);
            check(output, decoded, registry);
            fixture.reset();
            caught = false;
            try { output->getDestinationStorage(); }
            catch (const std::invalid_argument&) { caught = true; }
            require(caught, "script candidate followed destroyed fresh fixture");
            ++rejections;
            return allocations;
        }

        template <class Make, class Verify>
        size_t checkRestartRegistryCase(std::unique_ptr<DisposableTransferRehearsal>& original, Make make,
            Verify verifyOriginal, ESMStore& store, ESM::ReadersCache& readers, MWBase::ScriptManager& scripts,
            ESM::RefId ownerId, bool shared, const RestoreContent& content, size_t& rejections, bool rebuildScripts = false)
        {
            using Fixture = DisposableTransferRehearsal;
            using Candidate = Fixture::RestartRegistry;
            SerializedPair saved;
            SaveEnvelope envelope{ "OpenMW-0.51.0-test-inventory-runtime-1", { 1, 7, 19 },
                original->mSourceOwner.getPtr().getCellRef().getRefNum(),
                original->mDestinationOwner.getPtr().getCellRef().getRefNum(),
                original->mDestinationOwner.getPtr().getCellRef().getRefNum() };
            const std::array referenceIds{ content.mBases[0]->mId, content.mBases[1]->mId,
                ESM::RefId::stringRefId("serialization_owner"), ESM::RefId::stringRefId("serialization_soul"),
                ESM::RefId::stringRefId("serialization_faction"), ESM::RefId::stringRefId("serialization_key"),
                ESM::RefId::stringRefId("serialization_trap"), ESM::RefId::stringRefId("dormant_soul") };
            const SaveBindings bindings{ envelope, content, referenceIds };
            const auto oldBindings = original->restartBindings();
            const auto oldScripts = rebuildScripts ? original->restartScriptBindings() : Fixture::RestartScriptBindings{};
            const auto otherId = oldBindings.mOther.at(0).first;
            const auto otherBase = oldBindings.mOther.at(0).second.getCellRef().getRefId();
            {
                const auto prepared = make();
                MWWorld::Testing::serializePair(*original, prepared, content.mDeclarations, saved);
            }
            TransferSaveBytes bytes;
            encodeTransferSave(saved, bindings, bytes);
            verifyOriginal();
            original.reset();
            for (const auto& owner : oldBindings.mOwners)
                require(!owner.hasLiveReference(), "restart test retained the original fixture");
            SerializedPair decoded;
            decodeTransferSave(bytes, bindings, decoded);
            checkSavedValues(decoded, saved);
            std::unique_ptr<const RestoredPair> restored;
            restorePair(decoded, content, restored);
            auto fixture = std::make_unique<Fixture>(store, readers, scripts, ownerId, shared);
            // Explicit synthetic reconstruction of the unrelated store, not a
            // restart installer: its stable ID is supplied independently.
            fixture->mModel.setLastGeneratedRefNum({ otherId.mIndex - 1, -1 });
            ManualRef otherTemplate(store, otherBase);
            const auto other = *fixture->mOther.add(otherTemplate.getPtr(), 3, fixture->mOtherAdd);
            other.getCellRef() = other.getCellRef().copyWithCount(0);
            require(other.getCellRef().getRefNum() == otherId, "fresh other-store identity mismatch");
            fixture->mOther.setSelectedEnchantItem(fixture->mOther.end());
            if (rebuildScripts)
                return checkRestartScriptStorage(fixture, decoded, restored, content, envelope, oldScripts, rejections);
            const auto fresh = fixture->restartBindings();
            const auto before = snapshot(*fixture);
            const auto nodesBefore = restoredState(restored);
            const auto decodedBefore = pairOutputState(decoded);
            const auto verify = [&] {
                require(snapshot(*fixture) == before && restoredState(restored) == nodesBefore
                        && pairOutputState(decoded) == decodedBefore && !fixture->failedClosed(),
                    "restart preparation changed fixture, detached nodes or decoded metadata");
            };
            const auto check = [&](const auto& output) {
                const auto& registry = output->getBindings();
                require(registry.mRevision == decoded.mRestart.mRevision
                        && registry.mLastGenerated == decoded.mRestart.mLastGenerated,
                    "restart registry reconstructed revision or counter");
                size_t expected = 3 + fresh.mOther.size();
                const std::array inventories{ &restored->mSource, &restored->mDestination };
                const std::array stores{ &fixture->mSource, &fixture->mDestination };
                for (size_t side = 0; side < inventories.size(); ++side)
                {
                    const auto& inventory = *inventories[side];
                    expected += inventory.mNodes.size();
                    for (size_t i = 0; i < inventory.mViews.size(); ++i)
                    {
                        const auto id = inventory.mProposedIdentities[i];
                        const auto item = output->getItem(id);
                        require(item.mRef == inventory.mViews[i].mRef && item.hasLiveReference()
                                && item.mContainerStore == stores[side] && !item.mCell
                                && !item.mRef->mWorldModel && !item.getCellRef().getRefNum().isSet(),
                            "restart registry lost detached item/owner/lifetime");
                    }
                }
                for (const auto& owner : fresh.mOwners)
                    require(output->getItem(owner.getCellRef().getRefNum()) == owner,
                        "restart registry omitted a fresh owner");
                for (const auto& [id, ptr] : fresh.mOther)
                    require(output->getItem(id) == ptr && output->getItem(id).mContainerStore == &fixture->mOther,
                        "restart registry omitted dormant other-store membership");
                require(registry.mEntries.size() == expected && output->getItem({ UINT32_MAX, 0 }).isEmpty(),
                    "restart registry membership incomplete or extra");
            };
            const auto prepare = [&](auto& output) {
                fixture->prepareRestartRegistry(decoded, *restored, envelope, fresh, output);
            };
            const auto makeOutput = [&] {
                std::unique_ptr<const Candidate> output;
                prepare(output);
                return output;
            };
            const auto candidateState = [](const auto& output) {
                std::vector<std::tuple<const void*, ConstPtr>> nodes;
                for (const auto& [id, binding] : output->getBindings().mEntries)
                    nodes.emplace_back(&binding, output->getItem(id));
                return std::tuple{ output.get(), output->getBindings(), nodes };
            };
            size_t allocations = checkSerializationAllocations(
                makeOutput, prepare, candidateState, check, verify);
            // Arm the first ordinal after staging: publication and full candidate
            // retirement must not introduce another allocation.
            {
                auto output = makeOutput();
                Allocations::Trace trace;
                {
                    Allocations::Observe observe(trace, allocations + 1);
                    prepare(output);
                    output.reset();
                }
                require(trace.mTotal == allocations && trace.mFailures == 0 && trace.mOutstanding == 0
                        && trace.allocations(Allocations::Phase::Validation) == 0
                        && trace.allocations(Allocations::Phase::Publication) == 0,
                    "restart validation/publication/cleanup allocated");
            }
            auto output = makeOutput();
            const auto retry = [&] { prepare(output); check(output); verify(); };
            const auto reject = [&](const RestoredPair& input, const SaveEnvelope& e,
                                    const Fixture::RestartBindings& supplied, bool retryNow = true,
                                    const SerializedPair* inputSave = nullptr) {
                const auto prior = candidateState(output);
                const auto fixtureBefore = snapshot(*fixture);
                // Inputs are owned test values; compare serialization/storage via
                // the calling mutation checks as well as every retained view.
                Allocations::Trace trace;
                bool caught = false;
                {
                    Allocations::Observe observe(trace);
                    try
                    {
                        fixture->prepareRestartRegistry(inputSave ? *inputSave : decoded, input, e, supplied, output);
                    }
                    catch (const std::invalid_argument&)
                    {
                        caught = true;
                    }
                }
                require(caught && trace.mOutstanding == 0 && trace.mTrackingOverflow == 0
                        && trace.allocations(Allocations::Phase::Preparation) == 0
                        && trace.allocations(Allocations::Phase::Publication) == 0,
                    "invalid restart accepted, staged before validation or leaked");
                require(candidateState(output) == prior && snapshot(*fixture) == fixtureBefore,
                    "rejected restart changed caller candidate or fixture");
                for (size_t ordinal = 1; ordinal <= trace.mTotal; ++ordinal)
                {
                    Allocations::Trace failure;
                    caught = false;
                    {
                        Allocations::Observe observe(failure, ordinal);
                        try
                        {
                            fixture->prepareRestartRegistry(inputSave ? *inputSave : decoded, input, e, supplied, output);
                        }
                        catch (const std::bad_alloc&)
                        {
                            caught = true;
                        }
                    }
                    require(caught && failure.mFailures == 1 && failure.mOutstanding == 0
                            && candidateState(output) == prior && snapshot(*fixture) == fixtureBefore,
                        "restart rejection allocation changed output/fixture or leaked");
                }
                allocations += trace.mTotal;
                ++rejections;
                if (retryNow)
                    retry();
            };
            reject(*restored, envelope, oldBindings);
            const auto badBindings = [&](auto mutate) {
                auto bad = fresh;
                mutate(bad);
                reject(*restored, envelope, bad);
            };
            for (size_t side = 0; side < 3; ++side)
            {
                badBindings([&](auto& b) { b.mOwners[side] = {}; });
                badBindings([&](auto& b) { b.mOwners[side] = fresh.mOwners[(side + 1) % 3]; });
                badBindings([&](auto& b) { b.mStores[side] = fresh.mStores[(side + 1) % 3]; });
                if (oldBindings.mStores[side] != fresh.mStores[side])
                    badBindings([&](auto& b) { b.mStores[side] = oldBindings.mStores[side]; });
                badBindings([&](auto& b) { b.mStorage[side] = oldBindings.mStorage[side]; });
                badBindings([&](auto& b) { b.mLifetimes[side] = {}; });
                badBindings([&](auto& b) { b.mLifetimes[side] = oldBindings.mLifetimes[side]; });
                badBindings([&](auto& b) { b.mLifetimes[side] = fresh.mLifetimes[(side + 1) % 3]; });
            }
            badBindings([](auto& b) { b.mOther.clear(); });
            badBindings([](auto& b) { b.mOther.push_back(b.mOther.front()); });
            badBindings([](auto& b) { b.mOther.resize(1025); });
            badBindings([](auto& b) { b.mOther[0].second = {}; });
            badBindings([&](auto& b) { b.mOther[0].second = oldBindings.mOther[0].second; });
            badBindings([&](auto& b) { b.mOther[0].second = fresh.mOwners[0]; });
            badBindings([&](auto& b) { b.mOther[0].second.mContainerStore = &fixture->mSource; });
            badBindings([](auto& b) { ++b.mOther[0].first.mIndex; });
            for (auto member : { &SaveEnvelope::mSourceOwner, &SaveEnvelope::mDestinationOwner,
                     &SaveEnvelope::mInitiator })
            {
                auto bad = envelope;
                bad.*member = { UINT32_MAX, -1 };
                reject(*restored, bad, fresh);
            }
            const auto badRestored = [&](auto mutate, bool matchSave = true) {
                std::unique_ptr<const RestoredPair> bad;
                restorePair(decoded, content, bad);
                mutate(const_cast<RestoredPair&>(*bad));
                const auto state = restoredState(bad);
                auto input = decoded;
                if (matchSave)
                {
                    input.mRestart = bad->mRestart;
                    input.mSource.mProposedIdentities = bad->mSource.mProposedIdentities;
                    input.mDestination.mProposedIdentities = bad->mDestination.mProposedIdentities;
                }
                const auto inputBefore = pairOutputState(input);
                reject(*bad, envelope, fresh, true, &input);
                require(pairOutputState(input) == inputBefore, "rejected restart changed decoded input/storage");
                require(restoredState(bad) == state, "rejected restart changed detached input/storage");
            };
            badRestored([](auto& b) { ++b.mRestart.mRevision; }, false);
            badRestored([](auto& b) { ++b.mRestart.mLastGenerated.mIndex; }, false);
            for (auto value : invalidRestartValues(decoded.mRestart))
                badRestored([&](auto& b) { b.mRestart = value; });
            for (bool source : { true, false })
            {
                const auto mutate = [&](auto change) {
                    badRestored([&](auto& b) { change(source ? b.mSource : b.mDestination); });
                };
                mutate([](auto& b) { b.mProposedIdentities.clear(); });
                mutate([](auto& b) { b.mNodes.clear(); b.mProposedIdentities.clear(); b.mViews.clear(); });
                mutate([](auto& b) { b.mProposedIdentities.resize(1025); });
                mutate([](auto& b) { b.mViews.clear(); });
                mutate([](auto& b) { b.mViews[0] = {}; });
                mutate([&](auto& b) { b.mViews[0] = oldBindings.mOwners[0]; });
                mutate([&](auto& b) { b.mViews[0] = fresh.mOwners[0]; });
                mutate([&](auto& b) { b.mViews[0].mContainerStore = &fixture->mSource; });
                mutate([](auto& b) { b.mProposedIdentities[0] = {}; });
                mutate([](auto& b) { b.mProposedIdentities[0].mContentFile = -2; });
                mutate([&](auto& b) { b.mProposedIdentities[0] = envelope.mSourceOwner; });
                mutate([&](auto& b) { b.mProposedIdentities[0] = otherId; });
                mutate([&](auto& b) { b.mProposedIdentities[0] = { decoded.mRestart.mLastGenerated.mIndex + 1, -1 }; });
                mutate([](auto& b) { b.mNodes.front().mRef.setRefNum({ 42, -1 }); });
                mutate([&](auto& b) { b.mNodes.front().mWorldModel = &fixture->mModel; });
                mutate([](auto& b) {
                    while (b.mNodes.size() <= 1024)
                        b.mNodes.emplace_back(ESM::makeBlankCellRef(), b.mNodes.front().mBase);
                });
            }
            badRestored([](auto& b) { b.mDestination.mProposedIdentities[0] = b.mSource.mProposedIdentities[0]; });
            badRestored([](auto& b) { b.mSource.mProposedIdentities[1] = b.mSource.mProposedIdentities[0]; });
            badRestored([](auto& b) { std::swap(b.mSource, b.mDestination); }, false);
            for (const auto& ptr : { fresh.mOwners[0], other })
            {
                const auto id = ptr.getCellRef().getRefNum();
                ptr.getCellRef().setRefNum({ decoded.mRestart.mLastGenerated.mIndex + 1, -1 });
                reject(*restored, envelope, fresh, false);
                ptr.getCellRef().setRefNum(id);
                retry();
                ptr.mRef->mWorldModel = nullptr;
                reject(*restored, envelope, fresh, false);
                ptr.mRef->mWorldModel = &fixture->mModel;
                retry();
            }
            fixture->mOther.setPtr(fresh.mOwners[0], fixture->mModel);
            reject(*restored, envelope, fresh, false);
            fixture->mOther.setPtr(fresh.mOwners[2], fixture->mModel);
            retry();
            // A live but incomplete foreign mapping is also a rejection, without
            // relying on iteration over potentially stale registry values.
            {
                ManualRef unexpected(store, otherBase);
                fixture->mModel.registerPtr(unexpected.getPtr());
                reject(*restored, envelope, fresh, false);
            }
            prepare(output);
            check(output);
            require(restoredState(restored) == nodesBefore, "retry changed detached nodes");
            // Registry mutations above legitimately change only the fixture's
            // local revision/counter; use a separate fresh fixture for remaining
            // lifetime checks rather than resetting that state in production code.
            fixture.reset();
            bool expired = false;
            try
            {
                output->getItem(envelope.mSourceOwner);
            }
            catch (const std::invalid_argument&)
            {
                expired = true;
            }
            require(expired, "restart candidate followed a destroyed fixture");
            output.reset();
            // Counters beyond *all* survivors and representable maxima are data,
            // even when there is no next usable command revision or generated ID.
            fixture = std::make_unique<Fixture>(store, readers, scripts, ownerId, shared);
            fixture->mModel.setLastGeneratedRefNum({ otherId.mIndex - 1, -1 });
            fixture->mOther.add(otherTemplate.getPtr(), 3, fixture->mOtherAdd);
            const auto rebound = fixture->restartBindings();
            for (int empty = 1; empty <= 3; ++empty)
            {
                auto input = decoded;
                if (empty & 1)
                    input.mSource = {};
                if (empty & 2)
                    input.mDestination = {};
                for (auto& service : input.mScripts.mServices)
                {
                    std::erase_if(service.mEntries, [&](const auto& entry) {
                        for (size_t side = 0; side < 2; ++side)
                        {
                            const auto& ids = (side == 0 ? decoded.mSource : decoded.mDestination).mProposedIdentities;
                            if ((empty & (1 << side))
                                && std::find(ids.begin(), ids.end(), entry.mIdentity) != ids.end())
                                return true;
                        }
                        return false;
                    });
                    service.mCursor = service.mEntries.size();
                }
                TransferSaveBytes encoded;
                encodeTransferSave(input, bindings, encoded);
                decodeTransferSave(encoded, bindings, input);
                restorePair(input, content, restored);
                fixture->prepareRestartRegistry(input, *restored, envelope, rebound, output);
                require(output->getBindings().mEntries.size() == 3 + rebound.mOther.size()
                        + input.mSource.mObjects.size() + input.mDestination.mObjects.size(),
                    "restart registry mishandled an empty inventory");
            }
            for (const auto metadata : { TransferRestartMetadata{ decoded.mRestart.mRevision + 100,
                                            { decoded.mRestart.mLastGenerated.mIndex + 100, -1 } },
                     TransferRestartMetadata{ std::numeric_limits<size_t>::max(), { UINT32_MAX, -1 } } })
            {
                auto input = decoded;
                input.mRestart = metadata;
                TransferSaveBytes encoded;
                encodeTransferSave(input, bindings, encoded);
                decodeTransferSave(encoded, bindings, input);
                restorePair(input, content, restored);
                fixture->prepareRestartRegistry(input, *restored, envelope, rebound, output);
                require(output->getBindings().mRevision == metadata.mRevision
                        && output->getBindings().mLastGenerated == metadata.mLastGenerated,
                    "restart registry reset a counter/revision beyond surviving nodes");
            }
            const auto detachedId = restored->mSource.mProposedIdentities.front();
            restored.reset();
            expired = false;
            try
            {
                output->getItem(detachedId);
            }
            catch (const std::invalid_argument&)
            {
                expired = true;
            }
            require(expired, "restart candidate followed destroyed detached nodes");
            return allocations;
        }
    }

    enum class AllocationCheck
    {
        None,
        Rehearsal,
        Preparation,
        Serialization,
        ObjectState,
        LocalsRestore,
        Restore,
        RestartRegistry,
        RestartScripts,
        ScriptMetadata,
        Codec,
        FileSink,
        Command,
        Commit
    };

    static void checkTransferRehearsalCases(
        const ESMStore& content, AllocationCheck allocationCheck, const std::filesystem::path& scratch = {})
    {
        using Rehearsal = DisposableTransferRehearsal;
        using Stage = Rehearsal::Stage;
        using Pair = PreparedContainerTransfer;
        const bool localsRestore = allocationCheck == AllocationCheck::LocalsRestore;
        const bool fileSink = allocationCheck == AllocationCheck::FileSink;
        const bool command = allocationCheck == AllocationCheck::Command;
        const bool codec = allocationCheck == AllocationCheck::Codec || fileSink || command;
        const bool commit = allocationCheck == AllocationCheck::Commit || codec;
        const bool restartRegistry = allocationCheck == AllocationCheck::RestartRegistry;
        const bool restartScripts = allocationCheck == AllocationCheck::RestartScripts;
        const bool scriptMetadata = allocationCheck == AllocationCheck::ScriptMetadata;
        const bool inventoryRestore
            = allocationCheck == AllocationCheck::Restore || restartRegistry || restartScripts || scriptMetadata || commit;
        const bool serialization = allocationCheck == AllocationCheck::Serialization
            || allocationCheck == AllocationCheck::ObjectState || localsRestore || inventoryRestore;
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
        size_t cases = 0, restoredLocals = 0, localRejections = 0, codecAllocations = 0, codecRejections = 0;
        Allocations::Trace totals;
        FileEvidence fileEvidence;
        CommandEvidence commandEvidence;
        FileFault fileFault = FileFault::None;
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
                            const auto run = [&](size_t commitFailAt, size_t commitAllocations) -> size_t {
                                ConsumerCopyTrace copies;
                                auto fixtureOwner
                                    = std::make_unique<Rehearsal>(store, readers, scripts, ownerId, shared);
                                auto& fixture = *fixtureOwner;
                                if (command && cursorPosition == 1)
                                    fixture.mDestinationAdd.mPlayer = fixture.mSourceOwner.getPtr();
                                if (allocationCheck == AllocationCheck::Preparation)
                                    fixture.mRemoval.mInventoryUpdated
                                        = AllocatingConsumer(copies, fixture.mNotifications);
                                auto& source = fixture.mSource;
                                auto& destination = fixture.mDestination;
                                const auto item = *source.add(
                                    scriptedItem ? scripted.getPtr() : plain.getPtr(), 4, fixture.mSourceAdd);
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
                                    const auto selected
                                        = destination.add(dormantItem.getPtr(), 2, fixture.mDestinationAdd);
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
                                if (scriptMetadata || restartScripts)
                                {
                                    const auto registered = *source.add(scripted.getPtr(), 1, fixture.mSourceAdd);
                                    registered.getCellRef() = registered.getCellRef().copyWithCount(0);
                                    const auto unregistered = *source.add(scripted.getPtr(), 1, fixture.mSourceAdd);
                                    fixture.mSourceScripts.remove(unregistered);
                                    const auto destinationUnregistered
                                        = *destination.add(scripted.getPtr(), 1, fixture.mDestinationAdd);
                                    fixture.mDestinationAdd.mLocalScripts->remove(destinationUnregistered);
                                }
                                if (serialization)
                                {
                                    const auto decorate = [&](const Ptr& ptr) {
                                        if (allocationCheck == AllocationCheck::ObjectState || localsRestore
                                            || inventoryRestore)
                                        {
                                            auto ref = decoratedCellRef(ptr.getCellRef());
                                            if (codec && cursorPosition != 0)
                                            {
                                                ref.mScale = cursorPosition == 1 ? 3.5f : 0.25f;
                                                ref.mIsLocked = false;
                                                ref.mLockLevel = -42;
                                                ref.mTeleport = false;
                                                ref.mDoorDest.pos[0] = -0.f;
                                                ref.mDestCell.clear();
                                                ref.mGlobalVariable.clear();
                                                ref.mChargeIntRemainder = -0.f;
                                            }
                                            ptr.getCellRef() = CellRef(ref);
                                        }
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
                                        if (inventoryRestore && cursorPosition != 0)
                                        {
                                            data.setPosition(
                                                { { -0.f, std::numeric_limits<float>::denorm_min(), 300.f },
                                                    { 0.f, -0.f, std::numeric_limits<float>::lowest() } });
                                            if (cursorPosition == 99)
                                            {
                                                data.enable();
                                                data.activateByScript();
                                                ptr.getCellRef()
                                                    = ptr.getCellRef().copyWithCount(-ptr.getCellRef().getCount(false));
                                            }
                                            else
                                                data.onActivate();
                                            if (!locals.getScriptId().empty())
                                            {
                                                locals.mShorts = { -32768, 32767 };
                                                locals.mLongs = { std::numeric_limits<int32_t>::min(),
                                                    std::numeric_limits<int32_t>::max() };
                                                locals.mFloats = cursorPosition == 99
                                                    ? std::vector<float>{ std::numeric_limits<float>::lowest(),
                                                          std::numeric_limits<float>::max() }
                                                    : std::vector<float>{ -0.f,
                                                          std::numeric_limits<float>::denorm_min() };
                                            }
                                        }
                                    };
                                    decorate(item);
                                    decorate(dormant);
                                    if (scriptMetadata || restartScripts)
                                        for (const auto& node : fixture.sourceStorage())
                                            if (&node != item.mRef && &node != dormant.mRef)
                                                decorate(fixture.mModel.getPtr(node.mRef.getRefNum()));
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
                                if (inventoryRestore)
                                {
                                    // Generate and retire an actual registry node.
                                    // Stacking consumes no ID: its accepted counter
                                    // must remain beyond all surviving item IDs.
                                    {
                                        ManualRef retired(store, plainId);
                                        fixture.mModel.registerPtr(retired.getPtr());
                                    }
                                    const auto counter = fixture.mModel.getLastGeneratedRefNum();
                                    require(fixture.mModel.getPtr(counter).isEmpty(), "retired ID still registered");
                                    for (const auto* storage : { &fixture.sourceStorage(), &fixture.destinationStorage(),
                                             &fixture.otherStorage() })
                                        for (const auto& node : *storage)
                                            require(node.mRef.getRefNum().mIndex < counter.mIndex,
                                                "restart fixture lacks counter beyond surviving IDs");
                                }
                                const std::array supplied{ ContainerStoreResolution(
                                    fixture.mOther, fixture.mOtherOwner.getPtr()) };
                                const auto make = [&] {
                                    return source.prepareTransfer(item, quantity, destination, fixture.mRemoval,
                                        fixture.mDestinationAdd, supplied);
                                };
                                const auto before = snapshot(fixture);
                                const auto unrelated = nodeState(*other);
                                const auto verifyOriginal = [&] {
                                    require(snapshot(fixture) == before,
                                        "transfer changed original nodes/state/caches/cursors");
                                    require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                        "transfer emitted notifications/scripts or changed unrelated live state");
                                };
                                if (scriptMetadata)
                                {
                                    const std::array bases{ store.get<ESM::Miscellaneous>().find(plainId),
                                        store.get<ESM::Miscellaneous>().find(scriptedId) };
                                    totals.mTotal += checkScriptMetadataCase(fixtureOwner, make, verifyOriginal,
                                        { bases, script, scripts.getLocals(scriptId) }, localRejections);
                                    require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                        "script metadata affected independent fixture/listener/scripts");
                                    ++cases;
                                    return 0;
                                }
                                if (restartRegistry || restartScripts)
                                {
                                    const std::array bases{ store.get<ESM::Miscellaneous>().find(plainId),
                                        store.get<ESM::Miscellaneous>().find(scriptedId) };
                                    totals.mTotal += checkRestartRegistryCase(fixtureOwner, make, verifyOriginal,
                                        store, readers, scripts, ownerId, shared,
                                        { bases, script, scripts.getLocals(scriptId) }, localRejections, restartScripts);
                                    require(snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                        "restart registry affected independent fixture/listener/scripts");
                                    ++cases;
                                    return 0;
                                }
                                if (commit)
                                {
                                    const auto verifyUnrelated = [&] {
                                        require(
                                            snapshot(live) == liveBefore && listener.mCalls == 0 && scripts.mRuns == 0,
                                            "commit affected independent world/listener/scripts");
                                    };
                                    const auto incomplete = [&] {
                                        return source.prepareTransfer(
                                            item, quantity, destination, fixture.mRemoval, fixture.mDestinationAdd);
                                    };
                                    // Check the actual gameplay quantity independently of the serializer.
                                    {
                                        const auto pair = make();
                                        const int sign = cursorPosition == 99 ? -1 : 1;
                                        require(
                                            pair.getSourceItem().getCellRef().getCount(false) == sign * (4 - quantity)
                                                && pair.getDestinationItem().getCellRef().getCount(false)
                                                    == (!scriptedItem && stack ? sign * (7 + quantity) : quantity),
                                            "commit prepared wrong full/partial removal or stack result");
                                    }
                                    const std::array bases{ store.get<ESM::Miscellaneous>().find(plainId),
                                        store.get<ESM::Miscellaneous>().find(scriptedId) };
                                    if (command)
                                        return checkCommandCase(fixtureOwner, make, verifyOriginal, verifyUnrelated,
                                            { bases, script, scripts.getLocals(scriptId) }, scratch, item, quantity,
                                            commitFailAt, commitAllocations, fileFault, commandEvidence);
                                    if (fileSink)
                                        return checkFileCommitCase(fixtureOwner, make, verifyOriginal, verifyUnrelated,
                                            { bases, script, scripts.getLocals(scriptId) }, scratch, commitFailAt,
                                            commitAllocations, fileFault,
                                            !shared && scriptedItem && !stack && quantity == 1 && cursorPosition == 0,
                                            fileEvidence);
                                    return checkCommitCase(fixtureOwner, make, incomplete, verifyOriginal,
                                        verifyUnrelated, { bases, script, scripts.getLocals(scriptId) }, commitFailAt,
                                        commitAllocations, codec,
                                        !shared && scriptedItem && !stack && quantity == 1 && cursorPosition == 0,
                                        codecAllocations, codecRejections);
                                }
                                if (serialization)
                                {
                                    const auto run = [&]<bool ObjectStates>() {
                                        const auto& declarations = scripts.getLocals(scriptId);
                                        std::vector<ConstPtr> nodes;
                                        std::optional<RestoredPair> expected;
                                        auto output = pairOutputSentinel<ObjectStates>();
                                        std::optional<decltype(pairOutputState(output))> retained;
                                        {
                                            auto pair = make();
                                            nodes = ownedNodes(pair);
                                            const int sign = inventoryRestore && cursorPosition == 99 ? -1 : 1;
                                            require(pair.getSourceItem().getCellRef().getCount(false)
                                                        == sign * (4 - quantity)
                                                    && pair.getDestinationItem().getCellRef().getCount(false)
                                                        == (!scriptedItem && stack ? sign * (7 + quantity) : quantity),
                                                "serialization fixture lost full/partial removal or destination count");
                                            const bool malformed = !shared && scriptedItem && !stack && quantity == 1
                                                && cursorPosition == 0;
                                            if (!localsRestore && !inventoryRestore)
                                                totals.mTotal += checkPairSerialization<ObjectStates>(
                                                    fixture, pair, declarations, verifyOriginal, malformed);
                                            // Rehearsal must retain the same read-only owned values
                                            // and protected bindings for another serialization.
                                            pair = fixture.rehearse(std::move(pair));
                                            serializePair(fixture, pair, declarations, output);
                                            checkPairOutput(pair, output);
                                            if (inventoryRestore)
                                                expected.emplace(expectedRestoration(pair));
                                            require(ownedNodes(pair) == nodes,
                                                "serialization/rehearsal replaced owned nodes");
                                            retained = pairOutputState(output);
                                        }
                                        requireDiscarded(nodes);
                                        // Owned values and proposed identities outlive the pair.
                                        require(
                                            pairOutputState(output) == *retained, "discard invalidated owned output");
                                        verifyOriginal();
                                        if constexpr (ObjectStates)
                                        {
                                            if (inventoryRestore)
                                            {
                                                const std::array bases{ store.get<ESM::Miscellaneous>().find(plainId),
                                                    store.get<ESM::Miscellaneous>().find(scriptedId) };
                                                totals.mTotal += checkInventoryRestore(output,
                                                    { bases, script, declarations }, *expected, verifyOriginal,
                                                    !shared && scriptedItem && !stack && quantity == 1
                                                        && cursorPosition == 0,
                                                    localRejections);
                                                requireDiscarded(nodes);
                                            }
                                            if (localsRestore)
                                            {
                                                const auto verifyOwned = [&] {
                                                    require(pairOutputState(output) == *retained,
                                                        "locals restoration changed caller-owned serialized pair");
                                                    verifyOriginal();
                                                };
                                                // Read only retained owned serialized values, after
                                                // pair destruction; never follow a key or resolve objects.
                                                for (const auto* inventory : { &output.mSource, &output.mDestination })
                                                    for (const auto& object : inventory->mObjects)
                                                    {
                                                        if (!object.mHasLocals)
                                                            continue;
                                                        MWScript::Locals configured;
                                                        require(configured.configure(script, scripts),
                                                            "locals restore fixture did not configure");
                                                        totals.mTotal += checkLocalRestore(configured, object.mLocals,
                                                            declarations, verifyOwned, localRejections == 0,
                                                            localRejections);
                                                        ++restoredLocals;
                                                    }
                                            }
                                        }
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
                                    if (allocationCheck == AllocationCheck::ObjectState || localsRestore
                                        || inventoryRestore)
                                        run.template operator()<true>();
                                    else
                                        run.template operator()<false>();
                                    ++cases;
                                    return 0;
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
                                        std::cerr << "Allocation fixture: shared=" << shared
                                                  << " scripted=" << scriptedItem << " existing=" << stack
                                                  << " quantity=" << quantity << " cursor=" << cursorPosition << '\n';
                                        throw;
                                    }
                                    ++cases;
                                    totals.mTotal += trace.mTotal;
                                    totals.mPeakOutstanding = std::max(totals.mPeakOutstanding, trace.mPeakOutstanding);
                                    for (size_t i = 0; i < totals.mAllocations.size(); ++i)
                                        totals.mAllocations[i] += trace.mAllocations[i];
                                    return 0;
                                }
                                auto decision = make();
                                require(
                                    decision.getResolutionCompleteness().isComplete(), "rehearsal fixture incomplete");
                                const auto* bindings = &decision.getIteratorBindings();
                                const auto expectedRegistry = decision.getRelocation().mRegistry;
                                const auto expectedSourceScripts = decision.getRelocation().mSourceScripts;
                                const auto expectedDestinationScripts
                                    = shared ? expectedSourceScripts : *decision.getRelocation().mDestinationScripts;
                                const auto expectedSource = decision.getRelocation().mSource;
                                const auto expectedDestination = decision.getRelocation().mDestination;
                                const auto expectedSourceSelection = decision.getRelocation().mSourceSelection;
                                const auto expectedDestinationSelection
                                    = decision.getRelocation().mDestinationSelection;
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
                                    require(
                                        failed && !owned.hasLiveReference(), "failed rehearsal retained consumed pair");
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
                                        rejected
                                            = std::string_view(error.what()).find(reason) != std::string_view::npos;
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
                                    auto foreign = live.mSource.prepareTransfer(*live.mSource.begin(), 1,
                                        live.mDestination, live.mRemoval, live.mDestinationAdd, liveSupplied);
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
                                return 0;
                            };
                            if (commit)
                            {
                                const size_t allocations = run(0, 0);
                                ++cases;
                                totals.mTotal += allocations;
                                if (fileSink || command)
                                {
                                    for (auto fault : { FileFault::ReplaceError, FileFault::AfterReplace,
                                             FileFault::Barrier, FileFault::ReadOpen, FileFault::ReadSize,
                                             FileFault::Read, FileFault::ReadEof, FileFault::ReadClose })
                                    {
                                        fileFault = fault;
                                        run(0, 0);
                                    }
                                    fileFault = FileFault::None;
                                    // A representative scripted fixture exhausts all
                                    // observed ordinals; every matrix case exercises I/O.
                                    if (shared || (!command && !scriptedItem) || stack || quantity != 1 || cursorPosition != 0)
                                    {
                                        // Complete successful cleanup is observed in
                                        // every case, with the next ordinal armed.
                                        run(allocations + 1, allocations);
                                        continue;
                                    }
                                    if (command)
                                        commandEvidence.mAllocations += allocations;
                                    else
                                        fileEvidence.mAllocations += allocations;
                                }
                                for (size_t failAt = 1; failAt <= allocations + 1; ++failAt)
                                {
                                    try
                                    {
                                        run(failAt, allocations);
                                    }
                                    catch (...)
                                    {
                                        std::cerr << "Commit fixture: shared=" << shared << " scripted=" << scriptedItem
                                                  << " existing=" << stack << " quantity=" << quantity
                                                  << " cursor=" << cursorPosition << " ordinal=" << failAt << '/'
                                                  << allocations << '\n';
                                        throw;
                                    }
                                }
                            }
                            else
                                run(0, 0);
                        }
        if (allocationFailures)
        {
            require(cases == 48, "allocation failure matrix lost a fixture combination");
            if (command)
            {
                require(commandEvidence.mResultFailures == 2, "command result allocation failure coverage missing");
                std::cout << "Inventory command: cases=" << cases << " safe-rejections=" << commandEvidence.mRejected
                          << " stale/repeated=" << commandEvidence.mRepeated
                          << " fail-closed-outcomes=" << commandEvidence.mUncertain
                          << " allocation-failures=" << commandEvidence.mAllocations
                          << " result-allocation-failures=" << commandEvidence.mResultFailures
                          << " installation=0 retirement=0 publication=0 remaining-after-cleanup=0\n";
                return;
            }
            if (fileSink)
            {
                std::cout << "Inventory file sink: cases=" << cases
                          << " safe-file-rejections=" << fileEvidence.mRejected
                          << " fail-closed-outcomes=" << fileEvidence.mUncertain
                          << " invalid-file-rejections=" << fileEvidence.mMalformed
                          << " observed-allocation-failures=" << fileEvidence.mAllocations
                          << " installation=0 retirement=0 remaining-after-cleanup=0\n";
                return;
            }
            if (codec)
            {
                require(codecRejections > 0 && codecAllocations > 0, "codec coverage missing");
                std::cout << "Inventory binary codec: cases=" << cases
                          << " commit-allocation-failures=" << totals.mTotal
                          << " encoding/decoding-allocation-failures=" << codecAllocations
                          << " malformed-rejections=" << codecRejections
                          << " installation=0 retirement=0 remaining-after-cleanup=0\n";
                return;
            }
            if (commit)
            {
                std::cout << "Persistence-gated fixture commit: cases=" << cases
                          << " individually-failed=" << totals.mTotal
                          << " installation=0 retirement=0 remaining-after-cleanup=0"
                             " sink-decline/throw/bad-alloc=144 consumed/stale/incomplete-rejections=144\n";
                return;
            }
            if (scriptMetadata)
            {
                require(localRejections > 0 && totals.mTotal > 0, "script metadata coverage missing");
                std::cout << "Restart script metadata: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " malformed-rejections=" << localRejections
                          << " remaining-after-cleanup=0 fresh-decode-after-fixture-destruction=48\n";
                return;
            }
            if (restartScripts)
            {
                require(localRejections > 0 && totals.mTotal > 0, "restart script coverage missing");
                std::cout << "Detached restart scripts: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " malformed/stale-rejections=" << localRejections
                          << " validation=0 publication=0 remaining-after-cleanup=0 fresh-fixture-reconstruction=48\n";
                return;
            }
            if (restartRegistry)
            {
                require(localRejections > 0 && totals.mTotal > 0, "restart registry coverage missing");
                std::cout << "Detached restart registry: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " malformed/stale-rejections=" << localRejections
                          << " validation=0 publication=0 remaining-after-cleanup=0\n";
                return;
            }
            if (inventoryRestore)
            {
                require(localRejections > 0, "inventory restoration rejection coverage missing");
                std::cout << "Inventory restoration: cases=" << cases << " individually-failed=" << totals.mTotal
                          << " malformed-rejections=" << localRejections
                          << " remaining-after-cleanup=0 incomplete-pairs=48\n";
                return;
            }
            if (localsRestore)
            {
                require(restoredLocals > 0 && localRejections > 0, "locals restore coverage missing");
                std::cout << "Locals restoration: cases=" << cases << " restored-locals=" << restoredLocals
                          << " individually-failed=" << totals.mTotal << " malformed-rejections=" << localRejections
                          << " remaining-after-cleanup=0 incomplete-pairs=48\n";
                return;
            }
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

    void checkTransferLocalsRestore(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::LocalsRestore);
    }

    void checkTransferCodec(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Codec);
    }

    void checkTransferFileSink(const ESMStore& content, const std::filesystem::path& scratch)
    {
        // Refuse reuse of an existing directory. Cleanup only this owned leaf;
        // the caller's other scratch fixtures/content are never removed.
        require(std::filesystem::create_directory(scratch), "file sink scratch directory already exists");
        struct Cleanup
        {
            const std::filesystem::path& mPath;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(mPath, ignored);
            }
        } cleanup{ scratch };
        checkTransferRehearsalCases(content, AllocationCheck::FileSink, scratch);
        require(std::filesystem::remove(scratch / "inventory.bin") && std::filesystem::is_empty(scratch),
            "file sink test left unowned staging files");
    }

    void checkTransferCommand(const ESMStore& content, const std::filesystem::path& scratch)
    {
        require(std::filesystem::create_directory(scratch), "command scratch directory already exists");
        struct Cleanup
        {
            const std::filesystem::path& mPath;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(mPath, ignored);
            }
        } cleanup{ scratch };
        checkTransferRehearsalCases(content, AllocationCheck::Command, scratch);
        require(std::filesystem::remove(scratch / "inventory.bin") && std::filesystem::is_empty(scratch),
            "command test left staging files");
    }

    void checkTransferCommit(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Commit);
    }

    void checkTransferRestore(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Restore);
    }

    void checkTransferRestartRegistry(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::RestartRegistry);
    }

    void checkTransferScriptMetadata(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::ScriptMetadata);
    }

    void checkTransferRestartScripts(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::RestartScripts);
    }
}
