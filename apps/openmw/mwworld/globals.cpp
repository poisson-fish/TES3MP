#include "globals.hpp"
#include "defaultglobals.hpp"

#include <stdexcept>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include "esmstore.hpp"

namespace MWWorld
{
    std::vector<std::pair<GlobalVariableName, ESM::Variant>> generateDefaultGlobals()
    {
        return {
            // vanilla Morrowind does not define dayspassed.
            { Globals::sDaysPassed, ESM::Variant(1) }, // but the addons start counting at 1 :(
            { Globals::sWerewolfClawMult, ESM::Variant(25.f) },
            { Globals::sPCKnownWerewolf, ESM::Variant(0) },
            // following should exist in all versions of MW, but not necessarily in TCs
            { Globals::sGameHour, ESM::Variant(0) },
            { Globals::sTimeScale, ESM::Variant(30.f) },
            { Globals::sDay, ESM::Variant(1) },
            { Globals::sYear, ESM::Variant(1) },
            { Globals::sPCRace, ESM::Variant(0) },
            { Globals::sPCHasCrimeGold, ESM::Variant(0) },
            { Globals::sCrimeGoldDiscount, ESM::Variant(0) },
            { Globals::sCrimeGoldTurnIn, ESM::Variant(0) },
            { Globals::sPCHasTurnIn, ESM::Variant(0) },
        };
    }

    Globals::Collection::const_iterator Globals::find(std::string_view name) const
    {
        Collection::const_iterator iter = mVariables.find(name);

        if (iter == mVariables.end())
            throw std::runtime_error("unknown global variable: " + std::string{ name });

        return iter;
    }

    Globals::Collection::iterator Globals::find(std::string_view name)
    {
        Collection::iterator iter = mVariables.find(name);

        if (iter == mVariables.end())
            throw std::runtime_error("unknown global variable: " + std::string{ name });

        return iter;
    }

    void Globals::fill(const MWWorld::ESMStore& store)
    {
        mVariables.clear();

        const MWWorld::Store<ESM::Global>& globals = store.get<ESM::Global>();

        for (const ESM::Global& esmGlobal : globals)
        {
            mVariables.emplace(esmGlobal.mId, esmGlobal);
        }
        for (const auto& [name, value] : generateDefaultGlobals())
        {
            ESM::Global record;
            record.mId = ESM::RefId::stringRefId(name.getValue());
            record.mValue = value;
            record.mRecordFlags = 0;
            mVariables.emplace(record.mId, std::move(record));
        }
    }

    const ESM::Variant& Globals::operator[](GlobalVariableName name) const
    {
        return find(name.getValue())->second.mValue;
    }

    ESM::Variant& Globals::operator[](GlobalVariableName name)
    {
        return find(name.getValue())->second.mValue;
    }

    char Globals::getType(GlobalVariableName name) const
    {
        Collection::const_iterator iter = mVariables.find(name.getValue());

        if (iter == mVariables.end())
            return ' ';

        switch (iter->second.mValue.getType())
        {
            case ESM::VT_Short:
                return 's';
            case ESM::VT_Long:
                return 'l';
            case ESM::VT_Float:
                return 'f';

            default:
                return ' ';
        }
    }

    size_t Globals::countSavedGameRecords() const
    {
        return mVariables.size();
    }

    void Globals::write(ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        for (const auto& variable : mVariables)
        {
            writer.startRecord(ESM::REC_GLOB);
            variable.second.save(writer);
            writer.endRecord(ESM::REC_GLOB);
        }
    }

    bool Globals::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type == ESM::REC_GLOB)
        {
            ESM::Global global;
            bool isDeleted = false;

            // This readRecord() method is used when reading a saved game.
            // Deleted globals can't appear there, so isDeleted will be ignored here.
            global.load(reader, isDeleted);

            if (const auto iter = mVariables.find(global.mId); iter != mVariables.end())
                iter->second = std::move(global);

            return true;
        }

        return false;
    }
}
