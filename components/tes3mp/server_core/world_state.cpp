#include <tes3mp/world_state.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace
{
    using namespace TES3MP;

    bool validValue(const GlobalVariableValue& value) noexcept
    {
        const auto* number = std::get_if<float>(&value);
        return !number || std::isfinite(*number);
    }

    bool validTime(const CanonicalWorldTimeState& time) noexcept
    {
        return time.day >= 1 && time.day <= 30 && time.month <= 11 && time.year >= 0
            && time.millisecondsSinceMidnight < WorldMillisecondsPerDay
            && time.timeScaleUnits <= MaximumWorldTimeScaleUnits
            && time.subMillisecondRemainder < WorldTimeScaleUnitsPerOne && time.lastChangeTick <= time.lastAdvanceTick;
    }

    bool sameValue(const GlobalVariableValue& left, const GlobalVariableValue& right) noexcept
    {
        if (left.index() != right.index())
            return false;
        if (const auto* value = std::get_if<float>(&left))
            return std::bit_cast<std::uint32_t>(*value) == std::bit_cast<std::uint32_t>(std::get<float>(right));
        return left == right;
    }

    bool containsStage(const QuestCatalogEntry& quest, QuestStage stage) noexcept
    {
        return std::ranges::find(quest.stages, stage) != quest.stages.end();
    }

    bool validQuestJournalState(
        const QuestJournalCatalog& catalog, std::span<const CanonicalPlayerQuestJournalState> players) noexcept
    {
        if (players.size() > MaximumPlayerQuestJournalStates)
            return false;
        std::size_t totalQuests = 0;
        std::size_t totalJournal = 0;
        for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
        {
            const auto& player = players[playerIndex];
            if (playerIndex != 0 && players[playerIndex - 1].player >= player.player)
                return false;
            if (player.quests.size() > catalog.quests().size() || player.journal.size() > catalog.journal().size()
                || (player.quests.empty() && player.journal.empty())
                || player.quests.size() > MaximumCanonicalQuestStates - totalQuests
                || player.journal.size() > MaximumCanonicalJournalEntries - totalJournal)
                return false;
            totalQuests += player.quests.size();
            totalJournal += player.journal.size();
            std::size_t priorQuestIndex = 0;
            bool hasPriorQuest = false;
            for (const auto& quest : player.quests)
            {
                const auto declaration = std::ranges::find(catalog.quests(), quest.id, &QuestCatalogEntry::id);
                if (declaration == catalog.quests().end() || !containsStage(*declaration, quest.stage)
                    || quest.revision == QuestRevision::initial())
                    return false;
                const auto index = static_cast<std::size_t>(declaration - catalog.quests().begin());
                if ((hasPriorQuest && index <= priorQuestIndex) || quest.lastChangeTick.value() == 0)
                    return false;
                priorQuestIndex = index;
                hasPriorQuest = true;
            }
            JournalRevision expectedRevision = JournalRevision::initial();
            ServerTick priorChangeTick = ServerTick::initial();
            for (const auto& entry : player.journal)
            {
                if (!catalog.findJournalEntry(entry.id) || entry.changeTick.value() == 0
                    || entry.changeTick < priorChangeTick || entry.changeTick > player.lastJournalChangeTick)
                    return false;
                const auto next = expectedRevision.next();
                if (!next || entry.revision != *next)
                    return false;
                expectedRevision = *next;
                priorChangeTick = entry.changeTick;
            }
            if (player.journalRevision != expectedRevision
                || (player.journal.empty() && player.lastJournalChangeTick != ServerTick::initial())
                || (!player.journal.empty() && player.journal.back().changeTick != player.lastJournalChangeTick))
                return false;
            for (std::size_t index = 0; index < player.journal.size(); ++index)
                if (std::ranges::any_of(std::span(player.journal).first(index),
                        [&](const auto& prior) { return prior.id == player.journal[index].id; }))
                    return false;
        }
        return true;
    }

    bool containsRank(const FactionCatalogEntry& faction, FactionRank rank) noexcept
    {
        return std::ranges::find(faction.ranks, rank) != faction.ranks.end();
    }

    bool validFactionState(
        const FactionDialogueCatalog& catalog, std::span<const CanonicalPlayerFactionState> players) noexcept
    {
        if (players.size() > MaximumPlayerFactionStates)
            return false;
        std::size_t totalFactions = 0;
        for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
        {
            const auto& player = players[playerIndex];
            if ((playerIndex != 0 && players[playerIndex - 1].player >= player.player) || player.factions.empty()
                || player.factions.size() > catalog.factions().size()
                || player.factions.size() > MaximumCanonicalFactionStates - totalFactions)
                return false;
            totalFactions += player.factions.size();
            std::size_t priorFactionIndex = 0;
            bool hasPriorFaction = false;
            for (const auto& faction : player.factions)
            {
                const auto declaration = std::ranges::find(catalog.factions(), faction.id, &FactionCatalogEntry::id);
                if (declaration == catalog.factions().end() || (faction.rank && !containsRank(*declaration, *faction.rank)))
                    return false;
                const auto catalogIndex = static_cast<std::size_t>(declaration - catalog.factions().begin());
                if ((hasPriorFaction && catalogIndex <= priorFactionIndex)
                    || faction.rank.has_value()
                        != (faction.membershipRevision != FactionMembershipRevision::initial())
                    || faction.rank.has_value() != (faction.lastMembershipChangeTick != ServerTick::initial())
                    || (faction.reputationRevision == FactionReputationRevision::initial()
                        && (faction.reputation != 0 || faction.lastReputationChangeTick != ServerTick::initial()))
                    || (faction.reputationRevision != FactionReputationRevision::initial()
                        && faction.lastReputationChangeTick == ServerTick::initial())
                    || (!faction.rank && faction.reputationRevision == FactionReputationRevision::initial()))
                    return false;
                priorFactionIndex = catalogIndex;
                hasPriorFaction = true;
            }
        }
        return true;
    }

    bool eligible(const WeatherRegionCatalogEntry& region, WeatherId weather) noexcept
    {
        return std::ranges::find(region.eligibleWeather, weather) != region.eligibleWeather.end();
    }

    std::optional<ServerTick> addTicks(ServerTick tick, std::uint64_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::uint64_t>::max() - tick.value())
            return std::nullopt;
        return ServerTick::fromValue(tick.value() + amount);
    }

    bool validWeatherState(const WeatherCatalog& catalog, const CanonicalWeatherState& weather) noexcept
    {
        if (weather.regions.size() != catalog.regions().size())
            return false;
        for (std::size_t index = 0; index < weather.regions.size(); ++index)
        {
            const auto& state = weather.regions[index];
            const auto& declaration = catalog.regions()[index];
            if (state.region != declaration.id || !eligible(declaration, state.currentWeather)
                || !eligible(declaration, state.targetWeather) || state.lastChangeTick > weather.lastAdvanceTick
                || state.transitionStartTick > state.transitionEndTick
                || state.nextSelectionTick < state.transitionEndTick)
                return false;
            if (state.currentWeather == state.targetWeather
                && state.transitionStartTick != state.transitionEndTick)
                return false;
            if (state.currentWeather != state.targetWeather
                && (state.transitionStartTick == state.transitionEndTick
                    || state.lastChangeTick != state.transitionStartTick))
                return false;
        }
        return true;
    }

    std::optional<CanonicalWorldState> recreateWorld(const CanonicalWorldState& state, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globals,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions) noexcept
    {
        if (state.questJournalCatalog() && state.factionDialogueCatalog() && state.weatherCatalog() && state.weather())
            return CanonicalWorldState::create(time, globals, *state.questJournalCatalog(),
                *state.factionDialogueCatalog(), *state.weatherCatalog(), questJournal, factions, *state.weather());
        if (state.questJournalCatalog() && state.factionDialogueCatalog())
            return CanonicalWorldState::create(time, globals, *state.questJournalCatalog(),
                *state.factionDialogueCatalog(), questJournal, factions);
        if (state.questJournalCatalog())
            return CanonicalWorldState::create(time, globals, *state.questJournalCatalog(), questJournal);
        return CanonicalWorldState::create(time, globals);
    }
}

namespace TES3MP
{
    std::optional<GlobalVariableCatalog> GlobalVariableCatalog::create(
        std::span<const GlobalVariableCatalogEntry> entries) noexcept
    try
    {
        if (entries.size() > MaximumGlobalVariables)
            return std::nullopt;
        std::vector<GlobalVariableCatalogEntry> copy(entries.begin(), entries.end());
        for (std::size_t index = 0; index < copy.size(); ++index)
            if (!validValue(copy[index].initialValue)
                || std::ranges::any_of(
                    std::span(copy).first(index), [&](const auto& prior) { return prior.id == copy[index].id; }))
                return std::nullopt;
        return GlobalVariableCatalog(std::move(copy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const GlobalVariableCatalogEntry* GlobalVariableCatalog::find(GlobalVariableId id) const noexcept
    {
        const auto found = std::ranges::find(mEntries, id, &GlobalVariableCatalogEntry::id);
        return found == mEntries.end() ? nullptr : &*found;
    }

    std::optional<QuestJournalCatalog> QuestJournalCatalog::create(ContentManifestId manifest,
        std::span<const QuestCatalogEntry> quests, std::span<const JournalCatalogEntry> journal) noexcept
    try
    {
        if (quests.size() > MaximumQuestCatalogEntries || journal.size() > MaximumJournalCatalogEntries)
            return std::nullopt;
        std::vector<QuestCatalogEntry> questCopy(quests.begin(), quests.end());
        std::size_t totalStages = 0;
        for (std::size_t index = 0; index < questCopy.size(); ++index)
        {
            auto& quest = questCopy[index];
            if (quest.stages.empty() || quest.stages.size() > MaximumQuestStagesPerQuest
                || quest.stages.size() > MaximumQuestCatalogStages - totalStages
                || !containsStage(quest, quest.initialStage)
                || std::ranges::any_of(
                    std::span(questCopy).first(index), [&](const auto& prior) { return prior.id == quest.id; }))
                return std::nullopt;
            totalStages += quest.stages.size();
            for (std::size_t stageIndex = 1; stageIndex < quest.stages.size(); ++stageIndex)
                if (quest.stages[stageIndex - 1] >= quest.stages[stageIndex])
                    return std::nullopt;
        }
        std::vector<JournalCatalogEntry> journalCopy(journal.begin(), journal.end());
        for (std::size_t index = 0; index < journalCopy.size(); ++index)
        {
            const auto& entry = journalCopy[index];
            const auto* quest = [&]() -> const QuestCatalogEntry* {
                const auto found = std::ranges::find(questCopy, entry.quest, &QuestCatalogEntry::id);
                return found == questCopy.end() ? nullptr : &*found;
            }();
            if (!quest || !containsStage(*quest, entry.stage)
                || std::ranges::any_of(
                    std::span(journalCopy).first(index), [&](const auto& prior) { return prior.id == entry.id; }))
                return std::nullopt;
        }
        return QuestJournalCatalog(manifest, std::move(questCopy), std::move(journalCopy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const QuestCatalogEntry* QuestJournalCatalog::findQuest(QuestId id) const noexcept
    {
        const auto found = std::ranges::find(mQuests, id, &QuestCatalogEntry::id);
        return found == mQuests.end() ? nullptr : &*found;
    }

    const JournalCatalogEntry* QuestJournalCatalog::findJournalEntry(JournalEntryId id) const noexcept
    {
        const auto found = std::ranges::find(mJournal, id, &JournalCatalogEntry::id);
        return found == mJournal.end() ? nullptr : &*found;
    }

    std::optional<FactionDialogueCatalog> FactionDialogueCatalog::create(ContentManifestId manifest,
        std::span<const FactionCatalogEntry> factions,
        std::span<const DialogueChoiceCatalogEntry> dialogueChoices) noexcept
    try
    {
        if (factions.size() > MaximumFactionCatalogEntries
            || dialogueChoices.size() > MaximumDialogueChoiceCatalogEntries)
            return std::nullopt;
        std::vector<FactionCatalogEntry> factionCopy(factions.begin(), factions.end());
        std::size_t totalRanks = 0;
        for (std::size_t index = 0; index < factionCopy.size(); ++index)
        {
            const auto& faction = factionCopy[index];
            if (faction.ranks.empty() || faction.ranks.size() > MaximumFactionRanksPerFaction
                || faction.ranks.size() > MaximumFactionCatalogRanks - totalRanks
                || std::ranges::any_of(std::span(factionCopy).first(index),
                    [&](const auto& prior) { return prior.id == faction.id; }))
                return std::nullopt;
            totalRanks += faction.ranks.size();
            for (std::size_t rankIndex = 1; rankIndex < faction.ranks.size(); ++rankIndex)
                if (faction.ranks[rankIndex - 1] >= faction.ranks[rankIndex])
                    return std::nullopt;
        }
        std::vector<DialogueChoiceCatalogEntry> choiceCopy(dialogueChoices.begin(), dialogueChoices.end());
        for (std::size_t index = 0; index < choiceCopy.size(); ++index)
        {
            const auto& choice = choiceCopy[index];
            const FactionCatalogEntry* faction = nullptr;
            if (choice.requiredFaction)
            {
                const auto found
                    = std::ranges::find(factionCopy, *choice.requiredFaction, &FactionCatalogEntry::id);
                faction = found == factionCopy.end() ? nullptr : &*found;
            }
            if ((!choice.requiredFaction && (choice.minimumRank != FactionRank::initial()
                                                || choice.minimumReputation != 0))
                || (choice.requiredFaction
                    && (!faction || !containsRank(*faction, choice.minimumRank)))
                || std::ranges::any_of(std::span(choiceCopy).first(index),
                    [&](const auto& prior) { return prior.id == choice.id; }))
                return std::nullopt;
        }
        return FactionDialogueCatalog(manifest, std::move(factionCopy), std::move(choiceCopy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    const FactionCatalogEntry* FactionDialogueCatalog::findFaction(FactionId id) const noexcept
    {
        const auto found = std::ranges::find(mFactions, id, &FactionCatalogEntry::id);
        return found == mFactions.end() ? nullptr : &*found;
    }

    const DialogueChoiceCatalogEntry* FactionDialogueCatalog::findDialogueChoice(DialogueChoiceId id) const noexcept
    {
        const auto found = std::ranges::find(mDialogueChoices, id, &DialogueChoiceCatalogEntry::id);
        return found == mDialogueChoices.end() ? nullptr : &*found;
    }

    std::optional<WeatherCatalog> WeatherCatalog::create(ContentManifestId manifest,
        std::span<const WeatherId> weather, std::span<const WeatherRegionCatalogEntry> regions) noexcept
    try
    {
        if (weather.empty() || weather.size() > MaximumWeatherIdentities || regions.empty()
            || regions.size() > MaximumWeatherRegions)
            return std::nullopt;
        std::vector<WeatherId> weatherCopy(weather.begin(), weather.end());
        for (std::size_t index = 0; index < weatherCopy.size(); ++index)
            if (std::ranges::find(std::span(weatherCopy).first(index), weatherCopy[index])
                != std::span(weatherCopy).first(index).end())
                return std::nullopt;
        std::vector<WeatherRegionCatalogEntry> regionCopy(regions.begin(), regions.end());
        std::size_t totalEligibility = 0;
        for (std::size_t index = 0; index < regionCopy.size(); ++index)
        {
            const auto& region = regionCopy[index];
            if (region.selectionIntervalTicks == 0 || region.transitionDurationTicks == 0
                || region.selectionIntervalTicks > MaximumWeatherTimingTicks
                || region.transitionDurationTicks > MaximumWeatherTimingTicks || region.eligibleWeather.empty()
                || region.eligibleWeather.size() > MaximumWeatherEligibilityEntries - totalEligibility
                || std::ranges::any_of(std::span(regionCopy).first(index),
                    [&](const auto& prior) { return prior.id == region.id; }))
                return std::nullopt;
            totalEligibility += region.eligibleWeather.size();
            for (std::size_t weatherIndex = 0; weatherIndex < region.eligibleWeather.size(); ++weatherIndex)
            {
                const auto id = region.eligibleWeather[weatherIndex];
                if (std::ranges::find(weatherCopy, id) == weatherCopy.end()
                    || std::ranges::find(std::span(region.eligibleWeather).first(weatherIndex), id)
                        != std::span(region.eligibleWeather).first(weatherIndex).end())
                    return std::nullopt;
            }
            if (!eligible(region, region.initialWeather))
                return std::nullopt;
        }
        return WeatherCatalog(manifest, std::move(weatherCopy), std::move(regionCopy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    bool WeatherCatalog::contains(WeatherId id) const noexcept
    {
        return std::ranges::find(mWeather, id) != mWeather.end();
    }

    const WeatherRegionCatalogEntry* WeatherCatalog::findRegion(WeatherRegionId id) const noexcept
    {
        const auto found = std::ranges::find(mRegions, id, &WeatherRegionCatalogEntry::id);
        return found == mRegions.end() ? nullptr : &*found;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::create(
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept
    try
    {
        if (!validTime(time) || globals.size() > MaximumGlobalVariables)
            return std::nullopt;
        std::vector<CanonicalGlobalVariableState> copy(globals.begin(), globals.end());
        for (std::size_t index = 0; index < copy.size(); ++index)
            if (!validValue(copy[index].value)
                || std::ranges::any_of(
                    std::span(copy).first(index), [&](const auto& prior) { return prior.id == copy[index].id; }))
                return std::nullopt;
        return CanonicalWorldState(time, std::move(copy));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::create(CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept
    try
    {
        auto base = create(time, globals);
        if (!base || !validQuestJournalState(questJournalCatalog, questJournal))
            return std::nullopt;
        return CanonicalWorldState(time, std::vector<CanonicalGlobalVariableState>(globals.begin(), globals.end()),
            std::move(questJournalCatalog),
            std::vector<CanonicalPlayerQuestJournalState>(questJournal.begin(), questJournal.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::create(CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
        FactionDialogueCatalog factionDialogueCatalog, WeatherCatalog weatherCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions, CanonicalWeatherState weather) noexcept
    try
    {
        auto base = create(time, globals, questJournalCatalog, factionDialogueCatalog, questJournal, factions);
        if (!base || questJournalCatalog.manifest() != weatherCatalog.manifest()
            || !validWeatherState(weatherCatalog, weather))
            return std::nullopt;
        return CanonicalWorldState(time, std::vector<CanonicalGlobalVariableState>(globals.begin(), globals.end()),
            std::move(questJournalCatalog),
            std::vector<CanonicalPlayerQuestJournalState>(questJournal.begin(), questJournal.end()),
            std::move(factionDialogueCatalog),
            std::vector<CanonicalPlayerFactionState>(factions.begin(), factions.end()),
            std::move(weatherCatalog), std::move(weather));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::create(CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globals, QuestJournalCatalog questJournalCatalog,
        FactionDialogueCatalog factionDialogueCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions) noexcept
    try
    {
        auto base = create(time, globals);
        if (!base || questJournalCatalog.manifest() != factionDialogueCatalog.manifest()
            || !validQuestJournalState(questJournalCatalog, questJournal)
            || !validFactionState(factionDialogueCatalog, factions))
            return std::nullopt;
        return CanonicalWorldState(time, std::vector<CanonicalGlobalVariableState>(globals.begin(), globals.end()),
            std::move(questJournalCatalog),
            std::vector<CanonicalPlayerQuestJournalState>(questJournal.begin(), questJournal.end()),
            std::move(factionDialogueCatalog),
            std::vector<CanonicalPlayerFactionState>(factions.begin(), factions.end()));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::initial(
        CanonicalWorldTimeState time, const GlobalVariableCatalog& catalog) noexcept
    try
    {
        std::vector<CanonicalGlobalVariableState> globals;
        globals.reserve(catalog.entries().size());
        for (const auto& entry : catalog.entries())
            globals.push_back(
                { entry.id, entry.initialValue, GlobalVariableRevision::initial(), time.lastAdvanceTick });
        return create(time, globals);
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::initial(CanonicalWorldTimeState time,
        const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog) noexcept
    try
    {
        auto base = initial(time, globals);
        return base ? create(base->time(), base->globals(), std::move(questJournalCatalog), {}) : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::initial(CanonicalWorldTimeState time,
        const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog,
        FactionDialogueCatalog factionDialogueCatalog, WeatherCatalog weatherCatalog,
        RandomStateV1 weatherRandomState) noexcept
    try
    {
        auto base = initial(time, globals, questJournalCatalog, factionDialogueCatalog);
        if (!base)
            return std::nullopt;
        CanonicalWeatherState weather{ {}, weatherRandomState, time.lastAdvanceTick };
        weather.regions.reserve(weatherCatalog.regions().size());
        for (const auto& region : weatherCatalog.regions())
        {
            const auto nextSelection = addTicks(time.lastAdvanceTick, region.selectionIntervalTicks);
            if (!nextSelection)
                return std::nullopt;
            weather.regions.push_back({ region.id, region.initialWeather, region.initialWeather,
                time.lastAdvanceTick, time.lastAdvanceTick, *nextSelection, WeatherRevision::initial(),
                time.lastAdvanceTick });
        }
        return create(base->time(), base->globals(), std::move(questJournalCatalog),
            std::move(factionDialogueCatalog), std::move(weatherCatalog), {}, {}, std::move(weather));
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<CanonicalWorldState> CanonicalWorldState::initial(CanonicalWorldTimeState time,
        const GlobalVariableCatalog& globals, QuestJournalCatalog questJournalCatalog,
        FactionDialogueCatalog factionDialogueCatalog) noexcept
    try
    {
        auto base = initial(time, globals);
        return base ? create(base->time(), base->globals(), std::move(questJournalCatalog),
                          std::move(factionDialogueCatalog), {}, {})
                    : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }

    const CanonicalGlobalVariableState* CanonicalWorldState::find(GlobalVariableId id) const noexcept
    {
        const auto found = std::ranges::find(mGlobals, id, &CanonicalGlobalVariableState::id);
        return found == mGlobals.end() ? nullptr : &*found;
    }

    const CanonicalPlayerQuestJournalState* CanonicalWorldState::findQuestJournal(PlayerId player) const noexcept
    {
        const auto found
            = std::ranges::lower_bound(mQuestJournal, player, {}, &CanonicalPlayerQuestJournalState::player);
        return found == mQuestJournal.end() || found->player != player ? nullptr : &*found;
    }

    const CanonicalPlayerFactionState* CanonicalWorldState::findFactionState(PlayerId player) const noexcept
    {
        const auto found = std::ranges::lower_bound(mFactionStates, player, {}, &CanonicalPlayerFactionState::player);
        return found == mFactionStates.end() || found->player != player ? nullptr : &*found;
    }

    const CanonicalWeatherRegionState* CanonicalWorldState::findWeather(WeatherRegionId region) const noexcept
    {
        if (!mWeather)
            return nullptr;
        const auto found = std::ranges::find(mWeather->regions, region, &CanonicalWeatherRegionState::region);
        return found == mWeather->regions.end() ? nullptr : &*found;
    }

    CanonicalWorldMutationResult advanceCanonicalWorldTime(
        const CanonicalWorldState& state, ServerTick tick, std::uint64_t tickIntervalMilliseconds) noexcept
    try
    {
        const auto& current = state.time();
        if (!validTime(current) || tick < current.lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        if (tick == current.lastAdvanceTick)
            return state;
        const auto elapsedTicks = tick.value() - current.lastAdvanceTick.value();
        if (tickIntervalMilliseconds != 0
            && elapsedTicks > std::numeric_limits<std::uint64_t>::max() / tickIntervalMilliseconds)
            return CanonicalWorldMutationError::ArithmeticOverflow;
        const auto realMilliseconds = elapsedTicks * tickIntervalMilliseconds;
        if (current.timeScaleUnits != 0
            && realMilliseconds > (std::numeric_limits<std::uint64_t>::max() - current.subMillisecondRemainder)
                    / current.timeScaleUnits)
            return CanonicalWorldMutationError::ArithmeticOverflow;
        const auto scaled = realMilliseconds * current.timeScaleUnits + current.subMillisecondRemainder;
        const auto elapsedWorldMilliseconds = scaled / WorldTimeScaleUnitsPerOne;
        CanonicalWorldTimeState next = current;
        next.subMillisecondRemainder = static_cast<std::uint16_t>(scaled % WorldTimeScaleUnitsPerOne);
        next.lastAdvanceTick = tick;
        if (elapsedWorldMilliseconds != 0)
        {
            if (!current.revision.next())
                return CanonicalWorldMutationError::RevisionExhausted;
            if (elapsedWorldMilliseconds
                > std::numeric_limits<std::uint64_t>::max() - current.millisecondsSinceMidnight)
                return CanonicalWorldMutationError::ArithmeticOverflow;
            const auto total = static_cast<std::uint64_t>(current.millisecondsSinceMidnight) + elapsedWorldMilliseconds;
            const auto elapsedDays = total / WorldMillisecondsPerDay;
            next.millisecondsSinceMidnight = static_cast<std::uint32_t>(total % WorldMillisecondsPerDay);
            const auto currentDay = static_cast<std::uint64_t>(current.year) * 360
                + static_cast<std::uint64_t>(current.month) * 30 + current.day - 1;
            if (elapsedDays
                > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) * 360 + 359 - currentDay)
                return CanonicalWorldMutationError::ArithmeticOverflow;
            const auto absoluteDay = currentDay + elapsedDays;
            next.year = static_cast<std::int32_t>(absoluteDay / 360);
            next.month = static_cast<std::uint8_t>((absoluteDay % 360) / 30);
            next.day = static_cast<std::uint8_t>((absoluteDay % 30) + 1);
            next.revision = *current.revision.next();
            next.lastChangeTick = tick;
        }
        auto result = recreateWorld(state, next, state.globals(), state.questJournal(), state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult advanceCanonicalWorldTimeByHours(
        const CanonicalWorldState& state, ServerTick tick, std::uint8_t hours) noexcept
    try
    {
        const auto& current = state.time();
        if (!validTime(current) || hours == 0 || hours > MaximumWaitRestHours)
            return CanonicalWorldMutationError::InvalidState;
        if (tick < current.lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        const auto revision = current.revision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;

        const auto elapsed = static_cast<std::uint64_t>(hours) * WorldMillisecondsPerHour;
        const auto total = static_cast<std::uint64_t>(current.millisecondsSinceMidnight) + elapsed;
        const auto elapsedDays = total / WorldMillisecondsPerDay;
        const auto currentDay = static_cast<std::uint64_t>(current.year) * 360
            + static_cast<std::uint64_t>(current.month) * 30 + current.day - 1;
        if (elapsedDays
            > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) * 360 + 359 - currentDay)
            return CanonicalWorldMutationError::ArithmeticOverflow;

        CanonicalWorldTimeState next = current;
        const auto absoluteDay = currentDay + elapsedDays;
        next.year = static_cast<std::int32_t>(absoluteDay / 360);
        next.month = static_cast<std::uint8_t>((absoluteDay % 360) / 30);
        next.day = static_cast<std::uint8_t>((absoluteDay % 30) + 1);
        next.millisecondsSinceMidnight = static_cast<std::uint32_t>(total % WorldMillisecondsPerDay);
        next.revision = *revision;
        next.lastChangeTick = tick;
        next.lastAdvanceTick = tick;

        auto result = recreateWorld(state, next, state.globals(), state.questJournal(), state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalWorldTime(const CanonicalWorldState& state,
        WorldTimeRevision expectedRevision, CanonicalWorldTimeState replacement, ServerTick tick) noexcept
    try
    {
        const auto& current = state.time();
        if (expectedRevision != current.revision)
            return CanonicalWorldMutationError::RevisionMismatch;
        if (tick < current.lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        const auto revision = current.revision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        replacement.revision = *revision;
        replacement.lastChangeTick = tick;
        replacement.lastAdvanceTick = tick;
        replacement.subMillisecondRemainder = 0;
        auto result
            = recreateWorld(state, replacement, state.globals(), state.questJournal(), state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalGlobal(const CanonicalWorldState& state,
        const GlobalVariableCatalog& catalog, GlobalVariableId id, GlobalVariableRevision expectedRevision,
        GlobalVariableValue value, ServerTick tick) noexcept
    try
    {
        const auto* declaration = catalog.find(id);
        const auto* current = state.find(id);
        if (!declaration || !current)
            return CanonicalWorldMutationError::UnknownGlobal;
        if (declaration->type() != static_cast<GlobalVariableType>(value.index())
            || current->type() != declaration->type() || !validValue(value))
            return CanonicalWorldMutationError::TypeMismatch;
        if (current->revision != expectedRevision)
            return CanonicalWorldMutationError::RevisionMismatch;
        if (tick < current->lastChangeTick)
            return CanonicalWorldMutationError::TickRegression;
        if (sameValue(current->value, value))
            return state;
        const auto revision = current->revision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        std::vector<CanonicalGlobalVariableState> globals(state.globals().begin(), state.globals().end());
        auto found = std::ranges::find(globals, id, &CanonicalGlobalVariableState::id);
        found->value = std::move(value);
        found->revision = *revision;
        found->lastChangeTick = tick;
        auto result = recreateWorld(state, state.time(), globals, state.questJournal(), state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalQuestStage(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, QuestRevision expectedRevision, QuestStage stage, ServerTick tick) noexcept
    try
    {
        if (!state.questJournalCatalog())
            return CanonicalWorldMutationError::UnknownQuest;
        const auto& catalog = *state.questJournalCatalog();
        const auto* declaration = catalog.findQuest(quest);
        if (!declaration)
            return CanonicalWorldMutationError::UnknownQuest;
        if (!containsStage(*declaration, stage))
            return CanonicalWorldMutationError::UnknownQuestStage;

        std::vector<CanonicalPlayerQuestJournalState> players(state.questJournal().begin(), state.questJournal().end());
        auto playerState = std::ranges::lower_bound(players, player, {}, &CanonicalPlayerQuestJournalState::player);
        if (playerState == players.end() || playerState->player != player)
            playerState = players.insert(playerState, CanonicalPlayerQuestJournalState{ player });
        auto current = std::ranges::find(playerState->quests, quest, &CanonicalQuestState::id);
        const auto currentRevision
            = current == playerState->quests.end() ? QuestRevision::initial() : current->revision;
        const auto currentStage = current == playerState->quests.end() ? declaration->initialStage : current->stage;
        const auto currentTick = current == playerState->quests.end() ? ServerTick::initial() : current->lastChangeTick;
        if (expectedRevision != currentRevision)
            return CanonicalWorldMutationError::QuestRevisionMismatch;
        if (tick < currentTick)
            return CanonicalWorldMutationError::TickRegression;
        if (stage == currentStage)
            return state;
        const auto revision = currentRevision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        if (current == playerState->quests.end())
        {
            const auto targetRank = static_cast<std::size_t>(
                std::ranges::find(catalog.quests(), quest, &QuestCatalogEntry::id) - catalog.quests().begin());
            current = std::ranges::find_if(playerState->quests, [&](const auto& existing) {
                const auto rank = std::ranges::find(catalog.quests(), existing.id, &QuestCatalogEntry::id)
                    - catalog.quests().begin();
                return static_cast<std::size_t>(rank) > targetRank;
            });
            playerState->quests.insert(current, CanonicalQuestState{ quest, stage, *revision, tick });
        }
        else
        {
            current->stage = stage;
            current->revision = *revision;
            current->lastChangeTick = tick;
        }
        auto result = recreateWorld(state, state.time(), state.globals(), players, state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult addCanonicalJournalEntry(const CanonicalWorldState& state, PlayerId player,
        QuestId quest, JournalRevision expectedRevision, JournalEntryId entry, ServerTick tick) noexcept
    try
    {
        if (!state.questJournalCatalog())
            return CanonicalWorldMutationError::UnknownJournalEntry;
        const auto& catalog = *state.questJournalCatalog();
        if (!catalog.findQuest(quest))
            return CanonicalWorldMutationError::UnknownQuest;
        const auto* declaration = catalog.findJournalEntry(entry);
        if (!declaration)
            return CanonicalWorldMutationError::UnknownJournalEntry;
        if (declaration->quest != quest)
            return CanonicalWorldMutationError::JournalEntryQuestMismatch;

        std::vector<CanonicalPlayerQuestJournalState> players(state.questJournal().begin(), state.questJournal().end());
        auto playerState = std::ranges::lower_bound(players, player, {}, &CanonicalPlayerQuestJournalState::player);
        if (playerState == players.end() || playerState->player != player)
            playerState = players.insert(playerState, CanonicalPlayerQuestJournalState{ player });
        if (playerState->journalRevision != expectedRevision)
            return CanonicalWorldMutationError::JournalRevisionMismatch;
        if (tick < playerState->lastJournalChangeTick)
            return CanonicalWorldMutationError::TickRegression;
        if (std::ranges::any_of(playerState->journal, [&](const auto& value) { return value.id == entry; }))
            return CanonicalWorldMutationError::DuplicateJournalEntry;
        const auto revision = playerState->journalRevision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        playerState->journal.push_back({ entry, *revision, tick });
        playerState->journalRevision = *revision;
        playerState->lastJournalChangeTick = tick;
        auto result = recreateWorld(state, state.time(), state.globals(), players, state.factionStates());
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalFactionRank(const CanonicalWorldState& state, PlayerId player,
        FactionId faction, FactionMembershipRevision expectedRevision, FactionRank rank, ServerTick tick) noexcept
    try
    {
        if (!state.factionDialogueCatalog())
            return CanonicalWorldMutationError::UnknownFaction;
        const auto& catalog = *state.factionDialogueCatalog();
        const auto* declaration = catalog.findFaction(faction);
        if (!declaration)
            return CanonicalWorldMutationError::UnknownFaction;
        if (!containsRank(*declaration, rank))
            return CanonicalWorldMutationError::UnknownFactionRank;

        std::vector<CanonicalPlayerFactionState> players(state.factionStates().begin(), state.factionStates().end());
        auto playerState = std::ranges::lower_bound(players, player, {}, &CanonicalPlayerFactionState::player);
        if (playerState == players.end() || playerState->player != player)
            playerState = players.insert(playerState, CanonicalPlayerFactionState{ player });
        auto current = std::ranges::find(playerState->factions, faction, &CanonicalFactionState::id);
        const auto currentRevision = current == playerState->factions.end()
            ? FactionMembershipRevision::initial()
            : current->membershipRevision;
        const auto currentTick = current == playerState->factions.end()
            ? ServerTick::initial()
            : current->lastMembershipChangeTick;
        if (expectedRevision != currentRevision)
            return CanonicalWorldMutationError::FactionMembershipRevisionMismatch;
        if (tick < currentTick)
            return CanonicalWorldMutationError::TickRegression;
        if (current != playerState->factions.end() && current->rank == rank)
            return state;
        const auto revision = currentRevision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        if (current == playerState->factions.end())
        {
            const auto targetIndex = static_cast<std::size_t>(
                std::ranges::find(catalog.factions(), faction, &FactionCatalogEntry::id) - catalog.factions().begin());
            current = std::ranges::find_if(playerState->factions, [&](const auto& existing) {
                const auto index = std::ranges::find(catalog.factions(), existing.id, &FactionCatalogEntry::id)
                    - catalog.factions().begin();
                return static_cast<std::size_t>(index) > targetIndex;
            });
            playerState->factions.insert(current,
                CanonicalFactionState{ faction, rank, *revision, tick, 0,
                    FactionReputationRevision::initial(), ServerTick::initial() });
        }
        else
        {
            current->rank = rank;
            current->membershipRevision = *revision;
            current->lastMembershipChangeTick = tick;
        }
        auto result = recreateWorld(state, state.time(), state.globals(), state.questJournal(), players);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalFactionReputation(const CanonicalWorldState& state, PlayerId player,
        FactionId faction, FactionReputationRevision expectedRevision, std::int32_t reputation,
        ServerTick tick) noexcept
    try
    {
        if (!state.factionDialogueCatalog() || !state.factionDialogueCatalog()->findFaction(faction))
            return CanonicalWorldMutationError::UnknownFaction;
        const auto& catalog = *state.factionDialogueCatalog();
        std::vector<CanonicalPlayerFactionState> players(state.factionStates().begin(), state.factionStates().end());
        auto playerState = std::ranges::lower_bound(players, player, {}, &CanonicalPlayerFactionState::player);
        if (playerState == players.end() || playerState->player != player)
            playerState = players.insert(playerState, CanonicalPlayerFactionState{ player });
        auto current = std::ranges::find(playerState->factions, faction, &CanonicalFactionState::id);
        const auto currentRevision = current == playerState->factions.end()
            ? FactionReputationRevision::initial()
            : current->reputationRevision;
        const auto currentTick = current == playerState->factions.end()
            ? ServerTick::initial()
            : current->lastReputationChangeTick;
        const auto currentReputation = current == playerState->factions.end() ? 0 : current->reputation;
        if (expectedRevision != currentRevision)
            return CanonicalWorldMutationError::FactionReputationRevisionMismatch;
        if (tick < currentTick)
            return CanonicalWorldMutationError::TickRegression;
        if (reputation == currentReputation)
            return state;
        const auto revision = currentRevision.next();
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        if (current == playerState->factions.end())
        {
            const auto targetIndex = static_cast<std::size_t>(
                std::ranges::find(catalog.factions(), faction, &FactionCatalogEntry::id) - catalog.factions().begin());
            current = std::ranges::find_if(playerState->factions, [&](const auto& existing) {
                const auto index = std::ranges::find(catalog.factions(), existing.id, &FactionCatalogEntry::id)
                    - catalog.factions().begin();
                return static_cast<std::size_t>(index) > targetIndex;
            });
            playerState->factions.insert(current,
                CanonicalFactionState{ faction, std::nullopt, FactionMembershipRevision::initial(),
                    ServerTick::initial(), reputation, *revision, tick });
        }
        else
        {
            current->reputation = reputation;
            current->reputationRevision = *revision;
            current->lastReputationChangeTick = tick;
        }
        auto result = recreateWorld(state, state.time(), state.globals(), state.questJournal(), players);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    std::optional<CanonicalWorldMutationError> validateCanonicalDialogueChoice(
        const CanonicalWorldState& state, PlayerId player, DialogueChoiceId choice) noexcept
    try
    {
        if (!state.factionDialogueCatalog())
            return CanonicalWorldMutationError::UnknownDialogueChoice;
        const auto* declaration = state.factionDialogueCatalog()->findDialogueChoice(choice);
        if (!declaration)
            return CanonicalWorldMutationError::UnknownDialogueChoice;
        if (!declaration->requiredFaction)
            return std::nullopt;
        const auto* playerState = state.findFactionState(player);
        if (!playerState)
            return CanonicalWorldMutationError::DialogueChoiceIneligible;
        const auto found
            = std::ranges::find(playerState->factions, *declaration->requiredFaction, &CanonicalFactionState::id);
        if (found == playerState->factions.end() || !found->rank || *found->rank < declaration->minimumRank
            || found->reputation < declaration->minimumReputation)
            return CanonicalWorldMutationError::DialogueChoiceIneligible;
        return std::nullopt;
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult setCanonicalWeather(const CanonicalWorldState& state, WeatherRegionId region,
        WeatherRevision expectedRevision, WeatherId target, ServerTick tick) noexcept
    try
    {
        if (!state.weatherCatalog() || !state.weather())
            return CanonicalWorldMutationError::UnknownWeatherRegion;
        const auto* declaration = state.weatherCatalog()->findRegion(region);
        const auto* current = state.findWeather(region);
        if (!declaration || !current)
            return CanonicalWorldMutationError::UnknownWeatherRegion;
        if (!state.weatherCatalog()->contains(target))
            return CanonicalWorldMutationError::UnknownWeather;
        if (!eligible(*declaration, target))
            return CanonicalWorldMutationError::WeatherIneligible;
        if (current->revision != expectedRevision)
            return CanonicalWorldMutationError::WeatherRevisionMismatch;
        if (tick < state.weather()->lastAdvanceTick || tick < current->lastChangeTick)
            return CanonicalWorldMutationError::TickRegression;
        if (current->targetWeather == target)
            return state;
        const auto revision = current->revision.next();
        const auto transitionEnd = addTicks(tick, declaration->transitionDurationTicks);
        const auto nextSelection
            = transitionEnd ? addTicks(*transitionEnd, declaration->selectionIntervalTicks) : std::nullopt;
        if (!revision)
            return CanonicalWorldMutationError::RevisionExhausted;
        if (!transitionEnd || !nextSelection)
            return CanonicalWorldMutationError::ArithmeticOverflow;
        CanonicalWeatherState weather = *state.weather();
        auto found = std::ranges::find(weather.regions, region, &CanonicalWeatherRegionState::region);
        found->targetWeather = target;
        found->transitionStartTick = tick;
        found->transitionEndTick = target == found->currentWeather ? tick : *transitionEnd;
        found->nextSelectionTick
            = target == found->currentWeather ? *addTicks(tick, declaration->selectionIntervalTicks) : *nextSelection;
        found->revision = *revision;
        found->lastChangeTick = tick;
        weather.lastAdvanceTick = tick;
        auto result = CanonicalWorldState::create(state.time(), state.globals(), *state.questJournalCatalog(),
            *state.factionDialogueCatalog(), *state.weatherCatalog(), state.questJournal(), state.factionStates(),
            std::move(weather));
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult advanceCanonicalWeather(const CanonicalWorldState& state, ServerTick tick) noexcept
    try
    {
        if (!state.weatherCatalog() || !state.weather())
            return state;
        if (tick < state.weather()->lastAdvanceTick)
            return CanonicalWorldMutationError::TickRegression;
        if (tick == state.weather()->lastAdvanceTick)
            return state;
        CanonicalWeatherState weather = *state.weather();
        auto random = Xoshiro256StarStar::restore(weather.randomState);
        for (std::size_t index = 0; index < weather.regions.size(); ++index)
        {
            auto& region = weather.regions[index];
            const auto& declaration = state.weatherCatalog()->regions()[index];
            if (region.currentWeather != region.targetWeather && tick >= region.transitionEndTick)
            {
                const auto revision = region.revision.next();
                if (!revision)
                    return CanonicalWorldMutationError::RevisionExhausted;
                region.currentWeather = region.targetWeather;
                region.transitionStartTick = tick;
                region.transitionEndTick = tick;
                region.revision = *revision;
                region.lastChangeTick = tick;
            }
            if (region.currentWeather == region.targetWeather && tick >= region.nextSelectionTick)
            {
                const auto selection = random.uniformBelow(declaration.eligibleWeather.size());
                const auto nextSelection = addTicks(tick, declaration.selectionIntervalTicks);
                if (!selection || !nextSelection)
                    return CanonicalWorldMutationError::ArithmeticOverflow;
                const WeatherId target = declaration.eligibleWeather[static_cast<std::size_t>(*selection)];
                region.nextSelectionTick = *nextSelection;
                const auto revision = region.revision.next();
                if (!revision)
                    return CanonicalWorldMutationError::RevisionExhausted;
                region.revision = *revision;
                region.lastChangeTick = tick;
                if (target != region.currentWeather)
                {
                    const auto transitionEnd = addTicks(tick, declaration.transitionDurationTicks);
                    const auto afterTransition
                        = transitionEnd ? addTicks(*transitionEnd, declaration.selectionIntervalTicks) : std::nullopt;
                    if (!transitionEnd || !afterTransition)
                        return CanonicalWorldMutationError::ArithmeticOverflow;
                    region.targetWeather = target;
                    region.transitionStartTick = tick;
                    region.transitionEndTick = *transitionEnd;
                    region.nextSelectionTick = *afterTransition;
                }
            }
        }
        weather.randomState = random.snapshot();
        weather.lastAdvanceTick = tick;
        auto result = CanonicalWorldState::create(state.time(), state.globals(), *state.questJournalCatalog(),
            *state.factionDialogueCatalog(), *state.weatherCatalog(), state.questJournal(), state.factionStates(),
            std::move(weather));
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& catalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globals) noexcept
    try
    {
        if (catalog.entries().size() != globals.size())
            return CanonicalWorldMutationError::CatalogMismatch;
        for (std::size_t index = 0; index < globals.size(); ++index)
            if (catalog.entries()[index].id != globals[index].id
                || catalog.entries()[index].type() != globals[index].type())
                return CanonicalWorldMutationError::CatalogMismatch;
        auto result = CanonicalWorldState::create(time, globals);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::InvalidState);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal) noexcept
    try
    {
        if (expectedQuestJournalCatalog != restoredQuestJournalCatalog)
            return CanonicalWorldMutationError::CatalogMismatch;
        const auto restoredGlobals = restoreCanonicalWorldState(globals, time, globalStates);
        const auto* base = std::get_if<CanonicalWorldState>(&restoredGlobals);
        if (!base)
            return std::get<CanonicalWorldMutationError>(restoredGlobals);
        auto result
            = CanonicalWorldState::create(base->time(), base->globals(), expectedQuestJournalCatalog, questJournal);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::CatalogMismatch);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog,
        const FactionDialogueCatalog& expectedFactionDialogueCatalog, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        const FactionDialogueCatalog& restoredFactionDialogueCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions) noexcept
    try
    {
        if (expectedQuestJournalCatalog != restoredQuestJournalCatalog
            || expectedFactionDialogueCatalog != restoredFactionDialogueCatalog)
            return CanonicalWorldMutationError::CatalogMismatch;
        const auto restoredGlobals = restoreCanonicalWorldState(globals, time, globalStates);
        const auto* base = std::get_if<CanonicalWorldState>(&restoredGlobals);
        if (!base)
            return std::get<CanonicalWorldMutationError>(restoredGlobals);
        auto result = CanonicalWorldState::create(base->time(), base->globals(), expectedQuestJournalCatalog,
            expectedFactionDialogueCatalog, questJournal, factions);
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::CatalogMismatch);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }

    CanonicalWorldMutationResult restoreCanonicalWorldState(const GlobalVariableCatalog& globals,
        const QuestJournalCatalog& expectedQuestJournalCatalog,
        const FactionDialogueCatalog& expectedFactionDialogueCatalog, const WeatherCatalog& expectedWeatherCatalog,
        CanonicalWorldTimeState time, std::span<const CanonicalGlobalVariableState> globalStates,
        const QuestJournalCatalog& restoredQuestJournalCatalog,
        const FactionDialogueCatalog& restoredFactionDialogueCatalog, const WeatherCatalog& restoredWeatherCatalog,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions, CanonicalWeatherState weather) noexcept
    try
    {
        if (expectedWeatherCatalog != restoredWeatherCatalog)
            return CanonicalWorldMutationError::CatalogMismatch;
        const auto restored = restoreCanonicalWorldState(globals, expectedQuestJournalCatalog,
            expectedFactionDialogueCatalog, time, globalStates, restoredQuestJournalCatalog,
            restoredFactionDialogueCatalog, questJournal, factions);
        const auto* base = std::get_if<CanonicalWorldState>(&restored);
        if (!base)
            return std::get<CanonicalWorldMutationError>(restored);
        auto result = CanonicalWorldState::create(base->time(), base->globals(), expectedQuestJournalCatalog,
            expectedFactionDialogueCatalog, expectedWeatherCatalog, base->questJournal(), base->factionStates(),
            std::move(weather));
        return result ? CanonicalWorldMutationResult(std::move(*result))
                      : CanonicalWorldMutationResult(CanonicalWorldMutationError::CatalogMismatch);
    }
    catch (...)
    {
        return CanonicalWorldMutationError::InvalidState;
    }
}
