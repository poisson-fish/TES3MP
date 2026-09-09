#include <tes3mp/character_creation_protocol.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/client_character_creation_command_generated.h"
#include "generated/reliable_character_profile_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace TES3MP
{
    namespace
    {
        constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
        constexpr std::size_t MinimumBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;
        CharacterProtocolError error(CharacterProtocolErrorCode code, std::size_t observed = 0,
            std::size_t limit = 0) noexcept { return { code, observed, limit }; }

        std::optional<CharacterProtocolError> prefix(std::span<const std::byte> payload) noexcept
        {
            if (payload.size() < MinimumBufferBytes) return error(CharacterProtocolErrorCode::PayloadTooSmall,
                payload.size(), MinimumBufferBytes);
            if (payload.size() > ReliableOperationMaximumPayloadBytes) return error(
                CharacterProtocolErrorCode::PayloadTooLarge, payload.size(), ReliableOperationMaximumPayloadBytes);
            const auto declared = flatbuffers::GetSizePrefixedBufferLength(
                reinterpret_cast<const std::uint8_t*>(payload.data()));
            if (declared != payload.size()) return error(CharacterProtocolErrorCode::PayloadLengthMismatch,
                payload.size(), declared);
            return std::nullopt;
        }

        flatbuffers::Verifier verifier(std::span<const std::byte> payload)
        {
            flatbuffers::Verifier::Options options;
            options.max_depth = 8;
            options.max_tables = 4;
            options.max_size = ReliableOperationMaximumPayloadBytes + 1;
            return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
        }

        std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
        {
            const auto* first = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
            return { first, first + builder.GetSize() };
        }

        bool nonempty(const flatbuffers::String* value) noexcept { return value && !value->string_view().empty(); }

        template <class T>
        T copyScalar(const flatbuffers::Vector<T>* source, std::size_t index) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T result{};
            std::memcpy(&result, source->Data() + index * sizeof(T), sizeof(T));
            return flatbuffers::EndianScalar(result);
        }

        bool hasCommandOnlyDefaults(const Protocol::Schema::CharacterCreation::ClientCharacterCreationCommand& value,
            bool allowName, bool allowAppearance, bool allowClass, bool allowCustom, bool allowBirthsign) noexcept
        {
            return (allowName || value.name() == nullptr)
                && (allowAppearance || (value.race_id() == 0 && value.head_id() == 0 && value.hair_id() == 0))
                && (allowClass || value.class_id() == 0)
                && (allowCustom || (value.custom_class_name() == nullptr
                    && value.custom_class_description() == nullptr && value.favored_attributes() == nullptr
                    && value.minor_skills() == nullptr && value.major_skills() == nullptr))
                && (allowBirthsign || value.birthsign_id() == 0);
        }

        template <class T> std::optional<T> strong(std::uint64_t value) noexcept { return T::fromValue(value); }
        template <class Vector, std::size_t Size>
        bool copyBytes(const Vector* source, std::array<std::uint8_t, Size>& target) noexcept
        {
            if (!source || source->size() != Size) return false;
            for (std::size_t i = 0; i < Size; ++i) target[i] = copyScalar(source, i);
            return true;
        }
    }

    CharacterConfirmationResult characterConfirmationResult(CharacterProfileError value) noexcept
    {
        switch (value)
        {
            case CharacterProfileError::StaleRevision: return CharacterConfirmationResult::StaleRevision;
            case CharacterProfileError::AlreadyEstablished: return CharacterConfirmationResult::AlreadyEstablished;
            case CharacterProfileError::WrongPhase: return CharacterConfirmationResult::WrongPhase;
            case CharacterProfileError::InvalidName: return CharacterConfirmationResult::InvalidName;
            case CharacterProfileError::UnknownRace: return CharacterConfirmationResult::UnknownRace;
            case CharacterProfileError::InvalidAppearance: return CharacterConfirmationResult::InvalidAppearance;
            case CharacterProfileError::UnknownClass: return CharacterConfirmationResult::UnknownClass;
            case CharacterProfileError::InvalidCustomClass: return CharacterConfirmationResult::InvalidCustomClass;
            case CharacterProfileError::UnknownBirthsign: return CharacterConfirmationResult::UnknownBirthsign;
            case CharacterProfileError::Incomplete: return CharacterConfirmationResult::Incomplete;
            default: return CharacterConfirmationResult::InternalError;
        }
    }

    std::string_view characterConfirmationResultName(CharacterConfirmationResult value) noexcept
    {
        switch (value)
        {
            case CharacterConfirmationResult::Confirmed: return "Confirmed";
            case CharacterConfirmationResult::StaleRevision: return "StaleRevision";
            case CharacterConfirmationResult::AlreadyEstablished: return "AlreadyEstablished";
            case CharacterConfirmationResult::WrongPhase: return "WrongPhase";
            case CharacterConfirmationResult::InvalidName: return "InvalidName";
            case CharacterConfirmationResult::UnknownRace: return "UnknownRace";
            case CharacterConfirmationResult::InvalidAppearance: return "InvalidAppearance";
            case CharacterConfirmationResult::UnknownClass: return "UnknownClass";
            case CharacterConfirmationResult::InvalidCustomClass: return "InvalidCustomClass";
            case CharacterConfirmationResult::UnknownBirthsign: return "UnknownBirthsign";
            case CharacterConfirmationResult::Incomplete: return "Incomplete";
            case CharacterConfirmationResult::InternalError: return "InternalError";
        }
        return "UnknownCharacterConfirmationResult";
    }

    std::vector<std::byte> encodeClientCharacterCreationCommand(const ClientCharacterCreationCommand& value)
    {
        namespace S = Protocol::Schema::CharacterCreation;
        flatbuffers::FlatBufferBuilder builder;
        flatbuffers::Offset<flatbuffers::String> name, customName, customDescription;
        flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> attributes, minor, major;
        S::CommandKind kind = S::CommandKind::Unknown;
        std::uint64_t race = 0, head = 0, hair = 0, classId = 0, birthsign = 0;
        auto sex = S::CharacterSex::Female;
        auto specialization = S::ClassSpecialization::Combat;
        if (const auto* choice = std::get_if<SetCharacterName>(&value.command.choice))
        { kind = S::CommandKind::SetName; name = builder.CreateString(choice->name); }
        else if (const auto* choice = std::get_if<SetCharacterAppearance>(&value.command.choice))
        {
            kind = S::CommandKind::SetAppearance; race = choice->appearance.race.value();
            head = choice->appearance.head.value(); hair = choice->appearance.hair.value();
            sex = static_cast<S::CharacterSex>(choice->appearance.sex);
        }
        else if (const auto* choice = std::get_if<SetCharacterClass>(&value.command.choice))
        {
            if (const auto* predefined = std::get_if<ClassRecordId>(&choice->characterClass))
            { kind = S::CommandKind::SetClass; classId = predefined->value(); }
            else
            {
                kind = S::CommandKind::SetCustomClass;
                const auto& custom = std::get<CustomClassDefinition>(choice->characterClass);
                customName = builder.CreateString(custom.name); customDescription = builder.CreateString(custom.description);
                specialization = static_cast<S::ClassSpecialization>(custom.specialization);
                attributes = builder.CreateVector(custom.favoredAttributes.data(), custom.favoredAttributes.size());
                minor = builder.CreateVector(custom.minorSkills.data(), custom.minorSkills.size());
                major = builder.CreateVector(custom.majorSkills.data(), custom.majorSkills.size());
            }
        }
        else if (const auto* choice = std::get_if<SetCharacterBirthsign>(&value.command.choice))
        { kind = S::CommandKind::SetBirthsign; birthsign = choice->birthsign.value(); }
        else kind = S::CommandKind::Complete;
        S::ClientCharacterCreationCommandBuilder root(builder);
        root.add_session_id(value.sessionId.value()); root.add_session_generation(value.sessionGeneration.value());
        root.add_command_sequence(value.commandSequence.value()); root.add_command_id(value.commandId.value());
        root.add_observed_canonical_revision(value.observedCanonicalRevision.value());
        root.add_expected_profile_revision(value.command.expectedRevision.value()); root.add_kind(kind);
        if (name.o) root.add_name(name); root.add_race_id(race); root.add_head_id(head); root.add_hair_id(hair);
        root.add_sex(sex); root.add_class_id(classId);
        if (customName.o) root.add_custom_class_name(customName);
        if (customDescription.o) root.add_custom_class_description(customDescription);
        root.add_specialization(specialization);
        if (attributes.o) root.add_favored_attributes(attributes);
        if (minor.o) root.add_minor_skills(minor); if (major.o) root.add_major_skills(major);
        root.add_birthsign_id(birthsign);
        const auto offset = root.Finish();
        S::FinishSizePrefixedClientCharacterCreationCommandBuffer(builder, offset);
        return take(builder);
    }

    ClientCharacterCreationCommandDecodeResult decodeClientCharacterCreationCommand(std::span<const std::byte> payload)
    {
        namespace S = Protocol::Schema::CharacterCreation;
        if (auto invalid = prefix(payload)) return *invalid;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!S::SizePrefixedClientCharacterCreationCommandBufferHasIdentifier(bytes))
            return error(CharacterProtocolErrorCode::InvalidIdentifier);
        auto checked = verifier(payload);
        if (!S::VerifySizePrefixedClientCharacterCreationCommandBuffer(checked))
            return error(CharacterProtocolErrorCode::VerificationFailed);
        const auto* value = S::GetSizePrefixedClientCharacterCreationCommand(bytes);
        auto session = strong<SessionId>(value->session_id());
        auto generation = strong<SessionGeneration>(value->session_generation());
        auto sequence = strong<CommandSequence>(value->command_sequence());
        auto commandId = strong<CommandId>(value->command_id());
        auto canonical = CanonicalRevision::fromValue(value->observed_canonical_revision());
        auto revision = strong<CharacterProfileRevision>(value->expected_profile_revision());
        if (!session || !generation || !sequence || !commandId || !canonical || !revision)
            return error(CharacterProtocolErrorCode::InvalidStrongValue);
        CharacterCreationChoice choice;
        switch (value->kind())
        {
            case S::CommandKind::SetName:
                if (!nonempty(value->name()) || !isValidCharacterText(value->name()->string_view(), MaximumCharacterNameBytes)
                    || !hasCommandOnlyDefaults(*value, true, false, false, false, false))
                    return error(CharacterProtocolErrorCode::InvalidText);
                choice = SetCharacterName{ value->name()->str() }; break;
            case S::CommandKind::SetAppearance:
            {
                auto race = strong<RaceRecordId>(value->race_id()); auto head = strong<HeadRecordId>(value->head_id());
                auto hair = strong<HairRecordId>(value->hair_id());
                if (!race || !head || !hair || static_cast<unsigned>(value->sex()) > 1
                    || !hasCommandOnlyDefaults(*value, false, true, false, false, false))
                    return error(CharacterProtocolErrorCode::InvalidAppearance);
                choice = SetCharacterAppearance{ { *race, *head, *hair, static_cast<CharacterSex>(value->sex()) } };
                break;
            }
            case S::CommandKind::SetClass:
            {
                auto id = strong<ClassRecordId>(value->class_id());
                if (!id || !hasCommandOnlyDefaults(*value, false, false, true, false, false))
                    return error(CharacterProtocolErrorCode::UnexpectedField);
                choice = SetCharacterClass{ *id }; break;
            }
            case S::CommandKind::SetCustomClass:
            {
                if (!nonempty(value->custom_class_name()) || value->custom_class_description() == nullptr
                    || !isValidCharacterText(value->custom_class_name()->string_view(), MaximumCustomClassNameBytes)
                    || (value->custom_class_description()->size() != 0
                        && !isValidCharacterText(value->custom_class_description()->string_view(), MaximumCustomClassDescriptionBytes))
                    || static_cast<unsigned>(value->specialization()) > 2
                    || !hasCommandOnlyDefaults(*value, false, false, false, true, false))
                    return error(CharacterProtocolErrorCode::InvalidCustomClass);
                CustomClassDefinition custom;
                custom.name = value->custom_class_name()->str(); custom.description = value->custom_class_description()->str();
                custom.specialization = static_cast<ClassSpecialization>(value->specialization());
                if (!copyBytes(value->favored_attributes(), custom.favoredAttributes)
                    || !copyBytes(value->minor_skills(), custom.minorSkills)
                    || !copyBytes(value->major_skills(), custom.majorSkills))
                    return error(CharacterProtocolErrorCode::InvalidVectorSize);
                choice = SetCharacterClass{ std::move(custom) }; break;
            }
            case S::CommandKind::SetBirthsign:
            {
                auto id = strong<BirthsignRecordId>(value->birthsign_id());
                if (!id || !hasCommandOnlyDefaults(*value, false, false, false, false, true))
                    return error(CharacterProtocolErrorCode::UnexpectedField);
                choice = SetCharacterBirthsign{ *id }; break;
            }
            case S::CommandKind::Complete:
                if (!hasCommandOnlyDefaults(*value, false, false, false, false, false))
                    return error(CharacterProtocolErrorCode::UnexpectedField);
                choice = CompleteCharacterCreation{}; break;
            default: return error(CharacterProtocolErrorCode::UnknownCommandKind,
                static_cast<std::size_t>(value->kind()));
        }
        return ClientCharacterCreationCommand{ *session, *generation, *sequence, *commandId, *canonical,
            { *revision, std::move(choice) } };
    }

    std::vector<std::byte> encodeReliableCharacterProfile(const ReliableCharacterProfile& value)
    {
        namespace S = Protocol::Schema::CharacterProfile;
        flatbuffers::FlatBufferBuilder builder;
        const auto& profile = value.profile;
        const auto name = builder.CreateString(profile.name());
        std::uint64_t race = 0, head = 0, hair = 0, classId = 0, birthsign = 0;
        auto sex = S::CharacterSex::Female;
        if (profile.appearance()) { race = profile.appearance()->race.value(); head = profile.appearance()->head.value();
            hair = profile.appearance()->hair.value(); sex = static_cast<S::CharacterSex>(profile.appearance()->sex); }
        auto classKind = S::CharacterClassKind::None;
        flatbuffers::Offset<flatbuffers::String> customName, customDescription;
        flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> favored, minor, major;
        auto specialization = S::ClassSpecialization::Combat;
        if (profile.characterClass())
        {
            if (const auto* predefined = std::get_if<ClassRecordId>(&*profile.characterClass()))
            { classKind = S::CharacterClassKind::Predefined; classId = predefined->value(); }
            else
            {
                classKind = S::CharacterClassKind::Custom;
                const auto& custom = std::get<CustomClassDefinition>(*profile.characterClass());
                customName = builder.CreateString(custom.name); customDescription = builder.CreateString(custom.description);
                specialization = static_cast<S::ClassSpecialization>(custom.specialization);
                favored = builder.CreateVector(custom.favoredAttributes.data(), custom.favoredAttributes.size());
                minor = builder.CreateVector(custom.minorSkills.data(), custom.minorSkills.size());
                major = builder.CreateVector(custom.majorSkills.data(), custom.majorSkills.size());
            }
        }
        if (profile.birthsign()) birthsign = profile.birthsign()->value();
        const auto attributes = builder.CreateVector(profile.derived().attributes.data(), profile.derived().attributes.size());
        const auto skills = builder.CreateVector(profile.derived().skills.data(), profile.derived().skills.size());
        std::vector<std::uint64_t> spells; for (auto id : profile.derived().startingSpells) spells.push_back(id.value());
        const auto encodedSpells = builder.CreateVector(spells);
        std::vector<std::uint64_t> prototypes; std::vector<std::uint32_t> counts; std::vector<std::uint8_t> slots;
        for (const auto& item : profile.startingInventory()) { prototypes.push_back(item.prototype.value()); counts.push_back(item.count);
            slots.push_back(item.equipmentSlot.value_or(std::numeric_limits<std::uint8_t>::max())); }
        const auto encodedPrototypes = builder.CreateVector(prototypes); const auto encodedCounts = builder.CreateVector(counts);
        const auto encodedSlots = builder.CreateVector(slots);
        S::ReliableCharacterProfileBuilder root(builder);
        root.add_target_session_id(value.targetSessionId.value()); root.add_target_session_generation(value.targetSessionGeneration.value());
        root.add_player_id(value.playerId.value()); root.add_result(static_cast<S::ResultCode>(value.result));
        root.add_lifecycle(static_cast<S::CharacterLifecycle>(profile.lifecycle())); root.add_phase(static_cast<S::CharacterCreationPhase>(profile.phase()));
        root.add_revision(profile.revision().value()); root.add_name(name); root.add_race_id(race); root.add_head_id(head); root.add_hair_id(hair); root.add_sex(sex);
        root.add_class_kind(classKind); root.add_class_id(classId); if (customName.o) root.add_custom_class_name(customName);
        if (customDescription.o) root.add_custom_class_description(customDescription); root.add_specialization(specialization);
        if (favored.o) root.add_favored_attributes(favored); if (minor.o) root.add_minor_skills(minor); if (major.o) root.add_major_skills(major);
        root.add_birthsign_id(birthsign); root.add_attributes(attributes); root.add_skills(skills); root.add_starting_spells(encodedSpells);
        root.add_item_prototypes(encodedPrototypes); root.add_item_counts(encodedCounts); root.add_equipment_slots(encodedSlots);
        const auto offset = root.Finish(); S::FinishSizePrefixedReliableCharacterProfileBuffer(builder, offset); return take(builder);
    }

    ReliableCharacterProfileDecodeResult decodeReliableCharacterProfile(std::span<const std::byte> payload)
    {
        namespace S = Protocol::Schema::CharacterProfile;
        if (auto invalid = prefix(payload)) return *invalid;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!S::SizePrefixedReliableCharacterProfileBufferHasIdentifier(bytes)) return error(CharacterProtocolErrorCode::InvalidIdentifier);
        auto checked = verifier(payload); if (!S::VerifySizePrefixedReliableCharacterProfileBuffer(checked)) return error(CharacterProtocolErrorCode::VerificationFailed);
        const auto* value = S::GetSizePrefixedReliableCharacterProfile(bytes);
        auto session = strong<SessionId>(value->target_session_id()); auto generation = strong<SessionGeneration>(value->target_session_generation());
        auto player = strong<PlayerId>(value->player_id()); auto revision = strong<CharacterProfileRevision>(value->revision());
        if (!session || !generation || !player || !revision) return error(CharacterProtocolErrorCode::InvalidStrongValue);
        if (static_cast<unsigned>(value->result()) > static_cast<unsigned>(S::ResultCode::InternalError)) return error(CharacterProtocolErrorCode::UnknownResult);
        if (static_cast<unsigned>(value->lifecycle()) < 1 || static_cast<unsigned>(value->lifecycle()) > 3) return error(CharacterProtocolErrorCode::UnknownLifecycle);
        if (static_cast<unsigned>(value->phase()) < 1 || static_cast<unsigned>(value->phase()) > 6) return error(CharacterProtocolErrorCode::UnknownPhase);
        std::string name = value->name() ? value->name()->str() : std::string{};
        std::optional<CharacterAppearance> appearance;
        if (value->race_id() || value->head_id() || value->hair_id())
        {
            auto race = strong<RaceRecordId>(value->race_id()); auto head = strong<HeadRecordId>(value->head_id()); auto hair = strong<HairRecordId>(value->hair_id());
            if (!race || !head || !hair || static_cast<unsigned>(value->sex()) > 1) return error(CharacterProtocolErrorCode::InvalidAppearance);
            appearance = CharacterAppearance{ *race, *head, *hair, static_cast<CharacterSex>(value->sex()) };
        }
        std::optional<CharacterClass> selectedClass;
        if (value->class_kind() == S::CharacterClassKind::Predefined)
        { auto id = strong<ClassRecordId>(value->class_id()); if (!id) return error(CharacterProtocolErrorCode::InvalidProfile); selectedClass = *id; }
        else if (value->class_kind() == S::CharacterClassKind::Custom)
        {
            if (!value->custom_class_name() || !value->custom_class_description() || static_cast<unsigned>(value->specialization()) > 2)
                return error(CharacterProtocolErrorCode::InvalidCustomClass);
            CustomClassDefinition custom; custom.name = value->custom_class_name()->str(); custom.description = value->custom_class_description()->str();
            custom.specialization = static_cast<ClassSpecialization>(value->specialization());
            if (!copyBytes(value->favored_attributes(), custom.favoredAttributes) || !copyBytes(value->minor_skills(), custom.minorSkills)
                || !copyBytes(value->major_skills(), custom.majorSkills)) return error(CharacterProtocolErrorCode::InvalidVectorSize);
            selectedClass = std::move(custom);
        }
        else if (value->class_kind() != S::CharacterClassKind::None) return error(CharacterProtocolErrorCode::InvalidProfile);
        std::optional<BirthsignRecordId> birthsign; if (value->birthsign_id()) { birthsign = strong<BirthsignRecordId>(value->birthsign_id()); if (!birthsign) return error(CharacterProtocolErrorCode::InvalidProfile); }
        if (!value->attributes() || value->attributes()->size() != CharacterAttributeCount || !value->skills() || value->skills()->size() != CharacterSkillCount
            || !value->starting_spells() || value->starting_spells()->size() > MaximumStartingSpells) return error(CharacterProtocolErrorCode::InvalidVectorSize);
        CharacterDerivedState derived; for (std::size_t i=0;i<CharacterAttributeCount;++i) derived.attributes[i]=copyScalar(value->attributes(), i);
        for (std::size_t i=0;i<CharacterSkillCount;++i) derived.skills[i]=copyScalar(value->skills(), i);
        for (std::size_t i=0;i<value->starting_spells()->size();++i) { auto id=strong<SpellRecordId>(copyScalar(value->starting_spells(), i)); if(!id) return error(CharacterProtocolErrorCode::InvalidProfile); derived.startingSpells.push_back(*id); }
        const auto* prototypes=value->item_prototypes(); const auto* counts=value->item_counts(); const auto* slots=value->equipment_slots();
        if (!prototypes || !counts || !slots || prototypes->size()!=counts->size() || counts->size()!=slots->size() || prototypes->size()>MaximumStartingItems)
            return error(CharacterProtocolErrorCode::InvalidInventory);
        std::vector<StartingItem> inventory; inventory.reserve(prototypes->size());
        for (std::size_t i=0;i<prototypes->size();++i) { auto prototype=strong<ItemPrototypeId>(copyScalar(prototypes, i)); const auto count=copyScalar(counts, i); const auto slot=copyScalar(slots, i);
            if(!prototype || count==0 || (slot!=std::numeric_limits<std::uint8_t>::max() && slot>=19)) return error(CharacterProtocolErrorCode::InvalidInventory);
            inventory.push_back({*prototype,count,slot==std::numeric_limits<std::uint8_t>::max()?std::nullopt:std::optional(slot)}); }
        auto profile=CharacterProfile::restore(static_cast<CharacterLifecycle>(value->lifecycle()), static_cast<CharacterCreationPhase>(value->phase()),
            std::move(name), appearance, std::move(selectedClass), birthsign, std::move(derived), std::move(inventory), *revision);
        if(!profile) return error(CharacterProtocolErrorCode::InvalidProfile);
        return ReliableCharacterProfile{*session,*generation,*player,static_cast<CharacterConfirmationResult>(value->result()),std::move(*profile)};
    }
}
