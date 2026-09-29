#ifndef OPENMW_MWMECHANICS_WEREWOLFSTATS_HPP
#define OPENMW_MWMECHANICS_WEREWOLFSTATS_HPP

#include <array>

#include <components/esm/attr.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadskil.hpp>

#include "npcstats.hpp"
#include "../mwworld/esmstore.hpp"

namespace MWMechanics
{
    struct WerewolfSavedStats
    {
        std::array<float, ESM::Skill::Length> skills{};
        std::array<float, ESM::Attribute::Length> attributes{};
    };

    inline WerewolfSavedStats saveWerewolfStats(const NpcStats& stats)
    {
        WerewolfSavedStats result;
        for (size_t i = 0; i < result.skills.size(); ++i)
            result.skills[i] = stats.getSkill(ESM::Skill::indexToRefId(static_cast<int>(i))).getModified();
        for (size_t i = 0; i < result.attributes.size(); ++i)
            result.attributes[i] = stats.getAttribute(ESM::Attribute::indexToRefId(static_cast<int>(i))).getModified();
        return result;
    }

    inline void applyWerewolfStats(NpcStats& stats, const MWWorld::ESMStore& content)
    {
        const auto& gmst = content.get<ESM::GameSetting>();
        const float magickaMultiplier = gmst.find("fPCbaseMagickaMult")->mValue.getFloat();
        const DynamicStat<float> health = stats.getDynamic(0);
        stats.setHealth(health.getBase() * gmst.find("fWereWolfHealth")->mValue.getFloat());
        for (const auto& attribute : content.get<ESM::Attribute>())
        {
            AttributeValue value = stats.getAttribute(attribute.mId);
            value.setBase(value.getBase(), true);
            value.setModifier(attribute.mWerewolfValue - value.getBase());
            stats.setAttribute(attribute.mId, value, magickaMultiplier);
        }
        for (const auto& skill : content.get<ESM::Skill>())
        {
            if (skill.mId == ESM::Skill::Acrobatics)
                continue;
            SkillValue& value = stats.getSkill(skill.mId);
            value.setBase(value.getBase(), true);
            value.setModifier(skill.mWerewolfValue - value.getBase());
        }
    }

    inline void restoreWerewolfStats(NpcStats& stats, const MWWorld::ESMStore& content,
        const WerewolfSavedStats& saved)
    {
        const auto& gmst = content.get<ESM::GameSetting>();
        const float magickaMultiplier = gmst.find("fPCbaseMagickaMult")->mValue.getFloat();
        const DynamicStat<float> health = stats.getDynamic(0);
        stats.setHealth(health.getBase() / gmst.find("fWereWolfHealth")->mValue.getFloat());
        for (size_t i = 0; i < saved.skills.size(); ++i)
        {
            SkillValue& skill = stats.getSkill(ESM::Skill::indexToRefId(static_cast<int>(i)));
            skill.restore(skill.getDamage());
            skill.setModifier(saved.skills[i] - skill.getBase());
        }
        for (size_t i = 0; i < saved.attributes.size(); ++i)
        {
            const auto id = ESM::Attribute::indexToRefId(static_cast<int>(i));
            AttributeValue attribute = stats.getAttribute(id);
            attribute.restore(attribute.getDamage());
            attribute.setModifier(saved.attributes[i] - attribute.getBase());
            stats.setAttribute(id, attribute, magickaMultiplier);
        }
    }
}

#endif
