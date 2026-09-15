#include "test_allocations.hpp"
#include "transfer_rehearsal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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

        template <class Identity>
        void serializeInventory(const PreparedContainerTransfer::MiscList& storage, Identity identity,
            const Compiler::Locals& declarations, SerializedInventory& inventory)
        {
            inventory.mObjects.reserve(storage.size());
            inventory.mProposedIdentities.reserve(storage.size());
            size_t i = 0;
            for (const auto& node : storage)
            {
                inventory.mProposedIdentities.push_back(identity(i++));
                inventory.mObjects.emplace_back();
                auto& object = inventory.mObjects.back();
                object.blank();
                node.mRef.writeState(object);
                node.mData.write(object, declarations);
                object.mHasCustomState = false;
            }
        }

        void serializePair(const DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
            const Compiler::Locals& declarations, SerializedPair& output)
        {
            if (!fixture.mSource.validateTransfer(pair, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd)
                    .isComplete())
                throw std::invalid_argument("ObjectState serialization requires complete resolution");
            SerializedPair staged;
            const auto serialize = [&](const auto& storage, const auto& views, SerializedInventory& inventory) {
                serializeInventory(
                    storage,
                    [&](size_t i) {
                        const auto id = views.at(i).mIdentity;
                        return id.isSet() ? id : pair.getDestinationIdentity();
                    },
                    declarations, inventory);
            };
            serialize(pair.getSourceStorage(), pair.getRelocation().mSource, staged.mSource);
            serialize(pair.getDestinationStorage(), pair.getRelocation().mDestination, staged.mDestination);
            output.mSource.swap(staged.mSource);
            output.mDestination.swap(staged.mDestination);
        }

        // Disposable test-only composition, with exactly the stock MISC list type.
        // Published nodes are read-only and have no WorldModel or assigned identity.
        // Supplied records outlive this storage. No registry/script-service rebuild,
        // selection installation, durability or notification is implied.
        struct RestoredInventory
        {
            PreparedContainerTransfer::MiscList mNodes;
            std::vector<ESM::RefNum> mProposedIdentities;
        };

        struct RestoredPair
        {
            RestoredInventory mSource, mDestination;
        };

        struct RestoreContent
        {
            std::span<const ESM::Miscellaneous* const> mBases;
            const ESM::Script& mScript;
            const Compiler::Locals& mDeclarations;
        };

        void validateText(std::string_view text, bool optional = true)
        {
            if ((!optional && text.empty()) || text.size() > 4096 || text.find('\0') != std::string_view::npos)
                throw std::invalid_argument("Invalid detached inventory name");
        }

        void validateId(const ESM::RefId& id, bool optional = true)
        {
            if (optional && id.empty())
                return;
            if (!id.is<ESM::StringRefId>())
                throw std::invalid_argument("Detached inventory requires TES3 string IDs");
            validateText(id.getRefIdString(), false);
        }

        const ESM::Miscellaneous& suppliedBase(const ESM::RefId& id, const RestoreContent& content)
        {
            // Only search the explicit base-record collection, never a world/store.
            for (const auto* base : content.mBases)
                if (base->mId == id)
                    return *base;
            throw std::invalid_argument("Missing supplied inventory base record");
        }

        void validateRestore(const SerializedPair& input, const RestoreContent& content)
        {
            constexpr size_t maxObjects = 1024;
            if (content.mBases.empty() || content.mBases.size() > maxObjects)
                throw std::invalid_argument("Invalid supplied base count");
            validateId(content.mScript.mId, false);
            for (char type : { 's', 'l', 'f' })
            {
                if (content.mDeclarations.get(type).size() > 1024)
                    throw std::invalid_argument("Too many supplied declarations");
                const auto& names = content.mDeclarations.get(type);
                for (size_t i = 0; i < names.size(); ++i)
                {
                    validateText(names[i], false);
                    if (content.mDeclarations.getType(names[i]) != type
                        || content.mDeclarations.getIndex(names[i]) != static_cast<int>(i))
                        throw std::invalid_argument("Ambiguous supplied declaration");
                }
            }
            for (size_t i = 0; i < content.mBases.size(); ++i)
            {
                const auto* base = content.mBases[i];
                if (!base)
                    throw std::invalid_argument("Null supplied base record");
                validateId(base->mId, false);
                validateId(base->mScript);
                if (base->mId == "gold_001" || base->mId == "gold_005" || base->mId == "gold_010"
                    || base->mId == "gold_025" || base->mId == "gold_100"
                    || (!base->mScript.empty() && base->mScript != content.mScript.mId))
                    throw std::invalid_argument("Unsupported gold or missing script declaration");
                for (size_t j = 0; j < i; ++j)
                    if (content.mBases[j]->mId == base->mId)
                        throw std::invalid_argument("Ambiguous supplied base record");
            }
            const std::array inventories{ &input.mSource, &input.mDestination };
            // Validate every shape before scanning identity associations or staging.
            for (const auto* inventory : inventories)
                if (inventory->mObjects.size() > maxObjects
                    || inventory->mObjects.size() != inventory->mProposedIdentities.size())
                    throw std::invalid_argument("Invalid detached inventory membership");
            for (const auto* inventory : inventories)
                for (size_t i = 0; i < inventory->mObjects.size(); ++i)
                {
                    const auto& state = inventory->mObjects[i];
                    const auto& ref = state.mRef;
                    const auto id = inventory->mProposedIdentities[i];
                    if (!id.isSet() || id.mContentFile < -1 || ref.mRefNum.isSet())
                        throw std::invalid_argument("Invalid detached identity association");
                    size_t occurrences = 0;
                    for (const auto* collection : inventories)
                        occurrences += std::count(
                            collection->mProposedIdentities.begin(), collection->mProposedIdentities.end(), id);
                    if (occurrences != 1)
                        throw std::invalid_argument("Duplicate proposed inventory identity");
                    validateId(ref.mRefID, false);
                    const auto& base = suppliedBase(ref.mRefID, content);
                    for (const auto& key : { ref.mOwner, ref.mSoul, ref.mFaction, ref.mKey, ref.mTrap })
                        validateId(key); // Preserve semantic fields without resolving their targets.
                    validateText(ref.mGlobalVariable);
                    validateText(ref.mDestCell);
                    // Stock signed restocking counts are retained, including zero.
                    if (ref.mCount == std::numeric_limits<int32_t>::min() || !std::isfinite(ref.mScale)
                        || ref.mScale <= 0 || ref.mChargeInt < -1 || !std::isfinite(ref.mChargeIntRemainder)
                        || !std::isfinite(ref.mEnchantmentCharge) || ref.mEnchantmentCharge < -1)
                        throw std::invalid_argument("Invalid detached CellRef numeric value");
                    for (const auto* position : { &ref.mPos, &ref.mDoorDest })
                        for (int axis = 0; axis < 3; ++axis)
                            if (!std::isfinite(position->pos[axis]) || !std::isfinite(position->rot[axis]))
                                throw std::invalid_argument("Nonfinite detached CellRef position");
                    if (state.mLocals.mVariables.size() > 1024 || state.mAnimationState.mScriptedAnims.size() > 256)
                        throw std::invalid_argument("Oversized detached RefData");
                    for (const auto& [name, value] : state.mLocals.mVariables)
                        validateText(name, false);
                    for (const auto& animation : state.mAnimationState.mScriptedAnims)
                        validateText(animation.mGroup, false);
                    RefData::validateRestore(state, base.mScript, content.mDeclarations);
                }
        }

        void restorePair(
            const SerializedPair& input, const RestoreContent& content, std::unique_ptr<const RestoredPair>& output)
        {
            validateRestore(input, content);
            auto staged = std::make_unique<RestoredPair>();
            const auto restore = [&](const SerializedInventory& saved, RestoredInventory& inventory) {
                inventory.mProposedIdentities = saved.mProposedIdentities;
                for (const auto& object : saved.mObjects)
                {
                    const auto& base = suppliedBase(object.mRef.mRefID, content);
                    // Actual engine CellRef/LiveCellRef construction retains every
                    // field; RefData's explicit restore keeps strict locals/flags.
                    inventory.mNodes.emplace_back(object.mRef, &base);
                    inventory.mNodes.back().mData = RefData::restore(object, base.mScript, content.mDeclarations);
                }
            };
            restore(input.mSource, staged->mSource);
            restore(input.mDestination, staged->mDestination);
            // Both lists and their associations publish through one noexcept move.
            static_assert(noexcept(output = std::move(staged)));
            output = std::move(staged);
        }

        void serializePair(const RestoredPair& pair, const Compiler::Locals& declarations, SerializedPair& output)
        {
            SerializedPair staged;
            serializeInventory(
                pair.mSource.mNodes, [&](size_t i) { return pair.mSource.mProposedIdentities.at(i); }, declarations,
                staged.mSource);
            serializeInventory(
                pair.mDestination.mNodes, [&](size_t i) { return pair.mDestination.mProposedIdentities.at(i); },
                declarations, staged.mDestination);
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
                for (const auto& node : value.mNodes)
                {
                    nodes.push_back(nodeState(ConstPtr(&node)));
                    bases.push_back(node.mBase);
                    for (const auto& animation : node.mData.getAnimationState().mScriptedAnims)
                        strings.emplace_back(animation.mGroup.data(), animation.mGroup.capacity());
                }
                return std::tuple{ nodes, bases, strings, value.mProposedIdentities, value.mProposedIdentities.data(),
                    value.mProposedIdentities.capacity() };
            };
            return std::tuple{ pair.get(), inventory(pair->mSource), inventory(pair->mDestination) };
        }

        RestoredPair expectedRestoration(const PreparedContainerTransfer& pair)
        {
            RestoredPair result;
            const auto copy = [&](const auto& storage, const auto& views, RestoredInventory& inventory) {
                size_t i = 0;
                for (const auto& node : storage)
                {
                    auto& expected = inventory.mNodes.emplace_back(ESM::makeBlankCellRef(), node.mBase);
                    expected.mRef = node.mRef;
                    expected.mData = node.mData.copyForContainerTransfer();
                    // This transient engine flag is deliberately absent from ObjectState.
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
                    && ESM::RefId::stringRefId(output.mDestCell) == ref.getDestCell()
                    && output.mLockLevel == ref.getLockLevel() && output.mIsLocked == ref.isLocked()
                    && output.mKey == ref.getKey() && output.mTrap == ref.getTrap() && output.mReferenceBlocked == 1
                    && output.mPos == ref.getPosition(),
                "serialized CellRef field mismatch");
        }

        void checkRestored(const RestoredPair& restored, const RestoredPair& expected)
        {
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
        ObjectState,
        LocalsRestore,
        Restore
    };

    static void checkTransferRehearsalCases(const ESMStore& content, AllocationCheck allocationCheck)
    {
        using Rehearsal = DisposableTransferRehearsal;
        using Stage = Rehearsal::Stage;
        using Pair = PreparedContainerTransfer;
        const bool localsRestore = allocationCheck == AllocationCheck::LocalsRestore;
        const bool inventoryRestore = allocationCheck == AllocationCheck::Restore;
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
        size_t cases = 0, restoredLocals = 0, localRejections = 0;
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
                                    if (allocationCheck == AllocationCheck::ObjectState || localsRestore
                                        || inventoryRestore)
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
                                    if (inventoryRestore && cursorPosition != 0)
                                    {
                                        data.setPosition({ { -0.f, std::numeric_limits<float>::denorm_min(), 300.f },
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
                                                : std::vector<float>{ -0.f, std::numeric_limits<float>::denorm_min() };
                                        }
                                    }
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
                                    std::optional<RestoredPair> expected;
                                    auto output = pairOutputSentinel<ObjectStates>();
                                    std::optional<decltype(pairOutputState(output))> retained;
                                    {
                                        auto pair = make();
                                        nodes = ownedNodes(pair);
                                        const int sign = inventoryRestore && cursorPosition == 99 ? -1 : 1;
                                        require(
                                            pair.getSourceItem().getCellRef().getCount(false) == sign * (4 - quantity)
                                                && pair.getDestinationItem().getCellRef().getCount(false)
                                                    == (!scriptedItem && stack ? sign * (7 + quantity) : quantity),
                                            "serialization fixture lost full/partial removal or destination count");
                                        const bool malformed
                                            = !shared && scriptedItem && !stack && quantity == 1 && cursorPosition == 0;
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
                                        require(
                                            ownedNodes(pair) == nodes, "serialization/rehearsal replaced owned nodes");
                                        retained = pairOutputState(output);
                                    }
                                    requireDiscarded(nodes);
                                    // Owned values and proposed identities outlive the pair.
                                    require(pairOutputState(output) == *retained, "discard invalidated owned output");
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
                                                    totals.mTotal
                                                        += checkLocalRestore(configured, object.mLocals, declarations,
                                                            verifyOwned, localRejections == 0, localRejections);
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

    void checkTransferRestore(const ESMStore& content)
    {
        checkTransferRehearsalCases(content, AllocationCheck::Restore);
    }
}
