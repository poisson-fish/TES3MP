#include <tes3mp/character_profile.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <variant>

namespace
{
    using namespace TES3MP;
    template <class T> T id(std::uint64_t value) { return T::fromValue(value).value(); }
    void require(bool value) { if (!value) std::abort(); }

    CharacterContentCatalog catalog()
    {
        const auto zero = Turn32::fromValue(0);
        const CharacterAppearance female{ id<RaceRecordId>(1), id<HeadRecordId>(10), id<HairRecordId>(20), CharacterSex::Female };
        const CharacterAppearance male{ id<RaceRecordId>(1), id<HeadRecordId>(11), id<HairRecordId>(21), CharacterSex::Male };
        CharacterRaceDefinition race{ id<RaceRecordId>(1), { female, male } };
        race.femaleAttributes.fill(30);
        race.maleAttributes.fill(40);
        race.skillBonuses[3] = 5;
        race.spells = { id<SpellRecordId>(1) };
        CharacterClassDefinition cls{ id<ClassRecordId>(1), ClassSpecialization::Combat, { 0, 1 },
            { 2, 3, 4, 5, 6 }, { 7, 8, 9, 10, 11 } };
        CharacterBirthsignDefinition sign{ id<BirthsignRecordId>(1), { id<SpellRecordId>(2) } };
        const StartingItem item{ id<ItemPrototypeId>(1), 1, 0 };
        return *CharacterContentCatalog::create(testContentManifestId(),
            Transform(CellId::interior(id<CellSpaceId>(7)), Position3(61, -135, 24),
                Orientation3(zero, zero, Turn32::fromValue(4056358002u))),
            Transform(CellId::exterior(id<CellSpaceId>(8), 0, 0), Position3(100, 200, 300),
                Orientation3(zero, zero, zero)),
            std::span(&race, 1), std::span(&cls, 1), std::span(&sign, 1), std::span(&item, 1));
    }

    CharacterProfile apply(CharacterProfile current, CharacterCreationChoice choice)
    {
        auto result = applyCharacterCreation(current, catalog(), { current.revision(), std::move(choice) });
        require(std::holds_alternative<CharacterProfile>(result));
        return std::get<CharacterProfile>(std::move(result));
    }
}

int main()
{
    auto profile = CharacterProfile::fresh();
    require(profile.lifecycle() == CharacterLifecycle::NewCharacter
        && profile.phase() == CharacterCreationPhase::AwaitingName);
    require(!CharacterProfile::restore(CharacterLifecycle::CreatingCharacter,
        CharacterCreationPhase::AwaitingRace, {}, std::nullopt, std::nullopt, std::nullopt, {}, {},
        id<CharacterProfileRevision>(2)));
    require(!CharacterProfile::restore(CharacterLifecycle::CreatingCharacter,
        CharacterCreationPhase::AwaitingClass, "Jiub", std::nullopt, std::nullopt, std::nullopt, {}, {},
        id<CharacterProfileRevision>(2)));
    const auto stale = applyCharacterCreation(profile, catalog(),
        { id<CharacterProfileRevision>(2), SetCharacterName{ "Nerevarine" } });
    require(std::get<CharacterProfileError>(stale) == CharacterProfileError::StaleRevision
        && profile.lifecycle() == CharacterLifecycle::NewCharacter);
    require(std::get<CharacterProfileError>(applyCharacterCreation(profile, catalog(),
        { profile.revision(), SetCharacterName{ " bad" } })) == CharacterProfileError::InvalidName);

    profile = apply(std::move(profile), SetCharacterName{ "Nerevarine" });
    require(profile.lifecycle() == CharacterLifecycle::CreatingCharacter
        && profile.phase() == CharacterCreationPhase::AwaitingRace);
    profile = apply(std::move(profile), SetCharacterAppearance{
        { id<RaceRecordId>(1), id<HeadRecordId>(10), id<HairRecordId>(20), CharacterSex::Female } });
    profile = apply(std::move(profile), SetCharacterClass{ id<ClassRecordId>(1) });
    profile = apply(std::move(profile), SetCharacterBirthsign{ id<BirthsignRecordId>(1) });
    require(profile.phase() == CharacterCreationPhase::AwaitingReview
        && profile.derived().attributes[0] == 40 && profile.derived().skills[3] == 25
        && profile.derived().skills[7] == 35 && profile.derived().startingSpells.size() == 2);
    profile = apply(std::move(profile), CompleteCharacterCreation{});
    require(profile.lifecycle() == CharacterLifecycle::EstablishedCharacter
        && profile.phase() == CharacterCreationPhase::Complete && profile.startingInventory().size() == 1);
    require(std::get<CharacterProfileError>(applyCharacterCreation(profile, catalog(),
        { profile.revision(), SetCharacterName{ "Other" } })) == CharacterProfileError::AlreadyEstablished);
    std::cout << "character profile contracts passed\n";
}
