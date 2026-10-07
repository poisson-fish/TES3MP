#include "dialoguestate.hpp"
#include "../mwdialogue/quest.hpp"
#include "../mwdialogue/journalentry.hpp"
#include <components/misc/strings/algorithm.hpp>
#include <algorithm>
#include <stdexcept>

namespace MWScript
{
    namespace
    {
        void require(bool condition)
        { if (!condition) throw std::invalid_argument("Invalid or unowned scoped dialogue state"); }
    }
    DialogueState::DialogueState(const ExecutionContext& execution, State& state, std::span<const ESM::Dialogue> records)
        : mExecution(execution), mState(state), mRecords(records)
    {
        execution.validate();
        require(execution.ownership == ExecutionContext::Ownership::Character);
        validate();
    }
    void DialogueState::validateExecution(const ExecutionContext& value) const
    { require(value == mExecution); }
    const ESM::Dialogue& DialogueState::record(const ESM::RefId& id, ESM::Dialogue::Type type) const
    {
        auto found = std::ranges::find(mRecords, id, &ESM::Dialogue::mId);
        require(found != mRecords.end() && found->mType == type);
        return *found;
    }
    int DialogueState::journalIndex(const ESM::RefId& id) const
    {
        record(id, ESM::Dialogue::Journal);
        auto found = std::ranges::find(mState.quests, id, &ESM::QuestState::mTopic);
        return found == mState.quests.end() ? 0 : found->mState;
    }
    ESM::QuestState& DialogueState::quest(const ESM::RefId& id)
    {
        record(id, ESM::Dialogue::Journal);
        auto found = std::ranges::find(mState.quests, id, &ESM::QuestState::mTopic);
        if (found != mState.quests.end()) return *found;
        require(mState.quests.size() < MaximumRecords);
        return mState.quests.emplace_back(ESM::QuestState{id, 0, 0});
    }
    void DialogueState::setJournalIndex(const ESM::RefId& id, int index)
    {
        auto& saved = quest(id);
        MWDialogue::Quest value(saved, record(id, ESM::Dialogue::Journal));
        value.setIndex(index, false);
        value.write(saved);
    }
    void DialogueState::journal(const ESM::RefId& id, int index)
    {
        const auto& dialogue = record(id, ESM::Dialogue::Journal);
        const auto found = std::ranges::find_if(dialogue.mInfo,
            [&](const auto& info) { return info.mData.mJournalIndex == index; });
        // Stock Journal permits missing indices and advances the index only.
        if (found == dialogue.mInfo.end() || index == -1)
        { if (journalIndex(id) < index) setJournalIndex(id, index); return; }
        const Entry key{id, found->mId};
        const bool duplicate = std::ranges::find(mState.entries, key) != mState.entries.end();
        if (duplicate && !found->mResponse.empty())
        { if (journalIndex(id) < index) setJournalIndex(id, index); return; }
        require(duplicate || mState.entries.size() < MaximumEntries);
        auto& saved = quest(id);
        MWDialogue::Quest value(saved, dialogue);
        MWDialogue::JournalEntry entry; entry.mTopic = id; entry.mInfoId = found->mId;
        const bool restart = value.addEntry(entry, dialogue, false);
        value.write(saved);
        if (restart)
        {
            const auto name = [&](const ESM::Dialogue& topic) -> std::string_view {
                for (const auto& info : topic.mInfo)
                    if (info.mQuestStatus == ESM::DialInfo::QS_Name) return info.mResponse;
                return {};
            };
            for (auto& other : mState.quests)
                if (other.mFinished && Misc::StringUtils::ciEqual(name(record(other.mTopic, ESM::Dialogue::Journal)), name(dialogue)))
                    other.mFinished = 0;
        }
        // Text/timestamps/personal journal UI remain closed in this slice.
        if (!duplicate) mState.entries.push_back(key);
    }
    void DialogueState::addTopic(const ESM::RefId& id)
    {
        record(id, ESM::Dialogue::Topic);
        if (std::ranges::find(mState.topics, id) != mState.topics.end()) return;
        require(mState.topics.size() < MaximumRecords);
        mState.topics.push_back(id);
    }
    void DialogueState::validate() const
    {
        require(mRecords.size() <= MaximumRecords && mState.quests.size() <= MaximumRecords
            && mState.entries.size() <= MaximumEntries && mState.topics.size() <= MaximumRecords);
        for (size_t i = 0; i < mState.quests.size(); ++i)
        {
            const auto& value = mState.quests[i]; record(value.mTopic, ESM::Dialogue::Journal);
            require(value.mFinished <= 1);
            for (size_t j = 0; j < i; ++j) require(value.mTopic != mState.quests[j].mTopic);
        }
        for (size_t i = 0; i < mState.entries.size(); ++i)
        {
            const auto& entry = mState.entries[i]; const auto& topic = record(entry.topic, ESM::Dialogue::Journal);
            require(std::ranges::find(mState.quests, entry.topic, &ESM::QuestState::mTopic) != mState.quests.end());
            require(std::ranges::any_of(topic.mInfo, [&](const auto& info) {
                return info.mId == entry.info && info.mData.mJournalIndex != -1; }));
            for (size_t j = 0; j < i; ++j) require(entry != mState.entries[j]);
        }
        for (size_t i = 0; i < mState.topics.size(); ++i)
        {
            record(mState.topics[i], ESM::Dialogue::Topic);
            for (size_t j = 0; j < i; ++j) require(mState.topics[i] != mState.topics[j]);
        }
    }
}
