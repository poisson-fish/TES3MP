#ifndef GAME_MWDIALOG_QUEST_H
#define GAME_MWDIALOG_QUEST_H

#include "topic.hpp"

namespace ESM
{
    struct QuestState;
    struct Dialogue;
}

namespace MWDialogue
{
    /// \brief A quest in progress or a completed quest
    class Quest : public Topic
    {
        int mIndex;
        bool mFinished;

    public:
        Quest();

        Quest(const ESM::RefId& topic);

        Quest(const ESM::QuestState& state);
        Quest(const ESM::QuestState& state, const ESM::Dialogue& dialogue);

        std::string_view getName() const override;
        ///< May be an empty string

        int getIndex() const;

        void setIndex(int index);
        void setIndex(int index, bool notify);
        ///< Calling this function with a non-existent index will throw an exception.

        bool isFinished() const;
        void setFinished(bool finished);

        bool addEntry(const JournalEntry& entry) override;
        bool addEntry(const JournalEntry& entry, const ESM::Dialogue& dialogue, bool notify);
        ///< Add entry and adjust index accordingly. Returns true if the quest should be restarted.
        ///
        /// \note Redundant entries are ignored, but the index is still adjusted.

        void write(ESM::QuestState& state) const;
    };
}

#endif
