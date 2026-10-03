#include "playerconstruction.hpp"

#include "../mwworld/esmstore.hpp"
#include <components/esm/attr.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>
#include <algorithm>

namespace MWMechanics
{
    PlayerConstructionStats buildPlayerStats(const ESM::NPC& base, const ESM::Race* race,
        const ESM::Class* characterClass, const MWWorld::ESMStore& store)
    {
        PlayerConstructionStats result;
        for (size_t i = 0; i < base.mNpdt.mSkills.size(); ++i)
            result.mSkills[ESM::Skill::indexToRefId(static_cast<int>(i))].setBase(base.mNpdt.mSkills[i]);
        // Preserve the stock pre-race initialization, including its skill-array
        // fallback for attributes. Selecting a race replaces these values.
        for (size_t i = 0; i < base.mNpdt.mAttributes.size(); ++i)
            result.mAttributes[ESM::Attribute::indexToRefId(static_cast<int>(i))].setBase(base.mNpdt.mSkills[i]);
        if (race)
        {
            const bool male = (base.mFlags & ESM::NPC::Female) == 0;
            for (const ESM::Attribute& attribute : store.get<ESM::Attribute>())
                result.mAttributes[attribute.mId].setBase(static_cast<float>(race->mData.getAttribute(attribute.mId, male)));
            for (const ESM::Skill& skill : store.get<ESM::Skill>())
            {
                const auto bonus = std::find_if(race->mData.mBonus.begin(), race->mData.mBonus.end(),
                    [&](const auto& value) { return value.mSkill == ESM::Skill::refIdToIndex(skill.mId); });
                result.mSkills[skill.mId].setBase(5.f + (bonus == race->mData.mBonus.end() ? 0 : bonus->mBonus));
            }
        }
        if (characterClass)
        {
            for (int attribute : characterClass->mData.mAttribute)
            {
                const auto id = ESM::Attribute::indexToRefId(attribute);
                if (!id.empty()) result.mAttributes[id].setBase(result.mAttributes[id].getBase() + 10);
            }
            for (int i = 0; i < 2; ++i)
                for (const auto& skills : characterClass->mData.mSkills)
                {
                    const auto id = ESM::Skill::indexToRefId(skills[i]);
                    if (!id.empty()) result.mSkills[id].setBase(result.mSkills[id].getBase() + (i == 0 ? 10 : 25));
                }
            for (const ESM::Skill& skill : store.get<ESM::Skill>())
                if (skill.mData.mSpecialization == characterClass->mData.mSpecialization)
                    result.mSkills[skill.mId].setBase(result.mSkills[skill.mId].getBase() + 5);
        }
        return result;
    }
}
