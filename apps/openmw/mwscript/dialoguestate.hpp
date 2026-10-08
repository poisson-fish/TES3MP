#ifndef OPENMW_MWSCRIPT_DIALOGUESTATE_H
#define OPENMW_MWSCRIPT_DIALOGUESTATE_H

#include "executioncontext.hpp"
#include <components/esm3/loaddial.hpp>
#include <components/esm3/queststate.hpp>
#include <span>
#include <string>
#include <vector>

namespace MWScript
{
    // Borrowed transaction state. No Environment, singleton journal or UI writes.
    class DialogueState
    {
    public:
        static constexpr size_t MaximumRecords = 16;
        static constexpr size_t MaximumInfos = 64;
        static constexpr size_t MaximumEntries = 64;
        struct Entry { ESM::RefId topic, info; bool operator==(const Entry&) const = default; };
        struct State
        {
            std::vector<ESM::QuestState> quests;
            std::vector<Entry> entries;
            std::vector<ESM::RefId> topics;
        };
        struct Choice
        {
            std::string text;
            int id;
            bool operator==(const Choice&) const = default;
        };
        std::vector<Choice> choices;
        void addChoice(std::string_view, int);
        DialogueState(const ExecutionContext&, State&, std::span<const ESM::Dialogue>, bool choicesAllowed = false);
        const ESM::Dialogue& record(const ESM::RefId&, ESM::Dialogue::Type) const;
        int journalIndex(const ESM::RefId&) const;
        void setJournalIndex(const ESM::RefId&, int);
        void journal(const ESM::RefId&, int);
        void addTopic(const ESM::RefId&);
        void validate() const;
        void validateExecution(const ExecutionContext& value) const;
    private:
        ExecutionContext mExecution;
        State& mState;
        std::span<const ESM::Dialogue> mRecords;
        bool mChoicesAllowed;
        ESM::QuestState& quest(const ESM::RefId&);
    };
}
#endif
