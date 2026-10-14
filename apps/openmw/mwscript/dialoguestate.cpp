#include "dialoguestate.hpp"
#include "../mwdialogue/quest.hpp"
#include "../mwdialogue/journalentry.hpp"
#include "../mwworld/calendar.hpp"
#include <components/misc/strings/algorithm.hpp>
#include <components/interpreter/context.hpp>
#include <algorithm>
#include <stdexcept>

namespace MWScript
{
    bool DialogueState::Stamp::valid() const
    {
        return day >= 0 && month >= 0 && month < 12 && dayOfMonth >= 1
            && dayOfMonth <= MWWorld::Calendar::getDaysPerMonth(month);
    }
    namespace
    {
        void require(bool condition)
        { if (!condition) throw std::invalid_argument("Invalid or unowned scoped dialogue state"); }
    }
    DialogueState::DialogueState(
        const ExecutionContext& execution, State& state, std::span<const ESM::Dialogue> records, bool choicesAllowed)
        : mExecution(execution)
        , mState(state)
        , mRecords(records)
        , mChoicesAllowed(choicesAllowed)
    {
        execution.validate();
        require(execution.ownership == ExecutionContext::Ownership::Character);
        validate();
    }

    void DialogueState::addChoice(std::string_view text, int id)
    {
        if (!mChoicesAllowed || choices.size() >= 8 || id < 0 || text.empty() || text.size() > 256
            || text.find('\0') != text.npos
            || std::ranges::any_of(choices, [&](const auto& value) { return value.id == id; }))
            throw std::invalid_argument("Unadmitted or bounded dialogue choice");
        choices.push_back({ std::string(text), id });
    }
    void DialogueState::seedNpcDisposition(const ESM::RefId& id, int disposition)
    {
        require(mChoicesAllowed && id.is<ESM::StringRefId>() && !id.empty() && npcDispositions.size() < 64);
        if (std::ranges::find(npcDispositions, id, &NpcDisposition::record) == npcDispositions.end())
            npcDispositions.push_back({ id, disposition, false });
    }
    void DialogueState::modNpcDisposition(const ESM::RefId& id, int defaultBase, int delta)
    {
        require(mChoicesAllowed && id.is<ESM::StringRefId>() && !id.empty());
        auto found = std::ranges::find(npcDispositions, id, &NpcDisposition::record);
        if (found == npcDispositions.end())
        {
            require(npcDispositions.size() < 64);
            npcDispositions.push_back({ id, defaultBase, false });
            found = std::prev(npcDispositions.end());
        }
        const int64_t disposition = int64_t(found->disposition) + delta;
        if (disposition < INT32_MIN || disposition > INT32_MAX)
            throw std::invalid_argument("NPC disposition exceeds stock integer range");
        found->disposition = int(disposition);
        found->modified = true;
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
    void DialogueState::journal(const ESM::RefId& id, int index, Interpreter::Context* context)
    {
        const auto& dialogue = record(id, ESM::Dialogue::Journal);
        const auto found = std::ranges::find_if(dialogue.mInfo,
            [&](const auto& info) { return info.mData.mJournalIndex == index; });
        // Stock Journal permits missing indices and advances the index only.
        if (found == dialogue.mInfo.end() || index == -1)
        { if (journalIndex(id) < index) setJournalIndex(id, index); return; }
        const auto existing = std::ranges::find_if(mState.entries, [&](const auto& entry) {
            return entry.topic == id && entry.info == found->mId; });
        const bool duplicate = existing != mState.entries.end();
        if (duplicate && (mJournalText ? !existing->text.empty() : !found->mResponse.empty()))
        {
            if (journalIndex(id) < index)
            {
                setJournalIndex(id, index);
                if (mJournalText) { require(context); context->messageBox("#{sJournalEntry}", {}); }
            }
            return;
        }
        require(duplicate || mState.entries.size() < MaximumEntries);
        Entry key{id, found->mId};
        if (mJournalText)
        {
            require(context && mJournalText->stamp.valid());
            key.text = mJournalText->expand(found->mResponse, *context);
            key.stamp = mJournalText->stamp;
            size_t bytes = key.text.size();
            for (const auto& entry : mState.entries) bytes += entry.text.size();
            require(key.text.size() <= MaximumTextBytes && key.text.find('\0') == key.text.npos
                && bytes <= MaximumJournalTextBytes);
        }
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
        // Empty entries still participate in stock quest restart/finished semantics.
        const bool notify = mJournalText && !key.text.empty();
        if (!duplicate) mState.entries.push_back(std::move(key));
        else if (!key.text.empty())
        {
            mState.entries.erase(existing);
            mState.entries.push_back(std::move(key));
        }
        if (notify) context->messageBox("#{sJournalEntry}", {});
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
        size_t textBytes = 0;
        for (size_t i = 0; i < mState.entries.size(); ++i)
        {
            const auto& entry = mState.entries[i]; const auto& topic = record(entry.topic, ESM::Dialogue::Journal);
            require(std::ranges::find(mState.quests, entry.topic, &ESM::QuestState::mTopic) != mState.quests.end());
            require(std::ranges::any_of(topic.mInfo, [&](const auto& info) {
                return info.mId == entry.info && info.mData.mJournalIndex != -1; }));
            textBytes += entry.text.size();
            require(entry.text.size() <= MaximumTextBytes && entry.text.find('\0') == entry.text.npos
                && textBytes <= MaximumJournalTextBytes
                && (mChoicesAllowed ? entry.stamp.valid() : (entry.text.empty() && entry.stamp == Stamp{})));
            for (size_t j = 0; j < i; ++j)
                require(entry.topic != mState.entries[j].topic || entry.info != mState.entries[j].info);
        }
        for (size_t i = 0; i < mState.topics.size(); ++i)
        {
            record(mState.topics[i], ESM::Dialogue::Topic);
            for (size_t j = 0; j < i; ++j) require(mState.topics[i] != mState.topics[j]);
        }
    }
}
