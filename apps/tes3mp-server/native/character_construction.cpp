#include "character_construction.hpp"
#include <tes3mp/character_creation_protocol.hpp>

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
#include <map>
#include <stdexcept>
#include <cstdio>

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
                    throw std::invalid_argument("Native character stat/sex domain invalid");
                const auto spellLimit = mStore.get<ESM::GameSetting>().find("iAutoPCSpellMax")->mValue.getInteger();
                if (spellLimit < 0 || spellLimit > 65536 || mStore.get<ESM::Spell>().getSize() > 65536)
                    throw std::invalid_argument("Native character starting spell budget invalid");
                const auto eligible = std::ranges::count_if(mStore.get<ESM::Spell>(), [](const auto& spell) {
                    return spell.mData.mType == ESM::Spell::ST_Spell && (spell.mData.mFlags & ESM::Spell::F_PCStart);
                });
                // Stock uses 100 by default. Bound potential allocation rather
                // than rejecting that setting when only a few spells qualify.
                // Zero never reaches the stock replacement branch: all qualify.
                if ((spellLimit ? std::min<size_t>(size_t(eligible), size_t(spellLimit)) : size_t(eligible)) > MaximumStartingSpells)
                    throw std::invalid_argument("Native character starting spell selection exceeds budget");
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
                    if (index < 0 || index >= int(attributes.size()) || attributes[index])
                        throw std::invalid_argument("Native character class attribute domain invalid");
                    attributes[index] = true;
                }
                std::array<bool, CharacterSkillCount> skills{};
                for (const auto& pair : selected->mData.mSkills)
                    for (const auto index : pair)
                    {
                        if (index < 0 || index >= int(skills.size()) || skills[index])
                            throw std::invalid_argument("Native character class skill domain invalid");
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
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native character derivation rejected: %.240s\n", error.what());
                return {};
            }
            catch (...) { return {}; }
        };
    }

    CharacterContentCatalog withNativeCharacterConstruction(
        const CharacterContentCatalog& selections, const MWWorld::ESMStore& store)
    {
        return selections.withConstruction(std::make_shared<NativeCharacterConstruction>(store));
    }

    ConstructedCharacter constructNativeCharacter(const CharacterProfile& profile,
        const ESM::NPC& base, const MWWorld::ESMStore& store)
    {
        if (profile.lifecycle() != CharacterLifecycle::EstablishedCharacter || !profile.appearance()
            || !profile.characterClass() || !profile.birthsign() || !base.mScript.empty())
            throw std::invalid_argument("Native character completion requires a complete unscripted body");
        const auto derived = NativeCharacterConstruction(store).derive(*profile.appearance(),
            *profile.characterClass(), *profile.birthsign());
        if (!derived || *derived != profile.derived())
            throw std::invalid_argument("Native character derivation differs from winning content");
        ConstructedCharacter result{base, MWMechanics::NpcStats(store)};
        auto& body = result.body;
        body.mName = profile.name();
        body.mRace = resolve<ESM::Race>(store, profile.appearance()->race)->mId;
        body.mHead = resolve<ESM::BodyPart>(store, profile.appearance()->head)->mId;
        body.mHair = resolve<ESM::BodyPart>(store, profile.appearance()->hair)->mId;
        body.mFlags = profile.appearance()->sex == CharacterSex::Female ? ESM::NPC::Female : 0;
        body.mNpdtType = ESM::NPC::NPC_DEFAULT;
        body.mNpdt.mLevel = 1;
        body.mModel.clear();
        body.mFaction = {};
        body.mInventory.mList.clear();
        body.mSpells.mList.clear();
        if (const auto* cls = std::get_if<ClassRecordId>(&*profile.characterClass()))
            body.mClass = resolve<ESM::Class>(store, *cls)->mId;
        else
            // Custom classes stay in the owned profile; do not inherit the
            // template's class or mutate shared winning content.
            body.mClass = {};
        for (auto spell : derived->startingSpells)
            body.mSpells.mList.push_back(resolve<ESM::Spell>(store, spell)->mId);
        const float multiplier = store.get<ESM::GameSetting>().find("fPCbaseMagickaMult")->mValue.getFloat();
        if (!std::isfinite(multiplier) || multiplier < 0 || multiplier > 1000)
            throw std::invalid_argument("Native player magicka multiplier invalid");
        result.stats.setLevel(1);
        for (size_t i = 0; i < derived->attributes.size(); ++i)
        {
            MWMechanics::AttributeValue value;
            value.setBase(derived->attributes[i]);
            result.stats.setAttribute(ESM::Attribute::indexToRefId(int(i)), value, multiplier);
        }
        for (size_t i = 0; i < derived->skills.size(); ++i)
            result.stats.getSkill(ESM::Skill::indexToRefId(int(i))).setBase(derived->skills[i]);
        result.stats.updateHealth();
        result.stats.recalculateMagicka(multiplier);
        for (int i = 0; i < 3; ++i)
        {
            auto value = result.stats.getDynamic(i);
            value.setCurrent(value.getModified());
            result.stats.setDynamic(i, value);
        }
        return result;
    }

    std::vector<char> encodeNativeCharacterProfile(PlayerId player, const CharacterProfile& profile)
    {
        const auto bytes = encodeReliableCharacterProfile({SessionId::fromValue(1).value(),
            SessionGeneration::initial(), player, CharacterConfirmationResult::Confirmed, profile});
        return {reinterpret_cast<const char*>(bytes.data()), reinterpret_cast<const char*>(bytes.data() + bytes.size())};
    }
    CharacterProfile decodeNativeCharacterProfile(PlayerId player, std::span<const char> bytes)
    {
        auto decoded = decodeReliableCharacterProfile(std::as_bytes(bytes));
        const auto* value = std::get_if<ReliableCharacterProfile>(&decoded);
        if (!value || value->playerId != player || value->targetSessionId.value() != 1
            || value->targetSessionGeneration != SessionGeneration::initial()
            || value->result != CharacterConfirmationResult::Confirmed
            || value->profile.lifecycle() != CharacterLifecycle::EstablishedCharacter)
            throw std::invalid_argument("Native completed profile invalid");
        return value->profile;
    }

    std::optional<CharacterContentCatalog> discoverNativeCharacterContent(ContentManifestId manifest,
        Transform creationSpawn, Transform completionSpawn, std::span<const StartingItem> startingInventory,
        const MWWorld::ESMStore& store) noexcept
    try
    {
        if (startingInventory.size() > MaximumStartingItems) return {};
        // Bound external record scans as well as the selectable output. Body parts
        // include non-player anatomy; rejecting overflow avoids an unbounded scan.
        constexpr size_t MaximumScannedRecords = 65536;
        if (store.get<ESM::Race>().getSize() > MaximumScannedRecords
            || store.get<ESM::Class>().getSize() > MaximumScannedRecords
            || store.get<ESM::BirthSign>().getSize() > MaximumScannedRecords
            || store.get<ESM::BodyPart>().getSize() > MaximumScannedRecords
            || store.get<ESM::Spell>().getSize() > MaximumScannedRecords) return {};
        std::map<uint64_t, ESM::RefId> raceIds, classIds, signIds, partIds, spellIds;
        const auto identify = [](auto& ids, const ESM::RefId& record) -> std::optional<uint64_t> {
            const auto raw = characterRecordId(record.getRefIdString());
            if (!raw) return {};
            const auto [found, inserted] = ids.emplace(*raw, record);
            if (!inserted && found->second != record) return {};
            return raw;
        };
        for (const auto& entry : store.get<ESM::Spell>())
            if (const auto* record = store.get<ESM::Spell>().search(entry.mId);
                record && !identify(spellIds, record->mId)) return {};
        const auto spells = [&](const ESM::SpellList& source) -> std::optional<std::vector<SpellRecordId>> {
            if (source.mList.size() > MaximumStartingSpells) return {};
            std::vector<SpellRecordId> result;
            for (const auto& record : source.mList)
            {
                const auto raw = identify(spellIds, record);
                if (!raw || !store.get<ESM::Spell>().search(record)) return {};
                const auto id = SpellRecordId::fromValue(*raw);
                if (!id) return {};
                result.push_back(*id);
            }
            std::ranges::sort(result);
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        };
        std::vector<CharacterRaceDefinition> races;
        std::map<ESM::RefId, size_t> raceIndexes;
        for (const auto& entry : store.get<ESM::Race>())
        {
            const auto* record = store.get<ESM::Race>().search(entry.mId);
            if (!record || !(record->mData.mFlags & ESM::Race::Playable)
                || raceIndexes.contains(record->mId)) continue;
            if (races.size() == MaximumCharacterSelections) return {};
            const auto raw = identify(raceIds, record->mId);
            const auto powers = spells(record->mPowers);
            if (!raw || !powers) return {};
            CharacterRaceDefinition race{RaceRecordId::fromValue(*raw).value(), {}};
            race.spells = *powers;
            raceIndexes.emplace(record->mId, races.size());
            races.push_back(std::move(race));
        }
        struct Parts { std::array<std::vector<uint64_t>, 2> heads, hair; };
        std::vector<Parts> parts(races.size());
        for (const auto& entry : store.get<ESM::BodyPart>())
        {
            const auto* record = store.get<ESM::BodyPart>().search(entry.mId);
            if (!record || record->mData.mType != ESM::BodyPart::MT_Skin
                || ESM::isFirstPersonBodyPart(*record)
                || (record->mData.mFlags & ESM::BodyPart::BPF_NotPlayable)) continue;
            const auto race = raceIndexes.find(record->mRace);
            if (race == raceIndexes.end()) continue;
            const bool head = record->mData.mPart == ESM::BodyPart::MP_Head;
            if (!head && record->mData.mPart != ESM::BodyPart::MP_Hair) continue;
            const auto raw = identify(partIds, record->mId);
            if (!raw) return {};
            const size_t sex = (record->mData.mFlags & ESM::BodyPart::BPF_Female) ? 0 : 1;
            auto& ids = head ? parts[race->second].heads[sex] : parts[race->second].hair[sex];
            if (std::ranges::find(ids, *raw) != ids.end()) continue;
            if (ids.size() == MaximumRaceAppearances) return {};
            ids.push_back(*raw);
        }
        for (size_t i = 0; i < races.size(); ++i)
            for (size_t sex = 0; sex < 2; ++sex)
            {
                auto& heads = parts[i].heads[sex];
                auto& hair = parts[i].hair[sex];
                // Check the Cartesian product before constructing appearances.
                const auto remaining = MaximumRaceAppearances - races[i].appearances.size();
                if (!hair.empty() && heads.size() > remaining / hair.size()) return {};
                std::ranges::sort(heads); std::ranges::sort(hair);
                for (const auto head : heads) for (const auto style : hair)
                    races[i].appearances.push_back({races[i].id, HeadRecordId::fromValue(head).value(),
                        HairRecordId::fromValue(style).value(), static_cast<CharacterSex>(sex)});
            }
        // A playable record lacking usable head/hair cannot be faithfully offered.
        if (std::ranges::any_of(races, [](const auto& race) { return race.appearances.empty(); }))
        {
            for (const auto& [record, index] : raceIndexes) if (races[index].appearances.empty())
                std::fprintf(stderr, "Native character race has no compatible head/hair: %.128s\n", record.getRefIdString().data());
            return {};
        }
        std::vector<CharacterClassDefinition> classes;
        for (const auto& entry : store.get<ESM::Class>())
        {
            const auto* record = store.get<ESM::Class>().search(entry.mId);
            if (!record || !record->mData.mIsPlayable) continue;
            const auto raw = identify(classIds, record->mId);
            if (!raw) return {};
            if (std::ranges::any_of(classes, [&](const auto& value) { return value.id.value() == *raw; })) continue;
            if (classes.size() == MaximumCharacterSelections || record->mData.mSpecialization < 0
                || record->mData.mSpecialization > 2) return {};
            CharacterClassDefinition cls{ClassRecordId::fromValue(*raw).value(),
                static_cast<ClassSpecialization>(record->mData.mSpecialization)};
            for (size_t i = 0; i < cls.favoredAttributes.size(); ++i)
            {
                const auto value = record->mData.mAttribute[i];
                if (value < 0 || value >= int(CharacterAttributeCount)) return {};
                cls.favoredAttributes[i] = static_cast<uint8_t>(value);
            }
            for (size_t i = 0; i < cls.minorSkills.size(); ++i)
            {
                const auto minor = record->mData.mSkills[i][0], major = record->mData.mSkills[i][1];
                if (minor < 0 || minor >= int(CharacterSkillCount) || major < 0 || major >= int(CharacterSkillCount)) return {};
                cls.minorSkills[i] = static_cast<uint8_t>(minor); cls.majorSkills[i] = static_cast<uint8_t>(major);
            }
            classes.push_back(cls);
        }
        std::vector<CharacterBirthsignDefinition> signs;
        for (const auto& entry : store.get<ESM::BirthSign>())
        {
            const auto* record = store.get<ESM::BirthSign>().search(entry.mId);
            if (!record) continue;
            const auto raw = identify(signIds, record->mId);
            const auto powers = spells(record->mPowers);
            if (!raw || !powers) return {};
            if (std::ranges::any_of(signs, [&](const auto& value) { return value.id.value() == *raw; })) continue;
            if (signs.size() == MaximumCharacterSelections) return {};
            signs.push_back({BirthsignRecordId::fromValue(*raw).value(), *powers});
        }
        auto catalog = CharacterContentCatalog::create(manifest, creationSpawn, completionSpawn,
            races, classes, signs, startingInventory);
        if (!catalog) { std::fprintf(stderr, "Native discovered character catalog validation failed\n"); return {}; }
        return withNativeCharacterConstruction(*catalog, store);
    }
    catch (...) { return {}; }
}
