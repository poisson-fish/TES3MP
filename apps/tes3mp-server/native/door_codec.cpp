#include "door_codec.hpp"

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        void valid(bool value)
        {
            if (!value) throw std::invalid_argument("Invalid or incompatible native door bytes");
        }
        struct Cursor
        {
            std::span<const char> bytes;
            std::span<const char> take(size_t size)
            {
                valid(size <= bytes.size());
                const auto result = bytes.first(size);
                bytes = bytes.subspan(size);
                return result;
            }
            uint32_t number()
            {
                const auto data = take(4);
                uint32_t result = 0;
                for (size_t i = 0; i < 4; ++i)
                    result |= uint32_t(static_cast<unsigned char>(data[i])) << (8 * i);
                return result;
            }
            bool next(uint32_t tag) const
            {
                auto copy = *this;
                return !bytes.empty() && copy.number() == tag;
            }
            Cursor field(uint32_t tag, size_t size)
            {
                valid(number() == tag && number() == size);
                return {take(size)};
            }
        };
        std::vector<char> save(const ESM::DoorState& state)
        {
            std::ostringstream stream(std::ios::binary);
            stream.exceptions(std::ios::badbit | std::ios::failbit);
            ESM::ESMWriter writer;
            writer.setVersion(ESM::VER_130);
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.setType(0);
            writer.setRecordCount(1);
            writer.save(stream);
            writer.startRecord(ESM::REC_DOOR, 0);
            state.save(writer);
            writer.endRecord(ESM::REC_DOOR);
            writer.close();
            const auto bytes = stream.str();
            valid(bytes.size() <= MaxDoorBytes);
            return {bytes.begin(), bytes.end()};
        }
        // Current stock TES3 header: record header, FORM and HEDR. No strings.
        constexpr size_t HeaderBytes = 16 + 12 + 28;
        constexpr size_t RecordHeaderBytes = 16;
    }

    DoorBinding::DoorBinding(const ESM::Door& base, const ESM::CellRef& placement)
        : mDoor(base, placement), mInitialPosition(placement.mPos)
    {
        auto state = mDoor.initialState();
        // With default object state, the record contains only CellRef::save.
        state.mHasCustomState = true;
        mTemplate = save(state);
        preflight(mTemplate);
    }

    void DoorBinding::preflight(std::span<const char> bytes) const
    {
        valid(bytes.size() >= mTemplate.size() && bytes.size() <= MaxDoorBytes);
        Cursor file{bytes};
        const auto header = file.take(HeaderBytes);
        valid(std::equal(header.begin(), header.end(), mTemplate.begin()));
        valid(file.number() == ESM::REC_DOOR);
        valid(file.number() == bytes.size() - HeaderBytes - RecordHeaderBytes);
        valid(file.number() == 0 && file.number() == 0);
        const auto reference = file.take(mTemplate.size() - HeaderBytes - RecordHeaderBytes);
        valid(std::equal(reference.begin(), reference.end(), mTemplate.begin() + HeaderBytes + RecordHeaderBytes));
        // The only variable fields are stock position, custom-state flag and
        // ANIM direction, in stock order. No external names enter ESMReader.
        if (file.next(ESM::fourCC("POS_")))
        {
            auto position = file.field(ESM::fourCC("POS_"), 24);
            ESM::Position value;
            for (float& coordinate : value.pos) coordinate = std::bit_cast<float>(position.number());
            for (float& angle : value.rot) angle = std::bit_cast<float>(position.number());
            mDoor.validatePosition(value);
            valid(value != mInitialPosition);
        }
        bool custom = true;
        if (file.next(ESM::fourCC("HCUS")))
        {
            valid(file.field(ESM::fourCC("HCUS"), 1).bytes[0] == 0);
            custom = false;
        }
        if (file.next(ESM::fourCC("ANIM")))
        {
            const auto direction = file.field(ESM::fourCC("ANIM"), 4).number();
            valid(custom && direction >= 1 && direction <= 2);
        }
        valid(file.bytes.empty());
    }

    void encodeDoor(const ESM::DoorState& state, const DoorBinding& binding, std::vector<char>& output)
    {
        binding.door().validate(state);
        auto bytes = save(state);
        binding.preflight(bytes);
        output.swap(bytes);
    }

    std::shared_ptr<const ESM::DoorState> decodeDoor(std::span<const char> bytes, const DoorBinding& binding)
    {
        binding.preflight(bytes);
        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(std::string(bytes.data(), bytes.size()), std::ios::binary), {});
        valid(reader.getRecName() == ESM::REC_DOOR);
        reader.getRecHeader();
        auto state = std::make_shared<ESM::DoorState>();
        state->blank();
        state->mRef.loadId(reader, true);
        state->load(reader);
        valid(!reader.hasMoreSubs() && !reader.hasMoreRecs());
        binding.door().validate(*state);
        return state;
    }
}
