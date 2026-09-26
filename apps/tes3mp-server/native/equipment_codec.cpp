#include "equipment_codec.hpp"

#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventoryitem.hpp>
#include <apps/openmw/mwmechanics/activespells.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <cmath>
#include <limits>
#include <sstream>

namespace TES3MP::Native
{
    using namespace MWWorld;
    namespace
    {
        static_assert(std::endian::native == std::endian::little);

        void valid(bool condition)
        {
            if (!condition)
                throw std::invalid_argument("Invalid or incompatible plain equipment bytes");
        }

        void textValue(std::string_view text, bool required = false)
        {
            valid((!required || !text.empty()) && text.size() <= PlainEquipmentValues::MaxText
                && text.find('\0') == std::string_view::npos);
        }

        void validateBindings(const EquipmentBindings& bindings)
        {
            const auto& e = bindings.mEnvelope;
            textValue(e.mRuntime, true);
            valid(e.mRuntime.size() <= 128 && e.mActor.isSet()
                && std::any_of(e.mContent.begin(), e.mContent.end(), [](auto b) { return b != 0; })
                && bindings.mReferenceIds.size() <= MaxEquipmentReferenceIds);
            for (const auto& id : bindings.mReferenceIds)
            {
                valid(id.is<ESM::StringRefId>());
                textValue(id.getRefIdString(), true);
            }
        }

        void knownId(const ESM::RefId& id, const EquipmentBindings& bindings)
        {
            valid(id.empty()
                || std::find(bindings.mReferenceIds.begin(), bindings.mReferenceIds.end(), id)
                    != bindings.mReferenceIds.end());
        }

        // Views and fixed arrays only. Finish the entire structural pass before
        // constructing ESMReader, copying bytes, or allocating object/string data.
        struct Cursor
        {
            std::span<const char> mBytes;
            bool empty() const { return mBytes.empty(); }
            std::span<const char> take(size_t count)
            {
                valid(count <= mBytes.size());
                const auto result = mBytes.first(count);
                mBytes = mBytes.subspan(count);
                return result;
            }
            uint32_t number()
            {
                uint32_t result = 0;
                const auto bytes = take(4);
                for (size_t i = 0; i < 4; ++i)
                    result |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
                return result;
            }
            ESM::RefNum identity() { return { number(), std::bit_cast<int32_t>(number()) }; }
            bool next(uint32_t tag) const
            {
                auto copy = *this;
                return !empty() && copy.number() == tag;
            }
            Cursor sub(uint32_t tag)
            {
                valid(number() == tag);
                const auto size = number();
                return { take(size) };
            }
            Cursor field(uint32_t tag, size_t size)
            {
                auto result = sub(tag);
                valid(result.mBytes.size() == size);
                return result;
            }
            Cursor record(uint32_t tag)
            {
                valid(number() == tag);
                const auto size = number();
                valid(number() == 0 && number() == 0);
                return { take(size) };
            }
            void optional(uint32_t tag, size_t size)
            {
                if (next(tag))
                    field(tag, size);
            }
            void boolean(uint32_t tag)
            {
                if (next(tag))
                {
                    const auto value = field(tag, 1).mBytes[0];
                    valid(static_cast<unsigned char>(value) <= 1);
                }
            }
        };

        std::string_view text(std::span<const char> bytes)
        {
            return { bytes.data(), bytes.size() };
        }

        ESM::RefId reference(Cursor field, const EquipmentBindings& bindings)
        {
            valid(!field.empty());
            if (field.mBytes[0] == static_cast<char>(ESM::RefIdType::Empty))
            {
                valid(field.mBytes.size() == 1);
                return {};
            }
            valid(field.mBytes[0] == static_cast<char>(ESM::RefIdType::UnsizedString));
            const auto name = text(field.mBytes.subspan(1));
            textValue(name, true);
            for (const auto& id : bindings.mReferenceIds)
                if (id.getRefIdString() == name)
                    return id;
            valid(false);
            return {};
        }

        void covers(ESM::RefNum counter, ESM::RefNum id)
        {
            valid(id.isSet()
                && (id.mContentFile >= 0 || id.mContentFile > counter.mContentFile
                    || (id.mContentFile == counter.mContentFile && id.mIndex <= counter.mIndex)));
        }

        struct Preflight
        {
            ESM::RefNum mActor, mSelected, mCounter;
            std::array<ESM::RefNum, InventoryStore::Slots> mSlots{};
            uint32_t mCount;
            std::optional<EquipmentNpcStatsValues> mNpcStats;
        };

        Preflight preflight(std::span<const char> bytes, const EquipmentBindings& bindings)
        {
            valid(bytes.size() <= MaxEquipmentBytes);
            validateBindings(bindings);
            Cursor file{ bytes };
            auto header = file.record(ESM::fourCC("TES3"));
            valid(header.field(ESM::fourCC("FORM"), 4).number() == ESM::CurrentSaveGameFormatVersion);
            auto data = header.field(ESM::fourCC("HEDR"), 20);
            valid(data.number() == ESM::VER_130 && data.number() == 0);
            valid(data.number() == 0 && data.number() == 0); // No author/description allocations.
            const auto records = data.number();
            valid(header.empty());
            auto equipment = file.record(ESM::fourCC("EQUP"));
            const auto format = equipment.field(ESM::fourCC("FVER"), 4).number();
            const auto version = format == SlottedEquipmentFormatVersion
                ? equipment.field(ESM::fourCC("MODE"), 4).number() : format;
            valid(version == EquipmentFormatVersion || version == NpcEquipmentFormatVersion || version == ScriptedEquipmentFormatVersion);
            valid(text(equipment.sub(ESM::fourCC("RUNT")).mBytes) == bindings.mEnvelope.mRuntime);
            const auto content = equipment.field(ESM::fourCC("CONT"), 32).mBytes;
            valid(std::equal(content.begin(), content.end(), bindings.mEnvelope.mContent.begin(),
                [](char a, unsigned char b) { return static_cast<unsigned char>(a) == b; }));
            Preflight result;
            result.mActor = equipment.field(ESM::fourCC("ACTR"), 8).identity();
            if (format == SlottedEquipmentFormatVersion)
            {
                auto slots = equipment.field(ESM::fourCC("SLOT"), InventoryStore::Slots * 8);
                for (auto& slot : result.mSlots) slot = slots.identity();
            }
            else result.mSlots[InventoryStore::Slot_Shirt] = equipment.field(ESM::fourCC("SHRT"), 8).identity();
            result.mSelected = equipment.field(ESM::fourCC("SELE"), 8).identity();
            result.mCounter = equipment.field(ESM::fourCC("LGEN"), 8).identity();
            result.mCount = equipment.field(ESM::fourCC("SIZE"), 4).number();
            if (version != EquipmentFormatVersion)
            {
                auto& stats = result.mNpcStats.emplace();
                stats.mBase = reference(equipment.sub(ESM::fourCC("NPID")), bindings);
                const auto readTriples = [&](uint32_t tag, auto& values) {
                    auto field = equipment.field(tag, values.size() * 12);
                    for (auto& value : values)
                        for (float& number : value)
                            number = std::bit_cast<float>(field.number());
                };
                readTriples(ESM::fourCC("ATTR"), stats.mAttributes);
                readTriples(ESM::fourCC("DYNA"), stats.mDynamic);
                const auto count = equipment.field(ESM::fourCC("SCNT"), 4).number();
                valid(count <= stats.mSpells.size());
                for (size_t i = 0; i < count; ++i)
                {
                    stats.mSpells[i] = reference(equipment.sub(ESM::fourCC("KSPL")), bindings);
                    valid(!stats.mSpells[i].empty());
                }
                stats.mAbilityMagnitude = std::bit_cast<float>(equipment.field(ESM::fourCC("ABMG"), 4).number());
                stats.validate(bindings.mContent);
            }
            valid(equipment.empty() && bindings.mMaximumItems <= PlainEquipmentValues::MaxWorldItems && result.mCount <= bindings.mMaximumItems && records == result.mCount + 1
                && result.mActor == bindings.mEnvelope.mActor && result.mCounter.mContentFile < 0);
            covers(result.mCounter, result.mActor);
            std::array<ESM::RefNum, PlainEquipmentValues::MaxWorldItems> identities;
            bool shirtFound = result.mSlots[InventoryStore::Slot_Shirt] == ESM::RefNum{};
            bool selectedFound = result.mSelected == ESM::RefNum{};
            int64_t total = 0;
            float luckMagnitude = 0;
            for (size_t i = 0; i < result.mCount; ++i)
            {
                auto peek = file;
                const auto type = peek.number();
                valid(ContainerStore::isStorableType(type));
                auto object = file.record(type);
                valid(object.mBytes.size() <= MaxEquipmentObjectBytes);
                const auto id = object.field(ESM::fourCC("FRMR"), 8).identity();
                covers(result.mCounter, id);
                valid(id != result.mActor
                    && std::find(identities.begin(), identities.begin() + i, id) == identities.begin() + i);
                identities[i] = id;
                const auto baseId = reference(object.sub(ESM::fourCC("NAME")), bindings);
                const auto record = inventoryItemRecord(bindings.mContent, baseId);
                const auto* base = &record;
                valid(base->mType == type
                    && (base->mScript.empty() || (version == ScriptedEquipmentFormatVersion && !base->mEnchant.empty())));
                const auto magnitude = version != EquipmentFormatVersion && id == result.mSlots[InventoryStore::Slot_Shirt]
                    ? MWMechanics::constantFortifyLuckMagnitude(bindings.mContent, base->mEnchant) : 0.f;
                if (id == result.mSlots[InventoryStore::Slot_Shirt])
                    luckMagnitude = magnitude;
                const auto optionalId = [&](uint32_t tag) {
                    if (object.next(tag))
                        reference(object.sub(tag), bindings);
                };
                const auto optionalCString = [&](uint32_t tag) {
                    if (object.next(tag))
                    {
                        const auto value = object.sub(tag).mBytes;
                        valid(!value.empty() && value.back() == '\0');
                        textValue(text(value.first(value.size() - 1)));
                    }
                };
                // Exact stock field order rejects duplicates, unknown fields,
                // unbound locals, Lua, custom state and misplaced animation fields.
                object.optional(ESM::fourCC("XSCL"), 4);
                optionalId(ESM::fourCC("ANAM"));
                optionalCString(ESM::fourCC("BNAM"));
                optionalId(ESM::fourCC("XSOL"));
                optionalId(ESM::fourCC("CNAM"));
                object.optional(ESM::fourCC("INDX"), 4);
                object.optional(ESM::fourCC("XCHG"), 4);
                object.optional(ESM::fourCC("INTV"), 4);
                int32_t count = 1;
                if (object.next(ESM::fourCC("NAM9")))
                    count = std::bit_cast<int32_t>(object.field(ESM::fourCC("NAM9"), 4).number());
                valid(count != std::numeric_limits<int32_t>::min()
                    && (base->mScript.empty() || std::abs(static_cast<int64_t>(count)) <= 1));
                validateEquipmentItemSlots(record, id, count, result.mSlots, result.mNpcStats.has_value());
                total += std::abs(static_cast<int64_t>(count));
                valid(total <= std::numeric_limits<int>::max());
                if (id == result.mSlots[InventoryStore::Slot_Shirt])
                {
                    valid(std::abs(count) == 1);
                    shirtFound = true;
                }
                selectedFound = selectedFound || id == result.mSelected;
                object.optional(ESM::fourCC("DODT"), 24);
                optionalCString(ESM::fourCC("DNAM"));
                object.optional(ESM::fourCC("FLTV"), 4);
                optionalId(ESM::fourCC("KNAM"));
                optionalId(ESM::fourCC("TNAM"));
                object.optional(ESM::fourCC("UNAM"), 1);
                object.field(ESM::fourCC("DATA"), 24);
                if (!base->mScript.empty())
                {
                    const auto& declarations = equipmentDeclarations(bindings.mContent, base->mScript, bindings.mScriptLocals.get());
                    valid(object.field(ESM::fourCC("HLOC"), 1).mBytes[0] == 1);
                    // Canonical stock order; bounded trusted names, exact count/type,
                    // numeric validation before ESMReader or any local allocation.
                    for (char type : { 's', 'l', 'f' })
                        for (const auto& name : declarations.get(type))
                        {
                            valid(text(object.sub(ESM::fourCC("LOCA")).mBytes) == name);
                            const auto value = object.field(type == 'f' ? ESM::fourCC("FLTV") : ESM::fourCC("INTV"), 4).number();
                            if (type == 's')
                            {
                                const auto integer = std::bit_cast<int32_t>(value);
                                valid(integer >= std::numeric_limits<Interpreter::Type_Short>::min()
                                    && integer <= std::numeric_limits<Interpreter::Type_Short>::max());
                            }
                            else if (type == 'f')
                                valid(std::isfinite(std::bit_cast<float>(value)));
                        }
                }
                object.boolean(ESM::fourCC("ENAB"));
                object.optional(ESM::fourCC("POS_"), 24);
                object.optional(ESM::fourCC("FLAG"), 4);
                size_t animations = 0;
                while (object.next(ESM::fourCC("ANIS")))
                {
                    valid(++animations <= PlainEquipmentValues::MaxAnimations);
                    textValue(text(object.sub(ESM::fourCC("ANIS")).mBytes), true);
                    object.optional(ESM::fourCC("TIME"), 4);
                    object.boolean(ESM::fourCC("ABST"));
                    object.field(ESM::fourCC("COUN"), 8);
                }
                valid(object.field(ESM::fourCC("HCUS"), 1).mBytes[0] == 0);
                const auto saved = object.field(ESM::fourCC("XSAV"), 38).mBytes;
                valid(static_cast<unsigned char>(saved[8]) <= 1 && static_cast<unsigned char>(saved[9]) <= 1);
                const auto destination = object.sub(ESM::fourCC("XDST")).mBytes;
                if (!(destination.size() == 1 && destination[0] == 0))
                    textValue(text(destination));
                reference(object.sub(ESM::fourCC("XKEY")), bindings);
                object.field(ESM::fourCC("XPOS"), 24);
                for (size_t animation = 0; animation < animations; ++animation)
                    object.field(ESM::fourCC("XTIM"), 4);
                valid(object.empty());
            }
            for (auto slot : result.mSlots)
                valid(!slot.isSet() || std::find(identities.begin(), identities.begin() + result.mCount, slot)
                    != identities.begin() + result.mCount);
            valid(file.empty() && shirtFound && selectedFound
                && (!result.mNpcStats
                    || result.mNpcStats->mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)][1] == luckMagnitude));
            return result;
        }

        // Seeking supports stock ESMWriter length backpatches. Check every growth
        // before allocation, including a temporary per-object limit.
        class ByteBuffer : public std::streambuf
        {
            size_t mPosition = 0;

        public:
            EquipmentBytes mBytes;
            size_t mLimit = MaxEquipmentBytes;
            std::streamsize xsputn(const char* data, std::streamsize count) override
            {
                valid(count >= 0 && mPosition <= mLimit && static_cast<size_t>(count) <= mLimit - mPosition);
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
            object.save(writer, false);
            const auto& r = object.mRef;
            // Stock serialization clamps scale and omits fractional charge,
            // dormant door/lock fields and nonpositive animation times.
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

    void encodeEquipment(const PlainEquipmentValues& input, const EquipmentBindings& bindings, EquipmentBytes& output)
    {
        validateBindings(bindings);
        input.validate(bindings.mContent, bindings.mEnvelope.mActor, bindings.mScriptLocals.get(), bindings.mMaximumItems);
        for (const auto& object : input.mObjects)
            for (const auto& id : { object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul, object.mRef.mFaction,
                     object.mRef.mKey, object.mRef.mTrap })
                knownId(id, bindings);
        if (input.mNpcStats)
        {
            knownId(input.mNpcStats->mBase, bindings);
            for (const auto id : input.mNpcStats->mSpells)
                knownId(id, bindings);
        }
        ByteBuffer buffer;
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        ESM::ESMWriter writer;
        writer.setVersion(ESM::VER_130);
        writer.setType(0);
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.setRecordCount(static_cast<int>(1 + input.mObjects.size()));
        writer.save(stream);
        writer.startRecord("EQUP");
        const bool scripted = std::any_of(input.mObjects.begin(), input.mObjects.end(),
            [](const auto& object) { return object.mHasLocals != 0; });
        const auto mode = scripted ? ScriptedEquipmentFormatVersion
            : input.mNpcStats ? NpcEquipmentFormatVersion : EquipmentFormatVersion;
        bool slotted = false;
        for (int slot = 0; slot < InventoryStore::Slots; ++slot)
            slotted = slotted || (slot != InventoryStore::Slot_Shirt && input.mSlots[slot].isSet());
        writer.writeHNT("FVER", slotted ? SlottedEquipmentFormatVersion : mode);
        if (slotted) writer.writeHNT("MODE", mode);
        writer.writeHNString("RUNT", bindings.mEnvelope.mRuntime);
        writer.writeHNT("CONT", bindings.mEnvelope.mContent);
        writer.writeFormId(input.mActor, true, "ACTR");
        if (slotted)
        {
            writer.startSubRecord("SLOT");
            for (auto slot : input.mSlots)
            {
                writer.writeT(slot.mIndex);
                writer.writeT(slot.mContentFile);
            }
            writer.endRecord("SLOT");
        }
        else writer.writeFormId(input.mSlots[InventoryStore::Slot_Shirt], true, "SHRT");
        writer.writeFormId(input.mSelected, true, "SELE");
        writer.writeFormId(input.mLastGenerated, true, "LGEN");
        writer.writeHNT("SIZE", static_cast<uint32_t>(input.mObjects.size()));
        if (input.mNpcStats)
        {
            writer.writeHNRefId("NPID", input.mNpcStats->mBase);
            writer.writeHNT("ATTR", input.mNpcStats->mAttributes);
            writer.writeHNT("DYNA", input.mNpcStats->mDynamic);
            const auto& spells = input.mNpcStats->mSpells;
            const auto end = std::find(spells.begin(), spells.end(), ESM::RefId{});
            writer.writeHNT("SCNT", static_cast<uint32_t>(end - spells.begin()));
            for (auto it = spells.begin(); it != end; ++it)
                writer.writeHNRefId("KSPL", *it);
            writer.writeHNT("ABMG", input.mNpcStats->mAbilityMagnitude);
        }
        writer.endRecord("EQUP");
        for (const auto& object : input.mObjects)
        {
            buffer.mLimit = std::min(MaxEquipmentBytes, buffer.mBytes.size() + 16 + MaxEquipmentObjectBytes);
            const auto type = inventoryItemRecord(bindings.mContent, object.mRef.mRefID).mType;
            writer.startRecord(type);
            saveObject(writer, object);
            writer.endRecord(type);
        }
        writer.close();
        output.swap(buffer.mBytes);
    }

    void decodeEquipment(std::span<const char> bytes, const EquipmentBindings& bindings, PlainEquipmentValues& output)
    {
        const auto checked = preflight(bytes, bindings);
        auto stream = std::make_unique<std::istringstream>(std::string(bytes.data(), bytes.size()), std::ios::binary);
        stream->exceptions(std::ios::badbit | std::ios::failbit);
        ESM::ESMReader reader;
        reader.open(std::move(stream), {});
        valid(reader.getRecName() == ESM::fourCC("EQUP"));
        reader.getRecHeader();
        reader.skipRecord();
        PlainEquipmentValues staged{ checked.mActor, checked.mSlots, checked.mSelected, checked.mCounter, {} };
        staged.mNpcStats = checked.mNpcStats;
        staged.mObjects.reserve(checked.mCount);
        for (size_t i = 0; i < checked.mCount; ++i)
        {
            valid(ContainerStore::isStorableType(reader.getRecName().toInt()));
            reader.getRecHeader();
            auto& object = staged.mObjects.emplace_back();
            object.blank();
            object.mRef.loadId(reader, true);
            object.load(reader);
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
        valid(!reader.hasMoreRecs());
        staged.validate(bindings.mContent, bindings.mEnvelope.mActor, bindings.mScriptLocals.get(), bindings.mMaximumItems);
        // Reject redundant default fields and inconsistent stock/lossless fields.
        EquipmentBytes canonical;
        encodeEquipment(staged, bindings, canonical);
        valid(canonical.size() == bytes.size() && std::equal(canonical.begin(), canonical.end(), bytes.begin()));
        output.swap(staged);
    }
}
