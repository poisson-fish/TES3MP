#ifndef TES3MP_CHARACTER_PROFILE_HPP
#define TES3MP_CHARACTER_PROFILE_HPP

#include "content_identity.hpp"
#include "value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumCharacterNameBytes = 64;
    inline constexpr std::size_t MaximumCustomClassNameBytes = 64;
    inline constexpr std::size_t MaximumCustomClassDescriptionBytes = 512;
    inline constexpr std::size_t CharacterAttributeCount = 8;
    inline constexpr std::size_t CharacterSkillCount = 27;
    inline constexpr std::size_t MaximumStartingSpells = 64;
    inline constexpr std::size_t MaximumStartingItems = 64;

    enum class CharacterLifecycle : std::uint8_t
    {
        NewCharacter = 1,
        CreatingCharacter = 2,
        EstablishedCharacter = 3,
    };

    enum class CharacterCreationPhase : std::uint8_t
    {
        AwaitingName = 1,
        AwaitingRace = 2,
        AwaitingClass = 3,
        AwaitingBirthsign = 4,
        AwaitingReview = 5,
        Complete = 6,
    };

    enum class CharacterSex : std::uint8_t { Female = 0, Male = 1 };
    enum class ClassSpecialization : std::uint8_t { Combat = 0, Magic = 1, Stealth = 2 };

    struct CharacterAppearance
    {
        RaceRecordId race;
        HeadRecordId head;
        HairRecordId hair;
        CharacterSex sex = CharacterSex::Female;
        friend constexpr bool operator==(CharacterAppearance, CharacterAppearance) noexcept = default;
    };

    struct CustomClassDefinition
    {
        std::string name;
        std::string description;
        ClassSpecialization specialization = ClassSpecialization::Combat;
        std::array<std::uint8_t, 2> favoredAttributes{};
        std::array<std::uint8_t, 5> minorSkills{};
        std::array<std::uint8_t, 5> majorSkills{};
        friend bool operator==(const CustomClassDefinition&, const CustomClassDefinition&) noexcept = default;
    };

    using CharacterClass = std::variant<ClassRecordId, CustomClassDefinition>;

    struct CharacterDerivedState
    {
        std::array<std::uint16_t, CharacterAttributeCount> attributes{};
        std::array<std::uint16_t, CharacterSkillCount> skills{};
        std::vector<SpellRecordId> startingSpells;
        friend bool operator==(const CharacterDerivedState&, const CharacterDerivedState&) noexcept = default;
    };

    struct StartingItem
    {
        ItemPrototypeId prototype;
        std::uint32_t count = 1;
        std::optional<std::uint8_t> equipmentSlot;
        friend constexpr bool operator==(StartingItem, StartingItem) noexcept = default;
    };

    class CharacterProfile
    {
    public:
        static CharacterProfile fresh();
        static std::optional<CharacterProfile> restore(CharacterLifecycle lifecycle, CharacterCreationPhase phase,
            std::string name, std::optional<CharacterAppearance> appearance,
            std::optional<CharacterClass> characterClass, std::optional<BirthsignRecordId> birthsign,
            CharacterDerivedState derived, std::vector<StartingItem> inventory,
            CharacterProfileRevision revision) noexcept;

        CharacterLifecycle lifecycle() const noexcept { return mLifecycle; }
        CharacterCreationPhase phase() const noexcept { return mPhase; }
        const std::string& name() const noexcept { return mName; }
        const std::optional<CharacterAppearance>& appearance() const noexcept { return mAppearance; }
        const std::optional<CharacterClass>& characterClass() const noexcept { return mClass; }
        std::optional<BirthsignRecordId> birthsign() const noexcept { return mBirthsign; }
        const CharacterDerivedState& derived() const noexcept { return mDerived; }
        std::span<const StartingItem> startingInventory() const noexcept { return mStartingInventory; }
        CharacterProfileRevision revision() const noexcept { return mRevision; }

        friend bool operator==(const CharacterProfile&, const CharacterProfile&) noexcept = default;

    private:
        friend struct CharacterProfileMutationAccess;
        CharacterProfile(CharacterLifecycle lifecycle, CharacterCreationPhase phase, std::string name,
            std::optional<CharacterAppearance> appearance, std::optional<CharacterClass> characterClass,
            std::optional<BirthsignRecordId> birthsign, CharacterDerivedState derived,
            std::vector<StartingItem> inventory, CharacterProfileRevision revision) noexcept;

        CharacterLifecycle mLifecycle;
        CharacterCreationPhase mPhase;
        std::string mName;
        std::optional<CharacterAppearance> mAppearance;
        std::optional<CharacterClass> mClass;
        std::optional<BirthsignRecordId> mBirthsign;
        CharacterDerivedState mDerived;
        std::vector<StartingItem> mStartingInventory;
        CharacterProfileRevision mRevision;
    };

    struct CharacterRaceDefinition
    {
        RaceRecordId id;
        std::vector<CharacterAppearance> appearances;
        std::array<std::uint16_t, CharacterAttributeCount> femaleAttributes{};
        std::array<std::uint16_t, CharacterAttributeCount> maleAttributes{};
        std::array<std::int16_t, CharacterSkillCount> skillBonuses{};
        std::vector<SpellRecordId> spells;
    };

    struct CharacterClassDefinition
    {
        ClassRecordId id;
        ClassSpecialization specialization = ClassSpecialization::Combat;
        std::array<std::uint8_t, 2> favoredAttributes{};
        std::array<std::uint8_t, 5> minorSkills{};
        std::array<std::uint8_t, 5> majorSkills{};
    };

    struct CharacterBirthsignDefinition
    {
        BirthsignRecordId id;
        std::vector<SpellRecordId> spells;
    };

    class CharacterContentCatalog
    {
    public:
        static std::optional<CharacterContentCatalog> create(ContentManifestId manifest,
            Transform creationSpawn, Transform completionSpawn, std::span<const CharacterRaceDefinition> races,
            std::span<const CharacterClassDefinition> classes,
            std::span<const CharacterBirthsignDefinition> birthsigns,
            std::span<const StartingItem> startingInventory) noexcept;

        ContentManifestId manifest() const noexcept { return mManifest; }
        const Transform& creationSpawn() const noexcept { return mCreationSpawn; }
        const Transform& completionSpawn() const noexcept { return mCompletionSpawn; }
        const CharacterRaceDefinition* find(RaceRecordId id) const noexcept;
        const CharacterClassDefinition* find(ClassRecordId id) const noexcept;
        const CharacterBirthsignDefinition* find(BirthsignRecordId id) const noexcept;
        std::span<const StartingItem> startingInventory() const noexcept { return mStartingInventory; }

    private:
        CharacterContentCatalog(ContentManifestId manifest, Transform creationSpawn, Transform completionSpawn,
            std::vector<CharacterRaceDefinition> races, std::vector<CharacterClassDefinition> classes,
            std::vector<CharacterBirthsignDefinition> birthsigns, std::vector<StartingItem> inventory) noexcept;
        ContentManifestId mManifest;
        Transform mCreationSpawn;
        Transform mCompletionSpawn;
        std::vector<CharacterRaceDefinition> mRaces;
        std::vector<CharacterClassDefinition> mClasses;
        std::vector<CharacterBirthsignDefinition> mBirthsigns;
        std::vector<StartingItem> mStartingInventory;
    };

    struct SetCharacterName { std::string name; friend bool operator==(const SetCharacterName&, const SetCharacterName&) = default; };
    struct SetCharacterAppearance { CharacterAppearance appearance; friend bool operator==(const SetCharacterAppearance&, const SetCharacterAppearance&) = default; };
    struct SetCharacterClass { CharacterClass characterClass; friend bool operator==(const SetCharacterClass&, const SetCharacterClass&) = default; };
    struct SetCharacterBirthsign { BirthsignRecordId birthsign; friend bool operator==(const SetCharacterBirthsign&, const SetCharacterBirthsign&) = default; };
    struct CompleteCharacterCreation { friend constexpr bool operator==(CompleteCharacterCreation, CompleteCharacterCreation) = default; };
    using CharacterCreationChoice = std::variant<SetCharacterName, SetCharacterAppearance,
        SetCharacterClass, SetCharacterBirthsign, CompleteCharacterCreation>;

    struct CharacterCreationCommand
    {
        CharacterProfileRevision expectedRevision;
        CharacterCreationChoice choice;
        friend bool operator==(const CharacterCreationCommand&, const CharacterCreationCommand&) = default;
    };

    enum class CharacterProfileError : std::uint8_t
    {
        InvalidInitialState,
        StaleRevision,
        AlreadyEstablished,
        WrongPhase,
        InvalidName,
        UnknownRace,
        InvalidAppearance,
        UnknownClass,
        InvalidCustomClass,
        UnknownBirthsign,
        Incomplete,
        RevisionExhausted,
        PersistenceFailed,
        AllocationFailure,
    };

    using CharacterProfileApplyResult = std::variant<CharacterProfile, CharacterProfileError>;
    CharacterProfileApplyResult applyCharacterCreation(const CharacterProfile& current,
        const CharacterContentCatalog& catalog, const CharacterCreationCommand& command) noexcept;
    bool isValidCharacterText(std::string_view value, std::size_t maximumBytes) noexcept;
    std::optional<std::uint64_t> characterRecordId(std::string_view recordId) noexcept;
}

#endif
