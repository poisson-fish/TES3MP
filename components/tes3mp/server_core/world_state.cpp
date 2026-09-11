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

    std::optional<CanonicalWorldState> recreateWorld(const CanonicalWorldState& state, CanonicalWorldTimeState time,
        std::span<const CanonicalGlobalVariableState> globals,
        std::span<const CanonicalPlayerQuestJournalState> questJournal,
        std::span<const CanonicalPlayerFactionState> factions) noexcept
    {
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
}
