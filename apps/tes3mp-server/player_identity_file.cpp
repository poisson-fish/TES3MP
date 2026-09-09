#include "player_identity_file.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    constexpr std::string_view HeaderV4 = "TES3MP_PLAYER_IDENTITIES_V4";
    constexpr std::size_t MaximumFileBytes = 256 * 1024;

    class TemporaryFileCleanup
    {
    public:
        explicit TemporaryFileCleanup(std::filesystem::path path) noexcept
            : mPath(std::move(path))
        {
        }

        void activate() noexcept { mActive = true; }

        ~TemporaryFileCleanup()
        {
            if (!mActive)
                return;
            std::error_code ignored;
            std::filesystem::remove(mPath, ignored);
        }

    private:
        std::filesystem::path mPath;
        bool mActive = false;
    };

    std::optional<std::uint64_t> number(std::string_view value) noexcept
    {
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0)
            return std::nullopt;
        return result;
    }

    std::optional<TES3MP::CredentialDigest> digestFromHex(std::string_view value) noexcept
    {
        if (value.size() != TES3MP::CredentialDigestBytes * 2)
            return std::nullopt;
        const auto nibble = [](char digit) -> std::optional<std::uint8_t> {
            if (digit >= '0' && digit <= '9') return static_cast<std::uint8_t>(digit - '0');
            if (digit >= 'a' && digit <= 'f') return static_cast<std::uint8_t>(digit - 'a' + 10);
            if (digit >= 'A' && digit <= 'F') return static_cast<std::uint8_t>(digit - 'A' + 10);
            return std::nullopt;
        };
        TES3MP::CredentialDigest result;
        for (std::size_t index = 0; index < result.bytes.size(); ++index)
        {
            const auto high = nibble(value[index * 2]);
            const auto low = nibble(value[index * 2 + 1]);
            if (!high || !low)
                return std::nullopt;
            result.bytes[index] = static_cast<std::byte>((*high << 4u) | *low);
        }
        return result;
    }

    std::string hex(std::span<const std::byte> bytes)
    {
        constexpr char Digits[] = "0123456789abcdef";
        std::string result(bytes.size() * 2, '0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const auto value = std::to_integer<std::uint8_t>(bytes[index]);
            result[index * 2] = Digits[value >> 4u];
            result[index * 2 + 1] = Digits[value & 0x0fu];
        }
        return result;
    }

    std::string textHex(std::string_view value)
    {
        if (value.empty()) return "-";
        return hex({ reinterpret_cast<const std::byte*>(value.data()), value.size() });
    }

    std::optional<std::string> textFromHex(std::string_view value, std::size_t maximumBytes) noexcept
    try
    {
        if (value == "-") return std::string{};
        if (value.empty() || value.size() % 2 != 0 || value.size() > maximumBytes * 2) return std::nullopt;
        const auto nibble = [](char digit) -> std::optional<unsigned char> {
            if (digit >= '0' && digit <= '9') return static_cast<unsigned char>(digit - '0');
            if (digit >= 'a' && digit <= 'f') return static_cast<unsigned char>(digit - 'a' + 10);
            if (digit >= 'A' && digit <= 'F') return static_cast<unsigned char>(digit - 'A' + 10);
            return std::nullopt;
        };
        std::string result(value.size() / 2, '\0');
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const auto high = nibble(value[index * 2]);
            const auto low = nibble(value[index * 2 + 1]);
            if (!high || !low) return std::nullopt;
            result[index] = static_cast<char>((*high << 4u) | *low);
        }
        return result;
    }
    catch (...) { return std::nullopt; }

    bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) noexcept
    {
#ifdef _WIN32
        return MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        return std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
    }
}

namespace TES3MP::ServerApp
{
    std::variant<std::unique_ptr<PlayerIdentityFile>, PlayerIdentityFileError> PlayerIdentityFile::open(
        std::filesystem::path path) noexcept
    try
    {
        if (path.empty())
            return PlayerIdentityFileError::Unavailable;
        if (!std::filesystem::exists(path))
            return std::unique_ptr<PlayerIdentityFile>(new PlayerIdentityFile(std::move(path), {}));
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > MaximumFileBytes)
            return PlayerIdentityFileError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        std::string line;
        if (!stream || !std::getline(stream, line) || line != HeaderV4)
            return PlayerIdentityFileError::Malformed;
        std::vector<PersistedPlayerIdentity> records;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;
            std::istringstream fields(line);
            std::array<std::string, 5> values;
            if (!(fields >> values[0] >> values[1] >> values[2] >> values[3] >> values[4]))
                return PlayerIdentityFileError::Malformed;
            const auto player = number(values[0]);
            const auto entity = number(values[1]);
            const auto appearance = number(values[2]);
            const auto manifest = ContentManifestId::fromHex(values[3]);
            const auto digest = digestFromHex(values[4]);
            if (!player || !entity || !appearance || !manifest || !digest)
                return PlayerIdentityFileError::Malformed;
            const AuthenticatedAdmission::PlayerClaim claim{ *PlayerId::fromValue(*player),
                *EntityId::fromValue(*entity), *AppearanceId::fromValue(*appearance), *manifest };
            std::optional<CanonicalPlayerEntityState> savedPlayer;
            {
                unsigned present = 0;
                if (!(fields >> present) || present > 1)
                    return PlayerIdentityFileError::Malformed;
                if (present == 1)
                {
                    unsigned cellKind = 0;
                    std::uint64_t cellSpace = 0;
                    std::int32_t gridX = 0;
                    std::int32_t gridY = 0;
                    std::int64_t positionX = 0;
                    std::int64_t positionY = 0;
                    std::int64_t positionZ = 0;
                    std::uint32_t orientationX = 0;
                    std::uint32_t orientationY = 0;
                    std::uint32_t orientationZ = 0;
                    std::int64_t velocityX = 0;
                    std::int64_t velocityY = 0;
                    std::int64_t velocityZ = 0;
                    std::uint64_t revision = 0;
                    std::uint64_t epoch = 0;
                    std::uint64_t tick = 0;
                    unsigned locomotion = 0;
                    if (!(fields >> cellKind >> cellSpace >> gridX >> gridY >> positionX >> positionY >> positionZ
                            >> orientationX >> orientationY >> orientationZ >> velocityX >> velocityY >> velocityZ
                            >> revision >> epoch >> tick >> locomotion)
                        || cellKind > 1 || locomotion > static_cast<unsigned>(LocomotionMode::Jump))
                        return PlayerIdentityFileError::Malformed;
                    const auto space = CellSpaceId::fromValue(cellSpace);
                    const auto entityRevision = EntityRevision::fromValue(revision);
                    const auto authorityEpoch = AuthorityEpoch::fromValue(epoch);
                    const auto serverTick = ServerTick::fromValue(tick);
                    if (!space || !entityRevision || !authorityEpoch || !serverTick)
                        return PlayerIdentityFileError::Malformed;
                    const CellId cell = cellKind == 0 ? CellId::interior(*space)
                                                      : CellId::exterior(*space, gridX, gridY);
                    savedPlayer.emplace(claim.player, claim.entity, claim.appearance,
                        Transform(cell, Position3(positionX, positionY, positionZ),
                            Orientation3(Turn32::fromValue(orientationX), Turn32::fromValue(orientationY),
                                Turn32::fromValue(orientationZ))),
                        LinearVelocity3(velocityX, velocityY, velocityZ), *entityRevision, *authorityEpoch,
                        *serverTick, static_cast<LocomotionMode>(locomotion));
                }
            }
            CharacterProfile characterProfile = CharacterProfile::fresh();
            {
                unsigned lifecycleValue = 0;
                unsigned phaseValue = 0;
                std::uint64_t profileRevisionValue = 0;
                std::string nameValue;
                unsigned appearancePresent = 0;
                if (!(fields >> lifecycleValue >> phaseValue >> profileRevisionValue >> nameValue >> appearancePresent)
                    || lifecycleValue < 1 || lifecycleValue > 3 || phaseValue < 1 || phaseValue > 6
                    || appearancePresent > 1)
                    return PlayerIdentityFileError::Malformed;
                const auto revision = CharacterProfileRevision::fromValue(profileRevisionValue);
                const auto name = textFromHex(nameValue, MaximumCharacterNameBytes);
                if (!revision || !name) return PlayerIdentityFileError::Malformed;
                std::optional<CharacterAppearance> appearanceValue;
                if (appearancePresent)
                {
                    std::uint64_t race = 0, head = 0, hair = 0;
                    unsigned sex = 0;
                    if (!(fields >> race >> head >> hair >> sex) || sex > 1)
                        return PlayerIdentityFileError::Malformed;
                    auto raceId = RaceRecordId::fromValue(race);
                    auto headId = HeadRecordId::fromValue(head);
                    auto hairId = HairRecordId::fromValue(hair);
                    if (!raceId || !headId || !hairId) return PlayerIdentityFileError::Malformed;
                    appearanceValue = CharacterAppearance{ *raceId, *headId, *hairId,
                        static_cast<CharacterSex>(sex) };
                }
                unsigned classKind = 0;
                if (!(fields >> classKind) || classKind > 2) return PlayerIdentityFileError::Malformed;
                std::optional<CharacterClass> classValue;
                if (classKind == 1)
                {
                    std::uint64_t classIdValue = 0;
                    if (!(fields >> classIdValue)) return PlayerIdentityFileError::Malformed;
                    auto classId = ClassRecordId::fromValue(classIdValue);
                    if (!classId) return PlayerIdentityFileError::Malformed;
                    classValue = *classId;
                }
                else if (classKind == 2)
                {
                    std::string classNameValue, descriptionValue;
                    unsigned specialization = 0;
                    CustomClassDefinition custom;
                    if (!(fields >> classNameValue >> descriptionValue >> specialization) || specialization > 2)
                        return PlayerIdentityFileError::Malformed;
                    auto className = textFromHex(classNameValue, MaximumCustomClassNameBytes);
                    auto description = textFromHex(descriptionValue, MaximumCustomClassDescriptionBytes);
                    if (!className || !description) return PlayerIdentityFileError::Malformed;
                    custom.name = std::move(*className);
                    custom.description = std::move(*description);
                    custom.specialization = static_cast<ClassSpecialization>(specialization);
                    unsigned value = 0;
                    for (auto& entry : custom.favoredAttributes)
                    { if (!(fields >> value) || value >= CharacterAttributeCount) return PlayerIdentityFileError::Malformed; entry = static_cast<std::uint8_t>(value); }
                    for (auto& entry : custom.minorSkills)
                    { if (!(fields >> value) || value >= CharacterSkillCount) return PlayerIdentityFileError::Malformed; entry = static_cast<std::uint8_t>(value); }
                    for (auto& entry : custom.majorSkills)
                    { if (!(fields >> value) || value >= CharacterSkillCount) return PlayerIdentityFileError::Malformed; entry = static_cast<std::uint8_t>(value); }
                    classValue = std::move(custom);
                }
                unsigned birthsignPresent = 0;
                if (!(fields >> birthsignPresent) || birthsignPresent > 1) return PlayerIdentityFileError::Malformed;
                std::optional<BirthsignRecordId> birthsignValue;
                if (birthsignPresent)
                {
                    std::uint64_t raw = 0;
                    if (!(fields >> raw)) return PlayerIdentityFileError::Malformed;
                    birthsignValue = BirthsignRecordId::fromValue(raw);
                    if (!birthsignValue) return PlayerIdentityFileError::Malformed;
                }
                CharacterDerivedState derived;
                unsigned derivedValue = 0;
                for (auto& entry : derived.attributes)
                { if (!(fields >> derivedValue) || derivedValue > (std::numeric_limits<std::uint16_t>::max)()) return PlayerIdentityFileError::Malformed; entry = static_cast<std::uint16_t>(derivedValue); }
                for (auto& entry : derived.skills)
                { if (!(fields >> derivedValue) || derivedValue > (std::numeric_limits<std::uint16_t>::max)()) return PlayerIdentityFileError::Malformed; entry = static_cast<std::uint16_t>(derivedValue); }
                std::size_t spellCount = 0;
                if (!(fields >> spellCount) || spellCount > MaximumStartingSpells) return PlayerIdentityFileError::Malformed;
                for (std::size_t index = 0; index < spellCount; ++index)
                {
                    std::uint64_t raw = 0;
                    if (!(fields >> raw)) return PlayerIdentityFileError::Malformed;
                    auto id = SpellRecordId::fromValue(raw);
                    if (!id) return PlayerIdentityFileError::Malformed;
                    derived.startingSpells.push_back(*id);
                }
                std::size_t itemCount = 0;
                if (!(fields >> itemCount) || itemCount > MaximumStartingItems) return PlayerIdentityFileError::Malformed;
                std::vector<StartingItem> inventory;
                inventory.reserve(itemCount);
                for (std::size_t index = 0; index < itemCount; ++index)
                {
                    std::uint64_t prototypeValue = 0;
                    std::uint32_t count = 0;
                    int slot = -1;
                    if (!(fields >> prototypeValue >> count >> slot) || count == 0 || slot < -1 || slot >= 19)
                        return PlayerIdentityFileError::Malformed;
                    auto prototype = ItemPrototypeId::fromValue(prototypeValue);
                    if (!prototype) return PlayerIdentityFileError::Malformed;
                    inventory.push_back({ *prototype, count,
                        slot < 0 ? std::nullopt : std::optional<std::uint8_t>(static_cast<std::uint8_t>(slot)) });
                }
                auto restored = CharacterProfile::restore(static_cast<CharacterLifecycle>(lifecycleValue),
                    static_cast<CharacterCreationPhase>(phaseValue), std::move(*name), appearanceValue,
                    std::move(classValue), birthsignValue, std::move(derived), std::move(inventory), *revision);
                if (!restored) return PlayerIdentityFileError::Malformed;
                characterProfile = std::move(*restored);
            }
            const bool established
                = characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter;
            if ((!established && characterProfile != CharacterProfile::fresh())
                || established != savedPlayer.has_value()
                || (savedPlayer
                    && (savedPlayer->playerId() != claim.player || savedPlayer->entityId() != claim.entity
                        || savedPlayer->appearanceId() != claim.appearance)))
                return PlayerIdentityFileError::Malformed;
            std::string extra;
            if (fields >> extra)
                return PlayerIdentityFileError::Malformed;
            records.push_back({ claim, *digest, std::move(savedPlayer), std::move(characterProfile) });
            if (records.size() > MaximumPlayerIdentityRecords)
                return PlayerIdentityFileError::TooLarge;
        }
        if (!stream.eof())
            return PlayerIdentityFileError::Unavailable;
        return std::unique_ptr<PlayerIdentityFile>(new PlayerIdentityFile(std::move(path), std::move(records)));
    }
    catch (...)
    {
        return PlayerIdentityFileError::Unavailable;
    }

    bool PlayerIdentityFile::replace(std::span<const PersistedPlayerIdentity> records) noexcept
    try
    {
        if (records.size() > MaximumPlayerIdentityRecords)
            return false;
        std::vector<PersistedPlayerIdentity> candidate(records.begin(), records.end());
        for (const auto& record : candidate)
        {
            const bool established
                = record.characterProfile.lifecycle() == CharacterLifecycle::EstablishedCharacter;
            if ((!established && record.characterProfile != CharacterProfile::fresh())
                || established != record.savedPlayer.has_value()
                || (record.savedPlayer
                    && (record.savedPlayer->playerId() != record.claim.player
                        || record.savedPlayer->entityId() != record.claim.entity
                        || record.savedPlayer->appearanceId() != record.claim.appearance)))
                return false;
        }
        auto temporary = mPath;
        temporary += ".tmp";
        TemporaryFileCleanup cleanup(temporary);
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                return false;
            cleanup.activate();
            stream << HeaderV4 << '\n';
            for (const auto& record : candidate)
            {
                stream << record.claim.player.value() << ' ' << record.claim.entity.value() << ' '
                       << record.claim.appearance.value() << ' ' << hex(record.claim.contentManifest.bytes()) << ' '
                       << hex(record.credentialDigest.bytes);
                if (!record.savedPlayer)
                    stream << " 0";
                else
                {
                    const auto& player = *record.savedPlayer;
                    const auto& cell = player.transform().cell();
                    const auto kind = cell.kind() == CellId::Kind::Interior ? 0 : 1;
                    const auto space = cell.kind() == CellId::Kind::Interior ? cell.asInterior()->cellSpace()
                                                                            : cell.asExterior()->worldspace();
                    const auto gridX = cell.kind() == CellId::Kind::Exterior ? cell.asExterior()->gridX() : 0;
                    const auto gridY = cell.kind() == CellId::Kind::Exterior ? cell.asExterior()->gridY() : 0;
                    const auto position = player.transform().position();
                    const auto orientation = player.transform().orientation();
                    const auto velocity = player.linearVelocity();
                    stream << " 1 " << kind << ' ' << space.value() << ' ' << gridX << ' ' << gridY << ' '
                           << position.x() << ' ' << position.y() << ' ' << position.z() << ' '
                           << orientation.x().value() << ' ' << orientation.y().value() << ' '
                           << orientation.z().value() << ' ' << velocity.x() << ' ' << velocity.y() << ' '
                           << velocity.z() << ' ' << player.entityRevision().value() << ' '
                           << player.authorityEpoch().value() << ' ' << player.lastSpatialChangeTick().value() << ' '
                           << static_cast<unsigned>(player.locomotionMode());
                }
                const auto& profile = record.characterProfile;
                stream << ' ' << static_cast<unsigned>(profile.lifecycle()) << ' '
                       << static_cast<unsigned>(profile.phase()) << ' ' << profile.revision().value() << ' '
                       << textHex(profile.name()) << ' ' << (profile.appearance() ? 1 : 0);
                if (profile.appearance())
                    stream << ' ' << profile.appearance()->race.value() << ' ' << profile.appearance()->head.value()
                           << ' ' << profile.appearance()->hair.value() << ' '
                           << static_cast<unsigned>(profile.appearance()->sex);
                if (!profile.characterClass()) stream << " 0";
                else if (const auto* predefined = std::get_if<ClassRecordId>(&*profile.characterClass()))
                    stream << " 1 " << predefined->value();
                else
                {
                    const auto& custom = std::get<CustomClassDefinition>(*profile.characterClass());
                    stream << " 2 " << textHex(custom.name) << ' ' << textHex(custom.description) << ' '
                           << static_cast<unsigned>(custom.specialization);
                    for (auto value : custom.favoredAttributes) stream << ' ' << static_cast<unsigned>(value);
                    for (auto value : custom.minorSkills) stream << ' ' << static_cast<unsigned>(value);
                    for (auto value : custom.majorSkills) stream << ' ' << static_cast<unsigned>(value);
                }
                stream << ' ' << (profile.birthsign() ? 1 : 0);
                if (profile.birthsign()) stream << ' ' << profile.birthsign()->value();
                for (auto value : profile.derived().attributes) stream << ' ' << value;
                for (auto value : profile.derived().skills) stream << ' ' << value;
                stream << ' ' << profile.derived().startingSpells.size();
                for (auto value : profile.derived().startingSpells) stream << ' ' << value.value();
                stream << ' ' << profile.startingInventory().size();
                for (const auto& item : profile.startingInventory())
                    stream << ' ' << item.prototype.value() << ' ' << item.count << ' '
                           << (item.equipmentSlot ? static_cast<int>(*item.equipmentSlot) : -1);
                stream << '\n';
            }
            stream.flush();
            if (!stream)
                return false;
        }
        if (!replaceFile(temporary, mPath))
            return false;
        mRecords = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
