#include "character_content.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::size_t MaximumFileBytes = 2 * 1024 * 1024;
        CharacterContentError error(CharacterContentErrorCode code, std::size_t line = 0) noexcept
        { return { code, line }; }
        template <class T> std::optional<T> id(std::uint64_t raw) noexcept { return T::fromValue(raw); }

        std::optional<CellId> readCell(std::istringstream& fields)
        {
            std::string kind;
            std::uint64_t spaceRaw = 0;
            if (!(fields >> kind >> spaceRaw)) return std::nullopt;
            auto space = CellSpaceId::fromValue(spaceRaw);
            if (!space) return std::nullopt;
            if (kind == "interior") return CellId::interior(*space);
            std::int32_t x = 0, y = 0;
            if (kind != "exterior" || !(fields >> x >> y)) return std::nullopt;
            return CellId::exterior(*space, x, y);
        }
    }

    CharacterContentResult loadCharacterContent(
        const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return error(CharacterContentErrorCode::Unavailable);
        if (std::filesystem::file_size(path) > MaximumFileBytes)
            return error(CharacterContentErrorCode::TooLarge);
        std::ifstream stream(path, std::ios::binary);
        std::string line;
        if (!stream || !std::getline(stream, line) || line != "TES3MP_CHARACTERS_V2")
            return error(CharacterContentErrorCode::InvalidHeader, 1);
        std::optional<ContentManifestId> contentManifest;
        std::optional<Transform> spawn;
        std::optional<Transform> completionSpawn;
        std::vector<CharacterRaceDefinition> races;
        std::vector<CharacterClassDefinition> classes;
        std::vector<CharacterBirthsignDefinition> signs;
        std::vector<StartingItem> inventory;
        std::size_t lineNumber = 1;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (line.empty() || line.front() == '#') continue;
            std::istringstream fields(line);
            std::string kind;
            if (!(fields >> kind)) return error(CharacterContentErrorCode::Malformed, lineNumber);
            if (kind == "manifest")
            {
                std::string value;
                if (contentManifest || !(fields >> value) || !(contentManifest = ContentManifestId::fromHex(value)))
                    return error(CharacterContentErrorCode::Malformed, lineNumber);
            }
            else if (kind == "spawn")
            {
                auto cell = readCell(fields);
                std::int64_t x = 0, y = 0, z = 0;
                std::uint32_t rx = 0, ry = 0, rz = 0;
                if (spawn || !cell || !(fields >> x >> y >> z >> rx >> ry >> rz))
                    return error(CharacterContentErrorCode::Malformed, lineNumber);
                spawn = Transform(*cell, Position3(x, y, z), Orientation3(
                    Turn32::fromValue(rx), Turn32::fromValue(ry), Turn32::fromValue(rz)));
            }
            else if (kind == "completion_spawn")
            {
                auto cell = readCell(fields);
                std::int64_t x = 0, y = 0, z = 0;
                std::uint32_t rx = 0, ry = 0, rz = 0;
                if (completionSpawn || !cell || !(fields >> x >> y >> z >> rx >> ry >> rz))
                    return error(CharacterContentErrorCode::Malformed, lineNumber);
                completionSpawn = Transform(*cell, Position3(x, y, z), Orientation3(
                    Turn32::fromValue(rx), Turn32::fromValue(ry), Turn32::fromValue(rz)));
            }
            else if (kind == "race")
            {
                std::uint64_t raw = 0;
                if (!(fields >> raw) || !id<RaceRecordId>(raw)) return error(CharacterContentErrorCode::Malformed, lineNumber);
                CharacterRaceDefinition race{ *id<RaceRecordId>(raw) };
                unsigned value = 0;
                for (auto& entry : race.femaleAttributes) { if (!(fields >> value) || value > 65535) return error(CharacterContentErrorCode::Malformed,lineNumber); entry=static_cast<std::uint16_t>(value); }
                for (auto& entry : race.maleAttributes) { if (!(fields >> value) || value > 65535) return error(CharacterContentErrorCode::Malformed,lineNumber); entry=static_cast<std::uint16_t>(value); }
                int bonus = 0;
                for (auto& entry : race.skillBonuses) { if (!(fields >> bonus) || bonus < -32768 || bonus > 32767) return error(CharacterContentErrorCode::Malformed,lineNumber); entry=static_cast<std::int16_t>(bonus); }
                std::size_t spellCount = 0;
                if (!(fields >> spellCount) || spellCount > MaximumStartingSpells) return error(CharacterContentErrorCode::Malformed,lineNumber);
                for (std::size_t index=0; index<spellCount; ++index) { if (!(fields>>raw) || !id<SpellRecordId>(raw)) return error(CharacterContentErrorCode::Malformed,lineNumber); race.spells.push_back(*id<SpellRecordId>(raw)); }
                races.push_back(std::move(race));
            }
            else if (kind == "appearance")
            {
                std::uint64_t raceRaw=0,headRaw=0,hairRaw=0; unsigned sex=0;
                if (!(fields>>raceRaw>>headRaw>>hairRaw>>sex) || sex>1) return error(CharacterContentErrorCode::Malformed,lineNumber);
                auto raceId=id<RaceRecordId>(raceRaw); auto head=id<HeadRecordId>(headRaw); auto hair=id<HairRecordId>(hairRaw);
                if(!raceId||!head||!hair) return error(CharacterContentErrorCode::Malformed,lineNumber);
                auto found=std::find_if(races.begin(),races.end(),[&](const auto& value){return value.id==*raceId;});
                if(found==races.end()) return error(CharacterContentErrorCode::Malformed,lineNumber);
                found->appearances.push_back({*raceId,*head,*hair,static_cast<CharacterSex>(sex)});
            }
            else if (kind == "class")
            {
                std::uint64_t raw=0; unsigned specialization=0,value=0;
                if(!(fields>>raw>>specialization)||!id<ClassRecordId>(raw)||specialization>2) return error(CharacterContentErrorCode::Malformed,lineNumber);
                CharacterClassDefinition definition{*id<ClassRecordId>(raw),static_cast<ClassSpecialization>(specialization)};
                for(auto& entry:definition.favoredAttributes){if(!(fields>>value)||value>=CharacterAttributeCount)return error(CharacterContentErrorCode::Malformed,lineNumber);entry=static_cast<std::uint8_t>(value);}
                for(auto& entry:definition.minorSkills){if(!(fields>>value)||value>=CharacterSkillCount)return error(CharacterContentErrorCode::Malformed,lineNumber);entry=static_cast<std::uint8_t>(value);}
                for(auto& entry:definition.majorSkills){if(!(fields>>value)||value>=CharacterSkillCount)return error(CharacterContentErrorCode::Malformed,lineNumber);entry=static_cast<std::uint8_t>(value);}
                classes.push_back(definition);
            }
            else if (kind == "birthsign")
            {
                std::uint64_t raw=0; std::size_t count=0;
                if(!(fields>>raw>>count)||!id<BirthsignRecordId>(raw)||count>MaximumStartingSpells)return error(CharacterContentErrorCode::Malformed,lineNumber);
                CharacterBirthsignDefinition sign{*id<BirthsignRecordId>(raw)};
                for(std::size_t index=0;index<count;++index){if(!(fields>>raw)||!id<SpellRecordId>(raw))return error(CharacterContentErrorCode::Malformed,lineNumber);sign.spells.push_back(*id<SpellRecordId>(raw));}
                signs.push_back(std::move(sign));
            }
            else if (kind == "starting_item")
            {
                std::uint64_t raw=0; std::uint32_t count=0; int slot=-1;
                if(!(fields>>raw>>count>>slot)||!id<ItemPrototypeId>(raw)||count==0||slot < -1||slot>=19)return error(CharacterContentErrorCode::Malformed,lineNumber);
                inventory.push_back({*id<ItemPrototypeId>(raw),count,slot<0?std::nullopt:std::optional<std::uint8_t>(static_cast<std::uint8_t>(slot))});
            }
            else return error(CharacterContentErrorCode::Malformed, lineNumber);
            std::string extra;
            if (fields >> extra) return error(CharacterContentErrorCode::Malformed, lineNumber);
        }
        if (!stream.eof()) return error(CharacterContentErrorCode::Unavailable, lineNumber);
        if (!contentManifest || *contentManifest != manifest.id()) return error(CharacterContentErrorCode::ManifestMismatch);
        if (!spawn || !completionSpawn || !manifest.contains(spawn->cell())
            || !manifest.contains(completionSpawn->cell()))
            return error(CharacterContentErrorCode::UnknownCell);
        auto catalog=CharacterContentCatalog::create(
            *contentManifest,*spawn,*completionSpawn,races,classes,signs,inventory);
        if(!catalog)return error(CharacterContentErrorCode::InvalidCatalog);
        return std::move(*catalog);
    }
    catch (...) { return error(CharacterContentErrorCode::Unavailable); }

    std::string describeCharacterContentError(CharacterContentError value)
    {
        const char* description = "character content unavailable";
        switch(value.code)
        {
            case CharacterContentErrorCode::TooLarge: description="character content is too large"; break;
            case CharacterContentErrorCode::InvalidHeader: description="invalid character content header"; break;
            case CharacterContentErrorCode::Malformed: description="malformed character content"; break;
            case CharacterContentErrorCode::ManifestMismatch: description="character content manifest mismatch"; break;
            case CharacterContentErrorCode::UnknownCell: description="character creation spawn cell is not in the manifest"; break;
            case CharacterContentErrorCode::InvalidCatalog: description="invalid character content catalog"; break;
            default: break;
        }
        return value.line ? std::string(description)+" at line "+std::to_string(value.line) : description;
    }
}
