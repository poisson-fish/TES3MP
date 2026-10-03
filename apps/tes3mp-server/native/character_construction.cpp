#include "character_construction.hpp"

#include <apps/openmw/mwmechanics/autocalcspell.hpp>
#include <apps/openmw/mwmechanics/playerconstruction.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadspel.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace TES3MP::Native
{
    namespace
    {
        template<class Record, class Id>
        const Record* resolve(const MWWorld::ESMStore& store, Id id)
        {
            const Record* result = nullptr;
            for (const auto& record : store.get<Record>())
                if (characterRecordId(record.mId.getRefIdString()) == id.value())
                {
                    if (result && result->mId != record.mId) return nullptr; // Hash ambiguity never authorizes a choice.
                    result = store.get<Record>().search(record.mId);
                }
            return result;
        }

        class NativeCharacterConstruction final : public CharacterConstruction
        {
            const MWWorld::ESMStore& mStore;
        public:
            explicit NativeCharacterConstruction(const MWWorld::ESMStore& store) : mStore(store) {}

            std::optional<CharacterDerivedState> derive(const CharacterAppearance& appearance,
                const CharacterClass& characterClass, BirthsignRecordId birthsign) const noexcept override
            try
            {
                if (static_cast<unsigned>(appearance.sex) > 1
                    || mStore.get<ESM::Attribute>().getSize() != CharacterAttributeCount
                    || mStore.get<ESM::Skill>().getSize() != CharacterSkillCount)
                    return {};
                const auto spellLimit = mStore.get<ESM::GameSetting>().find("iAutoPCSpellMax")->mValue.getInteger();
                if (spellLimit <= 0 || spellLimit > int(MaximumStartingSpells)) return {};
                const auto* race = resolve<ESM::Race>(mStore, appearance.race);
                const auto* head = resolve<ESM::BodyPart>(mStore, appearance.head);
                const auto* hair = resolve<ESM::BodyPart>(mStore, appearance.hair);
                const auto* sign = resolve<ESM::BirthSign>(mStore, birthsign);
                const bool female = appearance.sex == CharacterSex::Female;
                const auto validPart = [&](const ESM::BodyPart* part, int type) {
                    return part && part->mRace == race->mId && part->mData.mPart == type
                        && part->mData.mType == ESM::BodyPart::MT_Skin && !ESM::isFirstPersonBodyPart(*part)
                        && !(part->mData.mFlags & ESM::BodyPart::BPF_NotPlayable)
                        && bool(part->mData.mFlags & ESM::BodyPart::BPF_Female) == female;
                };
                if (!race || !(race->mData.mFlags & ESM::Race::Playable) || !sign
                    || !validPart(head, ESM::BodyPart::MP_Head) || !validPart(hair, ESM::BodyPart::MP_Hair))
                    return {};
                if (race->mPowers.mList.size() > MaximumStartingSpells
                    || sign->mPowers.mList.size() > MaximumStartingSpells) return {};
                ESM::Class custom;
                const ESM::Class* selected = nullptr;
                if (const auto* predefined = std::get_if<ClassRecordId>(&characterClass))
                {
                    selected = resolve<ESM::Class>(mStore, *predefined);
                    if (!selected || !selected->mData.mIsPlayable) return {};
                }
                else
                {
                    // Profile validation bounds indexes and rejects duplicates.
                    const auto& value = std::get<CustomClassDefinition>(characterClass);
                    custom.blank();
                    custom.mData.mSpecialization = static_cast<int>(value.specialization);
                    for (size_t i = 0; i < value.favoredAttributes.size(); ++i)
                        custom.mData.mAttribute[i] = value.favoredAttributes[i];
                    for (size_t i = 0; i < value.minorSkills.size(); ++i)
                    {
                        custom.mData.mSkills[i][0] = value.minorSkills[i];
                        custom.mData.mSkills[i][1] = value.majorSkills[i];
                    }
                    selected = &custom;
                }
                if (selected->mData.mSpecialization < 0 || selected->mData.mSpecialization > 2) return {};
                std::array<bool, CharacterAttributeCount> attributes{};
                for (const auto index : selected->mData.mAttribute)
                {
                    if (index < 0 || index >= int(attributes.size()) || attributes[index]) return {};
                    attributes[index] = true;
                }
                std::array<bool, CharacterSkillCount> skills{};
                for (const auto& pair : selected->mData.mSkills)
                    for (const auto index : pair)
                    {
                        if (index < 0 || index >= int(skills.size()) || skills[index]) return {};
                        skills[index] = true;
                    }
                // Only sex is needed from the NPC base once race/class are selected.
                ESM::NPC base;
                base.blank();
                base.mFlags = female ? ESM::NPC::Female : 0;
                const auto stats = MWMechanics::buildPlayerStats(base, race, selected, mStore);
                CharacterDerivedState result;
                const auto encodeStat = [](float value) -> std::optional<uint16_t> {
                    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<uint16_t>::max()
                        || std::floor(value) != value) return {};
                    return static_cast<uint16_t>(value);
                };
                for (size_t i = 0; i < result.attributes.size(); ++i)
                {
                    const auto value = encodeStat(stats.mAttributes.at(ESM::Attribute::indexToRefId(int(i))).getBase());
                    if (!value) return {};
                    result.attributes[i] = *value;
                }
                for (size_t i = 0; i < result.skills.size(); ++i)
                {
                    const auto value = encodeStat(stats.mSkills.at(ESM::Skill::indexToRefId(int(i))).getBase());
                    if (!value) return {};
                    result.skills[i] = *value;
                }
                const auto addSpell = [&](ESM::RefId record) {
                    const auto raw = characterRecordId(record.getRefIdString());
                    if (!raw || !mStore.get<ESM::Spell>().search(record)) return false;
                    const auto id = SpellRecordId::fromValue(*raw);
                    if (!id || !resolve<ESM::Spell>(mStore, *id)) return false;
                    if (std::ranges::find(result.startingSpells, *id) != result.startingSpells.end()) return true;
                    if (result.startingSpells.size() == MaximumStartingSpells) return false;
                    result.startingSpells.push_back(*id);
                    return true;
                };
                for (const auto& spell : race->mPowers.mList) if (!addSpell(spell)) return {};
                for (const auto& spell : sign->mPowers.mList) if (!addSpell(spell)) return {};
                for (const auto& spell : MWMechanics::autoCalcPlayerSpells(stats.mSkills, stats.mAttributes, race, mStore))
                    if (!addSpell(spell)) return {};
                std::ranges::sort(result.startingSpells);
                return result;
            }
            catch (...) { return {}; }
        };
    }

    CharacterContentCatalog withNativeCharacterConstruction(
        const CharacterContentCatalog& selections, const MWWorld::ESMStore& store)
    {
        return selections.withConstruction(std::make_shared<NativeCharacterConstruction>(store));
    }
}
