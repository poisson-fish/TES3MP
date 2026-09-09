#include <tes3mp/character_profile.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <limits>
#include <ranges>

namespace TES3MP
{
    std::optional<std::uint64_t> characterRecordId(std::string_view recordId) noexcept
    {
        if (recordId.empty() || recordId.size() > 256)
            return std::nullopt;
        std::uint64_t value = 14695981039346656037ull;
        for (const unsigned char byte : recordId)
        {
            if (byte < 0x20 || byte == 0x7f)
                return std::nullopt;
            const auto normalized = byte >= 'A' && byte <= 'Z' ? static_cast<unsigned char>(byte + ('a' - 'A')) : byte;
            value ^= normalized;
            value *= 1099511628211ull;
        }
        return value == 0 ? std::optional<std::uint64_t>{} : std::optional<std::uint64_t>{ value };
    }

    namespace
    {
        bool validUtf8(std::string_view value) noexcept
        {
            for (std::size_t i = 0; i < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[i]);
                if (first < 0x80)
                {
                    if (first < 0x20 || first == 0x7f) return false;
                    ++i;
                    continue;
                }
                std::size_t count = 0;
                std::uint32_t codepoint = 0;
                if ((first & 0xe0) == 0xc0) { count = 2; codepoint = first & 0x1f; }
                else if ((first & 0xf0) == 0xe0) { count = 3; codepoint = first & 0x0f; }
                else if ((first & 0xf8) == 0xf0) { count = 4; codepoint = first & 0x07; }
                else return false;
                if (i + count > value.size()) return false;
                for (std::size_t j = 1; j < count; ++j)
                {
                    const auto next = static_cast<unsigned char>(value[i + j]);
                    if ((next & 0xc0) != 0x80) return false;
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                if ((count == 2 && codepoint < 0x80) || (count == 3 && codepoint < 0x800)
                    || (count == 4 && codepoint < 0x10000) || codepoint > 0x10ffff
                    || (codepoint >= 0xd800 && codepoint <= 0xdfff)
                    || (codepoint >= 0x80 && codepoint <= 0x9f))
                    return false;
                i += count;
            }
            return true;
        }

        bool validCustomClass(const CustomClassDefinition& value) noexcept
        {
            if (!isValidCharacterText(value.name, MaximumCustomClassNameBytes)
                || (!value.description.empty()
                    && !isValidCharacterText(value.description, MaximumCustomClassDescriptionBytes)))
                return false;
            if (static_cast<unsigned>(value.specialization) > static_cast<unsigned>(ClassSpecialization::Stealth))
                return false;
            std::array<bool, CharacterAttributeCount> attributes{};
            for (auto id : value.favoredAttributes)
                if (id >= CharacterAttributeCount || std::exchange(attributes[id], true)) return false;
            std::array<bool, CharacterSkillCount> skills{};
            for (auto id : value.minorSkills)
                if (id >= CharacterSkillCount || std::exchange(skills[id], true)) return false;
            for (auto id : value.majorSkills)
                if (id >= CharacterSkillCount || std::exchange(skills[id], true)) return false;
            return true;
        }

        CharacterDerivedState derive(const CharacterContentCatalog& catalog, const CharacterAppearance& appearance,
            const CharacterClass& selectedClass, BirthsignRecordId birthsign)
        {
            CharacterDerivedState result;
            const auto* race = catalog.find(appearance.race);
            const auto& base = appearance.sex == CharacterSex::Male ? race->maleAttributes : race->femaleAttributes;
            result.attributes = base;
            for (std::size_t i = 0; i < result.skills.size(); ++i)
                result.skills[i] = static_cast<std::uint16_t>(std::max<int>(0, 5 + race->skillBonuses[i]));
            result.startingSpells = race->spells;
            auto addClass = [&](const auto& definition) {
                for (auto id : definition.favoredAttributes)
                    result.attributes[id] = static_cast<std::uint16_t>(result.attributes[id] + 10);
                const auto specializationBegin = static_cast<std::size_t>(definition.specialization) * 9;
                for (std::size_t id = specializationBegin; id < specializationBegin + 9; ++id)
                    result.skills[id] = static_cast<std::uint16_t>(result.skills[id] + 5);
                for (auto id : definition.minorSkills)
                    result.skills[id] = static_cast<std::uint16_t>(result.skills[id] + 10);
                for (auto id : definition.majorSkills)
                    result.skills[id] = static_cast<std::uint16_t>(result.skills[id] + 25);
            };
            if (const auto* predefined = std::get_if<ClassRecordId>(&selectedClass))
                addClass(*catalog.find(*predefined));
            else
                addClass(std::get<CustomClassDefinition>(selectedClass));
            const auto* sign = catalog.find(birthsign);
            for (const auto spell : sign->spells)
                if (std::ranges::find(result.startingSpells, spell) == result.startingSpells.end())
                    result.startingSpells.push_back(spell);
            std::ranges::sort(result.startingSpells);
            return result;
        }

        bool completeSelections(std::string_view name, const std::optional<CharacterAppearance>& appearance,
            const std::optional<CharacterClass>& characterClass,
            const std::optional<BirthsignRecordId>& birthsign) noexcept
        { return !name.empty() && appearance && characterClass && birthsign; }
    }

    bool isValidCharacterText(std::string_view value, std::size_t maximumBytes) noexcept
    {
        return !value.empty() && value.size() <= maximumBytes && value.front() != ' ' && value.back() != ' '
            && validUtf8(value);
    }

    CharacterProfile::CharacterProfile(CharacterLifecycle lifecycle, CharacterCreationPhase phase, std::string name,
        std::optional<CharacterAppearance> appearance, std::optional<CharacterClass> characterClass,
        std::optional<BirthsignRecordId> birthsign, CharacterDerivedState derived,
        std::vector<StartingItem> inventory, CharacterProfileRevision revision) noexcept
        : mLifecycle(lifecycle), mPhase(phase), mName(std::move(name)), mAppearance(appearance),
          mClass(std::move(characterClass)), mBirthsign(birthsign), mDerived(std::move(derived)),
          mStartingInventory(std::move(inventory)), mRevision(revision)
    {
    }

    CharacterProfile CharacterProfile::fresh()
    {
        return CharacterProfile(CharacterLifecycle::NewCharacter, CharacterCreationPhase::AwaitingName, {},
            std::nullopt, std::nullopt, std::nullopt, {}, {}, CharacterProfileRevision::initial());
    }

    std::optional<CharacterProfile> CharacterProfile::restore(CharacterLifecycle lifecycle,
        CharacterCreationPhase phase, std::string name, std::optional<CharacterAppearance> appearance,
        std::optional<CharacterClass> characterClass, std::optional<BirthsignRecordId> birthsign,
        CharacterDerivedState derived, std::vector<StartingItem> inventory,
        CharacterProfileRevision revision) noexcept
    try
    {
        if (static_cast<unsigned>(lifecycle) < 1 || static_cast<unsigned>(lifecycle) > 3
            || static_cast<unsigned>(phase) < 1 || static_cast<unsigned>(phase) > 6
            || inventory.size() > MaximumStartingItems || derived.startingSpells.size() > MaximumStartingSpells
            || (!name.empty() && !isValidCharacterText(name, MaximumCharacterNameBytes)))
            return std::nullopt;
        const bool complete = completeSelections(name, appearance, characterClass, birthsign);
        const bool emptyDerived = std::ranges::all_of(derived.attributes, [](auto value) { return value == 0; })
            && std::ranges::all_of(derived.skills, [](auto value) { return value == 0; })
            && derived.startingSpells.empty();
        bool phaseValid = false;
        switch (phase)
        {
            case CharacterCreationPhase::AwaitingName:
                phaseValid = lifecycle == CharacterLifecycle::NewCharacter
                    && revision == CharacterProfileRevision::initial() && name.empty() && !appearance
                    && !characterClass && !birthsign && emptyDerived && inventory.empty();
                break;
            case CharacterCreationPhase::AwaitingRace:
                phaseValid = lifecycle == CharacterLifecycle::CreatingCharacter && !name.empty() && !appearance
                    && !characterClass && !birthsign && emptyDerived && inventory.empty();
                break;
            case CharacterCreationPhase::AwaitingClass:
                phaseValid = lifecycle == CharacterLifecycle::CreatingCharacter && !name.empty() && appearance
                    && !characterClass && !birthsign && emptyDerived && inventory.empty();
                break;
            case CharacterCreationPhase::AwaitingBirthsign:
                phaseValid = lifecycle == CharacterLifecycle::CreatingCharacter && !name.empty() && appearance
                    && characterClass && !birthsign && emptyDerived && inventory.empty();
                break;
            case CharacterCreationPhase::AwaitingReview:
                phaseValid = lifecycle == CharacterLifecycle::CreatingCharacter && complete;
                break;
            case CharacterCreationPhase::Complete:
                phaseValid = lifecycle == CharacterLifecycle::EstablishedCharacter && complete;
                break;
        }
        if (!phaseValid || (lifecycle != CharacterLifecycle::NewCharacter
                && revision == CharacterProfileRevision::initial())
            || std::ranges::any_of(inventory, [](const auto& item) {
                   return item.count == 0 || (item.equipmentSlot && *item.equipmentSlot >= 19);
               }))
            return std::nullopt;
        if (characterClass && std::holds_alternative<CustomClassDefinition>(*characterClass)
            && !validCustomClass(std::get<CustomClassDefinition>(*characterClass)))
            return std::nullopt;
        return CharacterProfile(lifecycle, phase, std::move(name), appearance, std::move(characterClass), birthsign,
            std::move(derived), std::move(inventory), revision);
    }
    catch (...) { return std::nullopt; }

    CharacterContentCatalog::CharacterContentCatalog(ContentManifestId manifest, Transform creationSpawn,
        Transform completionSpawn,
        std::vector<CharacterRaceDefinition> races, std::vector<CharacterClassDefinition> classes,
        std::vector<CharacterBirthsignDefinition> birthsigns, std::vector<StartingItem> inventory) noexcept
        : mManifest(manifest), mCreationSpawn(creationSpawn), mCompletionSpawn(completionSpawn),
          mRaces(std::move(races)), mClasses(std::move(classes)),
          mBirthsigns(std::move(birthsigns)), mStartingInventory(std::move(inventory)) {}

    std::optional<CharacterContentCatalog> CharacterContentCatalog::create(ContentManifestId manifest,
        Transform creationSpawn, Transform completionSpawn, std::span<const CharacterRaceDefinition> races,
        std::span<const CharacterClassDefinition> classes,
        std::span<const CharacterBirthsignDefinition> birthsigns,
        std::span<const StartingItem> startingInventory) noexcept
    try
    {
        if (races.empty() || classes.empty() || birthsigns.empty() || races.size() > 256 || classes.size() > 256
            || birthsigns.size() > 256 || startingInventory.size() > MaximumStartingItems)
            return std::nullopt;
        auto raceValues = std::vector<CharacterRaceDefinition>(races.begin(), races.end());
        auto classValues = std::vector<CharacterClassDefinition>(classes.begin(), classes.end());
        auto birthValues = std::vector<CharacterBirthsignDefinition>(birthsigns.begin(), birthsigns.end());
        auto inventory = std::vector<StartingItem>(startingInventory.begin(), startingInventory.end());
        std::ranges::sort(raceValues, {}, &CharacterRaceDefinition::id);
        std::ranges::sort(classValues, {}, &CharacterClassDefinition::id);
        std::ranges::sort(birthValues, {}, &CharacterBirthsignDefinition::id);
        if (std::ranges::adjacent_find(raceValues, {}, &CharacterRaceDefinition::id) != raceValues.end()
            || std::ranges::adjacent_find(classValues, {}, &CharacterClassDefinition::id) != classValues.end()
            || std::ranges::adjacent_find(birthValues, {}, &CharacterBirthsignDefinition::id) != birthValues.end())
            return std::nullopt;
        for (const auto& race : raceValues)
        {
            if (race.appearances.empty() || race.appearances.size() > 1024
                || race.spells.size() > MaximumStartingSpells
                || std::ranges::any_of(race.appearances, [&](const auto& value) { return value.race != race.id; }))
                return std::nullopt;
        }
        for (const auto& definition : classValues)
        {
            CustomClassDefinition validation{ "valid", {}, definition.specialization,
                definition.favoredAttributes, definition.minorSkills, definition.majorSkills };
            if (!validCustomClass(validation)) return std::nullopt;
        }
        for (const auto& sign : birthValues)
            if (sign.spells.size() > MaximumStartingSpells) return std::nullopt;
        for (const auto& item : inventory)
            if (item.count == 0 || (item.equipmentSlot && *item.equipmentSlot >= 19)) return std::nullopt;
        return CharacterContentCatalog(manifest, creationSpawn, completionSpawn, std::move(raceValues),
            std::move(classValues), std::move(birthValues), std::move(inventory));
    }
    catch (...) { return std::nullopt; }

    const CharacterRaceDefinition* CharacterContentCatalog::find(RaceRecordId id) const noexcept
    { const auto it = std::ranges::lower_bound(mRaces, id, {}, &CharacterRaceDefinition::id); return it != mRaces.end() && it->id == id ? &*it : nullptr; }
    const CharacterClassDefinition* CharacterContentCatalog::find(ClassRecordId id) const noexcept
    { const auto it = std::ranges::lower_bound(mClasses, id, {}, &CharacterClassDefinition::id); return it != mClasses.end() && it->id == id ? &*it : nullptr; }
    const CharacterBirthsignDefinition* CharacterContentCatalog::find(BirthsignRecordId id) const noexcept
    { const auto it = std::ranges::lower_bound(mBirthsigns, id, {}, &CharacterBirthsignDefinition::id); return it != mBirthsigns.end() && it->id == id ? &*it : nullptr; }

    CharacterProfileApplyResult applyCharacterCreation(const CharacterProfile& current,
        const CharacterContentCatalog& catalog, const CharacterCreationCommand& command) noexcept
    try
    {
        if (current.lifecycle() == CharacterLifecycle::EstablishedCharacter)
            return CharacterProfileError::AlreadyEstablished;
        if (command.expectedRevision != current.revision()) return CharacterProfileError::StaleRevision;
        const auto nextRevision = current.revision().next();
        if (!nextRevision) return CharacterProfileError::RevisionExhausted;
        auto name = current.name();
        auto appearance = current.appearance();
        auto selectedClass = current.characterClass();
        auto birthsign = current.birthsign();
        auto phase = current.phase();
        bool completing = false;
        if (const auto* nameChoice = std::get_if<SetCharacterName>(&command.choice))
        {
            if (phase != CharacterCreationPhase::AwaitingName && phase != CharacterCreationPhase::AwaitingReview)
                return CharacterProfileError::WrongPhase;
            if (!isValidCharacterText(nameChoice->name, MaximumCharacterNameBytes)) return CharacterProfileError::InvalidName;
            name = nameChoice->name;
            if (phase == CharacterCreationPhase::AwaitingName) phase = CharacterCreationPhase::AwaitingRace;
        }
        else if (const auto* appearanceChoice = std::get_if<SetCharacterAppearance>(&command.choice))
        {
            if (phase != CharacterCreationPhase::AwaitingRace && phase != CharacterCreationPhase::AwaitingReview)
                return CharacterProfileError::WrongPhase;
            const auto* race = catalog.find(appearanceChoice->appearance.race);
            if (!race) return CharacterProfileError::UnknownRace;
            if (std::ranges::find(race->appearances, appearanceChoice->appearance) == race->appearances.end())
                return CharacterProfileError::InvalidAppearance;
            appearance = appearanceChoice->appearance;
            if (phase == CharacterCreationPhase::AwaitingRace) phase = CharacterCreationPhase::AwaitingClass;
        }
        else if (const auto* classChoice = std::get_if<SetCharacterClass>(&command.choice))
        {
            if (phase != CharacterCreationPhase::AwaitingClass && phase != CharacterCreationPhase::AwaitingReview)
                return CharacterProfileError::WrongPhase;
            if (const auto* predefined = std::get_if<ClassRecordId>(&classChoice->characterClass))
            { if (!catalog.find(*predefined)) return CharacterProfileError::UnknownClass; }
            else if (!validCustomClass(std::get<CustomClassDefinition>(classChoice->characterClass)))
                return CharacterProfileError::InvalidCustomClass;
            selectedClass = classChoice->characterClass;
            if (phase == CharacterCreationPhase::AwaitingClass) phase = CharacterCreationPhase::AwaitingBirthsign;
        }
        else if (const auto* birthsignChoice = std::get_if<SetCharacterBirthsign>(&command.choice))
        {
            if (phase != CharacterCreationPhase::AwaitingBirthsign && phase != CharacterCreationPhase::AwaitingReview)
                return CharacterProfileError::WrongPhase;
            if (!catalog.find(birthsignChoice->birthsign)) return CharacterProfileError::UnknownBirthsign;
            birthsign = birthsignChoice->birthsign;
            phase = CharacterCreationPhase::AwaitingReview;
        }
        else
        {
            if (phase != CharacterCreationPhase::AwaitingReview) return CharacterProfileError::WrongPhase;
            if (!completeSelections(name, appearance, selectedClass, birthsign)) return CharacterProfileError::Incomplete;
            phase = CharacterCreationPhase::Complete;
            completing = true;
        }
        CharacterDerivedState derived;
        std::vector<StartingItem> inventory;
        if (completeSelections(name, appearance, selectedClass, birthsign))
        {
            derived = derive(catalog, *appearance, *selectedClass, *birthsign);
            inventory.assign(catalog.startingInventory().begin(), catalog.startingInventory().end());
        }
        auto result = CharacterProfile::restore(completing ? CharacterLifecycle::EstablishedCharacter
                                                           : CharacterLifecycle::CreatingCharacter,
            phase, std::move(name), appearance, std::move(selectedClass), birthsign,
            std::move(derived), std::move(inventory), *nextRevision);
        return result ? CharacterProfileApplyResult(std::move(*result)) : CharacterProfileError::InvalidInitialState;
    }
    catch (...) { return CharacterProfileError::AllocationFailure; }
}
