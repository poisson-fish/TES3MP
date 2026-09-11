#include "world_content.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace TES3MP::ServerApp
{
    namespace
    {
        constexpr std::string_view Header = "TES3MP_WORLD_V3";
        constexpr std::size_t MaximumFields = MaximumQuestStagesPerQuest + 3;

        template <class Value>
        std::optional<Value> number(std::string_view text) noexcept
        {
            Value value{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        std::optional<std::size_t> tokenize(
            std::string_view line, std::array<std::string_view, MaximumFields>& output) noexcept
        {
            std::size_t count = 0;
            for (std::size_t begin = 0; begin < line.size();)
            {
                while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                    ++begin;
                if (begin == line.size())
                    break;
                if (count == output.size())
                    return std::nullopt;
                auto end = begin;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                    ++end;
                output[count++] = line.substr(begin, end - begin);
                begin = end;
            }
            return count;
        }
    }

    WorldContentLoadResult loadWorldContent(const std::filesystem::path& path, const ContentManifest& manifest) noexcept
    try
    {
        if (path.empty() || !std::filesystem::is_regular_file(path))
            return WorldContentError::Unavailable;
        if (std::filesystem::file_size(path) > MaximumWorldContentBytes)
            return WorldContentError::TooLarge;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return WorldContentError::Unavailable;
        std::string text;
        text.reserve(MaximumWorldContentBytes + 1);
        char byte = 0;
        while (stream.get(byte))
        {
            text.push_back(byte);
            if (text.size() > MaximumWorldContentBytes)
                return WorldContentError::TooLarge;
        }
        if (!stream.eof())
            return WorldContentError::Unavailable;

        std::optional<ContentManifestId> declaredManifest;
        std::optional<CanonicalWorldTimeState> time;
        std::vector<GlobalVariableCatalogEntry> globals;
        std::vector<QuestCatalogEntry> quests;
        std::vector<JournalCatalogEntry> journal;
        std::vector<FactionCatalogEntry> factions;
        std::vector<DialogueChoiceCatalogEntry> dialogueChoices;
        std::size_t lineNumber = 0;
        for (std::size_t begin = 0; begin <= text.size();)
        {
            ++lineNumber;
            const auto lineEnd = text.find('\n', begin);
            const auto length = (lineEnd == std::string::npos ? text.size() : lineEnd) - begin;
            auto line = std::string_view(text).substr(begin, length);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNumber == 1)
            {
                if (line != Header)
                    return WorldContentError::Malformed;
            }
            else if (!line.empty())
            {
                std::array<std::string_view, MaximumFields> fields{};
                const auto count = tokenize(line, fields);
                if (!count || *count == 0)
                    return WorldContentError::Malformed;
                const auto values = std::span(fields).first(*count);
                if (values[0] == "manifest")
                {
                    if (values.size() != 2 || declaredManifest)
                        return WorldContentError::Malformed;
                    declaredManifest = ContentManifestId::fromHex(values[1]);
                    if (!declaredManifest)
                        return WorldContentError::Malformed;
                }
                else if (values[0] == "time")
                {
                    if (values.size() != 6 || time)
                        return WorldContentError::Malformed;
                    const auto day = number<std::uint8_t>(values[1]);
                    const auto month = number<std::uint8_t>(values[2]);
                    const auto year = number<std::int32_t>(values[3]);
                    const auto milliseconds = number<std::uint32_t>(values[4]);
                    const auto scale = number<std::uint32_t>(values[5]);
                    if (!day || !month || !year || !milliseconds || !scale)
                        return WorldContentError::Malformed;
                    time = CanonicalWorldTimeState{ *day, *month, *year, *milliseconds, *scale };
                }
                else if (values[0] == "global")
                {
                    if (values.size() != 4 || globals.size() == MaximumGlobalVariables)
                        return globals.size() == MaximumGlobalVariables ? WorldContentError::TooLarge
                                                                        : WorldContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto id = rawId ? GlobalVariableId::fromValue(*rawId) : std::nullopt;
                    if (!id)
                        return WorldContentError::Malformed;
                    GlobalVariableValue value;
                    if (values[2] == "short")
                    {
                        const auto parsed = number<std::int16_t>(values[3]);
                        if (!parsed)
                            return WorldContentError::Malformed;
                        value = *parsed;
                    }
                    else if (values[2] == "long")
                    {
                        const auto parsed = number<std::int32_t>(values[3]);
                        if (!parsed)
                            return WorldContentError::Malformed;
                        value = *parsed;
                    }
                    else if (values[2] == "float")
                    {
                        const auto parsed = number<float>(values[3]);
                        if (!parsed || !std::isfinite(*parsed))
                            return WorldContentError::Malformed;
                        value = *parsed;
                    }
                    else
                        return WorldContentError::Malformed;
                    globals.push_back({ *id, value });
                }
                else if (values[0] == "quest")
                {
                    if (values.size() < 4 || quests.size() == MaximumQuestCatalogEntries
                        || values.size() - 3 > MaximumQuestStagesPerQuest)
                        return quests.size() == MaximumQuestCatalogEntries
                                || values.size() - 3 > MaximumQuestStagesPerQuest
                            ? WorldContentError::TooLarge
                            : WorldContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto rawInitial = number<std::uint64_t>(values[2]);
                    const auto id = rawId ? QuestId::fromValue(*rawId) : std::nullopt;
                    const auto initial = rawInitial ? QuestStage::fromValue(*rawInitial) : std::nullopt;
                    if (!id || !initial)
                        return WorldContentError::Malformed;
                    QuestCatalogEntry quest{ *id, *initial, {} };
                    quest.stages.reserve(values.size() - 3);
                    for (const auto field : values.subspan(3))
                    {
                        const auto rawStage = number<std::uint64_t>(field);
                        const auto stage = rawStage ? QuestStage::fromValue(*rawStage) : std::nullopt;
                        if (!stage)
                            return WorldContentError::Malformed;
                        quest.stages.push_back(*stage);
                    }
                    quests.push_back(std::move(quest));
                }
                else if (values[0] == "journal")
                {
                    if (values.size() != 4 || journal.size() == MaximumJournalCatalogEntries)
                        return journal.size() == MaximumJournalCatalogEntries ? WorldContentError::TooLarge
                                                                              : WorldContentError::Malformed;
                    const auto rawEntry = number<std::uint64_t>(values[1]);
                    const auto rawQuest = number<std::uint64_t>(values[2]);
                    const auto rawStage = number<std::uint64_t>(values[3]);
                    const auto entry = rawEntry ? JournalEntryId::fromValue(*rawEntry) : std::nullopt;
                    const auto quest = rawQuest ? QuestId::fromValue(*rawQuest) : std::nullopt;
                    const auto stage = rawStage ? QuestStage::fromValue(*rawStage) : std::nullopt;
                    if (!entry || !quest || !stage)
                        return WorldContentError::Malformed;
                    journal.push_back({ *entry, *quest, *stage });
                }
                else if (values[0] == "faction")
                {
                    if (values.size() < 3 || factions.size() == MaximumFactionCatalogEntries
                        || values.size() - 2 > MaximumFactionRanksPerFaction)
                        return factions.size() == MaximumFactionCatalogEntries
                                || values.size() - 2 > MaximumFactionRanksPerFaction
                            ? WorldContentError::TooLarge
                            : WorldContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto id = rawId ? FactionId::fromValue(*rawId) : std::nullopt;
                    if (!id)
                        return WorldContentError::Malformed;
                    FactionCatalogEntry faction{ *id, {} };
                    faction.ranks.reserve(values.size() - 2);
                    for (const auto field : values.subspan(2))
                    {
                        const auto rawRank = number<std::uint64_t>(field);
                        const auto rank = rawRank ? FactionRank::fromValue(*rawRank) : std::nullopt;
                        if (!rank)
                            return WorldContentError::Malformed;
                        faction.ranks.push_back(*rank);
                    }
                    factions.push_back(std::move(faction));
                }
                else if (values[0] == "dialogue_choice")
                {
                    if ((values.size() != 3 && values.size() != 5)
                        || dialogueChoices.size() == MaximumDialogueChoiceCatalogEntries)
                        return dialogueChoices.size() == MaximumDialogueChoiceCatalogEntries
                            ? WorldContentError::TooLarge
                            : WorldContentError::Malformed;
                    const auto rawId = number<std::uint64_t>(values[1]);
                    const auto id = rawId ? DialogueChoiceId::fromValue(*rawId) : std::nullopt;
                    if (!id)
                        return WorldContentError::Malformed;
                    if (values.size() == 3)
                    {
                        if (values[2] != "unrestricted")
                            return WorldContentError::Malformed;
                        dialogueChoices.push_back({ *id });
                    }
                    else
                    {
                        const auto rawFaction = number<std::uint64_t>(values[2]);
                        const auto rawRank = number<std::uint64_t>(values[3]);
                        const auto faction = rawFaction ? FactionId::fromValue(*rawFaction) : std::nullopt;
                        const auto rank = rawRank ? FactionRank::fromValue(*rawRank) : std::nullopt;
                        const auto reputation = number<std::int32_t>(values[4]);
                        if (!faction || !rank || !reputation)
                            return WorldContentError::Malformed;
                        dialogueChoices.push_back({ *id, *faction, *rank, *reputation });
                    }
                }
                else
                    return WorldContentError::Malformed;
            }
            if (lineEnd == std::string::npos)
                break;
            begin = lineEnd + 1;
        }
        if (!declaredManifest || !time)
            return WorldContentError::Malformed;
        if (*declaredManifest != manifest.id())
            return WorldContentError::ManifestMismatch;
        auto catalog = GlobalVariableCatalog::create(globals);
        auto questJournal = QuestJournalCatalog::create(*declaredManifest, quests, journal);
        auto factionDialogue = FactionDialogueCatalog::create(*declaredManifest, factions, dialogueChoices);
        auto world = catalog && questJournal && factionDialogue
            ? CanonicalWorldState::initial(*time, *catalog, *questJournal, *factionDialogue)
            : std::nullopt;
        if (!catalog || !questJournal || !factionDialogue || !world)
            return WorldContentError::InvalidCatalog;
        return WorldContent{
            std::move(*catalog), std::move(*questJournal), std::move(*factionDialogue), std::move(*world) };
    }
    catch (...)
    {
        return WorldContentError::Unavailable;
    }
}
