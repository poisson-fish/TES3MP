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
            if (base->mId == "gold_001" || base->mId == "gold_005" || base->mId == "gold_010" || base->mId == "gold_025"
                || base->mId == "gold_100" || (!base->mScript.empty() && base->mScript != content.mScript.mId))
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
            }
    }

    namespace
    {
        static_assert(std::endian::native == std::endian::little);
        constexpr size_t MaxObjects = 1024;
        constexpr uint32_t CodecVersion = 1;

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

        std::array<uint32_t, 2> preflight(std::span<const char> bytes, const SaveBindings& bindings)
        {
            valid(bytes.size() <= MaxTransferSaveBytes);
            validateBindings(bindings);
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
            auto sizes = pair.sub(ESM::fourCC("SIZE"));
            const std::array counts{ sizes.number(), sizes.number() };
            valid(sizes.empty() && pair.empty() && counts[0] <= MaxObjects && counts[1] <= MaxObjects);
            valid(records == 1 + counts[0] + counts[1]);
            std::array<ESM::RefNum, MaxObjects * 2> identities;
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
                    valid(std::find(identities.begin(), identities.begin() + used, value) == identities.begin() + used);
                    identities[used++] = value;
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
                            case ESM::fourCC("HLOC"):
                            case ESM::fourCC("ENAB"):
                                valid(size == 1 && static_cast<unsigned char>(field.mBytes[0]) <= 1);
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
            valid(file.empty());
            return counts;
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
        ByteBuffer buffer;
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        ESM::ESMWriter writer;
        writer.setVersion(ESM::VER_130);
        writer.setType(0);
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.setRecordCount(static_cast<int>(1 + input.mSource.mObjects.size() + input.mDestination.mObjects.size()));
        writer.save(stream);
        writer.startRecord("PAIR");
        const auto& e = bindings.mEnvelope;
        writer.writeHNT("FVER", CodecVersion);
        writer.writeHNString("RUNT", e.mRuntime);
        writer.writeHNT("CONT", e.mContent);
        writer.writeFormId(e.mSourceOwner, true, "SOWN");
        writer.writeFormId(e.mDestinationOwner, true, "DOWN");
        writer.writeFormId(e.mInitiator, true, "INIT");
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
        writer.close();
        preflight(buffer.mBytes, bindings);
        output.swap(buffer.mBytes);
    }

    void decodeTransferSave(std::span<const char> bytes, const SaveBindings& bindings, SerializedPair& output)
    {
        const auto counts = preflight(bytes, bindings);
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
        size_t side = 0;
        for (auto* inventory : { &staged.mSource, &staged.mDestination })
        {
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
        valid(!reader.hasMoreRecs());
        validateRestore(staged, bindings.mContent);
        // Reject duplicate/out-of-order/default redundant fields and inconsistent
        // stock/extension values. Publish only the unique encoding of this state.
        TransferSaveBytes canonical;
        encodeTransferSave(staged, bindings, canonical);
        valid(canonical.size() == bytes.size() && std::equal(canonical.begin(), canonical.end(), bytes.begin()));
        output.mSource.swap(staged.mSource);
        output.mDestination.swap(staged.mDestination);
    }
}
