#include "personal_script.hpp"
#include <algorithm>
#include <apps/openmw/mwdialogue/filter.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/disposition.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/scriptstate.hpp>
#include <apps/openmw/mwscript/referencestate.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <bit>
#include <cmath>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/interpreter/defines.hpp>
#include <limits>
#include <source_location>
#include <sstream>

namespace TES3MP::Native
{
    namespace
    {
        struct EncounterPreparationRequired {};
        constexpr uint64_t Magic = 0x3154535054335354;
        constexpr uint64_t ScheduledMagic = 0x3254535054335354;
        constexpr uint64_t BankMagic = 0x3354535054335354;
        constexpr uint64_t DialogueMagic = 0x3454535054335354;
        constexpr uint64_t EncounterMagic = 0x3854535054335354;
        constexpr uint64_t EncounterReturnMagic = 0x3954535054335354;
        constexpr uint64_t IntentMagic = 0x3754535054335354;
        void require(bool value, std::source_location location = std::source_location::current())
        {
            if (!value)
                throw std::invalid_argument(
                    "Invalid or incompatible personal script state at line " + std::to_string(location.line()));
        }
        void word(std::vector<char>& bytes, uint64_t value)
        { for (size_t i = 0; i < 8; ++i) bytes.push_back(static_cast<char>(value >> (8 * i))); }
        struct Cursor
        {
            std::span<const char> bytes;
            std::span<const char> take(size_t count)
            { require(count <= bytes.size()); auto result = bytes.first(count); bytes = bytes.subspan(count); return result; }
            uint64_t word()
            {
                uint64_t result = 0; auto value = take(8);
                for (size_t i = 0; i < 8; ++i) result |= uint64_t(static_cast<unsigned char>(value[i])) << (8 * i);
                return result;
            }
        };
        void ref(std::vector<char>& bytes, const ESM::RefId& id)
        {
            const std::string_view name = id.empty() ? std::string_view{} : id.getRefIdString();
            require(name.size() <= MWScript::WorldScript::MaximumNameBytes && name.find('\0') == std::string_view::npos);
            word(bytes, name.size()); bytes.insert(bytes.end(), name.begin(), name.end());
        }
        ESM::RefId ref(Cursor& cursor)
        {
            const auto count = cursor.word(); require(count <= MWScript::WorldScript::MaximumNameBytes);
            const auto bytes = cursor.take(size_t(count));
            require(std::ranges::find(bytes, '\0') == bytes.end());
            return ESM::RefId::stringRefId(std::string_view(bytes.data(), bytes.size()));
        }
        void progression(std::vector<char>& bytes, const PersonalScripts::Instance& state, bool journalText)
        {
            ref(bytes, state.lastTopic); ref(bytes, state.lastInfo); word(bytes, std::bit_cast<uint32_t>(state.lastChoice));
            word(bytes, state.dialogue.quests.size());
            for (const auto& quest : state.dialogue.quests)
            { ref(bytes, quest.mTopic); word(bytes, std::bit_cast<uint32_t>(quest.mState)); word(bytes, quest.mFinished); }
            word(bytes, state.dialogue.entries.size());
            for (const auto& entry : state.dialogue.entries)
            {
                ref(bytes, entry.topic); ref(bytes, entry.info);
                if (journalText)
                {
                    word(bytes, entry.text.size()); bytes.insert(bytes.end(), entry.text.begin(), entry.text.end());
                    word(bytes, entry.stamp.day); word(bytes, entry.stamp.month); word(bytes, entry.stamp.dayOfMonth);
                }
            }
            word(bytes, state.dialogue.topics.size());
            for (const auto& topic : state.dialogue.topics) ref(bytes, topic);
        }
        void progression(Cursor& cursor, PersonalScripts::Instance* state, bool journalText)
        {
            const auto topic = ref(cursor), lastInfo = ref(cursor); const auto choice = cursor.word(); require(choice <= UINT32_MAX);
            if (state) { state->lastTopic = topic; state->lastInfo = lastInfo; state->lastChoice = std::bit_cast<int32_t>(uint32_t(choice)); }
            auto count = cursor.word(); require(count <= MWScript::DialogueState::MaximumRecords);
            for (size_t i = 0; i < count; ++i)
            {
                auto id = ref(cursor); const auto index = cursor.word(), finished = cursor.word();
                require(index <= UINT32_MAX && finished <= 1);
                if (state) state->dialogue.quests.push_back({id, std::bit_cast<int32_t>(uint32_t(index)), static_cast<unsigned char>(finished)});
            }
            count = cursor.word(); require(count <= MWScript::DialogueState::MaximumEntries);
            size_t textBytes = 0;
            for (size_t i = 0; i < count; ++i)
            {
                auto id = ref(cursor), info = ref(cursor);
                MWScript::DialogueState::Entry entry{id, info};
                if (journalText)
                {
                    const auto size = cursor.word(); require(size <= MWScript::DialogueState::MaximumTextBytes);
                    textBytes += size; require(textBytes <= MWScript::DialogueState::MaximumJournalTextBytes);
                    const auto text = cursor.take(size); require(std::ranges::find(text, '\0') == text.end());
                    const auto day = cursor.word(), month = cursor.word(), dayOfMonth = cursor.word();
                    require(day <= INT32_MAX && month < 12 && dayOfMonth <= 31);
                    entry.stamp = {int(day), int(month), int(dayOfMonth)}; require(entry.stamp.valid());
                    if (state) entry.text.assign(text.data(), text.size());
                }
                if (state) state->dialogue.entries.push_back(std::move(entry));
            }
            count = cursor.word(); require(count <= MWScript::DialogueState::MaximumRecords);
            for (size_t i = 0; i < count; ++i) { auto id = ref(cursor); if (state) state->dialogue.topics.push_back(id); }
        }
        void conversation(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, state.dialogueRevision);
            word(bytes, state.dialogueCommand);
            const auto& request = state.dialogueRequest;
            for (auto value : { request.story, request.speaker, request.life, request.revision })
                word(bytes, value);
            ref(bytes, ESM::RefId::stringRefId(request.topic));
            word(bytes, std::bit_cast<uint32_t>(request.choice));
            word(bytes, state.choices.size());
            for (const auto& choice : state.choices)
            {
                word(bytes, choice.text.size());
                bytes.insert(bytes.end(), choice.text.begin(), choice.text.end());
                word(bytes, std::bit_cast<uint32_t>(choice.id));
            }
            word(bytes, state.talkedSpeakers.size());
            for (auto speaker : state.talkedSpeakers)
                word(bytes, speaker);
        }
        void conversation(Cursor& cursor, PersonalScripts::Instance* state)
        {
            const auto revision = cursor.word(), command = cursor.word();
            DialogueIntent request{ cursor.word(), cursor.word(), cursor.word(), cursor.word(), ref(cursor).toString(),
                -1 };
            const auto choice = cursor.word();
            require(choice <= UINT32_MAX);
            request.choice = std::bit_cast<int32_t>(uint32_t(choice));
            if (state)
            {
                state->dialogueRevision = revision;
                state->dialogueCommand = command;
                state->dialogueRequest = request;
            }
            auto count = cursor.word();
            require(count <= 8);
            for (size_t i = 0; i < count; ++i)
            {
                const auto size = cursor.word();
                require(size && size <= 256);
                const auto text = cursor.take(size);
                require(std::ranges::find(text, '\0') == text.end());
                const auto id = cursor.word();
                require(id <= INT32_MAX);
                if (state)
                    state->choices.push_back({ std::string(text.data(), text.size()), int(id) });
            }
            count = cursor.word();
            require(count <= 64);
            for (size_t i = 0; i < count; ++i)
            {
                const auto id = cursor.word();
                require(id);
                if (state)
                    state->talkedSpeakers.push_back(id);
            }
        }
        void npcs(std::vector<char>& bytes, const PersonalScripts::Instance& state)
        {
            word(bytes, state.npcs.size());
            for (const auto& npc : state.npcs)
            {
                word(bytes, npc.reference); word(bytes, npc.life); ref(bytes, npc.record);
                word(bytes, std::bit_cast<uint32_t>(npc.disposition));
            }
        }
        void npcs(Cursor& cursor, PersonalScripts::Instance* state)
        {
            const auto count = cursor.word(); require(count <= PersonalScripts::MaximumNpcsPerStory);
            for (size_t i = 0; i < count; ++i)
            {
                PersonalScripts::Instance::Npc npc{cursor.word(), cursor.word(), ref(cursor)};
                const auto disposition = cursor.word(); require(disposition <= UINT32_MAX);
                npc.disposition = std::bit_cast<int32_t>(uint32_t(disposition));
                require(npc.reference && npc.life && !npc.record.empty());
                if (state) state->npcs.push_back(std::move(npc));
            }
        }
        struct Bank final : MWScript::ScriptState
        {
            const MWScript::ExecutionContext& expected;
            std::vector<const SharedWorldScript*> scripts;
            std::vector<MWScript::GlobalScriptDesc> instances;
            Bank(const MWScript::ExecutionContext& execution, const PersonalScripts::Definition& definition,
                const PersonalScripts::Instance& state) : expected(execution), scripts{definition.script.get()}
            {
                for (const auto& member : definition.members) scripts.push_back(member.script.get());
                instances.reserve(scripts.size());
                for (size_t i = 0; i < scripts.size(); ++i)
                {
                    const auto& member = i ? state.members[i - 1] : state.runtime;
                    auto& descriptor = instances.emplace_back();
                    descriptor.mLocals = MWScript::Locals::restore(member.runtime.script.mLocals,
                        member.runtime.script.mId, scripts[i]->declarations());
                    descriptor.mRunning = member.runtime.script.mRunning;
                }
            }
            Binding resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id) override
            {
                require(execution == expected);
                for (size_t i = 0; i < instances.size(); ++i)
                    if (instances[i].mLocals.getScriptId() == id) return {instances[i], scripts[i]->declarations()};
                throw std::invalid_argument("Script lacks character/story bank ownership");
            }
            void write(PersonalScripts::Instance& state) const
            {
                for (size_t i = 0; i < scripts.size(); ++i)
                {
                    auto& member = i ? state.members[i - 1] : state.runtime;
                    member.runtime.script.mRunning = instances[i].mRunning;
                    member.runtime.script.mLocals.mVariables.clear();
                    require(instances[i].mLocals.write(member.runtime.script.mLocals, scripts[i]->declarations()));
                }
            }
        };
        struct FilterContext final : MWScript::InterpreterContext
        {
            const std::vector<ESM::Global>& globals;
            const std::string_view cellName;
            const std::optional<std::string_view> customClassName;
            const Interpreter::Context* journalGlobals = nullptr;
            FilterContext(const MWScript::ExecutionContext& execution, const MWWorld::ESMStore& content,
                const MWScript::ReferenceResolver& resolver, Bank& bank, MWScript::DialogueState& dialogue,
                const std::vector<ESM::Global>& values, MWMechanics::NpcStats* playerStats, std::string_view cell,
                std::optional<std::string_view> customClass)
                : InterpreterContext(nullptr, execution, content, resolver, playerStats, &bank, &dialogue)
                , globals(values), cellName(cell), customClassName(customClass) {}
            std::string_view getPCClass() const override
            {
                if (!customClassName) return InterpreterContext::getPCClass();
                require(getPlayer().get<ESM::NPC>()->mBase->mClass.empty());
                return *customClassName;
            }
            std::string_view getCurrentCellName() const override { return cellName; }
            std::vector<std::string> getGlobals() const override
            {
                if (journalGlobals) return journalGlobals->getGlobals();
                std::vector<std::string> names;
                for (const auto& value : globals) names.emplace_back(value.mId.getRefIdString());
                return names;
            }
            const ESM::Variant& global(std::string_view id) const
            {
                auto found = std::ranges::find(globals, ESM::RefId::stringRefId(id), &ESM::Global::mId);
                require(found != globals.end()); return found->mValue;
            }
            char getGlobalType(std::string_view id) const override
            {
                if (journalGlobals) return journalGlobals->getGlobalType(id);
                auto found = std::ranges::find(globals, ESM::RefId::stringRefId(id), &ESM::Global::mId);
                if (found == globals.end()) return ' ';
                return found->mValue.getType() == ESM::VT_Float ? 'f' : found->mValue.getType() == ESM::VT_Short ? 's' : 'l';
            }
            int getGlobalShort(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalShort(id) : global(id).getInteger(); }
            int getGlobalLong(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalLong(id) : global(id).getInteger(); }
            float getGlobalFloat(std::string_view id) const override
            { return journalGlobals ? journalGlobals->getGlobalFloat(id) : global(id).getFloat(); }
        };
        struct JournalText final : MWScript::DialogueState::JournalText
        {
            const FilterContext& snapshot;
            explicit JournalText(const FilterContext& value, MWScript::DialogueState::Stamp date) : snapshot(value)
            { stamp = date; }
            std::string expand(std::string_view source, Interpreter::Context& globals) const override
            {
                auto context = snapshot;
                context.journalGlobals = &globals;
                return Interpreter::fixDefinesDialog(source, context, MWScript::DialogueState::MaximumTextBytes);
            }
        };
        void context(std::vector<char>& bytes, const MWScript::ExecutionContext& value)
        {
            for (const auto field : {value.campaign, value.story, value.physicalScope, value.character,
                value.sourceReference, value.sourceLife, value.invocation, value.causalEvent}) word(bytes, field);
        }
        MWScript::ExecutionContext context(Cursor& cursor)
        {
            using Context = MWScript::ExecutionContext;
            return {Context::Ownership::Character, cursor.word(), cursor.word(), cursor.word(), cursor.word(),
                cursor.word(), cursor.word(), cursor.word(), cursor.word()};
        }
    }

    PersonalScripts::PersonalScripts(uint64_t campaign, std::vector<Definition> definitions, CredentialCrypto& crypto,
        bool scheduled, bool banks, bool intents, bool returns)
        : mCampaign(campaign)
        , mDefinitions(std::move(definitions))
        , mScheduled(scheduled)
        , mBanks(banks)
        , mIntents(intents)
        , mReturns(returns)
    {
        require(campaign && !mDefinitions.empty() && mDefinitions.size() <= MaximumInstances);
        size_t scripts = 0;
        require(!mBanks || mScheduled);
        mDialogues = std::ranges::any_of(mDefinitions, [](const auto& entry) { return !entry.dialogues.empty(); });
        require(!mDialogues || mBanks);
        require(!mIntents || mDialogues);
        mEncounters = std::ranges::any_of(mDefinitions, [](const auto& entry) { return bool(entry.encounter); });
        require(!mEncounters || mIntents);
        require(!mReturns || mEncounters);
        mResults.resize(mDefinitions.size());
        for (size_t i = 0; i < mDefinitions.size(); ++i)
        {
            const auto& entry = mDefinitions[i];
            require(entry.character && entry.story && entry.script);
            require(!mEncounters || (entry.encounter == mDefinitions.front().encounter && entry.encounter && entry.encounter->reference
                && entry.encounter->record.is<ESM::StringRefId>() && !entry.encounter->record.empty()
                && entry.encounter->cell.is<ESM::StringRefId>() && !entry.encounter->cell.empty()
                && entry.encounter->record.getRefIdString().size() <= 128
                && entry.encounter->cell.getRefIdString().size() <= 128));
            require(entry.members.size() < MaximumScriptsPerBank
                && (mBanks || (entry.members.empty() && entry.running))
                && entry.script->bankCapable() == mBanks);
            scripts += 1 + entry.members.size(); require(scripts <= MaximumInstances);
            std::vector<ESM::RefId> names{entry.script->initial().runtime.script.mId};
            for (const auto& member : entry.members)
            {
                require(member.script && member.script->bankCapable());
                const auto id = member.script->initial().runtime.script.mId;
                require(std::ranges::find(names, id) == names.end()); names.push_back(id);
            }
            for (size_t j = 0; j < i; ++j)
                require(entry.character != mDefinitions[j].character || entry.story != mDefinitions[j].story);
            require(!mDialogues || (!entry.dialogues.empty() && entry.dialogues.size() <= MWScript::DialogueState::MaximumRecords));
            size_t infos = 0;
            for (size_t d = 0; d < entry.dialogues.size(); ++d)
            {
                const auto& dialogue = entry.dialogues[d];
                require(dialogue.mId.is<ESM::StringRefId>() && !dialogue.mId.empty()
                    && dialogue.mId.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                    && dialogue.mId.getRefIdString().find('\0') == std::string_view::npos
                    && (dialogue.mType == ESM::Dialogue::Topic || dialogue.mType == ESM::Dialogue::Journal));
                for (size_t j = 0; j < d; ++j) require(entry.dialogues[j].mId != dialogue.mId);
                infos += dialogue.mInfo.size(); require(infos <= MWScript::DialogueState::MaximumInfos);
                std::vector<ESM::RefId> identities;
                for (const auto& info : dialogue.mInfo)
                {
                    require(!info.mId.empty() && info.mId.is<ESM::StringRefId>()
                        && info.mId.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                        && info.mId.getRefIdString().find('\0') == std::string_view::npos
                        && info.mQuestStatus >= ESM::DialInfo::QS_None && info.mQuestStatus <= ESM::DialInfo::QS_Restart
                        && (!mIntents || dialogue.mType != ESM::Dialogue::Topic
                            || (info.mData.mDisposition >= 0 && info.mData.mDisposition <= 100))
                        && info.mResponse.size() <= 4096
                        && info.mResponse.find('\0') == std::string::npos
                        && info.mResultScript.size() <= MWScript::WorldScript::MaximumSourceBytes
                        && info.mSelects.size() <= 16 && std::ranges::find(identities, info.mId) == identities.end());
                    for (const auto& name : {info.mActor, info.mRace, info.mClass, info.mFaction, info.mPcFaction, info.mCell})
                        require(name.empty() || (name.is<ESM::StringRefId>()
                            && name.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                            && name.getRefIdString().find('\0') == std::string_view::npos));
                    for (const auto& select : info.mSelects)
                    {
                        require(select.mVariable.size() <= MWScript::WorldScript::MaximumNameBytes
                            && select.mVariable.find('\0') == std::string::npos);
                        if (const auto* value = std::get_if<float>(&select.mValue)) require(std::isfinite(*value));
                    }
                    identities.push_back(info.mId);
                    if (dialogue.mType == ESM::Dialogue::Topic)
                        mResults[i].push_back({dialogue.mId, info.mId, entry.script->compileResult(info.mResultScript, mEncounters,
                            mEncounters ? entry.encounter->record : ESM::RefId{})});
                }
            }
        }
        // Reserve two authenticated dialogue calls, including encounter discovery/retry.
        require(!mIntents || scripts <= MaximumInstances - (mEncounters ? 4 : 2));
        // Includes every script's content/source/defaults/limits digest and the
        // complete ordered ownership policy. No session identities or pointers.
        auto binding = encode(initial());
        if (mEncounters) for (const auto& entry : mDefinitions)
        {
            word(binding, entry.encounter->reference); ref(binding, entry.encounter->record); ref(binding, entry.encounter->cell);
        }
        if (mEncounters)
        {
            constexpr std::string_view limits = "terminal-source-disable-1;one-npc;one-cell;static-geometry;12-banks;4-calls;no-summons";
            binding.insert(binding.end(), limits.begin(), limits.end());
        }
        if (mDialogues)
        {
            constexpr std::string_view limits = "scoped-dialogue-results-1;16;64;16;8192;4096;64;128;1048576";
            binding.insert(binding.end(), limits.begin(), limits.end());
            if (mIntents)
            {
                constexpr std::string_view intentLimits
                    = "authenticated-dialogue-5;14;2;8;256;4096;32768;64;source-npc-disposition-64;moddisposition;journal-text-16384;game-calendar;root-first;bounded-substitutions;stock-disposition;unscripted-"
                      "speaker;primary-bank";
                binding.insert(binding.end(), intentLimits.begin(), intentLimits.end());
            }
            std::ostringstream output(std::ios::binary); ESM::ESMWriter writer;
            writer.setVersion(ESM::VER_130); writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion); writer.save(output);
            for (const auto& entry : mDefinitions)
                for (const auto& dialogue : entry.dialogues)
                {
                    writer.startRecord("DIAL"); dialogue.save(writer); writer.endRecord("DIAL");
                    for (const auto& info : dialogue.mInfo)
                    { writer.startRecord("INFO"); info.save(writer); writer.endRecord("INFO"); }
                }
            writer.close(); const auto policy = output.str(); binding.insert(binding.end(), policy.begin(), policy.end());
        }
        require(crypto.sha256(std::as_bytes(std::span(binding)), mIdentity));
    }

    PersonalScripts::State PersonalScripts::initial() const
    {
        State state;
        for (const auto& entry : mDefinitions)
        {
            state.push_back({{MWScript::ExecutionContext::Ownership::Character, mCampaign, entry.story, 1,
                entry.character, 0, 0, 0, 0}, 0, entry.script->initial()});
            auto& instance = state.back(); instance.runtime.runtime.script.mRunning = entry.running;
            if (mIntents)
                for (const auto& dialogue : entry.dialogues)
                    if (dialogue.mType == ESM::Dialogue::Topic)
                    {
                        instance.dialogue.topics.push_back(dialogue.mId);
                        break;
                    }
            for (const auto& member : entry.members)
            {
                auto value = member.script->initial(); value.runtime.script.mRunning = member.running;
                value.runtime.globals.clear(); instance.members.push_back(std::move(value));
            }
        }
        return state;
    }

    ReliablePersonalDialogue PersonalScripts::view(const Instance& state, SessionId target,
        SessionGeneration generation, ServerTick tick, CanonicalRevision revision)
    {
        ReliablePersonalDialogue value{target, generation, tick, revision};
        value.story = state.last.story; value.revision = state.dialogueRevision;
        value.command = state.dialogueCommand; value.speaker = state.dialogueRequest.speaker;
        value.life = state.dialogueRequest.life; value.topic = state.dialogueRequest.topic;
        for (const auto& c : state.choices) value.choices.push_back({c.text, c.id});
        for (const auto& q : state.dialogue.quests)
            value.quests.push_back({q.mTopic.toString(), q.mState, bool(q.mFinished)});
        for (const auto& e : state.dialogue.entries)
            value.entries.push_back({e.topic.toString(), e.info.toString(), e.text,
                e.stamp.day, e.stamp.month, e.stamp.dayOfMonth});
        for (const auto& t : state.dialogue.topics) value.topics.push_back(t.toString());
        return value;
    }

    void PersonalScripts::validate(const Instance& state, size_t index) const
    {
        const auto& expected = mDefinitions.at(index);
        const auto& last = state.last;
        require(!state.encounterLife || (mEncounters && last.sourceReference == expected.encounter->reference
            && last.sourceLife == state.encounterLife && state.choices.empty()));
        require(!state.encounterReturned || (mReturns && state.encounterLife));
        if (state.encounterLife)
            require(std::ranges::any_of(state.npcs, [&](const auto& npc) {
                return npc.reference == expected.encounter->reference && npc.life == state.encounterLife
                    && npc.record == expected.encounter->record;
            }));
        require(last.ownership == MWScript::ExecutionContext::Ownership::Character && last.campaign == mCampaign
            && last.character == expected.character && last.story == expected.story && last.physicalScope == (state.encounterLife ? index + 2 : 1)
            && (mDialogues || (!last.sourceReference && !last.sourceLife))
            && (mBanks ? last.invocation >= state.runtime.tick : last.invocation == state.runtime.tick)
            && last.causalEvent <= state.eventCursor
            && (!last.causalEvent || last.causalEvent == state.eventCursor)
            && (last.invocation || (!last.causalEvent && !state.eventCursor && !state.scheduledTick))
            && (mScheduled || !state.scheduledTick));
        if (mIntents)
        {
            require(bool(state.dialogueRevision) == bool(state.dialogueCommand) && state.choices.size() <= 8
                && state.talkedSpeakers.size() <= 64);
            if (state.dialogueRevision)
            {
                const auto& request = state.dialogueRequest;
                require(validDialogueIntent(request) && request.story == expected.story
                    && request.revision + 1 == state.dialogueRevision);
                auto execution = last;
                if (!execution.invocation)
                    execution.invocation = 1;
                auto copy = state.dialogue;
                MWScript::DialogueState dialogue(execution, copy, expected.dialogues, mIntents);
                (void)dialogue.record(ESM::RefId::stringRefId(request.topic), ESM::Dialogue::Topic);
                require(std::ranges::find(state.talkedSpeakers, request.speaker) != state.talkedSpeakers.end());
            }
            else
                require(
                    state.dialogueRequest == DialogueIntent{} && state.choices.empty() && state.talkedSpeakers.empty());
            for (size_t i = 0; i < state.choices.size(); ++i)
            {
                const auto& choice = state.choices[i];
                require(choice.id >= 0 && !choice.text.empty() && choice.text.size() <= 256
                    && choice.text.find('\0') == choice.text.npos
                    && std::ranges::none_of(
                        std::span(state.choices).first(i), [&](const auto& c) { return c.id == choice.id; }));
            }
            for (size_t i = 0; i < state.talkedSpeakers.size(); ++i)
                require(state.talkedSpeakers[i]
                    && std::find(
                           state.talkedSpeakers.begin(), state.talkedSpeakers.begin() + i, state.talkedSpeakers[i])
                        == state.talkedSpeakers.begin() + i);
            require(state.npcs.size() <= MaximumNpcsPerStory);
            for (size_t i = 0; i < state.npcs.size(); ++i)
            {
                const auto& npc = state.npcs[i];
                require(npc.reference && npc.life && npc.record.is<ESM::StringRefId>() && !npc.record.empty()
                    && npc.record.getRefIdString().size() <= MWScript::WorldScript::MaximumNameBytes
                    && npc.record.getRefIdString().find('\0') == std::string_view::npos
                    && std::ranges::find(state.talkedSpeakers, npc.reference) != state.talkedSpeakers.end()
                    && std::ranges::none_of(std::span(state.npcs).first(i),
                        [&](const auto& previous) { return previous.reference == npc.reference; }));
            }
        }
        else
            require(!state.dialogueRevision && !state.dialogueCommand && state.dialogueRequest == DialogueIntent{}
                && state.choices.empty() && state.talkedSpeakers.empty() && state.npcs.empty());
        require(state.members.size() == expected.members.size());
        for (const auto& member : state.members)
            require(member.tick <= last.invocation && member.runtime.globals.empty());
        if (last.invocation) last.validate();
        if (mDialogues)
        {
            auto execution = last; if (!execution.invocation) execution.invocation = 1;
            auto progress = state.dialogue;
            MWScript::DialogueState dialogue(execution, progress, expected.dialogues, mIntents);
            require(state.lastTopic.empty() == !last.sourceReference && state.lastTopic.empty() == state.lastInfo.empty());
            if (!state.lastTopic.empty())
            {
                const auto& topic = dialogue.record(state.lastTopic, ESM::Dialogue::Topic);
                require(last.causalEvent && state.lastChoice >= -1
                    && std::ranges::any_of(topic.mInfo, [&](const auto& info) { return info.mId == state.lastInfo; }));
            }
            else require(state.lastChoice == -1);
        }
        else require(state.dialogue.quests.empty() && state.dialogue.entries.empty() && state.dialogue.topics.empty()
            && state.lastTopic.empty() && state.lastInfo.empty() && state.lastChoice == -1);
        if (mIntents)
            require(!encodeReliablePersonalDialogue(view(state, *SessionId::fromValue(1), SessionGeneration::initial(),
                ServerTick::initial(), CanonicalRevision::initial())).empty());
    }

    std::vector<char> PersonalScripts::encode(const State& state) const
    {
        require(state.size() == mDefinitions.size());
        for (size_t i = 0; i < state.size(); ++i) if (state[i].encounterLife)
            require(std::ranges::none_of(std::span(state).first(i), [&](const auto& previous) {
                return previous.encounterLife && previous.last.character == state[i].last.character;
            }));
        std::vector<char> bytes;
        word(bytes,
            mReturns         ? EncounterReturnMagic
                : mEncounters ? EncounterMagic
                : mIntents   ? IntentMagic
                : mDialogues ? DialogueMagic
                : mBanks     ? BankMagic
                : mScheduled ? ScheduledMagic
                             : Magic);
        for (auto value : mIdentity.bytes) bytes.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
        word(bytes, state.size());
        for (size_t i = 0; i < state.size(); ++i)
        {
            validate(state[i], i);
            const auto image = mDefinitions[i].script->encode(state[i].runtime);
            context(bytes, state[i].last); word(bytes, state[i].eventCursor); word(bytes, image.size());
            if (mScheduled) word(bytes, state[i].scheduledTick);
            bytes.insert(bytes.end(), image.begin(), image.end());
            if (mBanks)
            {
                word(bytes, state[i].members.size());
                for (size_t j = 0; j < state[i].members.size(); ++j)
                {
                    auto member = state[i].members[j]; member.runtime.globals = state[i].runtime.runtime.globals;
                    const auto encoded = mDefinitions[i].members[j].script->encode(member);
                    word(bytes, encoded.size()); bytes.insert(bytes.end(), encoded.begin(), encoded.end());
                }
            }
            if (mDialogues) progression(bytes, state[i], mIntents);
            if (mIntents)
            {
                conversation(bytes, state[i]);
                npcs(bytes, state[i]);
                if (mEncounters) word(bytes, state[i].encounterLife);
                if (mReturns) word(bytes, state[i].encounterReturned);
            }
        }
        require(bytes.size() <= MaximumImageBytes);
        return bytes;
    }

    PersonalScripts::State PersonalScripts::restore(std::span<const char> bytes) const
    {
        require(bytes.size() <= MaximumImageBytes);
        Cursor input{bytes};
        require(input.word()
            == (mReturns         ? EncounterReturnMagic
                : mEncounters    ? EncounterMagic
                : mIntents   ? IntentMagic
                    : mDialogues ? DialogueMagic
                    : mBanks     ? BankMagic
                    : mScheduled ? ScheduledMagic
                                 : Magic));
        const auto identity = input.take(32);
        require(std::equal(identity.begin(), identity.end(), reinterpret_cast<const char*>(mIdentity.bytes.data())));
        const auto count = input.word(); require(count == mDefinitions.size());
        // Complete the outer structural pass before allocating any state.
        Cursor preflight = input;
        for (size_t i = 0; i < count; ++i)
        {
            preflight.take(72);
            const auto size = preflight.word(); require(size && size <= SharedWorldScript::MaximumImageBytes);
            if (mScheduled) preflight.take(8);
            preflight.take(size);
            if (mBanks)
            {
                require(preflight.word() == mDefinitions[i].members.size());
                for (size_t j = 0; j < mDefinitions[i].members.size(); ++j)
                {
                    const auto memberSize = preflight.word();
                    require(memberSize && memberSize <= SharedWorldScript::MaximumImageBytes); preflight.take(memberSize);
                }
            }
            if (mDialogues) progression(preflight, nullptr, mIntents);
            if (mIntents)
            {
                conversation(preflight, nullptr);
                npcs(preflight, nullptr);
                if (mEncounters) preflight.take(8);
                if (mReturns) require(preflight.word() <= 1);
            }
        }
        require(preflight.bytes.empty());
        State state;
        state.reserve(size_t(count));
        for (size_t i = 0; i < count; ++i)
        {
            auto last = context(input); const auto event = input.word(), size = input.word();
            const auto scheduledTick = mScheduled ? input.word() : 0;
            Instance entry{last, event, mDefinitions[i].script->restore(input.take(size)), scheduledTick};
            if (mBanks)
            {
                require(input.word() == mDefinitions[i].members.size());
                for (const auto& definition : mDefinitions[i].members)
                {
                    const auto memberSize = input.word();
                    auto member = definition.script->restore(input.take(memberSize));
                    member.runtime.globals.clear(); entry.members.push_back(std::move(member));
                }
            }
            if (mDialogues) progression(input, &entry, mIntents);
            if (mIntents)
            {
                conversation(input, &entry);
                npcs(input, &entry);
                if (mEncounters) entry.encounterLife = input.word();
                if (mReturns) { const auto returned = input.word(); require(returned <= 1); entry.encounterReturned = returned; }
            }
            validate(entry, i); state.push_back(std::move(entry));
        }
        require(encode(state) == std::vector<char>(bytes.begin(), bytes.end()));
        return state;
    }

    PersonalScripts::Prepared PersonalScripts::prepare(const State& before, const MWScript::ExecutionContext& execution,
        const MWWorld::ESMStore& content, const MWScript::ReferenceResolver& resolver, size_t budget) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = execution.invocation;
        try
        {
            execution.validate();
            require(before.size() == mDefinitions.size());
            const auto found = std::ranges::find_if(mDefinitions, [&](const auto& entry) {
                return entry.character == execution.character && entry.story == execution.story;
            });
            require(found != mDefinitions.end());
            const auto index = size_t(found - mDefinitions.begin());
            const auto& current = before[index]; validate(current, index);
            require(execution.ownership == current.last.ownership && execution.campaign == mCampaign
                && execution.physicalScope == 1 && !execution.sourceReference && !execution.sourceLife);
            result.script = current.runtime.runtime.script.mId;
            if (execution == current.last)
            { result.outcome = SharedWorldScript::Outcome::Committed; return prepared; }
            require(budget && budget <= MaximumInvocationsPerTransaction);
            require(current.last.invocation != std::numeric_limits<uint64_t>::max()
                && execution.invocation == current.last.invocation + 1
                && (!execution.causalEvent || execution.causalEvent > current.eventCursor));
            if (mBanks) return prepareBank(before, index, execution, content, resolver, budget);
            auto invocation = found->script->prepare(current.runtime, execution, content, resolver);
            result = std::move(invocation.result);
            if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed) return prepared;
            prepared.state = before;
            prepared.state[index] = {execution, std::max(current.eventCursor, execution.causalEvent),
                std::move(invocation.state), current.scheduledTick};
            prepared.image = encode(prepared.state);
            prepared.invocations = 1;
        }
        catch (const std::exception& error)
        {
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareBank(const State& before, size_t index,
        const MWScript::ExecutionContext& execution, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = execution.invocation;
        try
        {
            (void)encode(before);
            prepared.state = before;
            auto& state = prepared.state[index];
            const auto& definition = mDefinitions[index];
            Bank bank(execution, definition, state);
            const auto& scripts = bank.scripts;
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                auto& descriptor = bank.instances[i];
                if (!descriptor.mRunning) continue;
                require(prepared.invocations < budget);
                auto& member = i ? state.members[i - 1] : state.runtime;
                auto input = member;
                input.runtime.globals = state.runtime.runtime.globals;
                input.runtime.script.mRunning = descriptor.mRunning;
                input.runtime.script.mLocals.mVariables.clear();
                require(descriptor.mLocals.write(input.runtime.script.mLocals, scripts[i]->declarations()));
                auto invocation = scripts[i]->prepareBank(input, execution, content, resolver, bank, descriptor);
                result = std::move(invocation.result); result.presentation.clear();
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                { prepared.state.clear(); prepared.invocations = 0; result.executed = false; return prepared; }
                state.runtime.runtime.globals = std::move(invocation.state.runtime.globals);
                member.tick = invocation.state.tick;
                ++prepared.invocations;
            }
            bank.write(state);
            state.lastTopic = {}; state.lastInfo = {}; state.lastChoice = -1;
            state.last = execution; state.eventCursor = std::max(state.eventCursor, execution.causalEvent);
            prepared.image = encode(prepared.state);
            prepared.invocations = std::max(size_t(1), prepared.invocations);
            result.outcome = SharedWorldScript::Outcome::Committed;
            result.executed = true; // The bank occurrence/marker commits even when all members are stopped.
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    uint64_t PersonalScripts::physicalScope(const State& state, uint64_t character)
    {
        for (const auto& instance : state)
            if (instance.last.character == character && instance.encounterLife && !instance.encounterReturned)
                return instance.last.physicalScope;
        return 1;
    }

    PersonalScripts::Prepared PersonalScripts::prepareReturn(const State& before, uint64_t character) const
    {
        Prepared prepared;
        require(mReturns && character);
        (void)encode(before);
        const auto found = std::ranges::find_if(before, [&](const auto& instance) {
            return instance.last.character == character && instance.encounterLife;
        });
        require(found != before.end());
        prepared.result.outcome = SharedWorldScript::Outcome::Committed;
        if (found->encounterReturned) return prepared;
        prepared.state = before;
        prepared.state[size_t(found - before.begin())].encounterReturned = true;
        prepared.image = encode(prepared.state);
        prepared.result.executed = true;
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareDialogue(const State& before,
        const MWScript::ExecutionContext& execution, const ESM::RefId& topic, int choice, int disposition,
        bool talkedToPlayer, std::string_view cellName, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget, const MWMechanics::NpcStats* playerStats,
        std::optional<std::string_view> customClassName, const MWMechanics::NpcStats* speakerStats,
        std::optional<MWScript::DialogueState::Stamp> journalStamp, bool encounterPrepared) const
    {
        Prepared prepared; auto& result = prepared.result; result.invocationTick = execution.invocation;
        try
        {
            execution.validate(); (void)encode(before);
            require(mDialogues && execution.campaign == mCampaign && execution.physicalScope == 1
                && physicalScope(before, execution.character) == 1
                && execution.ownership == MWScript::ExecutionContext::Ownership::Character
                && execution.sourceReference && execution.sourceLife && execution.causalEvent
                && choice >= -1 && disposition >= 0 && disposition <= 100 && cellName.size() <= 256);
            const auto found = std::ranges::find_if(mDefinitions, [&](const auto& entry) {
                return entry.character == execution.character && entry.story == execution.story; });
            require(found != mDefinitions.end());
            const auto index = size_t(found - mDefinitions.begin()); const auto& current = before[index];
            require(!current.encounterLife); // A dormant removal never reopens this story's source.
            result.script = current.runtime.runtime.script.mId;
            if (execution == current.last)
            {
                require(topic == current.lastTopic && choice == current.lastChoice);
                result.outcome = SharedWorldScript::Outcome::Committed; return prepared;
            }
            require(budget && budget <= MaximumInvocationsPerTransaction
                && current.last.invocation != UINT64_MAX && execution.invocation == current.last.invocation + 1
                && execution.causalEvent > current.eventCursor && current.runtime.tick != UINT64_MAX);
            prepared.state = before; auto& state = prepared.state[index];
            auto runningExecution = execution;
            if (encounterPrepared) runningExecution.physicalScope = index + 2;
            const auto origin = mEncounters ? resolver.resolve(execution, {}, false) : MWWorld::Ptr{};
            if (mEncounters) require(origin.hasLiveReference() && origin.getType() == ESM::NPC::sRecordId);
            struct Scene final : MWScript::ReferenceState
            {
                MWScript::ExecutionContext expected;
                MWWorld::Ptr source;
                bool prepared = false, removed = false;
                void validate(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const
                {
                    require(execution == expected && ptr.hasLiveReference() && ptr == source
                        && ptr.getType() == ESM::NPC::sRecordId);
                }
                void disable(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) override
                {
                    validate(execution, ptr);
                    if (!prepared) throw EncounterPreparationRequired{};
                    removed = true;
                    source.getRefData().disable();
                }
                bool disabled(const MWScript::ExecutionContext& execution, const MWWorld::Ptr& ptr) const override
                { validate(execution, ptr); return removed; }
            } scene;
            std::optional<MWWorld::ManualRef> privateSource;
            struct Resolver final : MWScript::ReferenceResolver
            {
                const MWScript::ReferenceResolver& upstream;
                Scene& scene;
                MWWorld::Ptr origin;
                MWScript::ExecutionContext originExecution;
                Resolver(const MWScript::ReferenceResolver& resolver, Scene& state, MWWorld::Ptr ptr,
                    MWScript::ExecutionContext execution)
                    : upstream(resolver), scene(state), origin(ptr), originExecution(execution) {}
                MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& id, bool activeOnly) const override
                {
                    require(execution == scene.expected);
                    auto ptr = upstream.resolve(originExecution, id, activeOnly);
                    if (ptr == origin)
                        return activeOnly && scene.removed ? MWWorld::Ptr{} : scene.source;
                    return ptr;
                }
            } scopedResolver(resolver, scene, origin, execution);
            if (mEncounters)
            {
                require(execution.sourceReference == found->encounter->reference
                    && origin.getCellRef().getRefId() == found->encounter->record && origin.getRefData().isEnabled());
                privateSource.emplace(content, found->encounter->record);
                privateSource->getPtr().getCellRef() = origin.getCellRef();
                scene.expected = runningExecution; scene.source = privateSource->getPtr(); scene.prepared = encounterPrepared;
            }

            const MWScript::ReferenceResolver& runningResolver = mEncounters ? scopedResolver : resolver;
            MWScript::DialogueState dialogue(runningExecution, state.dialogue, found->dialogues, mIntents);
            const auto& record = dialogue.record(topic, ESM::Dialogue::Topic);
            Bank bank(runningExecution, *found, state);
            // Player stats remain read-only. The source NPC snapshot has one
            // admitted result writer and restores this story's durable base.
            std::optional<MWMechanics::NpcStats> stats;
            if (playerStats) stats.emplace(*playerStats);
            FilterContext script(runningExecution, content, runningResolver, bank, dialogue, state.runtime.runtime.globals,
                stats ? &*stats : nullptr, cellName, customClassName);
            JournalText journalText(script, journalStamp.value_or(MWScript::DialogueState::Stamp{}));
            if (mIntents) dialogue.setJournalText(journalText);
            const auto actor = script.resolveReference({});
            require(actor.hasLiveReference()
                && (mIntents ? actor.getClass().getScript(actor).empty()
                             : actor.getClass().getScript(actor) == result.script));
            std::optional<MWMechanics::NpcStats> npcStats;
            const auto ownedNpc = std::ranges::find(state.npcs, execution.sourceReference, &Instance::Npc::reference);
            if (mIntents && speakerStats)
            {
                require(actor.getType() == ESM::NPC::sRecordId);
                npcStats.emplace(*speakerStats);
                if (ownedNpc != state.npcs.end())
                {
                    require(ownedNpc->record == actor.getCellRef().getRefId() && ownedNpc->life <= execution.sourceLife);
                    npcStats->setBaseDisposition(ownedNpc->disposition);
                }
            }
            else require(ownedNpc == state.npcs.end());
            const int priorDisposition = npcStats ? npcStats->getBaseDisposition() : 0;
            if (mIntents)
            {
                disposition = 0;
                if (std::ranges::any_of(record.mInfo, [](const auto& info) { return info.mData.mDisposition != 0; }))
                {
                    require(stats && npcStats && actor.getType() == ESM::NPC::sRecordId);
                    const auto player = script.resolveReference(ESM::RefId::stringRefId("player"));
                    require(player.hasLiveReference() && player.getType() == ESM::NPC::sRecordId);
                    disposition = MWMechanics::derivedDisposition(*actor.get<ESM::NPC>()->mBase, *npcStats,
                        *player.get<ESM::NPC>()->mBase, *stats, content.get<ESM::GameSetting>(),
                        [&](const ESM::RefId& from, const ESM::RefId& to) {
                            // Faction-reaction result writes remain closed in this policy.
                            const auto& reactions = content.get<ESM::Faction>().find(from)->mReactions;
                            const auto found = reactions.find(to);
                            return found == reactions.end() ? 0 : found->second;
                        });
                }
            }
            MWDialogue::Filter::Scoped scoped{ script, dialogue, disposition, cellName,
                mIntents ? result.script : ESM::RefId{} };
            MWDialogue::Filter filter(actor, choice, talkedToPlayer, scoped);
            const auto response = filter.search(record, false);
            require(response.second != nullptr);
            const auto program = std::ranges::find_if(mResults[index], [&](const auto& value) {
                return value.topic == topic && value.info == response.second->mId; });
            require(program != mResults[index].end());
            // Stock dialogue expands the response before executing its result block.
            // Keep the private text local until every staged result succeeds.
            const auto text = mIntents ? Interpreter::fixDefinesDialog(response.second->mResponse, script, 4096) : std::string{};
            auto invocation = found->script->executeResult(state.runtime, runningExecution, content, runningResolver,
                bank, bank.instances.front(), dialogue, program->program, result.trace, npcStats ? &*npcStats : nullptr,
                mEncounters ? &scene : nullptr);
            if (scene.removed)
            {
                require(dialogue.choices.empty()); // A removed speaker cannot own a pending choice.
                state.encounterLife = execution.sourceLife;
            }
            if (scene.removed || (npcStats && (ownedNpc != state.npcs.end() || npcStats->getBaseDisposition() != priorDisposition)))
            {
                const Instance::Npc changed{execution.sourceReference, execution.sourceLife,
                    actor.getCellRef().getRefId(), npcStats ? npcStats->getBaseDisposition() : actor.get<ESM::NPC>()->mBase->mNpdt.mDisposition};
                if (ownedNpc != state.npcs.end()) *ownedNpc = changed;
                else
                {
                    require(state.npcs.size() < MaximumNpcsPerStory);
                    state.npcs.push_back(changed);
                }
            }
            state.runtime.runtime.globals = std::move(invocation.state.globals);
            if (mIntents)
            {
                state.choices = std::move(dialogue.choices);
                prepared.response = text;
                result.presentation = std::move(invocation.presentation);
            }
            ++state.runtime.tick;
            bank.write(state);
            state.last = runningExecution; state.eventCursor = execution.causalEvent;
            if (state.encounterLife) state.last.physicalScope = index + 2;
            state.lastTopic = topic; state.lastInfo = response.second->mId; state.lastChoice = choice;
            if (!mIntents)
                prepared.image = encode(prepared.state);
            prepared.invocations = 1;
            result.outcome = SharedWorldScript::Outcome::Committed; result.executed = true;
        }
        catch (const EncounterPreparationRequired&)
        {
            // Neither locals, globals, progression nor output from discovery survives.
            // Admission already bound the sole cell/source; prepare a terminal private
            // reference and retry the identical causal occurrence from committed state.
            if (budget < 2)
            {
                prepared.state.clear(); prepared.image.clear(); prepared.response.clear(); result.presentation.clear();
                result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false;
                result.diagnostic = "Encounter discovery/retry exceeds invocation budget";
                return prepared;
            }
            auto retry = prepareDialogue(before, execution, topic, choice, disposition, talkedToPlayer, cellName,
                content, resolver, budget - 1, playerStats, customClassName, speakerStats, journalStamp, true);
            retry.result.trace.executed += result.trace.executed;
            if (retry.result.executed) ++retry.invocations;
            return retry;
        }
        catch (const MWScript::ScriptContextUnavailable& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            prepared.response.clear(); result.presentation.clear(); result.executed = false;
            result.outcome = SharedWorldScript::Outcome::Deferred; result.diagnostic = error.what();
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            prepared.response.clear(); result.presentation.clear();
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false;
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }

    PersonalScripts::Prepared PersonalScripts::prepareIntent(const State& before, uint64_t character, uint64_t command,
        const DialogueIntent& request, std::string_view cellName, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver, size_t budget, const MWMechanics::NpcStats* playerStats,
        std::optional<std::string_view> customClassName, const MWMechanics::NpcStats* speakerStats,
        std::optional<MWScript::DialogueState::Stamp> journalStamp) const
    {
        Prepared rejected;
        try
        {
            require(mIntents && character && command && validDialogueIntent(request));
            (void)encode(before);
            const auto found = std::ranges::find_if(before, [&](const auto& state) {
                return state.last.character == character && state.last.story == request.story;
            });
            require(found != before.end());
            if (command == found->dialogueCommand)
            {
                require(request == found->dialogueRequest);
                rejected.result.outcome = SharedWorldScript::Outcome::Committed;
                return rejected;
            }
            require(request.revision == found->dialogueRevision && found->last.invocation != UINT64_MAX
                && found->eventCursor != UINT64_MAX);
            if (request.choice >= 0)
                require(request.speaker == found->dialogueRequest.speaker && request.life == found->dialogueRequest.life
                    && request.topic == found->dialogueRequest.topic
                    && std::ranges::any_of(
                        found->choices, [&](const auto& choice) { return choice.id == request.choice; }));
            else
            {
                require(found->choices.empty() || request.speaker != found->dialogueRequest.speaker
                    || request.life != found->dialogueRequest.life);
                require(std::ranges::find(found->dialogue.topics, ESM::RefId::stringRefId(request.topic))
                    != found->dialogue.topics.end());
            }
            auto execution = found->last;
            ++execution.invocation;
            execution.causalEvent = found->eventCursor + 1;
            execution.sourceReference = request.speaker;
            execution.sourceLife = request.life;
            const bool talked
                = std::ranges::find(found->talkedSpeakers, request.speaker) != found->talkedSpeakers.end();
            auto prepared = prepareDialogue(before, execution, ESM::RefId::stringRefId(request.topic), request.choice,
                0, talked, cellName, content, resolver, budget, playerStats, customClassName, speakerStats, journalStamp);
            if (prepared.result.outcome != SharedWorldScript::Outcome::Committed || !prepared.result.executed)
                return prepared;
            auto& state = prepared.state[size_t(found - before.begin())];
            state.dialogueRevision = request.revision + 1;
            state.dialogueCommand = command;
            state.dialogueRequest = request;
            if (!talked)
            {
                require(state.talkedSpeakers.size() < 64);
                state.talkedSpeakers.push_back(request.speaker);
            }
            auto output = view(state, *SessionId::fromValue(1), SessionGeneration::initial(),
                ServerTick::initial(), CanonicalRevision::initial());
            output.response = prepared.response; output.messages = prepared.result.presentation;
            require(!encodeReliablePersonalDialogue(output).empty());
            prepared.image = encode(prepared.state);
            return prepared;
        }
        catch (const std::exception& error)
        {
            rejected.result.diagnostic
                = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
            return rejected;
        }
    }

    PersonalScripts::Prepared PersonalScripts::prepareScheduled(const State& before, uint64_t worldTick,
        std::span<const uint64_t> activeCharacters, const MWWorld::ESMStore& content,
        const MWScript::ReferenceResolver& resolver) const
    {
        Prepared prepared;
        auto& result = prepared.result;
        result.invocationTick = worldTick;
        try
        {
            require(mScheduled && worldTick && before.size() == mDefinitions.size()
                && activeCharacters.size() <= MaximumInstances);
            for (size_t i = 0; i < activeCharacters.size(); ++i)
            {
                require(activeCharacters[i] && std::ranges::any_of(mDefinitions,
                    [&](const auto& entry) { return entry.character == activeCharacters[i]; }));
                require(std::find(activeCharacters.begin(), activeCharacters.begin() + i, activeCharacters[i])
                    == activeCharacters.begin() + i);
            }
            // Validate the complete candidate before executing any instance.
            (void)encode(before);
            for (const auto& entry : before) require(entry.scheduledTick <= worldTick);
            prepared.state = before;
            bool processed = false;
            for (size_t i = 0; i < before.size(); ++i)
            {
                const auto& current = before[i];
                if (current.encounterLife || physicalScope(before, current.last.character) != 1 || current.scheduledTick == worldTick || std::ranges::find(activeCharacters, current.last.character)
                    == activeCharacters.end()) continue;
                require(prepared.invocations < MaximumInvocationsPerTransaction
                    && current.last.invocation != std::numeric_limits<uint64_t>::max());
                auto execution = current.last;
                ++execution.invocation; execution.causalEvent = 0; execution.sourceReference = execution.sourceLife = 0;
                auto invocation = prepare(prepared.state, execution, content, resolver,
                    MaximumInvocationsPerTransaction - prepared.invocations);
                result = std::move(invocation.result);
                // Personal presentation remains closed; never send it as shared output.
                result.presentation.clear();
                if (result.outcome != SharedWorldScript::Outcome::Committed || !result.executed)
                {
                    result.diagnostic = "character=" + std::to_string(execution.character)
                        + " story=" + std::to_string(execution.story) + " scope=1 source=0 life=0 " + result.diagnostic;
                    result.diagnostic.resize(std::min(result.diagnostic.size(), SharedWorldScript::MaximumDiagnosticBytes));
                    prepared.state.clear(); prepared.invocations = 0; result.executed = false;
                    return prepared;
                }
                prepared.state.swap(invocation.state);
                prepared.state[i].scheduledTick = worldTick;
                processed = true;
                prepared.invocations += invocation.invocations;
                require(prepared.invocations <= MaximumInvocationsPerTransaction);
            }
            result.outcome = SharedWorldScript::Outcome::Committed;
            result.executed = processed;
            if (result.executed) prepared.image = encode(prepared.state);
            else prepared.state.clear();
        }
        catch (const std::exception& error)
        {
            prepared.state.clear(); prepared.image.clear(); prepared.invocations = 0;
            result.outcome = SharedWorldScript::Outcome::Rejected; result.executed = false; result.presentation.clear();
            result.diagnostic = std::string_view(error.what()).substr(0, SharedWorldScript::MaximumDiagnosticBytes);
        }
        return prepared;
    }
}
