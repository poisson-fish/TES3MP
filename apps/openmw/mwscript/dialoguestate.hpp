#ifndef OPENMW_MWSCRIPT_DIALOGUESTATE_H
#define OPENMW_MWSCRIPT_DIALOGUESTATE_H

#include "executioncontext.hpp"
#include <components/esm3/loaddial.hpp>
#include <components/esm3/queststate.hpp>
#include <span>
#include <string>
#include <vector>

namespace Interpreter { class Context; }

namespace MWScript
{
    // Borrowed transaction state. No Environment, singleton journal or UI writes.
    class DialogueState
    {
    public:
        static constexpr size_t MaximumRecords = 16;
        static constexpr size_t MaximumInfos = 64;
        static constexpr size_t MaximumEntries = 64;
        static constexpr size_t MaximumTextBytes = 4096;
        static constexpr size_t MaximumJournalTextBytes = 16384;
        struct Stamp
        {
            int day = 0, month = 0, dayOfMonth = 0;
            bool valid() const;
            bool operator==(const Stamp&) const = default;
        };
        struct Entry
        {
            ESM::RefId topic, info;
            std::string text;
            Stamp stamp;
            bool operator==(const Entry&) const = default;
        };
        struct JournalText
        {
            Stamp stamp;
            virtual ~JournalText() = default;
            virtual std::string expand(std::string_view, Interpreter::Context&) const = 0;
        };
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
        void journal(const ESM::RefId&, int, Interpreter::Context* = nullptr);
        void setJournalText(const JournalText& value) { mJournalText = &value; }
        void addTopic(const ESM::RefId&);
        void validate() const;
        void validateExecution(const ExecutionContext& value) const;
    private:
        ExecutionContext mExecution;
        State& mState;
        std::span<const ESM::Dialogue> mRecords;
        bool mChoicesAllowed;
        const JournalText* mJournalText = nullptr;
        ESM::QuestState& quest(const ESM::RefId&);
    };
}
#endif
