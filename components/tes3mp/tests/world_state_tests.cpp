#include <tes3mp/world_state.hpp>

#include <array>
#include <bit>
#include <iostream>
#include <limits>

namespace
{
    using namespace TES3MP;

    template <class T>
    T id(std::uint64_t value)
    {
        return T::fromValue(value).value();
    }

    GlobalVariableCatalog catalog()
    {
        const std::array entries{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int16_t{ 2 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(2), std::int32_t{ 3 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(3), 4.5f } };
        return GlobalVariableCatalog::create(entries).value();
    }

    CanonicalWorldState world()
    {
        CanonicalWorldTimeState time;
        time.day = 30;
        time.month = 11;
        time.year = 427;
        time.millisecondsSinceMidnight = 23 * WorldMillisecondsPerHour + 59 * 60 * 1000;
        time.timeScaleUnits = 30 * WorldTimeScaleUnitsPerOne;
        return CanonicalWorldState::initial(time, catalog()).value();
    }

    QuestJournalCatalog questCatalog()
    {
        const std::array quests{
            QuestCatalogEntry{
                id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10), id<QuestStage>(20) } },
            QuestCatalogEntry{ id<QuestId>(20), id<QuestStage>(5), { id<QuestStage>(5), id<QuestStage>(15) } },
        };
        const std::array journal{
            JournalCatalogEntry{ id<JournalEntryId>(100), id<QuestId>(10), id<QuestStage>(10) },
            JournalCatalogEntry{ id<JournalEntryId>(101), id<QuestId>(10), id<QuestStage>(20) },
        };
        return QuestJournalCatalog::create(testContentManifest().id(), quests, journal).value();
    }

    CanonicalWorldState questWorld()
    {
        CanonicalWorldTimeState time;
        return CanonicalWorldState::initial(time, catalog(), questCatalog()).value();
    }

    FactionDialogueCatalog factionCatalog()
    {
        const std::array factions{ FactionCatalogEntry{
            id<FactionId>(30), { id<FactionRank>(0), id<FactionRank>(1), id<FactionRank>(2) } } };
        const std::array choices{ DialogueChoiceCatalogEntry{ id<DialogueChoiceId>(40) },
            DialogueChoiceCatalogEntry{ id<DialogueChoiceId>(41), id<FactionId>(30), id<FactionRank>(1), 10 } };
        return FactionDialogueCatalog::create(testContentManifest().id(), factions, choices).value();
    }

    CanonicalWorldState factionWorld()
    {
        return CanonicalWorldState::initial(
            CanonicalWorldTimeState{}, catalog(), questCatalog(), factionCatalog())
            .value();
    }

    bool time_advances_exactly_across_calendar_boundaries()
    {
        const auto advanced = advanceCanonicalWorldTime(world(), id<ServerTick>(125), 16);
        const auto* result = std::get_if<CanonicalWorldState>(&advanced);
        return result && result->time().year == 428 && result->time().month == 0 && result->time().day == 1
            && result->time().millisecondsSinceMidnight == 0 && result->time().revision.value() == 2
            && result->time().lastChangeTick == id<ServerTick>(125)
            && result->time().lastAdvanceTick == id<ServerTick>(125);
    }

    bool typed_global_updates_preserve_revision_and_tick()
    {
        const auto initial = world();
        const auto changed = setCanonicalGlobal(initial, catalog(), id<GlobalVariableId>(2),
            GlobalVariableRevision::initial(), std::int32_t{ 99 }, id<ServerTick>(7));
        const auto* result = std::get_if<CanonicalWorldState>(&changed);
        const auto* global = result ? result->find(id<GlobalVariableId>(2)) : nullptr;
        if (!global || std::get<std::int32_t>(global->value) != 99 || global->revision.value() != 2
            || global->lastChangeTick != id<ServerTick>(7))
            return false;
        return std::get<CanonicalWorldMutationError>(setCanonicalGlobal(*result, catalog(), id<GlobalVariableId>(2),
                   GlobalVariableRevision::initial(), std::int32_t{ 100 }, id<ServerTick>(8)))
            == CanonicalWorldMutationError::RevisionMismatch
            && std::get<CanonicalWorldMutationError>(setCanonicalGlobal(
                   *result, catalog(), id<GlobalVariableId>(2), id<GlobalVariableRevision>(2), 1.f, id<ServerTick>(8)))
            == CanonicalWorldMutationError::TypeMismatch;
    }

    bool restore_requires_the_exact_ordered_typed_catalog()
    {
        const auto initial = world();
        auto missing = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        missing.pop_back();
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), missing))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto extra = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        extra.push_back(
            { id<GlobalVariableId>(4), std::int16_t{ 0 }, GlobalVariableRevision::initial(), ServerTick::initial() });
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), extra))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto reordered = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        std::swap(reordered[0], reordered[1]);
        if (std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), reordered))
            != CanonicalWorldMutationError::CatalogMismatch)
            return false;
        auto mismatched = std::vector<CanonicalGlobalVariableState>(initial.globals().begin(), initial.globals().end());
        mismatched[1].value = 3.f;
        return std::get<CanonicalWorldMutationError>(restoreCanonicalWorldState(catalog(), initial.time(), mismatched))
            == CanonicalWorldMutationError::CatalogMismatch;
    }

    bool invalid_catalog_and_nonfinite_values_reject_without_partial_state()
    {
        const std::array duplicate{ GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int16_t{ 1 } },
            GlobalVariableCatalogEntry{ id<GlobalVariableId>(1), std::int32_t{ 2 } } };
        const std::array nonfinite{ GlobalVariableCatalogEntry{
            id<GlobalVariableId>(1), std::bit_cast<float>(std::uint32_t{ 0x7f800000 }) } };
        return !GlobalVariableCatalog::create(duplicate) && !GlobalVariableCatalog::create(nonfinite);
    }

    bool quest_and_journal_transactions_are_typed_revisioned_and_per_player()
    {
        auto changed = setCanonicalQuestStage(questWorld(), id<PlayerId>(1), id<QuestId>(10), QuestRevision::initial(),
            id<QuestStage>(10), id<ServerTick>(3));
        auto* first = std::get_if<CanonicalWorldState>(&changed);
        if (!first)
            return false;
        changed = addCanonicalJournalEntry(*first, id<PlayerId>(1), id<QuestId>(10), JournalRevision::initial(),
            id<JournalEntryId>(100), id<ServerTick>(3));
        const auto* result = std::get_if<CanonicalWorldState>(&changed);
        const auto* player = result ? result->findQuestJournal(id<PlayerId>(1)) : nullptr;
        if (!player || player->quests.size() != 1 || player->quests[0].stage != id<QuestStage>(10)
            || player->quests[0].revision.value() != 2 || player->quests[0].lastChangeTick != id<ServerTick>(3)
            || player->journal.size() != 1 || player->journal[0].id != id<JournalEntryId>(100)
            || player->journalRevision.value() != 2 || player->lastJournalChangeTick != id<ServerTick>(3))
            return false;
        return std::get<CanonicalWorldMutationError>(setCanonicalQuestStage(*result, id<PlayerId>(1), id<QuestId>(10),
                   QuestRevision::initial(), id<QuestStage>(20), id<ServerTick>(4)))
            == CanonicalWorldMutationError::QuestRevisionMismatch
            && std::get<CanonicalWorldMutationError>(setCanonicalQuestStage(*result, id<PlayerId>(1), id<QuestId>(10),
                   id<QuestRevision>(2), id<QuestStage>(99), id<ServerTick>(4)))
            == CanonicalWorldMutationError::UnknownQuestStage
            && std::get<CanonicalWorldMutationError>(addCanonicalJournalEntry(*result, id<PlayerId>(1), id<QuestId>(10),
                   id<JournalRevision>(2), id<JournalEntryId>(100), id<ServerTick>(4)))
            == CanonicalWorldMutationError::DuplicateJournalEntry;
    }

    bool quest_journal_restore_requires_the_exact_manifest_catalog()
    {
        const auto source = questWorld();
        const std::array changedQuests{ QuestCatalogEntry{
            id<QuestId>(10), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10) } } };
        const std::array<JournalCatalogEntry, 0> noJournal{};
        const auto mismatched
            = QuestJournalCatalog::create(testContentManifest().id(), changedQuests, noJournal).value();
        const auto restored = restoreCanonicalWorldState(
            catalog(), questCatalog(), source.time(), source.globals(), mismatched, source.questJournal());
        return std::get<CanonicalWorldMutationError>(restored) == CanonicalWorldMutationError::CatalogMismatch;
    }

    bool malformed_quest_catalogs_reject_atomically()
    {
        const std::array duplicateStages{ QuestCatalogEntry{
            id<QuestId>(1), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(0) } } };
        const std::array<JournalCatalogEntry, 0> none{};
        const std::array validQuest{ QuestCatalogEntry{
            id<QuestId>(1), id<QuestStage>(0), { id<QuestStage>(0), id<QuestStage>(10) } } };
        const std::array unknownQuestJournal{ JournalCatalogEntry{
            id<JournalEntryId>(1), id<QuestId>(2), id<QuestStage>(10) } };
        return !QuestJournalCatalog::create(testContentManifest().id(), duplicateStages, none)
            && !QuestJournalCatalog::create(testContentManifest().id(), validQuest, unknownQuestJournal);
    }

    bool faction_membership_reputation_and_dialogue_eligibility_are_revisioned()
    {
        auto rankChanged = setCanonicalFactionRank(factionWorld(), id<PlayerId>(1), id<FactionId>(30),
            FactionMembershipRevision::initial(), id<FactionRank>(1), id<ServerTick>(3));
        auto* ranked = std::get_if<CanonicalWorldState>(&rankChanged);
        if (!ranked)
            return false;
        auto reputationChanged = setCanonicalFactionReputation(*ranked, id<PlayerId>(1), id<FactionId>(30),
            FactionReputationRevision::initial(), 10, id<ServerTick>(3));
        const auto* result = std::get_if<CanonicalWorldState>(&reputationChanged);
        const auto* player = result ? result->findFactionState(id<PlayerId>(1)) : nullptr;
        if (!player || player->factions.size() != 1 || player->factions[0].rank != id<FactionRank>(1)
            || player->factions[0].membershipRevision.value() != 2 || player->factions[0].reputation != 10
            || player->factions[0].reputationRevision.value() != 2
            || validateCanonicalDialogueChoice(*result, id<PlayerId>(1), id<DialogueChoiceId>(41)))
            return false;
        return std::get<CanonicalWorldMutationError>(setCanonicalFactionRank(*result, id<PlayerId>(1),
                   id<FactionId>(30), FactionMembershipRevision::initial(), id<FactionRank>(2), id<ServerTick>(4)))
                == CanonicalWorldMutationError::FactionMembershipRevisionMismatch
            && std::get<CanonicalWorldMutationError>(setCanonicalFactionRank(*result, id<PlayerId>(1),
                   id<FactionId>(30), id<FactionMembershipRevision>(2), id<FactionRank>(9), id<ServerTick>(4)))
                == CanonicalWorldMutationError::UnknownFactionRank
            && validateCanonicalDialogueChoice(factionWorld(), id<PlayerId>(1), id<DialogueChoiceId>(41))
                == CanonicalWorldMutationError::DialogueChoiceIneligible
            && validateCanonicalDialogueChoice(*result, id<PlayerId>(1), id<DialogueChoiceId>(99))
                == CanonicalWorldMutationError::UnknownDialogueChoice;
    }

    bool malformed_faction_and_choice_catalogs_reject_atomically()
    {
        const std::array duplicateRanks{ FactionCatalogEntry{
            id<FactionId>(1), { id<FactionRank>(0), id<FactionRank>(0) } } };
        const std::array validFaction{
            FactionCatalogEntry{ id<FactionId>(1), { id<FactionRank>(0), id<FactionRank>(1) } } };
        const std::array badChoice{
            DialogueChoiceCatalogEntry{ id<DialogueChoiceId>(1), id<FactionId>(2), id<FactionRank>(0), 0 } };
        return !FactionDialogueCatalog::create(testContentManifest().id(), duplicateRanks, {})
            && !FactionDialogueCatalog::create(testContentManifest().id(), validFaction, badChoice);
    }
}

int main()
{
    const std::array tests{
        std::pair{
            "time_advances_exactly_across_calendar_boundaries", &time_advances_exactly_across_calendar_boundaries },
        std::pair{
            "typed_global_updates_preserve_revision_and_tick", &typed_global_updates_preserve_revision_and_tick },
        std::pair{
            "restore_requires_the_exact_ordered_typed_catalog", &restore_requires_the_exact_ordered_typed_catalog },
        std::pair{ "invalid_catalog_and_nonfinite_values_reject_without_partial_state",
            &invalid_catalog_and_nonfinite_values_reject_without_partial_state },
        std::pair{ "quest_and_journal_transactions_are_typed_revisioned_and_per_player",
            &quest_and_journal_transactions_are_typed_revisioned_and_per_player },
        std::pair{ "quest_journal_restore_requires_the_exact_manifest_catalog",
            &quest_journal_restore_requires_the_exact_manifest_catalog },
        std::pair{ "malformed_quest_catalogs_reject_atomically", &malformed_quest_catalogs_reject_atomically },
        std::pair{ "faction_membership_reputation_and_dialogue_eligibility_are_revisioned",
            &faction_membership_reputation_and_dialogue_eligibility_are_revisioned },
        std::pair{ "malformed_faction_and_choice_catalogs_reject_atomically",
            &malformed_faction_and_choice_catalogs_reject_atomically },
    };
    for (const auto& [name, test] : tests)
        if (!test())
        {
            std::cerr << "failed: " << name << '\n';
            return 1;
        }
    return 0;
}
