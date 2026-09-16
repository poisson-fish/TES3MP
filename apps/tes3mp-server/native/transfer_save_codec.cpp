#include "transfer_save_codec.hpp"

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/compiler/locals.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadscpt.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>

namespace MWWorld::Testing
{
    namespace
    {
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

        void validateRestart(const TransferRestartMetadata& restart)
        {
            // Retain the codec's bounded generated-ID domain (-1).
            // Exhausted but representable values are preserved, not reset; a
            // future resumption path must reject revision/counter overflow.
            if (restart.mRevision == 0 || restart.mRevision > std::numeric_limits<size_t>::max()
                || restart.mLastGenerated.mContentFile != -1 || restart.mLastGenerated.mIndex == 0)
                throw std::invalid_argument("Invalid detached restart metadata");
        }

        void validateRestartIdentity(const TransferRestartMetadata& restart, ESM::RefNum id)
        {
            if (id.mContentFile == -1 && id.mIndex > restart.mLastGenerated.mIndex)
                throw std::invalid_argument("Detached identity exceeds saved generation counter");
        }

    }

    const ESM::Miscellaneous& suppliedBase(const ESM::RefId& id, const RestoreContent& content)
    {
        // Only search the explicit base-record collection, never a world/store.
        for (const auto* base : content.mBases)
            if (base->mId == id)
                return *base;
        throw std::invalid_argument("Missing supplied inventory base record");
    }

    static void validateContent(const RestoreContent& content)
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
            if (base->mId == "gold_001" || base->mId == "gold_005" || base->mId == "gold_010" || base->mId == "gold_025"
                || base->mId == "gold_100" || (!base->mScript.empty() && base->mScript != content.mScript.mId))
                throw std::invalid_argument("Unsupported gold or missing script declaration");
            for (size_t j = 0; j < i; ++j)
                if (content.mBases[j]->mId == base->mId)
                    throw std::invalid_argument("Ambiguous supplied base record");
        }
    }

    namespace
    {
        struct ScriptView
        {
            bool mShared;
            std::span<const TransferScriptItem> mOther;
            std::array<std::span<const TransferScriptRegistration>, 2> mEntries;
            std::array<size_t, 2> mCursors;
        };

        void validateScripts(const TransferRestartMetadata& restart,
            const std::array<std::span<const TransferScriptItem>, 2>& inventories,
            const ScriptView& scripts, const RestoreContent& content)
        {
            const auto valid = [](bool condition) {
                if (!condition)
                    throw std::invalid_argument("Invalid restart script metadata binding or cursor");
            };
            valid(scripts.mOther.size() <= MaxTransferInventoryItems);
            valid(scripts.mEntries[0].size() <= MaxTransferScriptEntries
                && scripts.mEntries[1].size() <= MaxTransferScriptEntries - scripts.mEntries[0].size());
            valid(!scripts.mShared || (scripts.mEntries[1].empty() && scripts.mCursors[1] == 0));
            const std::array items{ inventories[0], inventories[1], scripts.mOther };
            for (const auto& item : scripts.mOther)
            {
                valid(item.mIdentity.isSet() && item.mIdentity.mContentFile >= -1);
                validateRestartIdentity(restart, item.mIdentity);
                validateId(item.mBase, false);
                const auto& base = suppliedBase(item.mBase, content);
                valid(!item.mConfigured || !base.mScript.empty());
                size_t matches = 0;
                for (const auto collection : items)
                    matches += std::count_if(collection.begin(), collection.end(),
                        [&](const auto& value) { return value.mIdentity == item.mIdentity; });
                valid(matches == 1);
            }
            for (size_t service = 0; service < 2; ++service)
            {
                valid(scripts.mCursors[service] <= scripts.mEntries[service].size());
                for (const auto& entry : scripts.mEntries[service])
                {
                    validateId(entry.mScript, false);
                    valid(entry.mScript == content.mScript.mId);
                    size_t matches = 0;
                    for (size_t side = 0; side < items.size(); ++side)
                        for (const auto& item : items[side])
                            if (item.mIdentity == entry.mIdentity)
                            {
                                valid(service == (side == 1 && !scripts.mShared ? 1 : 0)
                                    && item.mConfigured && suppliedBase(item.mBase, content).mScript == entry.mScript);
                                ++matches;
                            }
                    valid(matches == 1);
                    size_t registrations = 0;
                    for (const auto entries : scripts.mEntries)
                        registrations += std::count_if(entries.begin(), entries.end(),
                            [&](const auto& value) { return value.mIdentity == entry.mIdentity; });
                    valid(registrations == 1);
                }
            }
        }
    }

    void validateTransferSelection(ESM::RefNum selection, std::span<const ESM::RefNum> identities)
    {
        // Never normalize malformed unset values or drop a foreign selection.
        // Count-zero members are valid stock selections; iteration skips them.
        if (selection == ESM::RefNum{})
            return;
        if (!selection.isSet() || selection.mContentFile < -1
            || std::count(identities.begin(), identities.end(), selection) != 1)
            throw std::invalid_argument("Invalid detached inventory selection");
    }

    void validateRestore(const SerializedPair& input, const RestoreContent& content)
    {
        validateRestart(input.mRestart);
        validateContent(content);
        constexpr size_t maxObjects = MaxTransferInventoryItems;
        const std::array inventories{ &input.mSource, &input.mDestination };
        std::array<std::array<TransferScriptItem, maxObjects>, 2> scriptItems;
        // Validate every shape before scanning identity associations or staging.
        for (const auto* inventory : inventories)
            if (inventory->mObjects.size() > maxObjects
                || inventory->mObjects.size() != inventory->mProposedIdentities.size())
                throw std::invalid_argument("Invalid detached inventory membership");
        for (const auto* inventory : inventories)
        {
            validateTransferSelection(inventory->mSelection, inventory->mProposedIdentities);
            for (size_t i = 0; i < inventory->mObjects.size(); ++i)
            {
                const auto& state = inventory->mObjects[i];
                const auto& ref = state.mRef;
                const auto id = inventory->mProposedIdentities[i];
                if (!id.isSet() || id.mContentFile < -1 || ref.mRefNum.isSet())
                    throw std::invalid_argument("Invalid detached identity association");
                validateRestartIdentity(input.mRestart, id);
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
                if (ref.mCount == std::numeric_limits<int32_t>::min() || !std::isfinite(ref.mScale) || ref.mScale <= 0
                    || ref.mChargeInt < -1 || !std::isfinite(ref.mChargeIntRemainder)
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
                scriptItems[inventory == inventories[0] ? 0 : 1][i] = { id, base.mId, state.mHasLocals != 0 };
            }
        }
        const auto& scripts = input.mScripts;
        validateScripts(input.mRestart,
            { std::span(scriptItems[0]).first(input.mSource.mObjects.size()),
                std::span(scriptItems[1]).first(input.mDestination.mObjects.size()) },
            { scripts.mShared, scripts.mOther, { scripts.mServices[0].mEntries, scripts.mServices[1].mEntries },
                { scripts.mServices[0].mCursor, scripts.mServices[1].mCursor } }, content);
    }

    namespace
    {
        static_assert(std::endian::native == std::endian::little);
        constexpr size_t MaxObjects = 1024;
        constexpr uint32_t CodecVersion = 4;

        [[noreturn]] void invalid()
        {
            throw std::invalid_argument("Invalid or incompatible test inventory save");
        }

        void valid(bool condition)
        {
            if (!condition)
                invalid();
        }

        // Both passes use views only until every external count and length has
        // been bounded. Never construct an ESMReader (it allocates) before this.
        struct Cursor
        {
            std::span<const char> mBytes;
            std::span<const char> take(size_t size)
            {
                valid(size <= mBytes.size());
                auto result = mBytes.first(size);
                mBytes = mBytes.subspan(size);
                return result;
            }
            uint32_t number()
            {
                const auto b = take(4);
                uint32_t result = 0;
                for (size_t i = 0; i < 4; ++i)
                    result |= static_cast<uint32_t>(static_cast<unsigned char>(b[i])) << (8 * i);
                return result;
            }
            ESM::RefNum identity() { return { number(), std::bit_cast<int32_t>(number()) }; }
            uint64_t revision()
            {
                const uint64_t low = number();
                return low | (static_cast<uint64_t>(number()) << 32);
            }
            bool empty() const { return mBytes.empty(); }
            Cursor sub(uint32_t tag)
            {
                valid(number() == tag);
                const auto size = number();
                return { take(size) };
            }
            Cursor record(uint32_t tag)
            {
                valid(number() == tag);
                const auto size = number();
                valid(number() == 0 && number() == 0);
                return { take(size) };
            }
        };

        void same(std::span<const char> bytes, std::string_view expected)
        {
            valid(bytes.size() == expected.size() && std::equal(bytes.begin(), bytes.end(), expected.begin()));
        }

        bool validIdentity(ESM::RefNum id)
        {
            return id.isSet() && id.mContentFile >= -1;
        }

        void validateBindings(const SaveBindings& bindings)
        {
            const auto& e = bindings.mEnvelope;
            validateText(e.mRuntime, false);
            valid(e.mRuntime.size() <= 128);
            valid(std::any_of(e.mContent.begin(), e.mContent.end(), [](auto b) { return b != 0; }));
            valid(validIdentity(e.mSourceOwner) && validIdentity(e.mDestinationOwner) && validIdentity(e.mInitiator));
            valid(e.mSourceOwner != e.mDestinationOwner);
            valid(!bindings.mReferenceIds.empty() && bindings.mReferenceIds.size() <= 4096);
            for (const auto& id : bindings.mReferenceIds)
                validateId(id, false);
        }

        void reference(std::span<const char> bytes, const SaveBindings& bindings)
        {
            valid(!bytes.empty());
            if (bytes.front() == static_cast<char>(ESM::RefIdType::Empty))
            {
                valid(bytes.size() == 1);
                return;
            }
            valid(bytes.front() == static_cast<char>(ESM::RefIdType::UnsizedString));
            const std::string_view name(bytes.data() + 1, bytes.size() - 1);
            validateText(name, false);
            valid(std::any_of(bindings.mReferenceIds.begin(), bindings.mReferenceIds.end(),
                [&](const auto& id) { return id.getRefIdString() == name; }));
        }

        struct SavePreflight
        {
            std::array<uint32_t, 2> mCounts;
            std::array<ESM::RefNum, 2> mSelections;
            TransferRestartMetadata mRestart;
            bool mShared = true;
            uint32_t mOtherCount = 0;
            std::array<TransferScriptItem, MaxTransferInventoryItems> mOther;
            std::array<TransferScriptRegistration, MaxTransferScriptEntries> mEntries;
            std::array<uint32_t, 2> mEntryCounts{}, mCursors{};
        };

        ESM::RefId scriptReference(Cursor field, const SaveBindings& bindings, bool base)
        {
            const auto bytes = field.mBytes;
            valid(!bytes.empty() && bytes.front() == static_cast<char>(ESM::RefIdType::UnsizedString));
            const std::string_view name(bytes.data() + 1, bytes.size() - 1);
            validateText(name, false);
            if (base)
            {
                for (const auto* record : bindings.mContent.mBases)
                    if (record->mId.getRefIdString() == name)
                        return record->mId;
            }
            else if (bindings.mContent.mScript.mId.getRefIdString() == name)
                return bindings.mContent.mScript.mId;
            invalid();
        }

        void validateScriptOwners(const TransferScriptMetadata& scripts, const SaveEnvelope& envelope)
        {
            for (const auto& item : scripts.mOther)
                valid(item.mIdentity != envelope.mSourceOwner && item.mIdentity != envelope.mDestinationOwner
                    && item.mIdentity != envelope.mInitiator);
        }

        void validateRestartOwners(const TransferRestartMetadata& restart, const SaveEnvelope& envelope)
        {
            for (const auto id : { envelope.mSourceOwner, envelope.mDestinationOwner, envelope.mInitiator })
                validateRestartIdentity(restart, id);
        }

        SavePreflight preflight(std::span<const char> bytes, const SaveBindings& bindings)
        {
            valid(bytes.size() <= MaxTransferSaveBytes);
            validateBindings(bindings);
            validateContent(bindings.mContent);
            SavePreflight result;
            Cursor file{ bytes };
            auto header = file.record(ESM::fourCC("TES3"));
            auto form = header.sub(ESM::fourCC("FORM"));
            valid(form.number() == ESM::CurrentSaveGameFormatVersion && form.empty());
            auto data = header.sub(ESM::fourCC("HEDR"));
            valid(data.number() == ESM::VER_130 && data.number() == 0);
            // Only empty author/description; never expose inner string lengths
            // from a foreign header to OpenMW's string allocator.
            valid(data.number() == 0 && data.number() == 0);
            const auto records = data.number();
            valid(data.empty() && header.empty());
            auto pair = file.record(ESM::fourCC("PAIR"));
            auto version = pair.sub(ESM::fourCC("FVER"));
            valid(version.number() == CodecVersion && version.empty());
            same(pair.sub(ESM::fourCC("RUNT")).mBytes, bindings.mEnvelope.mRuntime);
            const auto content = pair.sub(ESM::fourCC("CONT")).mBytes;
            valid(content.size() == bindings.mEnvelope.mContent.size());
            valid(std::equal(content.begin(), content.end(), bindings.mEnvelope.mContent.begin(),
                [](char a, unsigned char b) { return static_cast<unsigned char>(a) == b; }));
            const auto& e = bindings.mEnvelope;
            for (const auto& [tag, expected] : { std::pair{ ESM::fourCC("SOWN"), e.mSourceOwner },
                     std::pair{ ESM::fourCC("DOWN"), e.mDestinationOwner },
                     std::pair{ ESM::fourCC("INIT"), e.mInitiator } })
            {
                auto id = pair.sub(tag);
                valid(id.identity() == expected && id.empty());
            }
            auto revision = pair.sub(ESM::fourCC("RREV"));
            auto counter = pair.sub(ESM::fourCC("LGEN"));
            const TransferRestartMetadata restart{ revision.revision(), counter.identity() };
            valid(revision.empty() && counter.empty());
            validateRestart(restart);
            validateRestartOwners(restart, e);
            size_t selectionSide = 0;
            for (const auto tag : { ESM::fourCC("SSEL"), ESM::fourCC("DSEL") })
            {
                auto field = pair.sub(tag);
                result.mSelections[selectionSide++] = field.identity();
                valid(field.empty());
            }
            auto sizes = pair.sub(ESM::fourCC("SIZE"));
            const std::array counts{ sizes.number(), sizes.number() };
            valid(sizes.empty() && pair.empty() && counts[0] <= MaxObjects && counts[1] <= MaxObjects);
            valid(records == 2 + counts[0] + counts[1]);
            std::array<ESM::RefNum, MaxObjects * 2> identities;
            std::array<std::array<TransferScriptItem, MaxObjects>, 2> scriptItems;
            size_t used = 0;
            for (size_t side = 0; side < counts.size(); ++side)
                for (size_t i = 0; i < counts[side]; ++i)
                {
                    auto object = file.record(side == 0 ? ESM::fourCC("OBJS") : ESM::fourCC("OBJD"));
                    valid(object.mBytes.size() <= MaxTransferObjectBytes);
                    auto id = object.sub(ESM::fourCC("IDEN"));
                    const auto value = id.identity();
                    valid(id.empty() && validIdentity(value) && value != e.mSourceOwner && value != e.mDestinationOwner
                        && value != e.mInitiator);
                    validateRestartIdentity(restart, value);
                    valid(std::find(identities.begin(), identities.begin() + used, value) == identities.begin() + used);
                    identities[used++] = value;
                    auto& scriptItem = scriptItems[side][i];
                    scriptItem.mIdentity = value;
                    size_t fields = 0, locals = 0, animations = 0, times = 0;
                    while (!object.empty())
                    {
                        valid(++fields <= 4096);
                        const auto tag = object.number();
                        auto field = Cursor{ object.take(object.number()) };
                        const auto size = field.mBytes.size();
                        switch (tag)
                        {
                            case ESM::fourCC("NAME"):
                                reference(field.mBytes, bindings);
                                for (const auto* base : bindings.mContent.mBases)
                                    if (field.mBytes.size() > 1 && base->mId.getRefIdString()
                                            == std::string_view(field.mBytes.data() + 1, size - 1))
                                        scriptItem.mBase = base->mId;
                                valid(!scriptItem.mBase.empty());
                                break;
                            case ESM::fourCC("ANAM"):
                            case ESM::fourCC("XSOL"):
                            case ESM::fourCC("CNAM"):
                            case ESM::fourCC("KNAM"):
                            case ESM::fourCC("TNAM"):
                            case ESM::fourCC("XKEY"):
                                reference(field.mBytes, bindings);
                                break;
                            case ESM::fourCC("LOCA"):
                                valid(++locals <= 1024);
                                validateText({ field.mBytes.data(), size }, false);
                                break;
                            case ESM::fourCC("ANIS"):
                                valid(++animations <= 256);
                                validateText({ field.mBytes.data(), size }, false);
                                break;
                            case ESM::fourCC("BNAM"):
                            case ESM::fourCC("DNAM"):
                                // Stock HNCString appends one terminator.
                                valid(size > 0 && field.mBytes.back() == '\0');
                                validateText({ field.mBytes.data(), size - 1 });
                                break;
                            case ESM::fourCC("XDST"):
                                if (!(size == 1 && field.mBytes[0] == 0))
                                    validateText({ field.mBytes.data(), size });
                                break;
                            case ESM::fourCC("FRMR"):
                                valid(size == 8 && field.identity() == ESM::RefNum{});
                                break;
                            case ESM::fourCC("COUN"):
                                valid(size == 8);
                                break;
                            case ESM::fourCC("DODT"):
                            case ESM::fourCC("DATA"):
                            case ESM::fourCC("POS_"):
                            case ESM::fourCC("XPOS"):
                                valid(size == 24);
                                break;
                            case ESM::fourCC("XSAV"):
                                valid(size == 38 && static_cast<unsigned char>(field.mBytes[8]) <= 1
                                    && static_cast<unsigned char>(field.mBytes[9]) <= 1);
                                break;
                            case ESM::fourCC("HCUS"):
                            case ESM::fourCC("ABST"):
                            case ESM::fourCC("ENAB"):
                                valid(size == 1 && static_cast<unsigned char>(field.mBytes[0]) <= 1);
                                break;
                            case ESM::fourCC("HLOC"):
                                valid(size == 1 && static_cast<unsigned char>(field.mBytes[0]) <= 1);
                                scriptItem.mConfigured = field.mBytes[0] != 0;
                                break;
                            case ESM::fourCC("UNAM"):
                                valid(size == 1);
                                break;
                            case ESM::fourCC("STTV"):
                                valid(size == 2);
                                break;
                            case ESM::fourCC("XTIM"):
                                valid(++times <= 256);
                                [[fallthrough]];
                            case ESM::fourCC("XSCL"):
                            case ESM::fourCC("INDX"):
                            case ESM::fourCC("XCHG"):
                            case ESM::fourCC("INTV"):
                            case ESM::fourCC("NAM9"):
                            case ESM::fourCC("FLTV"):
                            case ESM::fourCC("FLAG"):
                            case ESM::fourCC("TIME"):
                                valid(size == 4);
                                break;
                            default:
                                invalid(); // Includes Lua/custom data and deletion markers.
                        }
                    }
                    valid(times == animations);
                }
            validateTransferSelection(result.mSelections[0], std::span(identities).first(counts[0]));
            validateTransferSelection(result.mSelections[1], std::span(identities).subspan(counts[0], counts[1]));
            auto scripts = file.record(ESM::fourCC("SCRP"));
            auto map = scripts.sub(ESM::fourCC("SMAP"));
            const auto destination = map.number();
            valid(destination <= 1 && map.empty()); // Source is always 0.
            result.mShared = destination == 0;
            auto otherCount = scripts.sub(ESM::fourCC("OCNT"));
            result.mOtherCount = otherCount.number();
            valid(result.mOtherCount <= MaxTransferInventoryItems && otherCount.empty());
            for (size_t i = 0; i < result.mOtherCount; ++i)
            {
                auto binding = scripts.sub(ESM::fourCC("BIND"));
                auto& item = result.mOther[i];
                item.mIdentity = binding.identity();
                const auto configured = binding.number();
                valid(configured <= 1 && binding.empty());
                item.mConfigured = configured != 0;
                item.mBase = scriptReference(scripts.sub(ESM::fourCC("BASE")), bindings, true);
                valid(item.mIdentity != e.mSourceOwner && item.mIdentity != e.mDestinationOwner
                    && item.mIdentity != e.mInitiator);
            }
            size_t entries = 0;
            for (size_t service = 0; service < (result.mShared ? 1u : 2u); ++service)
            {
                auto serviceHeader = scripts.sub(ESM::fourCC("SERV"));
                const auto count = result.mEntryCounts[service] = serviceHeader.number();
                result.mCursors[service] = serviceHeader.number();
                valid(serviceHeader.empty() && count <= MaxTransferScriptEntries - entries);
                for (size_t i = 0; i < count; ++i)
                {
                    auto id = scripts.sub(ESM::fourCC("SREF"));
                    auto& entry = result.mEntries[entries++];
                    entry.mIdentity = id.identity();
                    valid(id.empty());
                    entry.mScript = scriptReference(scripts.sub(ESM::fourCC("SCPT")), bindings, false);
                }
            }
            valid(scripts.empty());
            validateScripts(restart,
                { std::span(scriptItems[0]).first(counts[0]), std::span(scriptItems[1]).first(counts[1]) },
                { result.mShared, std::span(result.mOther).first(result.mOtherCount),
                    { std::span(result.mEntries).first(result.mEntryCounts[0]),
                        std::span(result.mEntries).subspan(result.mEntryCounts[0], result.mEntryCounts[1]) },
                    { result.mCursors[0], result.mCursors[1] } }, bindings.mContent);
            valid(file.empty());
            result.mCounts = counts;
            result.mRestart = restart;
            return result;
        }

        // Bounded output with seeking for ESMWriter's length backpatches. Every
        // growth is checked before vector allocation, including total byte limit.
        class ByteBuffer : public std::streambuf
        {
            size_t mPosition = 0;

        public:
            TransferSaveBytes mBytes;
            size_t mLimit = MaxTransferSaveBytes;
            std::streamsize xsputn(const char* data, std::streamsize count) override
            {
                valid(count >= 0 && static_cast<size_t>(count) <= mLimit - mPosition);
                const auto end = mPosition + static_cast<size_t>(count);
                if (end > mBytes.size())
                    mBytes.resize(end);
                if (count)
                    std::memcpy(mBytes.data() + mPosition, data, static_cast<size_t>(count));
                mPosition = end;
                return count;
            }
            int_type overflow(int_type ch) override
            {
                if (traits_type::eq_int_type(ch, traits_type::eof()))
                    return traits_type::not_eof(ch);
                const char value = traits_type::to_char_type(ch);
                xsputn(&value, 1);
                return ch;
            }
            pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode) override
            {
                const auto origin = dir == std::ios_base::beg ? 0
                    : dir == std::ios_base::cur               ? mPosition
                                                              : mBytes.size();
                valid(offset >= -static_cast<off_type>(origin)
                    && offset <= static_cast<off_type>(mBytes.size() - origin));
                mPosition = static_cast<size_t>(static_cast<off_type>(origin) + offset);
                return static_cast<pos_type>(mPosition);
            }
            pos_type seekpos(pos_type position, std::ios_base::openmode mode) override
            {
                return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
            }
        };

        void saveObject(ESM::ESMWriter& writer, const ESM::ObjectState& object)
        {
            object.save(writer, false); // Reuse engine CellRef, locals, flags and animation fields.
            const auto& r = object.mRef;
            // Stock save intentionally clamps/omits these values. The strict
            // owned-state codec preserves them without changing stock behavior.
            writer.startSubRecord("XSAV");
            writer.writeT(r.mScale);
            writer.writeT(r.mChargeIntRemainder);
            writer.writeT(static_cast<uint8_t>(r.mTeleport));
            writer.writeT(static_cast<uint8_t>(r.mIsLocked));
            writer.writeT(r.mLockLevel);
            writer.writeComposite(r.mDoorDest);
            writer.endRecord("XSAV");
            writer.writeHNString("XDST", r.mDestCell);
            writer.writeHNRefId("XKEY", r.mKey);
            writer.writeNamedComposite("XPOS", object.mPosition);
            for (const auto& animation : object.mAnimationState.mScriptedAnims)
                writer.writeHNT("XTIM", animation.mTime);
        }
    }

    void encodeTransferSave(const SerializedPair& input, const SaveBindings& bindings, TransferSaveBytes& output)
    {
        validateBindings(bindings);
        validateRestore(input, bindings.mContent);
        validateRestartOwners(input.mRestart, bindings.mEnvelope);
        validateScriptOwners(input.mScripts, bindings.mEnvelope);
        ByteBuffer buffer;
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        ESM::ESMWriter writer;
        writer.setVersion(ESM::VER_130);
        writer.setType(0);
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.setRecordCount(static_cast<int>(2 + input.mSource.mObjects.size() + input.mDestination.mObjects.size()));
        writer.save(stream);
        writer.startRecord("PAIR");
        const auto& e = bindings.mEnvelope;
        writer.writeHNT("FVER", CodecVersion);
        writer.writeHNString("RUNT", e.mRuntime);
        writer.writeHNT("CONT", e.mContent);
        writer.writeFormId(e.mSourceOwner, true, "SOWN");
        writer.writeFormId(e.mDestinationOwner, true, "DOWN");
        writer.writeFormId(e.mInitiator, true, "INIT");
        writer.writeHNT("RREV", input.mRestart.mRevision);
        writer.writeFormId(input.mRestart.mLastGenerated, true, "LGEN");
        writer.writeFormId(input.mSource.mSelection, true, "SSEL");
        writer.writeFormId(input.mDestination.mSelection, true, "DSEL");
        writer.startSubRecord("SIZE");
        writer.writeT(static_cast<uint32_t>(input.mSource.mObjects.size()));
        writer.writeT(static_cast<uint32_t>(input.mDestination.mObjects.size()));
        writer.endRecord("SIZE");
        writer.endRecord("PAIR");
        for (const auto* inventory : { &input.mSource, &input.mDestination })
            for (size_t i = 0; i < inventory->mObjects.size(); ++i)
            {
                const auto tag = inventory == &input.mSource ? ESM::fourCC("OBJS") : ESM::fourCC("OBJD");
                const auto start = buffer.mBytes.size();
                buffer.mLimit = std::min(MaxTransferSaveBytes, start + 16 + MaxTransferObjectBytes);
                writer.startRecord(tag);
                writer.writeFormId(inventory->mProposedIdentities[i], true, "IDEN");
                saveObject(writer, inventory->mObjects[i]);
                writer.endRecord(tag);
                valid(buffer.mBytes.size() - start - 16 <= MaxTransferObjectBytes);
            }
        buffer.mLimit = MaxTransferSaveBytes;
        writer.startRecord("SCRP");
        const auto& scripts = input.mScripts;
        writer.writeHNT("SMAP", static_cast<uint32_t>(scripts.mShared ? 0 : 1));
        writer.writeHNT("OCNT", static_cast<uint32_t>(scripts.mOther.size()));
        for (const auto& item : scripts.mOther)
        {
            writer.startSubRecord("BIND");
            writer.writeT(item.mIdentity.mIndex);
            writer.writeT(item.mIdentity.mContentFile);
            writer.writeT(static_cast<uint32_t>(item.mConfigured));
            writer.endRecord("BIND");
            writer.writeHNRefId("BASE", item.mBase);
        }
        for (size_t service = 0; service < (scripts.mShared ? 1u : 2u); ++service)
        {
            const auto& value = scripts.mServices[service];
            writer.startSubRecord("SERV");
            writer.writeT(static_cast<uint32_t>(value.mEntries.size()));
            writer.writeT(static_cast<uint32_t>(value.mCursor));
            writer.endRecord("SERV");
            for (const auto& entry : value.mEntries)
            {
                writer.writeFormId(entry.mIdentity, true, "SREF");
                writer.writeHNRefId("SCPT", entry.mScript);
            }
        }
        writer.endRecord("SCRP");
        writer.close();
        preflight(buffer.mBytes, bindings);
        output.swap(buffer.mBytes);
    }

    void decodeTransferSave(std::span<const char> bytes, const SaveBindings& bindings, SerializedPair& output)
    {
        const auto checked = preflight(bytes, bindings);
        const auto& counts = checked.mCounts;
        // All nested lengths, counts, RefIds and context identities have already
        // been checked. The bounded copy also gives ESMReader an owning stream.
        auto stream = std::make_unique<std::istringstream>(std::string(bytes.data(), bytes.size()), std::ios::binary);
        stream->exceptions(std::ios::badbit | std::ios::failbit);
        ESM::ESMReader reader;
        reader.open(std::move(stream), {});
        valid(reader.getRecName() == ESM::fourCC("PAIR"));
        reader.getRecHeader();
        reader.skipRecord(); // Exactly matched by allocation-free preflight.
        SerializedPair staged;
        staged.mRestart = checked.mRestart;
        staged.mScripts.mShared = checked.mShared;
        staged.mScripts.mOther.assign(checked.mOther.begin(), checked.mOther.begin() + checked.mOtherCount);
        size_t offset = 0;
        for (size_t service = 0; service < 2; ++service)
        {
            auto& value = staged.mScripts.mServices[service];
            value.mCursor = checked.mCursors[service];
            value.mEntries.assign(checked.mEntries.begin() + offset,
                checked.mEntries.begin() + offset + checked.mEntryCounts[service]);
            offset += checked.mEntryCounts[service];
        }
        size_t side = 0;
        for (auto* inventory : { &staged.mSource, &staged.mDestination })
        {
            inventory->mSelection = checked.mSelections[side];
            inventory->mObjects.reserve(counts[side]);
            inventory->mProposedIdentities.reserve(counts[side]);
            for (size_t i = 0; i < counts[side]; ++i)
            {
                valid(reader.getRecName() == (side == 0 ? ESM::fourCC("OBJS") : ESM::fourCC("OBJD")));
                reader.getRecHeader();
                inventory->mProposedIdentities.push_back(reader.getFormId(true, "IDEN"));
                auto& object = inventory->mObjects.emplace_back();
                object.blank();
                object.mRef.loadId(reader, true);
                object.load(reader);
                // The modern on-wire version was checked above. Detached owned
                // ObjectStates use DefaultFormatVersion, with no legacy converter.
                object.mVersion = ESM::DefaultFormatVersion;
                auto& r = object.mRef;
                uint8_t teleport = 0, locked = 0;
                reader.getHNT("XSAV", r.mScale, r.mChargeIntRemainder, teleport, locked, r.mLockLevel, r.mDoorDest.pos,
                    r.mDoorDest.rot);
                r.mTeleport = teleport != 0;
                r.mIsLocked = locked != 0;
                r.mDestCell = reader.getHNString("XDST");
                r.mKey = reader.getHNRefId("XKEY");
                reader.getNamedComposite("XPOS", object.mPosition);
                for (auto& animation : object.mAnimationState.mScriptedAnims)
                    reader.getHNT(animation.mTime, "XTIM");
                valid(!reader.hasMoreSubs());
            }
            ++side;
        }
        valid(reader.getRecName() == ESM::fourCC("SCRP"));
        reader.getRecHeader();
        reader.skipRecord(); // All semantic script metadata was checked without allocation.
        valid(!reader.hasMoreRecs());
        validateRestore(staged, bindings.mContent);
        // Reject duplicate/out-of-order/default redundant fields and inconsistent
        // stock/extension values. Publish only the unique encoding of this state.
        TransferSaveBytes canonical;
        encodeTransferSave(staged, bindings, canonical);
        valid(canonical.size() == bytes.size() && std::equal(canonical.begin(), canonical.end(), bytes.begin()));
        output.swap(staged);
    }
}
