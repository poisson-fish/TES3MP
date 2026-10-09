#include "personal_script.hpp"
#include <apps/openmw/mwgui/referenceinterface.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwdialogue/dialoguemanagerimp.hpp>
#include <apps/openmw/mwdialogue/journalimp.hpp>
#include <apps/openmw/mwdialogue/selectwrapper.hpp>
#include <apps/openmw/mwmechanics/disposition.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwscript/dialogueextensions.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/locals.hpp>
#include <apps/openmw/mwscript/miscextensions.hpp>
#include <apps/openmw/mwscript/referencestate.hpp>
#include <apps/openmw/mwscript/statsextensions.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/tes3mp/engine_coordinator.hpp>
#include <components/compiler/extensions.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/interpreter/defines.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <iostream>
#include <sstream>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::Native;
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
    constexpr MWScript::DialogueState::Stamp JournalDate{42, 0, 1};
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class F> void rejects(F&& operation, const char* message)
    { try { operation(); } catch (const std::exception&) { return; } throw std::runtime_error(message); }
    ESM::DialogueCondition condition(ESM::DialogueCondition::Function function, std::string variable, int value)
    {
        ESM::DialogueCondition select; select.mFunction = function; select.mVariable = variable;
        select.mComparison = ESM::DialogueCondition::Comp_Eq; select.mValue = value; return select;
    }
    std::vector<ESM::Dialogue> records(std::string late = {})
    {
        std::vector<ESM::Dialogue> result;
        ESM::Dialogue quest; quest.mId = id("journey_t37"); quest.blank(); quest.mType = ESM::Dialogue::Journal;
        for (const auto index : {0, 10, 20, 30, 40})
        {
            ESM::DialInfo info; info.blank(); info.mId = id("journal_" + std::to_string(index));
            info.mData.mJournalIndex = index; info.mResponse = index == 30 ? "" : "A milestone.";
            info.mQuestStatus = index == 0 ? ESM::DialInfo::QS_Name : index == 30 ? ESM::DialInfo::QS_Restart
                : index == 20 || index == 40 ? ESM::DialInfo::QS_Finished : ESM::DialInfo::QS_None;
            quest.mInfo.push_back(info);
        }
        result.push_back(quest);
        ESM::Dialogue topic; topic.mId = id("passage_k61"); topic.blank(); topic.mType = ESM::Dialogue::Topic;
        ESM::DialInfo first; first.blank(); first.mId = id("initial_a"); first.mActor = id("speaker_r29");
        first.mSelects = {condition(ESM::DialogueCondition::Function_Journal, "journey_t37", 0),
            condition(ESM::DialogueCondition::Function_Global, "personal_counter", 0),
            condition(ESM::DialogueCondition::Function_Local, "calls", 0)};
        first.mResultScript = "set calls to calls + 1\nset personal_counter to personal_counter + 1\n"
            "set relay_n48.calls to relay_n48.calls + 3\nJournal journey_t37 10\nAddTopic followup_p82\n" + late;
        topic.mInfo.push_back(first);
        ESM::DialInfo next; next.blank(); next.mId = id("choice_b"); next.mActor = first.mActor;
        next.mSelects = {condition(ESM::DialogueCondition::Function_Journal, "journey_t37", 10),
            condition(ESM::DialogueCondition::Function_Local, "calls", 1),
            condition(ESM::DialogueCondition::Function_Choice, "", 7),
            condition(ESM::DialogueCondition::Function_TalkedToPc, "", 1)};
        next.mResultScript = "Journal journey_t37 20\nset calls to GetJournalIndex journey_t37\n"
            "StopScript relay_n48\nAddTopic followup_p82";
        topic.mInfo.push_back(next); result.push_back(topic);
        ESM::Dialogue followup; followup.mId = id("followup_p82"); followup.blank(); followup.mType = ESM::Dialogue::Topic;
        ESM::DialInfo lower; lower.blank(); lower.mId = id("lower_c"); lower.mActor = first.mActor;
        lower.mResultScript = "SetJournalIndex journey_t37 5\nJournal journey_t37 10\nJournal journey_t37 77\n";
        followup.mInfo.push_back(lower); result.push_back(followup);
        ESM::Dialogue restart; restart.mId = id("restart_z18"); restart.blank(); restart.mType = ESM::Dialogue::Topic;
        ESM::DialInfo empty; empty.blank(); empty.mId = id("restart_d"); empty.mActor = first.mActor;
        empty.mResultScript = "Journal journey_t37 30\nJournal journey_t37 40\nJournal journey_t37 30";
        restart.mInfo.push_back(empty); result.push_back(restart);
        ESM::Dialogue named; named.mId = id("named_h43"); named.blank(); named.mType = ESM::Dialogue::Topic;
        ESM::DialInfo title; title.blank(); title.mId = id("title_e"); title.mActor = first.mActor;
        title.mResultScript = "Journal journey_t37 0";
        named.mInfo.push_back(title); result.push_back(named);
        return result;
    }
    struct Fixture
    {
        MWWorld::ESMStore content;
        std::vector<PersonalScripts::Definition> definitions;
        MWWorld::ManualRef actor;
        static MWWorld::ESMStore& populate(MWWorld::ESMStore& content, bool intents)
        {
            ESM::NPC npc;
            npc.blank();
            npc.mId = id("speaker_r29");
            if (!intents)
                npc.mScript = id("ledger_v63");
            content.insertStatic(npc); return content;
        }
        Fixture(CredentialCrypto& crypto, std::string late = {}, bool intents = false,
            std::vector<ESM::Global> extraGlobals = {})
            : actor(populate(content, intents), id("speaker_r29"))
        {
            ESM::Global global; global.blank(); global.mId = id("personal_counter");
            global.mValue.setType(ESM::VT_Long); global.mValue.setInteger(0);
            extraGlobals.insert(extraGlobals.begin(), global);
            std::vector<ESM::Script> bank;
            for (const char* name : {"ledger_v63", "relay_n48"})
            {
                ESM::Script script; script.blank(); script.mId = id(name);
                script.mScriptText = std::string("begin ") + name + "\nlong calls\nend\n";
                bank.push_back(script); content.insertStatic(script);
            }
            auto first = std::make_shared<SharedWorldScript>(bank[0], extraGlobals, "dialogue-content-1", crypto, bank);
            auto second = std::make_shared<SharedWorldScript>(bank[1], extraGlobals, "dialogue-content-1", crypto, bank);
            for (const auto [character, story] : {std::pair{11u, 31u}, {22u, 31u}, {11u, 32u}})
                definitions.push_back({character, story, first, {{second, false}}, false, records(late)});
            if (intents)
                for (auto& definition : definitions)
                {
                    auto& topic = definition.dialogues[1];
                    topic.mInfo.front().mResponse = "Your passage awaits.";
                    topic.mInfo.front().mResultScript
                        = "MessageBox \"Private notice\"\n" + topic.mInfo.front().mResultScript;
                    topic.mInfo.front().mResultScript += "\nChoice \"Proceed\" 7 \"Wait\" 9";
                    topic.mInfo.back().mResponse = "Your journey is complete.";
                }
        }
    };
    struct Resolver final : MWScript::ReferenceResolver
    {
        MWScript::ExecutionContext expected;
        MWWorld::Ptr actor, player;
        uint64_t sourceLife = 3;
        bool wait = false, waitPlayer = false;
        MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& reference, bool) const override
        {
            require(execution == expected, "Dialogue lookup lost invocation identity");
            require(execution.sourceReference == 41 && execution.sourceLife == sourceLife, "Stale source/life accepted");
            if (reference == "player")
            {
                if (waitPlayer) throw MWScript::ScriptContextUnavailable();
                return player;
            }
            if ((reference.empty() || reference == id("ledger_v63")) && wait)
                throw MWScript::ScriptContextUnavailable();
            if (reference.empty() || reference == id("ledger_v63")
                || (actor.hasLiveReference() && reference == actor.getCellRef().getRefId())) return actor;
            throw std::invalid_argument("Unowned reference");
        }
    };
    int calls(const SharedWorldScript::State& state)
    { return state.runtime.script.mLocals.mVariables.front().second.getInteger(); }
    auto context(const PersonalScripts::Instance& state)
    { auto value = state.last; ++value.invocation; value.sourceReference = 41; value.sourceLife = 3; value.causalEvent = 53 + value.invocation; return value; }
    auto prepare(const PersonalScripts& runtime, const PersonalScripts::State& state, MWScript::ExecutionContext execution,
        Fixture& fixture, Resolver& resolver, const char* topic = "passage_k61", int choice = -1, bool talked = false)
    {
        resolver.expected = execution;
        return runtime.prepareDialogue(state, execution, id(topic), choice, 50, talked, "", fixture.content, resolver);
    }
    void isolation(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto);
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true);
        Resolver resolver; resolver.actor = fixture.actor.getPtr();
        auto state = runtime.initial(); const auto before = runtime.encode(state); const auto execution = context(state[0]);
        const auto named = prepare(runtime, state, execution, fixture, resolver, "named_h43");
        require(named.result.executed && named.state[0].dialogue.entries.size() == 1
            && named.state[0].dialogue.entries.front().info == id("journal_0")
            && named.state[0].dialogue.quests.front().mState == 0,
            "Explicit Journal at the stock quest-name index lost its entry");
        auto candidate = prepare(runtime, state, execution, fixture, resolver);
        require(candidate.result.executed && candidate.result.outcome == SharedWorldScript::Outcome::Committed,
            candidate.result.diagnostic.c_str());
        require(runtime.encode(state) == before && candidate.result.presentation.empty(), "Preparation leaked state/output");
        state.swap(candidate.state);
        require(calls(state[0].runtime) == 1 && calls(state[0].members[0]) == 3 && state[0].dialogue.quests[0].mState == 10
            && state[0].dialogue.entries.size() == 1 && state[0].dialogue.topics == std::vector{id("followup_p82")}
            && state[1].dialogue.quests.empty() && state[2].dialogue.topics.empty(), "Characters/stories shared dialogue state");
        const auto committed = runtime.encode(state);
        auto replay = prepare(runtime, state, execution, fixture, resolver);
        require(!replay.result.executed && replay.result.outcome == SharedWorldScript::Outcome::Committed && replay.image.empty(),
            "Dialogue replay executed twice");
        require(prepare(runtime, state, execution, fixture, resolver, "followup_p82").result.outcome == SharedWorldScript::Outcome::Rejected,
            "Conflicting topic replay accepted");
        for (int fault = 0; fault < 8; ++fault)
        {
            auto invalid = context(state[0]);
            if (fault == 0) ++invalid.campaign;
            if (fault == 1) ++invalid.story;
            if (fault == 2) ++invalid.character;
            if (fault == 3) ++invalid.physicalScope;
            if (fault == 4) ++invalid.sourceReference;
            if (fault == 5) ++invalid.sourceLife;
            if (fault == 6) ++invalid.invocation;
            if (fault == 7) invalid.causalEvent = execution.causalEvent;
            require(prepare(runtime, state, invalid, fixture, resolver, "passage_k61", 7, true).result.outcome
                == SharedWorldScript::Outcome::Rejected && runtime.encode(state) == committed, "Invalid dialogue identity mutated progress");
        }
        require(prepare(runtime, state, context(state[0]), fixture, resolver).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Choice filter matched without choice");
        candidate = prepare(runtime, state, context(state[0]), fixture, resolver, "passage_k61", 7, true);
        require(candidate.result.executed, candidate.result.diagnostic.c_str());
        require(candidate.result.executed && candidate.state[0].lastInfo == id("choice_b")
            && candidate.state[0].dialogue.quests[0].mState == 20 && candidate.state[0].dialogue.quests[0].mFinished
            && calls(candidate.state[0].runtime) == 20 && !candidate.state[0].members[0].runtime.script.mRunning,
            "Stock choice/result/index/running state differs");
        state.swap(candidate.state);
        candidate = prepare(runtime, state, context(state[1]), fixture, resolver);
        require(candidate.result.executed && candidate.state[1].lastInfo == id("initial_a")
            && candidate.state[0].dialogue.quests[0].mState == 20, "Second character inherited first branch");
        state.swap(candidate.state);
        candidate = prepare(runtime, state, context(state[2]), fixture, resolver);
        require(candidate.result.executed && candidate.state[2].lastInfo == id("initial_a"), "Second story inherited first branch");
        state.swap(candidate.state);
        candidate = prepare(runtime, state, context(state[0]), fixture, resolver, "followup_p82");
        require(candidate.result.executed && candidate.state[0].dialogue.quests[0].mState == 77
            && candidate.state[0].dialogue.entries.size() == 2, "Missing/duplicate Journal indices differ from stock");
        state.swap(candidate.state);
        candidate = prepare(runtime, state, context(state[0]), fixture, resolver, "restart_z18");
        require(candidate.result.executed && !candidate.state[0].dialogue.quests[0].mFinished
            && candidate.state[0].dialogue.quests[0].mState == 77 && candidate.state[0].dialogue.entries.size() == 4,
            "Empty duplicate Journal did not retain stock restart/finished semantics");
        require(runtime.encode(runtime.restore(committed)) == committed, "Progression image did not round trip");
        std::cout << "dialogue isolation: two characters/two stories; ordered Journal/global/local/Choice/TalkedToPC filters; stock indices/results/bank writes\n";
    }
    void rollback(CredentialCrypto& crypto)
    {
        for (const auto& late : {std::string("Goodbye"), std::string("FillJournal"), std::string("StartScript relay_n48"),
                 std::string("ledger_v63->Journal journey_t37 20")})
        {
            Fixture fixture(crypto, late); PersonalScripts runtime(7, fixture.definitions, crypto, true, true);
            Resolver resolver; resolver.actor = fixture.actor.getPtr(); resolver.wait = late.starts_with("ledger_v63->");
            auto state = runtime.initial(); const auto before = runtime.encode(state); const auto execution = context(state[0]);
            const auto failed = prepare(runtime, state, execution, fixture, resolver);
            require(failed.result.outcome == (resolver.wait ? SharedWorldScript::Outcome::Deferred : SharedWorldScript::Outcome::Rejected)
                && !failed.result.executed && failed.state.empty() && failed.image.empty() && runtime.encode(state) == before,
                "Late result failure leaked locals/globals/journals/topics/receipt");
            if (resolver.wait)
            {
                resolver.wait = false; auto retry = prepare(runtime, state, execution, fixture, resolver);
                require(retry.result.executed && retry.state[0].dialogue.entries.size() == 2
                    && calls(retry.state[0].runtime) == 1 && retry.state[0].eventCursor == execution.causalEvent,
                    "Deferred dialogue could not retry exactly once");
            }
        }
        std::cout << "dialogue rollback: late unsupported opcode and unavailable dependency discard every staged effect; retry consumes once\n";
    }
    void recovery(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto); PersonalScripts runtime(7, fixture.definitions, crypto, true, true);
        Resolver resolver; resolver.actor = fixture.actor.getPtr(); auto state = runtime.initial();
        const auto execution = context(state[0]); auto candidate = prepare(runtime, state, execution, fixture, resolver);
        require(candidate.result.executed, candidate.result.diagnostic.c_str());
        const auto saved = candidate.image;
        PersonalScripts restarted(7, fixture.definitions, crypto, true, true); auto restored = restarted.restore(saved);
        require(restarted.encode(restored) == saved && restored[0].lastInfo == id("initial_a")
            && restored[0].dialogue.entries.size() == 1 && restored[1].dialogue.entries.empty(), "Restart lost isolated progress");
        require(!prepare(restarted, restored, execution, fixture, resolver).result.executed, "Restart replay executed result");
        auto corrupt = saved; corrupt.pop_back(); rejects([&] { restarted.restore(corrupt); }, "Truncated progress accepted");
        corrupt = saved; corrupt.push_back(0); rejects([&] { restarted.restore(corrupt); }, "Trailing progress accepted");
        auto invalid = restored; invalid[0].dialogue.topics.push_back(id("unowned"));
        rejects([&] { restarted.encode(invalid); }, "Unowned topic accepted");
        invalid = restored; invalid[0].dialogue.entries.push_back(invalid[0].dialogue.entries.front());
        rejects([&] { restarted.encode(invalid); }, "Duplicate journal entry accepted");
        auto changed = fixture.definitions; changed[0].dialogues[1].mInfo.front().mResultScript += "\nset calls to 9";
        PersonalScripts incompatible(7, changed, crypto, true, true);
        rejects([&] { incompatible.restore(saved); }, "Changed result source accepted old progress");
        for (auto& definition : changed) definition.dialogues.clear();
        PersonalScripts legacy(7, changed, crypto, true, true);
        rejects([&] { legacy.restore(saved); }, "Dialogue downgrade accepted");
        rejects([&] { runtime.restore(legacy.encode(legacy.initial())); }, "Dialogue silent upgrade accepted");
        std::cout << "dialogue recovery: content/source/policy bound progress and silent replay; malformed/unowned/duplicate and upgrade/downgrade reject\n";
    }
    void intents(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto, {}, true);
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true);
        auto state = runtime.initial();
        const auto original = runtime.encode(state);
        Resolver resolver;
        resolver.actor = fixture.actor.getPtr();
        const auto run = [&](uint64_t character, uint64_t command, DialogueIntent request) {
            const auto found = std::ranges::find_if(
                state, [&](const auto& s) { return s.last.character == character && s.last.story == request.story; });
            auto execution = found->last;
            ++execution.invocation;
            execution.causalEvent = found->eventCursor + 1;
            execution.sourceReference = 41;
            execution.sourceLife = 3;
            resolver.expected = execution;
            return runtime.prepareIntent(state, character, command, request, "", fixture.content, resolver,
                PersonalScripts::MaximumInvocationsPerTransaction, nullptr, {}, nullptr, JournalDate);
        };
        DialogueIntent request{ 31, 41, 3, 0, "passage_k61", -1 };
        auto prepared = run(11, 101, request);
        require(prepared.result.executed && prepared.response == "Your passage awaits."
                && prepared.state[0].choices.size() == 2
                && prepared.result.presentation == std::vector<std::string>{ "Private notice", "#{sJournalEntry}" }
                && prepared.state[0].dialogueRevision == 1 && prepared.state[1].dialogueRevision == 0
                && prepared.state[2].dialogueRevision == 0,
            "Intent routing/presentation leaked between characters/stories");
        require(runtime.encode(state) == original, "Undurable intent mutated installed state");
        auto retry = run(11, 101, request);
        require(prepared.image == retry.image, "Rollback retry changed progression");
        resolver.wait = true;
        const auto deferred = run(11, 101, request);
        require(deferred.result.outcome == SharedWorldScript::Outcome::Deferred && deferred.image.empty()
                && deferred.response.empty() && runtime.encode(state) == original,
            "Unavailable dialogue source leaked state/output");
        resolver.wait = false;
        require(run(11, 101, request).image == prepared.image, "Deferred intent could not retry unchanged");
        state = std::move(prepared.state);
        const auto committed = runtime.encode(state);
        auto duplicate = run(11, 101, request);
        require(!duplicate.result.executed && duplicate.result.outcome == SharedWorldScript::Outcome::Committed
                && duplicate.response.empty() && duplicate.result.presentation.empty(),
            "Duplicate replayed response/result");
        auto forged = request;
        forged.topic = "followup_p82";
        require(run(11, 101, forged).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Conflicting receipt accepted");
        request.revision = 1;
        request.choice = 9;
        require(run(11, 102, request).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Unmatched choice mutated state");
        request.choice = 7;
        request.life = 2;
        require(run(11, 102, request).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Stale speaker life accepted");
        request.life = 3;
        auto choice = run(11, 102, request);
        require(choice.result.executed && choice.state[0].choices.empty()
                && choice.state[0].dialogue.quests[0].mFinished && choice.response == "Your journey is complete.",
            "Committed choice failed stock progression");
        auto recovered = runtime.restore(committed);
        state = recovered;
        require(
            state[0].choices.size() == 2 && state[1].choices.empty(), "Reconnect/restart lost isolated pending choice");
        auto restarted = run(11, 102, request);
        require(restarted.image == choice.image, "Restart changed retry outcome");
        state = runtime.restore(choice.image);
        require(!run(11, 102, request).result.executed, "Restart repeated committed choice");
        request = { 31, 41, 3, 0, "passage_k61", -1 };
        auto other = run(22, 101, request);
        require(other.result.executed && other.state[1].dialogueRevision == 1 && other.state[0].dialogueRevision == 2,
            "Second client inherited first client's receipt/progression");
        Fixture bad(crypto, "Goodbye", true);
        PersonalScripts badRuntime(7, bad.definitions, crypto, true, true, true);
        auto badState = badRuntime.initial();
        resolver.expected = badState[0].last;
        ++resolver.expected.invocation;
        resolver.expected.causalEvent = 1;
        resolver.expected.sourceReference = 41;
        resolver.expected.sourceLife = 3;
        resolver.actor = bad.actor.getPtr();
        auto failed = badRuntime.prepareIntent(badState, 11, 101, request, "", bad.content, resolver,
            PersonalScripts::MaximumInvocationsPerTransaction, nullptr, {}, nullptr, JournalDate);
        require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.image.empty()
                && failed.response.empty() && failed.result.presentation.empty(),
            "Late rejected result leaked presentation/progression");
        PersonalScripts old(7, fixture.definitions, crypto, true, true);
        rejects([&] { old.restore(committed); }, "V85 downgrade accepted");
        rejects([&] { runtime.restore(old.encode(old.initial())); }, "V84 silent upgrade accepted");
        std::cout << "dialogue intents: two characters/stories; offered Choice bound to source life/revision; atomic "
                     "rollback/retry; durable receipt and silent reconnect/restart\n";
    }

    void playerStats(CredentialCrypto& crypto)
    {
        using C = ESM::DialogueCondition;
        Fixture fixture(crypto, {}, true);
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            ESM::Attribute attribute;
            attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
            fixture.content.insertStatic(attribute);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            ESM::Skill skill; skill.blank();
            skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
            fixture.content.insertStatic(skill);
        }
        MWWorld::ManualRef player(fixture.content, id("speaker_r29"));
        MWMechanics::NpcStats stats(fixture.content);
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            MWMechanics::AttributeValue value;
            value.setBase(40 + i); value.setModifier(20.75f); value.damage(2.f);
            stats.setAttribute(ESM::Attribute::indexToRefId(i), value, 0.f);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            MWMechanics::SkillValue value;
            value.setBase(10 + i); value.setModifier(4.75f); value.damage(2.f);
            stats.setSkill(ESM::Skill::indexToRefId(i), value);
        }
        for (int i = 0; i < 3; ++i)
            stats.setDynamic(i, MWMechanics::DynamicStat<float>(100, 20, 60.5f + i), MWWorld::TimeStamp{});
        Resolver resolver; resolver.actor = fixture.actor.getPtr(); resolver.player = player.getPtr();
        DialogueIntent request{31, 41, 3, 0, "passage_k61", -1};
        const auto run = [&](const PersonalScripts& runtime, const PersonalScripts::State& state,
                             uint64_t character, uint64_t story, const MWMechanics::NpcStats* snapshot) {
            const auto& instance = *std::ranges::find_if(state, [&](const auto& value) {
                return value.last.character == character && value.last.story == story;
            });
            resolver.expected = instance.last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = instance.eventCursor + 1;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = 3;
            auto intent = request; intent.story = story;
            return runtime.prepareIntent(state, character, 101, intent, "", fixture.content, resolver,
                PersonalScripts::MaximumInvocationsPerTransaction, snapshot, {}, nullptr, JournalDate);
        };
        std::vector<C::Function> functions{C::Function_PcStrength, C::Function_PcIntelligence,
            C::Function_PcWillpower, C::Function_PcAgility, C::Function_PcSpeed, C::Function_PcEndurance,
            C::Function_PcPersonality, C::Function_PcLuck, C::Function_PcHealth, C::Function_PcMagicka,
            C::Function_PcFatigue, C::Function_PcHealthPercent};
        for (int i = C::Function_PcBlock; i <= C::Function_PcHandToHand; ++i)
            functions.push_back(static_cast<C::Function>(i));
        for (auto function : functions)
        {
            MWDialogue::SelectWrapper select(condition(function, "", 0));
            const bool skill = function >= C::Function_PcBlock && function <= C::Function_PcHandToHand;
            const bool dynamic = function == C::Function_PcHealth || function == C::Function_PcMagicka
                || function == C::Function_PcFatigue;
            const float value = function == C::Function_PcHealthPercent ? 50.f : dynamic
                ? stats.getDynamic(select.getArgument()).getCurrent() : skill
                ? static_cast<int>(stats.getSkill(ESM::Skill::indexToRefId(select.getArgument())).getModified())
                : static_cast<int>(stats.getAttribute(ESM::Attribute::indexToRefId(select.getArgument())).getModified());
            auto definitions = fixture.definitions;
            for (auto& definition : definitions)
            {
                auto check = condition(function, "", 0); check.mValue = value;
                definition.dialogues[1].mInfo.front().mSelects.insert(
                    definition.dialogues[1].mInfo.front().mSelects.begin(), check);
            }
            PersonalScripts runtime(7, definitions, crypto, true, true, true);
            auto state = runtime.initial(); const auto before = runtime.encode(state);
            auto prepared = run(runtime, state, 11, 31, &stats);
            if (!prepared.result.executed)
                throw std::runtime_error("Staged stat filter " + std::to_string(function) + ": " + prepared.result.diagnostic);
            require(prepared.result.executed && runtime.encode(state) == before
                && prepared.state[1].dialogueRevision == 0 && prepared.state[2].dialogueRevision == 0,
                "Staged stat filter failed or crossed ownership");
            require(!run(runtime, state, 11, 31, nullptr).result.executed, "Unstaged stat read fell back to stock player");
            MWMechanics::NpcStats other(fixture.content);
            require(!run(runtime, state, 22, 31, &other).result.executed,
                "Second character inherited first character's stats");
            resolver.waitPlayer = true;
            auto deferred = run(runtime, state, 11, 31, &stats);
            require(deferred.result.outcome == SharedWorldScript::Outcome::Deferred && deferred.state.empty()
                && deferred.response.empty() && deferred.result.presentation.empty(), "Missing player was treated as zero/absence");
            resolver.waitPlayer = false;
            require(run(runtime, state, 11, 31, &stats).image == prepared.image, "Read snapshot retry changed result");
            state = runtime.restore(prepared.image);
            require(!run(runtime, state, 11, 31, &stats).result.executed, "Recovered stat filter replayed response");
            require(run(runtime, state, 11, 32, &stats).result.executed, "First story consumed another story's opportunity");
        }
        for (auto comparison : {C::Comp_Eq, C::Comp_Ne, C::Comp_Gt, C::Comp_Ge, C::Comp_Ls, C::Comp_Le})
        {
            for (float threshold : {57.5f, 58.f, 58.5f})
            {
                auto check = condition(C::Function_PcStrength, "", 0);
                check.mComparison = comparison; check.mValue = threshold;
                auto definitions = fixture.definitions;
                for (auto& definition : definitions)
                    definition.dialogues[1].mInfo.front().mSelects.insert(
                        definition.dialogues[1].mInfo.front().mSelects.begin(), check);
                PersonalScripts runtime(7, definitions, crypto, true, true, true);
                const bool expected = MWDialogue::SelectWrapper(check).selectCompare(58);
                auto prepared = run(runtime, runtime.initial(), 11, 31, &stats);
                require(prepared.result.executed == expected, "Scoped comparison changed stock integer truncation/comparison");
            }
        }
        for (auto function : {C::Function_PcLevel, C::Function_PcCrimeLevel, C::Function_Item, C::Function_Weather,
                             C::Function_PcCommonDisease})
        {
            auto definitions = fixture.definitions;
            for (auto& definition : definitions)
                definition.dialogues[1].mInfo.front().mSelects.insert(
                    definition.dialogues[1].mInfo.front().mSelects.begin(), condition(function, "unowned", 0));
            PersonalScripts runtime(7, definitions, crypto, true, true, true);
            const auto failed = run(runtime, runtime.initial(), 11, 31, &stats);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty(),
                "Read-only stats opened an unstaged service");
        }
        // A successful read cannot open writes in the result interpreter.
        rejects([&] {
            Fixture bad(crypto, "player->SetStrength 1", true);
            PersonalScripts runtime(7, bad.definitions, crypto, true, true, true);
        }, "Read-only stat filters admitted stat result writes");
        for (const char* late : {"Goodbye"})
        {
            Fixture bad(crypto, late, true);
            for (auto& definition : bad.definitions)
                definition.dialogues[1].mInfo.front().mSelects.insert(
                    definition.dialogues[1].mInfo.front().mSelects.begin(), condition(C::Function_PcStrength, "", 58));
            PersonalScripts runtime(7, bad.definitions, crypto, true, true, true);
            auto state = runtime.initial(); const auto before = runtime.encode(state);
            auto failed = run(runtime, state, 11, 31, &stats);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
                && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty()
                && runtime.encode(state) == before && stats.getAttribute(ESM::Attribute::Strength).getModified() == 58.75f,
                "Late stat result failure leaked state or changed read snapshot");
        }
        std::cout << "dialogue player stats: 8 modified attributes/27 skills, 3 current resources/health percent; "
                     "character/story isolation, missing-player defer, read-only result boundary and silent recovery\n";
    }

    void disposition(CredentialCrypto& crypto, bool npcResults = false)
    {
        Fixture fixture(crypto, {}, true);
        ESM::Attribute attribute; attribute.mId = ESM::Attribute::Personality;
        fixture.content.insertStatic(attribute);
        for (const auto& [name, number] : std::vector<std::pair<const char*, float>>{
            {"fDispRaceMod", 10}, {"fDispPersonalityMult", .5f}, {"fDispPersonalityBase", 50},
            {"fDispFactionRankMult", .2f}, {"fDispFactionRankBase", 1}, {"fDispFactionMod", .5f},
            {"fDispCrimeMod", .1f}, {"fDispDiseaseMod", -20}, {"fDispWeaponDrawn", -10}})
        {
            ESM::GameSetting setting; setting.blank(); setting.mId = id(name);
            setting.mValue.setType(ESM::VT_Float); setting.mValue.setFloat(number);
            fixture.content.insertStatic(setting);
        }
        for (const char* name : {"guild_q84", "a_guild", "b_guild"})
        {
            ESM::Faction faction; faction.blank(); faction.mId = id(name);
            for (int i = 0; i < 10; ++i) faction.mRanks[i] = "Rank " + std::to_string(i);
            faction.mReactions = {{id("guild_q84"), 10}, {id("a_guild"), 8}, {id("b_guild"), -4}};
            fixture.content.insertStatic(faction);
        }
        for (const auto& [name, race] : std::vector<std::pair<const char*, const char*>>{
            {"reader_a", "race_a"}, {"reader_b", "race_b"}, {"speaker_a", "race_a"}, {"speaker_b", "race_b"}})
        {
            ESM::NPC npc; npc.blank(); npc.mId = id(name); npc.mRace = id(race);
            npc.mFaction = id("guild_q84"); fixture.content.insertStatic(npc);
        }
        MWWorld::ManualRef a(fixture.content, id("reader_a")), b(fixture.content, id("reader_b"));
        MWWorld::ManualRef sa(fixture.content, id("speaker_a")), sb(fixture.content, id("speaker_b"));
        MWMechanics::NpcStats statsA(fixture.content), statsB(fixture.content), speaker(fixture.content);
        const auto personality = [](auto& stats, float value) {
            MWMechanics::AttributeValue attribute; attribute.setBase(value); attribute.setModifier(.75f);
            stats.setAttribute(ESM::Attribute::Personality, attribute, 0.f);
        };
        personality(statsA, 30); personality(statsB, 70);
        speaker.setBaseDisposition(50); speaker.setCrimeDispositionModifier(3);
        const auto calculation = [&](const auto& playerStats, const auto& speakerStats, bool clamp = true) {
            return MWMechanics::derivedDisposition(*sa.getPtr().get<ESM::NPC>()->mBase, speakerStats,
                *a.getPtr().get<ESM::NPC>()->mBase, playerStats, fixture.content.get<ESM::GameSetting>(),
                [&](const ESM::RefId& from, const ESM::RefId& to) {
                    const auto& reactions = fixture.content.get<ESM::Faction>().find(from)->mReactions;
                    const auto found = reactions.find(to); return found == reactions.end() ? 0 : found->second;
                }, clamp);
        };
        require(calculation(statsA, speaker) == 53, "Stock base/crime/race/modified-personality truncation changed");
        auto ranked = statsA; ranked.joinFaction(id("guild_q84"));
        ranked.setFactionRank(id("guild_q84"), 2, fixture.content);
        require(calculation(ranked, speaker) == 60, "Stock own-faction reaction/rank changed");
        ranked.expell(id("guild_q84"), false);
        require(calculation(ranked, speaker) == 53, "Expelled own faction contributed disposition");
        auto foreign = statsA;
        foreign.joinFaction(id("a_guild")); foreign.joinFaction(id("b_guild"));
        require(calculation(foreign, speaker) == 51, "Stock least favorable faction reaction changed");
        foreign.expell(id("a_guild"), false); foreign.expell(id("b_guild"), false);
        require(calculation(foreign, speaker) == 53, "Expelled foreign faction contributed disposition");
        auto bounty = statsA; bounty.setBounty(100);
        require(calculation(bounty, speaker) == 43, "Stock bounty penalty changed");
        auto drawn = statsA; drawn.setDrawState(MWMechanics::DrawState::Weapon);
        require(calculation(drawn, speaker) == 43, "Stock weapon penalty changed");
        drawn.setDrawState(MWMechanics::DrawState::Spell);
        require(calculation(drawn, speaker) == 53, "Spell draw incorrectly applied weapon penalty");
        for (auto type : {ESM::Spell::ST_Disease, ESM::Spell::ST_Blight})
        {
            ESM::Spell disease; disease.blank(); disease.mId = id("disease_" + std::to_string(type));
            disease.mData.mType = type; auto diseased = statsA; diseased.getSpells().add(&disease, false);
            require(calculation(diseased, speaker) == 33, "Stock disease penalty changed");
        }
        auto charmed = speaker;
        charmed.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Charm), MWMechanics::EffectParam(6.75f));
        require(calculation(statsA, charmed) == 60, "Stock Charm addition/truncation changed");
        auto extreme = speaker; extreme.setBaseDisposition(500);
        require(calculation(statsA, extreme) == 100 && calculation(statsA, extreme, false) == 503,
            "Stock upper clamp/unclamped result changed");
        extreme.setBaseDisposition(-500);
        require(calculation(statsA, extreme) == 0 && calculation(statsA, extreme, false) == -496,
            "Stock lower clamp/truncation changed");

        for (auto& definition : fixture.definitions)
        {
            auto& topic = definition.dialogues[1]; auto& high = topic.mInfo.front();
            high.mActor = {}; high.mData.mDisposition = 60; high.mResponse = "High disposition.";
            auto low = high; low.mId = id("low_disposition"); low.mData.mDisposition = 0;
            low.mResponse = "Low disposition."; topic.mInfo.insert(std::next(topic.mInfo.begin()), low);
            topic.mInfo.back().mActor = {};
            if (npcResults)
            {
                for (auto& info : topic.mInfo) info.mResultScript += "\nModDisposition 17";
                topic.mInfo.back().mData.mDisposition = 61;
                topic.mInfo.back().mResultScript += "\nModDisposition -26";
                ESM::DialInfo returned; returned.blank(); returned.mId = id("returned_s52");
                returned.mData.mDisposition = 61; returned.mResponse = "Returned.";
                returned.mSelects = {condition(ESM::DialogueCondition::Function_Journal, "journey_t37", 20)};
                returned.mResultScript = "ModDisposition 1";
                topic.mInfo.push_back(returned);
            }
        }
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true);
        auto state = runtime.initial(); const auto before = runtime.encode(state);
        Resolver resolver; resolver.actor = sa.getPtr();
        const auto run = [&](const PersonalScripts& policy, const PersonalScripts::State& input,
            uint64_t character, uint64_t story, const MWMechanics::NpcStats* playerStats,
            const MWMechanics::NpcStats* speakerStats, uint64_t command = 101, int choice = -1, uint64_t revision = 0,
            uint64_t life = 3) {
            const auto found = std::ranges::find_if(input, [&](const auto& value) {
                return value.last.character == character && value.last.story == story; });
            resolver.player = character == 11 ? a.getPtr() : b.getPtr();
            resolver.expected = found->last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = found->eventCursor + 1;
            resolver.sourceLife = life;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = life;
            return policy.prepareIntent(input, character, command, {story, 41, life, revision, "passage_k61", choice},
                "", fixture.content, resolver, PersonalScripts::MaximumInvocationsPerTransaction,
                playerStats, {}, speakerStats, JournalDate);
        };
        const auto low = run(runtime, state, 11, 31, &statsA, &speaker);
        const auto high = run(runtime, state, 22, 31, &statsB, &speaker);
        require(low.result.executed && high.result.executed && low.response == "Low disposition."
            && high.response == "High disposition." && low.state[1].dialogueRevision == 0
            && low.state[2].dialogueRevision == 0 && high.state[0].dialogueRevision == 0
            && runtime.encode(state) == before, "Private disposition branches crossed character/story or mutated before commit");
        if (npcResults)
        {
            const PersonalScripts::Instance::Npc expected{41, 3, id("speaker_a"), 67};
            require(low.state[0].npcs == std::vector{expected} && low.state[1].npcs.empty() && low.state[2].npcs.empty()
                && high.state[1].npcs == std::vector{expected} && high.state[0].npcs.empty()
                && speaker.getBaseDisposition() == 50, "NPC result crossed stories/characters or changed source snapshot");
        }
        require(run(runtime, state, 11, 31, &statsA, &charmed).response == "High disposition.",
            "Committed speaker Charm did not change the exact threshold branch");
        require(run(runtime, state, 22, 31, &drawn, &speaker).response == "Low disposition.",
            "Authenticated race was replaced by another character's race");
        resolver.actor = sb.getPtr();
        require(run(runtime, state, 11, 31, &statsA, &charmed).response == "Low disposition.",
            "Selected speaker race was replaced by a cached speaker");
        resolver.actor = sa.getPtr();
        for (const auto [playerSnapshot, speakerSnapshot] : {
            std::pair<const MWMechanics::NpcStats*, const MWMechanics::NpcStats*>{nullptr, &speaker}, {&statsA, nullptr}})
        {
            const auto failed = run(runtime, state, 11, 31, playerSnapshot, speakerSnapshot);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
                && failed.response.empty() && failed.result.presentation.empty(), "Unstaged disposition fell back to stock context");
        }
        for (bool missingPlayer : {false, true})
        {
            resolver.wait = !missingPlayer; resolver.waitPlayer = missingPlayer;
            const auto failed = run(runtime, state, 11, 31, &statsA, &speaker);
            require(failed.result.outcome == SharedWorldScript::Outcome::Deferred && failed.state.empty()
                && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty(),
                "Unavailable disposition dependency leaked a branch/result");
        }
        resolver.wait = resolver.waitPlayer = false;
        require(run(runtime, state, 11, 31, &statsA, &speaker).image == low.image, "Disposition retry changed the candidate");
        auto late = fixture.definitions;
        for (auto& definition : late)
            for (auto& info : definition.dialogues[1].mInfo) info.mResultScript += "\nGoodbye";
        PersonalScripts failing(7, late, crypto, true, true, true);
        for (const auto character : {11u, 22u})
        {
            const auto failed = run(failing, failing.initial(), character, 31, character == 11 ? &statsA : &statsB, &speaker);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
                && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty(),
                "Late disposition result failure leaked bank/progression/choice/private output");
        }
        state = runtime.restore(low.image);
        if (npcResults)
        {
            require(state[0].npcs == low.state[0].npcs, "Recovery lost story-owned NPC state");
            auto invalid = state; invalid[0].npcs.push_back(invalid[0].npcs.front());
            rejects([&] { runtime.encode(invalid); }, "Duplicate NPC placement admitted");
            invalid = state; invalid[0].npcs.front().life = 0;
            rejects([&] { runtime.encode(invalid); }, "Zero NPC life admitted");
            invalid = state; invalid[0].npcs.front().record = id(std::string(129, 'q'));
            rejects([&] { runtime.encode(invalid); }, "Unbounded NPC record admitted");
            invalid = state; invalid[0].npcs.front().life = 4;
            const auto stale = run(runtime, invalid, 11, 31, &statsA, &speaker, 102, 7, 1);
            require(stale.result.outcome == SharedWorldScript::Outcome::Rejected && stale.state.empty(),
                "Older source life replaced story-owned NPC state");
            resolver.actor = sb.getPtr();
            const auto rebound = run(runtime, state, 11, 31, &statsA, &speaker, 102, 7, 1);
            require(rebound.result.outcome == SharedWorldScript::Outcome::Rejected && rebound.state.empty(),
                "Another NPC record inherited a placement relationship");
            resolver.actor = sa.getPtr();
            for (size_t size = 0; size < low.image.size(); ++size)
                rejects([&] { runtime.restore(std::span(low.image).first(size)); }, "Truncated NPC image accepted");
            auto previousPolicy = low.image; previousPolicy[7] = char(0x36);
            rejects([&] { runtime.restore(previousPolicy); }, "Prior dialogue image silently restored NPC state");
            auto oversized = low.image; oversized[oversized.size() - 8] = 65;
            rejects([&] { runtime.restore(oversized); }, "Unbounded NPC count admitted before allocation");
        }
        const auto duplicate = run(runtime, state, 11, 31, &statsA, &speaker);
        require(duplicate.result.outcome == SharedWorldScript::Outcome::Committed && !duplicate.result.executed
            && duplicate.response.empty() && duplicate.result.presentation.empty() && state[0].choices.size() == 2,
            "Restart lost choices or replayed the private disposition branch");
        require(run(runtime, state, 11, 32, &statsA, &speaker).result.executed, "Disposition consumed another story");
        const auto completed = run(runtime, state, 11, 31, &statsA, &speaker, 102, 7, 1);
        require(completed.result.executed && completed.state[0].dialogue.quests.front().mFinished,
            "Recovered disposition choice could not continue");
        if (npcResults)
            require(completed.state[0].npcs.front().disposition == 58 && completed.state[1].npcs.empty()
                && speaker.getBaseDisposition() == 50, "Recovered NPC base did not feed filters and stock additive result");
        state = runtime.restore(completed.image);
        const auto finished = run(runtime, state, 11, 31, &statsA, &speaker, 102, 7, 1);
        require(!finished.result.executed && finished.response.empty() && state[0].choices.empty(),
            "Finished disposition recovery replayed output/choices");
        if (npcResults)
        {
            auto maximum = speaker; maximum.setBaseDisposition(INT32_MAX);
            require(calculation(statsA, maximum) == 100, "Maximum NPC base overflowed stock filtering");
            rejects([&] { calculation(statsA, maximum, false); }, "Unclamped NPC overflow accepted");
            auto large = fixture.definitions;
            for (auto& definition : large)
                for (auto& info : definition.dialogues[1].mInfo)
                    if (info.mId == id("choice_b")) info.mResultScript = "Journal journey_t37 20\nModDisposition -9";
                    else info.mResultScript += "\nModDisposition 2147483580";
            PersonalScripts largeBase(7, large, crypto, true, true, true);
            const auto raised = run(largeBase, largeBase.initial(), 11, 31, &statsA, &speaker);
            require(raised.result.executed && raised.state[0].npcs.front().disposition == INT32_MAX,
                "Valid maximum stock NPC base was rejected or clamped on write");
            const auto lowered = run(largeBase, largeBase.restore(raised.image), 11, 31, &statsA, &speaker, 102, 7, 1);
            require(lowered.result.executed && lowered.state[0].npcs.front().disposition == INT32_MAX - 9,
                "Recovered large NPC base overflowed filters or stock additive results");
            const auto revived = run(runtime, state, 11, 31, &statsA, &speaker, 103, -1, 2, 4);
            require(revived.result.executed && revived.response == "Returned."
                && revived.state[0].npcs.front().disposition == 59 && revived.state[0].npcs.front().life == 4,
                "Legitimate new source life reset its placement relationship");
            const auto oldLife = run(runtime, revived.state, 11, 31, &statsA, &speaker, 104, -1, 3);
            require(oldLife.result.outcome == SharedWorldScript::Outcome::Rejected && oldLife.state.empty(),
                "Old source life changed the restored NPC relationship");
            auto foreign = fixture.definitions;
            for (auto& definition : foreign)
                for (auto& info : definition.dialogues[1].mInfo) info.mResultScript += "\nrelay_n48->ModDisposition 99";
            PersonalScripts unowned(7, foreign, crypto, true, true, true);
            const auto escaped = run(unowned, unowned.initial(), 11, 31, &statsA, &speaker);
            require(escaped.result.outcome == SharedWorldScript::Outcome::Rejected && escaped.state.empty()
                && escaped.response.empty() && escaped.result.presentation.empty() && speaker.getBaseDisposition() == 50,
                "Explicit foreign NPC result escaped source ownership or leaked prior writes");
            auto overflow = fixture.definitions;
            for (auto& definition : overflow)
                for (auto& info : definition.dialogues[1].mInfo) info.mResultScript += "\nModDisposition 2147483647";
            PersonalScripts overflowing(7, overflow, crypto, true, true, true);
            const auto failed = run(overflowing, overflowing.initial(), 11, 31, &statsA, &speaker);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
                && failed.response.empty() && failed.result.presentation.empty() && speaker.getBaseDisposition() == 50,
                "Overflowing NPC result leaked state/output or changed source stats");
        }
        for (int threshold : {-1, 101})
        {
            auto invalid = fixture.definitions; invalid[0].dialogues[1].mInfo.front().mData.mDisposition = threshold;
            rejects([&] { PersonalScripts rejected(7, invalid, crypto, true, true, true); }, "Unbounded threshold admitted");
        }
        auto changed = fixture.definitions; changed[0].dialogues[1].mInfo.front().mData.mDisposition = 61;
        PersonalScripts incompatible(7, changed, crypto, true, true, true);
        rejects([&] { incompatible.restore(low.image); }, "Changed threshold policy restored incompatible progress");
        std::cout << "dialogue disposition: shared stock terms/truncation/clamps, authenticated character/speaker; "
                     "private threshold branches, waits, rollback/retry, policy binding and silent pending/finished recovery\n";
        if (npcResults) std::cout << "story NPC: stock ModDisposition, source/record/life binding, character/story isolation; "
            "late/overflow rollback, bounded image validation, restored filters and silent duplicate recovery\n";
    }

    void substitutions(CredentialCrypto& crypto)
    {
        const auto global = [](const char* name, ESM::VarType type, float number) {
            ESM::Global value; value.blank(); value.mId = id(name); value.mValue.setType(type);
            if (type == ESM::VT_Float) value.mValue.setFloat(number);
            else value.mValue.setInteger(int(number));
            return value;
        };
        Fixture fixture(crypto, {}, true, {global("personal_counter_long", ESM::VT_Long, 7),
            global("personal_flag", ESM::VT_Short, -3), global("personal_fraction", ESM::VT_Float, 1.5f)});
        for (const char* name : {"reader", "speaker"})
        {
            ESM::Race race; race.blank(); race.mId = id(std::string(name) + "_race"); race.mName = race.mId.toString();
            ESM::Class cls; cls.blank(); cls.mId = id(std::string(name) + "_class"); cls.mName = cls.mId.toString();
            fixture.content.insertStatic(race); fixture.content.insertStatic(cls);
        }
        ESM::Faction faction; faction.blank(); faction.mId = id("guild_q84"); faction.mName = "Guild Q84";
        for (int i = 0; i < 10; ++i) faction.mRanks[i] = "Rank " + std::to_string(i);
        fixture.content.insertStatic(faction);
        const auto npc = [&](const char* name, const char* display, bool speaker, int rank) {
            ESM::NPC value; value.blank(); value.mId = id(name); value.mName = display;
            value.mRace = id(speaker ? "speaker_race" : "reader_race");
            value.mClass = id(speaker ? "speaker_class" : "reader_class");
            if (speaker) { value.mFaction = faction.mId; value.mNpdt.mRank = static_cast<unsigned char>(rank); }
            fixture.content.insertStatic(value);
        };
        npc("reader_a", "Ada ^PCName", false, 0); npc("reader_b", "Beren", false, 0);
        npc("speaker_a", "Guide A", true, 2); npc("speaker_b", "Guide B", true, 6);
        MWWorld::ManualRef a(fixture.content, id("reader_a")), b(fixture.content, id("reader_b"));
        MWWorld::ManualRef sa(fixture.content, id("speaker_a")), sb(fixture.content, id("speaker_b"));
        MWMechanics::NpcStats statsA(fixture.content), statsB(fixture.content);
        statsA.setBounty(11); statsB.setBounty(29);
        statsA.joinFaction(faction.mId); statsA.setFactionRank(faction.mId, 3, fixture.content);
        const std::string builtins = "%PCName|^PCRace|%PCClass|%name|%race|%class|%faction|%rank|"
            "%pcrank|%pcnextrank|%nextpcrank|%pccrimelevel";
        const auto response = builtins + "|%cell|%PERSONAL_counter_long|%personal_counter|%personal_flag|"
            "%personal_fraction|%unknown|100%";
        for (auto& definition : fixture.definitions)
        {
            auto& first = definition.dialogues[1].mInfo.front();
            first.mActor = {}; first.mResponse = response;
            first.mSelects.erase(first.mSelects.begin() + 1); // Vary the owned global independently by story.
            definition.dialogues[1].mInfo.back().mActor = {};
        }
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true);
        auto state = runtime.initial(); state[2].runtime.runtime.globals.front().mValue.setInteger(4);
        const auto before = runtime.encode(state);
        Resolver resolver; resolver.actor = sa.getPtr(); resolver.player = a.getPtr();
        const auto run = [&](const PersonalScripts& policy, const PersonalScripts::State& input,
                             uint64_t character, uint64_t story, const MWMechanics::NpcStats* stats,
                             uint64_t command = 101, std::optional<std::string_view> customClass = {}) {
            const auto found = std::ranges::find_if(input,
                [&](const auto& value) { return value.last.character == character && value.last.story == story; });
            resolver.expected = found->last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = found->eventCursor + 1;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = 3;
            return policy.prepareIntent(input, character, command, {story, 41, 3, 0, "passage_k61", -1},
                "Context Cell", fixture.content, resolver, PersonalScripts::MaximumInvocationsPerTransaction, stats, customClass,
                nullptr, JournalDate);
        };
        MWScript::InterpreterContext stock(nullptr, sa.getPtr(), a.getPtr(), fixture.content, &statsA);
        const auto expected = Interpreter::fixDefinesDialog(builtins, stock) + "|Context Cell|7|0|-3|1.5|%unknown|100%";
        auto prepared = run(runtime, state, 11, 31, &statsA);
        require(prepared.result.executed && prepared.response == expected
            && prepared.state[0].runtime.runtime.globals.front().mValue.getInteger() == 1
            && prepared.state[1].dialogueRevision == 0 && prepared.state[2].dialogueRevision == 0
            && prepared.result.presentation == std::vector<std::string>{"Private notice", "#{sJournalEntry}"}
            && runtime.encode(state) == before,
            "Response differs from stock speaker/character substitution or pre-result global ordering");
        require(run(runtime, state, 11, 31, &statsA).image == prepared.image, "Substitution rollback retry differs");
        auto otherStory = run(runtime, state, 11, 32, &statsA);
        require(otherStory.result.executed && otherStory.response.find("|7|4|-3|1.5|") != std::string::npos,
            "Response read another story's globals");
        resolver.actor = sb.getPtr(); resolver.player = b.getPtr();
        auto other = run(runtime, state, 22, 31, &statsB);
        require(other.result.executed && other.response.starts_with("Beren|reader_race|reader_class|Guide B|")
            && other.response.find("|Rank 6|Rank 0|Rank 0|Rank 0|29|") != std::string::npos,
            "Response inherited another character/speaker or changed stock absent-faction defaults");
        resolver.actor = sa.getPtr(); resolver.player = a.getPtr();
        const auto empty = [&](const auto& value) {
            require(!value.result.executed && value.state.empty() && value.image.empty() && value.response.empty()
                && value.result.presentation.empty() && runtime.encode(state) == before,
                "Failed substitution leaked progress or partial private output");
        };
        resolver.waitPlayer = true;
        auto deferred = run(runtime, state, 11, 31, &statsA); empty(deferred);
        require(deferred.result.outcome == SharedWorldScript::Outcome::Deferred, "Replacement wait swallowed");
        resolver.waitPlayer = false;
        empty(run(runtime, state, 11, 31, nullptr));
        resolver.player = {}; empty(run(runtime, state, 11, 31, &statsA)); resolver.player = a.getPtr();
        empty(run(runtime, state, 11, 31, &statsA, 101, "Wayfarer"));
        ESM::NPC customBody; customBody.blank(); customBody.mId = id("custom_reader");
        customBody.mName = "Cerys"; customBody.mRace = id("reader_race"); fixture.content.insertStatic(customBody);
        MWWorld::ManualRef custom(fixture.content, customBody.mId); resolver.player = custom.getPtr();
        auto customOutput = run(runtime, state, 11, 31, &statsA, 101, "Wayfarer");
        require(customOutput.result.executed && customOutput.response.starts_with("Cerys|reader_race|Wayfarer|"),
            "Canonical custom class text was absent or inherited the NPC template class");
        empty(run(runtime, state, 11, 31, &statsA)); resolver.player = a.getPtr();
        {
            MWWorld::ManualRef expired(fixture.content, id("reader_a")); resolver.player = expired.getPtr();
        }
        empty(run(runtime, state, 11, 31, &statsA)); resolver.player = a.getPtr();
        statsA.setFactionRank(faction.mId, 9, fixture.content);
        auto maximumRank = run(runtime, state, 11, 31, &statsA);
        require(maximumRank.result.executed && maximumRank.response.find("|Rank 2|Rank 9|Rank 9|Rank 9|11|") != std::string::npos,
            "Next rank escaped stock maximum");
        statsA.setFactionRank(faction.mId, 3, fixture.content);
        require(run(runtime, state, 11, 31, &statsA).image == prepared.image, "Replacement wait consumed occurrence");
        auto late = fixture.definitions;
        for (auto& definition : late) definition.dialogues[1].mInfo.front().mResultScript += "\nGoodbye";
        PersonalScripts bad(7, late, crypto, true, true, true);
        auto lateFailure = run(bad, bad.initial(), 11, 31, &statsA); empty(lateFailure);
        require(lateFailure.result.outcome == SharedWorldScript::Outcome::Rejected, "Late unsupported result accepted");
        auto unsupported = fixture.definitions;
        for (auto& definition : unsupported) definition.dialogues[1].mInfo.front().mResponse = "%PCName %actionjump";
        PersonalScripts action(7, unsupported, crypto, true, true, true);
        empty(run(action, action.initial(), 11, 31, &statsA));
        npc("large_reader", std::string(64, 'x').c_str(), false, 0);
        MWWorld::ManualRef large(fixture.content, id("large_reader")); resolver.player = large.getPtr();
        for (int count : {64, 65})
        {
            auto bounded = fixture.definitions; std::string text;
            for (int i = 0; i < count; ++i) text += "^PCName";
            for (auto& definition : bounded) definition.dialogues[1].mInfo.front().mResponse = text;
            PersonalScripts limit(7, bounded, crypto, true, true, true);
            auto value = run(limit, limit.initial(), 11, 31, &statsA);
            if (count == 64) require(value.result.executed && value.response.size() == 4096, "Exact output bound rejected");
            else empty(value);
        }
        resolver.player = a.getPtr();
        auto resumed = runtime.restore(prepared.image);
        auto replay = run(runtime, resumed, 11, 31, &statsA);
        require(!replay.result.executed && replay.result.outcome == SharedWorldScript::Outcome::Committed
            && replay.response.empty() && replay.result.presentation.empty() && resumed[0].choices.size() == 2,
            "Recovery replayed substitutions/messages or lost choices");
        // A different policy must not inherit another context's cached global names.
        Fixture second(crypto, {}, true, {global("other_bank", ESM::VT_Long, 23)});
        for (auto& definition : second.definitions) definition.dialogues[1].mInfo.front().mResponse = "%other_bank";
        PersonalScripts separate(7, second.definitions, crypto, true, true, true);
        resolver.actor = second.actor.getPtr();
        auto separateOutput = run(separate, separate.initial(), 11, 31, nullptr);
        require(separateOutput.result.executed && separateOutput.response == "23", "Global-name cache crossed policies");
        rejects([&] { separate.restore(prepared.image); }, "Changed substitution policy restored incompatible receipt");
        ESM::Cell cell; cell.blank(); cell.mData.mFlags = ESM::Cell::Interior; cell.mName = "Named Interior";
        require(MWWorld::Cell(cell).getCellName(fixture.content) == cell.mName, "Stock interior display name changed");
        cell.mData.mFlags = 0; cell.mName.clear(); cell.mRegion = id("region_a");
        ESM::Region region; region.blank(); region.mId = cell.mRegion; region.mName = "Region A";
        fixture.content.insertStatic(region);
        require(MWWorld::Cell(cell).getCellName(fixture.content) == region.mName, "Stock unnamed exterior region changed");
        region.mName.clear(); region.mId = id("region_b"); cell.mRegion = region.mId; fixture.content.insertStatic(region);
        require(MWWorld::Cell(cell).getCellName(fixture.content) == region.mId.getRefIdString(), "Stock unnamed region ID changed");
        cell.mRegion = {};
        ESM::GameSetting defaultName; defaultName.mId = id("sDefaultCellname");
        defaultName.mValue.setType(ESM::VT_String); defaultName.mValue.setString("Wilderness");
        fixture.content.insertStatic(defaultName);
        require(MWWorld::Cell(cell).getCellName(fixture.content) == "Wilderness", "Stock default exterior name changed");
        std::cout << "dialogue substitutions: stock character/speaker/rank/bounty/cell and typed owned globals; "
            "pre-result reads, two characters/stories, no recursive replacement; bounds, waits and late-result rollback; "
            "retry, silent receipt/choice recovery and per-policy global lookup\n";
    }

    void client(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto, {}, true);
        struct Coordinator final : TES3MP::OpenMWAdapter::EngineCoordinator
        {
            MWWorld::Ptr actor;
            std::vector<ReliablePersonalDialogue> views;
            std::vector<DialogueIntent> submitted;
            void frame(float) noexcept override {}
            bool ownsPersonalDialogue() const noexcept override { return true; }
            std::span<const ReliablePersonalDialogue> personalDialogue() const noexcept override { return views; }
            std::optional<std::pair<uint64_t, uint64_t>> dialogueSpeaker(
                const MWWorld::Ptr& value) const noexcept override
            {
                return value == actor ? std::optional(std::pair{ uint64_t(41), uint64_t(3) }) : std::nullopt;
            }
            bool submitPersonalDialogue(uint64_t story, std::pair<uint64_t, uint64_t> speaker, std::string_view topic,
                int choice) noexcept override
            {
                if (speaker != std::pair{ uint64_t(41), uint64_t(3) })
                    return false;
                submitted.push_back({ story, 41, 3, views.front().revision, std::string(topic), choice });
                return true;
            }
        } coordinator;
        coordinator.actor = fixture.actor.getPtr();
        ReliablePersonalDialogue view{ *SessionId::fromValue(1), SessionGeneration::initial(), ServerTick::initial(),
            CanonicalRevision::initial() };
        view.story = 31;
        view.topics = { "passage_k61" };
        coordinator.views.push_back(view);
        struct Callback final : MWBase::DialogueManager::ResponseCallback
        {
            std::vector<std::string> responses;
            void addResponse(std::string_view title, std::string_view text) override
            {
                responses.push_back(std::string(title) + ":" + std::string(text));
            }
        } callback;
        MWBase::Environment environment;
        MWDialogue::Journal journal;
        environment.setJournal(journal);
        environment.setMultiplayerCoordinator(&coordinator);
        Compiler::Extensions extensions;
        Translation::Storage translation;
        MWDialogue::DialogueManager manager(extensions, translation);
        require(manager.startDialogue(fixture.actor.getPtr(), &callback) && callback.responses.empty(),
            "Native start ran local greeting/result");
        manager.persuade(0, &callback);
        manager.applyBarterDispositionChange(20);
        require(manager.checkServiceRefused(&callback) && callback.responses.empty(),
            "Native dialogue executed an unadmitted physical service");
        require(manager.getAvailableTopics() == std::list<std::string>{ "passage_k61" },
            "Client did not install owned root topic");
        manager.keywordSelected("passage_k61", &callback);
        require(coordinator.submitted.size() == 1 && coordinator.submitted.back().story == 31
                && coordinator.submitted.back().choice == -1 && callback.responses.empty(),
            "Topic intent executed local result or displayed an undurable response");
        view.revision = 1;
        view.command = 101;
        view.speaker = 41;
        view.life = 3;
        view.topic = "passage_k61";
        view.response = "Committed for Ada ^PCName.";
        view.choices = { { "Proceed", 7 } };
        view.quests = { { "journey_t37", 10, false } };
        coordinator.views.front() = view;
        auto foreign = view;
        foreign.story = 32;
        manager.presentPersonalDialogue(foreign);
        require(callback.responses.empty(), "Foreign story presented in current dialogue");
        foreign = view;
        foreign.life = 2;
        manager.presentPersonalDialogue(foreign);
        require(callback.responses.empty(), "Old speaker life presented");
        manager.presentPersonalDialogue(view);
        require(callback.responses.size() == 2 && callback.responses.front() == "passage_k61:Committed for Ada ^PCName."
                && manager.isInChoice() && manager.getChoices().front().second == 7,
            "Committed response/choice/progression not presented");
        manager.questionAnswered(7, &callback);
        require(coordinator.submitted.size() == 2 && coordinator.submitted.back().topic == "passage_k61"
                && coordinator.submitted.back().choice == 7 && callback.responses.size() == 2,
            "Choice intent ran client result code");
        view.revision = 2;
        view.command = 102;
        view.choices.clear();
        view.quests.front().index = 20;
        view.quests.front().finished = true;
        view.response = "Committed finish.";
        coordinator.views.front() = view;
        manager.presentPersonalDialogue(view);
        require(callback.responses.size() == 4 && !manager.isInChoice(), "Committed choice/progression missing");
        // Opening a recovered menu receives persistent data without transient response.
        view.response.clear();
        view.messages.clear();
        coordinator.views.front() = view;
        Callback resumed;
        require(manager.startDialogue(fixture.actor.getPtr(), &resumed) && resumed.responses.size() == 1
                && resumed.responses.front().find("finished") != std::string::npos,
            "Reconnect replayed response or lost progression");
        auto second = view;
        second.story = 32; second.revision = 1; second.topic = "followup_p82";
        second.topics = {"followup_p82"}; second.choices = {{"Continue another story", 9}};
        second.response = "Already seen response";
        coordinator.views.push_back(second);
        manager.getAvailableTopics();
        manager.keywordSelected("followup_p82 (story 32)", &resumed);
        require(coordinator.submitted.size() == 2 && manager.isInChoice() && manager.getChoices().front().second == 9
            && std::ranges::none_of(resumed.responses, [](const auto& response) {
                return response.find("Already seen response") != std::string::npos;
            }), "Recovered second-story choices sent another topic or replayed response");
        manager.questionAnswered(9, &resumed);
        require(coordinator.submitted.back().story == 32 && coordinator.submitted.back().topic == "followup_p82",
            "Recovered second-story choice used first story");
        manager.goodbyeSelected();
        manager.clear();
        std::cout
            << "dialogue client: stock manager consumes committed owned views; topic/choice only submit; "
               "source-life/story isolation; progression and silent reentry; no Journal/World/Lua services installed\n";
    }

    void journal(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto, {}, true);
        for (const auto& [name, display] : std::vector<std::pair<const char*, const char*>>{
            {"reader_a", "Ada ^PCName"}, {"reader_b", "Beren"}, {"guide_a", "Guide A"}, {"guide_b", "Guide B"}})
        {
            ESM::NPC value; value.blank(); value.mId = id(name); value.mName = display;
            fixture.content.insertStatic(value);
        }
        MWWorld::ManualRef a(fixture.content, id("reader_a")), b(fixture.content, id("reader_b"));
        MWWorld::ManualRef sa(fixture.content, id("guide_a")), sb(fixture.content, id("guide_b"));
        MWMechanics::NpcStats statsA(fixture.content), statsB(fixture.content);
        statsA.setBounty(11); statsB.setBounty(29);
        const std::string source = "%PCName|^name|%PCClass|%cell|%personal_counter|%pccrimelevel";
        for (auto& definition : fixture.definitions)
        {
            for (auto& info : definition.dialogues[0].mInfo)
                if (info.mData.mJournalIndex == 10 || info.mData.mJournalIndex == 20) info.mResponse = source;
            for (auto& topic : definition.dialogues)
                if (topic.mType == ESM::Dialogue::Topic)
                    for (auto& info : topic.mInfo) info.mActor = {};
        }
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true);
        auto state = runtime.initial(); const auto before = runtime.encode(state);
        Resolver resolver; resolver.actor = sa.getPtr();
        const auto run = [&](const PersonalScripts& policy, const PersonalScripts::State& input,
            uint64_t character = 11, uint64_t story = 31, uint64_t command = 101,
            int choice = -1, std::optional<MWScript::DialogueState::Stamp> stamp = JournalDate,
            std::string_view topic = "passage_k61", std::string_view customClass = "Custom scholar") {
            const auto& current = *std::ranges::find_if(input, [&](const auto& value) {
                return value.last.character == character && value.last.story == story; });
            resolver.player = character == 11 ? a.getPtr() : b.getPtr();
            resolver.expected = current.last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = current.eventCursor + 1;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = 3;
            return policy.prepareIntent(input, character, command,
                {story, 41, 3, current.dialogueRevision, std::string(topic), choice}, "Authoritative cell",
                fixture.content, resolver, PersonalScripts::MaximumInvocationsPerTransaction,
                character == 11 ? &statsA : &statsB, customClass, nullptr, stamp);
        };
        auto first = run(runtime, state);
        require(first.result.executed, first.result.diagnostic.c_str());
        const auto& entry = first.state[0].dialogue.entries.front();
        require(entry.text == "Ada ^PCName|Guide A|Custom scholar|Authoritative cell|1|11"
            && entry.stamp == JournalDate && first.state[1].dialogue.entries.empty()
            && first.state[2].dialogue.entries.empty() && runtime.encode(state) == before,
            "Journal expansion/stamp lost authenticated context, result-time global or private ownership");
        require(run(runtime, state).image == first.image, "Journal rollback retry changed bytes");
        resolver.actor = sb.getPtr();
        const auto second = run(runtime, state, 22);
        require(second.result.executed && second.state[1].dialogue.entries.front().text
            == "Beren|Guide B|Custom scholar|Authoritative cell|1|29" && second.state[0].dialogue.entries.empty(),
            "Journal crossed authenticated characters/speakers");
        resolver.actor = sa.getPtr();
        require(run(runtime, state, 11, 32).state[2].dialogue.entries.front() == entry,
            "Second story inherited or lost its personal journal");
        for (auto stamp : {std::optional<MWScript::DialogueState::Stamp>{},
            std::optional<MWScript::DialogueState::Stamp>{{42, 1, 29}},
            std::optional<MWScript::DialogueState::Stamp>{{-1, 0, 1}}})
        {
            const auto failed = run(runtime, state, 11, 31, 101, -1, stamp);
            require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
                && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty()
                && runtime.encode(state) == before, "Missing/invalid journal calendar leaked results");
        }
        resolver.waitPlayer = true;
        auto failed = run(runtime, state);
        require(failed.result.outcome == SharedWorldScript::Outcome::Deferred && failed.state.empty()
            && failed.response.empty() && failed.result.presentation.empty() && runtime.encode(state) == before,
            "Unavailable journal player fell back or leaked results");
        resolver.waitPlayer = false;
        const std::string oversized(4096, 'x');
        failed = run(runtime, state, 11, 31, 101, -1, JournalDate, "passage_k61", oversized);
        require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
            && runtime.encode(state) == before, "Expanded journal text exceeded its bound");
        auto badDefinitions = fixture.definitions;
        for (auto& definition : badDefinitions)
            definition.dialogues[1].mInfo.front().mResultScript += "\nGoodbye";
        PersonalScripts bad(7, badDefinitions, crypto, true, true, true);
        failed = run(bad, bad.initial());
        require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
            && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty(),
            "Late failure leaked expanded journal text/date");
        state = first.state;
        auto finished = run(runtime, state, 11, 31, 102, 7, MWScript::DialogueState::Stamp{43, 0, 2});
        require(finished.result.executed && finished.state[0].dialogue.entries.size() == 2
            && finished.state[0].dialogue.entries[0] == entry
            && finished.state[0].dialogue.entries[1].stamp == MWScript::DialogueState::Stamp{43, 0, 2},
            "Later journal entry restamped/re-expanded prior text");
        state = finished.state;
        const auto duplicate = run(runtime, state, 11, 31, 103, -1,
            MWScript::DialogueState::Stamp{44, 0, 3}, "followup_p82");
        require(duplicate.result.executed && duplicate.state[0].dialogue.entries == state[0].dialogue.entries
            && duplicate.state[0].dialogue.quests.front().mState == 77,
            "Duplicate/missing Journal index changed committed text/date");
        PersonalScripts restarted(7, fixture.definitions, crypto, true, true, true);
        auto restored = restarted.restore(finished.image);
        require(restarted.encode(restored) == finished.image && restored[0].dialogue.entries == state[0].dialogue.entries,
            "Journal recovery re-expanded or restamped entries");
        resolver.expected = restored[0].last;
        auto replay = restarted.prepareIntent(restored, 11, 102, restored[0].dialogueRequest, "Changed cell",
            fixture.content, resolver);
        require(!replay.result.executed && replay.response.empty() && replay.result.presentation.empty(),
            "Recovered journal command replayed output");
        auto invalid = restored; invalid[0].dialogue.entries[0].stamp = {42, 1, 29};
        rejects([&] { runtime.encode(invalid); }, "Invalid durable calendar accepted");
        invalid = restored; invalid[0].dialogue.entries[0].text.assign(4097, 'x');
        rejects([&] { runtime.encode(invalid); }, "Oversized durable text accepted");
        invalid = restored; auto copied = invalid[0].dialogue.entries[0]; copied.text = "Contradictory duplicate";
        invalid[0].dialogue.entries.push_back(copied);
        rejects([&] { runtime.encode(invalid); }, "Duplicate identity with changed text accepted");
        auto corrupt = finished.image; corrupt.pop_back();
        rejects([&] { runtime.restore(corrupt); }, "Truncated journal image accepted");
        corrupt = finished.image; corrupt[7] = '5';
        rejects([&] { runtime.restore(corrupt); }, "Prior journal policy silently upgraded");
        // Stock suppresses empty expanded entries in its duplicate journal search.
        // A later speaker can make the same INFO visible for the first time.
        auto emptyDefinitions = fixture.definitions;
        for (auto& definition : emptyDefinitions)
            std::ranges::find_if(definition.dialogues[0].mInfo, [](const auto& info) {
                return info.mData.mJournalIndex == 10; })->mResponse = "%name";
        ESM::NPC unnamed; unnamed.blank(); unnamed.mId = id("unnamed_guide");
        fixture.content.insertStatic(unnamed);
        MWWorld::ManualRef unnamedGuide(fixture.content, unnamed.mId); resolver.actor = unnamedGuide.getPtr();
        PersonalScripts emptyRuntime(7, emptyDefinitions, crypto, true, true, true);
        auto empty = run(emptyRuntime, emptyRuntime.initial());
        require(empty.result.executed && empty.state[0].dialogue.entries[0].text.empty(),
            "Empty stock substitution produced a visible journal entry");
        empty = run(emptyRuntime, empty.state, 11, 31, 102, 7);
        ESM::NPC guide; guide.blank(); guide.mId = id("named_guide"); guide.mName = "Guide G64";
        fixture.content.insertStatic(guide);
        MWWorld::ManualRef namedGuide(fixture.content, guide.mId); resolver.actor = namedGuide.getPtr();
        auto visible = run(emptyRuntime, empty.state, 11, 31, 103, -1,
            MWScript::DialogueState::Stamp{44, 0, 3}, "followup_p82");
        require(visible.result.executed && visible.state[0].dialogue.entries.size() == 2
            && visible.state[0].dialogue.entries.back().info == id("journal_10")
            && visible.state[0].dialogue.entries.back().text == "Guide G64"
            && visible.state[0].dialogue.entries.back().stamp == MWScript::DialogueState::Stamp{44, 0, 3},
            "Previously empty Journal INFO suppressed later stock text/date");
        resolver.actor = sa.getPtr();
        std::ostringstream journalContent(std::ios::binary);
        ESM::ESMWriter writer; writer.setVersion(ESM::VER_130); writer.save(journalContent);
        for (auto dialogue : fixture.definitions.front().dialogues)
        {
            dialogue.mStringId = dialogue.mId.toString();
            writer.startRecord("DIAL"); dialogue.save(writer); writer.endRecord("DIAL");
            for (const auto& info : dialogue.mInfo)
            { writer.startRecord("INFO"); info.save(writer); writer.endRecord("INFO"); }
        }
        writer.close(); ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(journalContent.str(), std::ios::binary), {});
        ESM::Dialogue* loaded = nullptr; fixture.content.load(reader, nullptr, loaded);
        fixture.content.getWritable<ESM::Dialogue>().setUp();
        MWDialogue::Journal presentation;
        auto view = PersonalScripts::view(restored[0], *SessionId::fromValue(1), SessionGeneration::initial(),
            ServerTick::initial(), CanonicalRevision::initial());
        require(decodeReliablePersonalDialogue(encodeReliablePersonalDialogue(view)) == view,
            "Private journal wire text/date did not round trip");
        presentation.installPersonalJournal(view, fixture.content);
        require(presentation.getEntries().size() == 2 && presentation.getEntries().front().getText() == entry.text
            && presentation.getEntries().front().mDay == 42 && presentation.getEntries().back().mDayOfMonth == 2
            && presentation.getQuestOrNull(id("journey_t37"))->isFinished()
            && presentation.getJournalIndex(id("journey_t37")) == 20,
            "Stock journal presentation lost committed text/date/quest status");
        rejects([&] { presentation.setJournalIndex(id("journey_t37"), 99); }, "Client wrote private journal progression");
        auto other = PersonalScripts::view(second.state[1], *SessionId::fromValue(1), SessionGeneration::initial(),
            ServerTick::initial(), CanonicalRevision::initial());
        other.story = 32;
        presentation.installPersonalJournal(other, fixture.content);
        require(presentation.getEntries().size() == 2, "Foreign story replaced selected journal");
        presentation.selectPersonalJournal(32);
        require(presentation.getEntries().size() == 1 && presentation.getEntries().front().getText().starts_with("Beren")
            && !presentation.getQuestOrNull(id("journey_t37"))->isFinished(), "Journal story selection merged progress");
        other.entries.front().info = "missing";
        rejects([&] { presentation.installPersonalJournal(other, fixture.content); }, "Unknown client journal INFO accepted");
        require(presentation.getEntries().front().getText().starts_with("Beren"), "Invalid journal installation was not atomic");
        presentation.clear(); require(presentation.getEntries().empty(), "Session cleanup retained private journal");
        std::cout << "journal: stock authenticated expansion, result-time globals, authoritative calendar; private text/date "
            "isolation, waits/bounds/late rollback, duplicate semantics, durable recovery and silent stock journal installation\n";
    }

    void encounter(CredentialCrypto& crypto, bool enable = false)
    {
        Fixture fixture(crypto, {}, true);
        struct WindowReference final : MWGui::ReferenceInterface
        {
            int closed = 0;
            void bind(const MWWorld::Ptr& ptr) { mPtr = ptr; }
            void onReferenceUnavailable() override { ++closed; }
        } window;
        auto replica = std::make_unique<MWWorld::ManualRef>(fixture.content, id("speaker_r29"));
        window.bind(replica->getPtr()); window.checkReferenceAvailable();
        require(!window.closed, "Living dialogue replica was discarded");
        replica.reset(); window.checkReferenceAvailable(); window.checkReferenceAvailable();
        require(window.closed == 1, "Destroyed dialogue replica was dereferenced or closed repeatedly");
        for (auto& definition : fixture.definitions)
        {
            definition.encounter = PersonalScripts::EncounterBinding{41, id("speaker_r29"), id("room_e87")};
            definition.dialogues[1].mInfo.front().mResultScript =
                "set calls to calls + 1\nJournal journey_t37 10\nDisable\n"
                "set personal_counter to GetDisabled\nMessageBox \"Removed for this story\"";
        }
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true);
        auto state = runtime.initial(); const auto before = runtime.encode(state);
        Resolver resolver; resolver.actor = fixture.actor.getPtr();
        const auto run = [&](const PersonalScripts& policy, const PersonalScripts::State& input,
            uint64_t character, uint64_t command = 101, size_t budget = 16) {
            const auto& instance = *std::ranges::find_if(input, [&](const auto& value) {
                return value.last.character == character && value.last.story == 31;
            });
            resolver.expected = instance.last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = instance.eventCursor + 1;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = 3;
            return policy.prepareIntent(input, character, command, {31, 41, 3, instance.dialogueRevision, "passage_k61", -1},
                "Room", fixture.content, resolver, budget, nullptr, {}, nullptr, JournalDate);
        };
        auto alice = run(runtime, state, 11);
        if (!alice.result.executed) throw std::runtime_error("Encounter invocation: " + alice.result.diagnostic);
        require(alice.state[0].encounterLife == 3 && alice.state[0].last.physicalScope == 2,
            "Disable did not establish a terminal source-life-bound scope");
        require(calls(alice.state[0].runtime) == 1, "Disable discovery/retry doubled local mutations");
        require(alice.state[0].runtime.runtime.globals.front().mValue.getInteger() == 1,
            "GetDisabled did not read the staged source state");
        require(std::ranges::count(alice.result.presentation, "Removed for this story") == 1
            && alice.state[0].dialogue.entries.size() == 1,
            "Disable discovery/retry duplicated private output");
        require(fixture.actor.getPtr().getRefData().isEnabled() && runtime.encode(state) == before,
            "Private Disable mutated its public source or committed bank");
        require(alice.invocations == 2 && run(runtime, state, 11, 101, 1).result.outcome
            == SharedWorldScript::Outcome::Rejected, "Discovery/retry escaped the invocation budget");
        auto namedDefinitions = fixture.definitions;
        for (auto& definition : namedDefinitions)
            definition.dialogues[1].mInfo.front().mResultScript
                = "speaker_r29->Disable\nset personal_counter to speaker_r29->GetDisabled";
        PersonalScripts named(7, namedDefinitions, crypto, true, true, true);
        const auto namedRemoval = run(named, named.initial(), 11);
        require(namedRemoval.result.executed && namedRemoval.state[0].encounterLife == 3
            && namedRemoval.state[0].runtime.runtime.globals.front().mValue.getInteger() == 1,
            "Explicit source lookup disagreed with implicit Disable/GetDisabled");
        MWWorld::ManualRef foreignPlayer(fixture.content, id("speaker_r29"));
        resolver.player = foreignPlayer.getPtr();
        for (auto& definition : namedDefinitions)
            definition.dialogues[1].mInfo.front().mResultScript = "player->Disable";
        PersonalScripts foreign(7, namedDefinitions, crypto, true, true, true);
        const auto foreignRemoval = run(foreign, foreign.initial(), 11);
        require(foreignRemoval.result.outcome == SharedWorldScript::Outcome::Rejected
            && foreignRemoval.state.empty() && foreignPlayer.getPtr().getRefData().isEnabled(),
            "Encounter writer disabled a foreign player reference");
        require(PersonalScripts::physicalScope(alice.state, 11) == 2
            && PersonalScripts::physicalScope(alice.state, 22) == 1
            && alice.state[1].encounterLife == 0 && calls(alice.state[1].runtime) == 0
            && alice.state[2].encounterLife == 0, "Removal consumed another character/story opportunity");
        auto bob = run(runtime, alice.state, 22);
        require(bob.result.executed && bob.state[1].encounterLife == 3 && bob.state[1].last.physicalScope == 3,
            "B could not complete the same stock result independently after A's removal");
        require(runtime.encode(runtime.restore(bob.image)) == bob.image, "Scoped origins/receipts failed exact restart");
        auto replay = runtime.prepareIntent(bob.state, 11, 101, {31, 41, 3, 0, "passage_k61", -1}, "Room",
            fixture.content, resolver, 16, nullptr, {}, nullptr, JournalDate);
        require(replay.result.outcome == SharedWorldScript::Outcome::Committed && !replay.result.executed
            && replay.result.presentation.empty(), "Terminal receipt replayed a result or required its removed speaker");
        require(run(runtime, alice.state, 11, 102).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Removed source remained available to fresh dialogue");
        resolver.wait = true;
        auto waiting = run(runtime, state, 11);
        require(waiting.result.outcome == SharedWorldScript::Outcome::Deferred && waiting.state.empty()
            && runtime.encode(state) == before, "Unavailability masqueraded as a committed removal");
        resolver.wait = false;
        auto definitions = fixture.definitions;
        for (auto& definition : definitions) definition.dialogues[1].mInfo.front().mResultScript += "\nGoodbye";
        PersonalScripts late(7, definitions, crypto, true, true, true);
        auto failed = run(late, late.initial(), 11);
        require(failed.result.outcome == SharedWorldScript::Outcome::Rejected && failed.state.empty()
            && failed.image.empty() && failed.response.empty() && failed.result.presentation.empty()
            && fixture.actor.getPtr().getRefData().isEnabled(), "Late result failure leaked private/public scene state");
        auto invalid = bob.state;
        invalid[1].last.physicalScope = 2;
        rejects([&] { runtime.encode(invalid); }, "Recovery accepted another story's physical scope");
        invalid = bob.state; invalid[2].encounterLife = 3; invalid[2].last = invalid[0].last;
        invalid[2].last.story = 32; invalid[2].last.physicalScope = 4;
        rejects([&] { runtime.encode(invalid); }, "Character inhabited two scopes");
        for (size_t size = 0; size < alice.image.size(); ++size)
            rejects([&] { runtime.restore(std::span(alice.image).first(size)); }, "Truncated encounter image restored");
        auto priorDefinitions = fixture.definitions;
        for (auto& definition : priorDefinitions)
        {
            definition.encounter.reset(); definition.dialogues[1].mInfo.front().mResultScript = "Journal journey_t37 10";
        }
        PersonalScripts prior(7, priorDefinitions, crypto, true, true, true);
        rejects([&] { runtime.restore(prior.encode(prior.initial())); }, "Encounter silently migrated prior policy");
        PersonalScripts returning(7, fixture.definitions, crypto, true, true, true, true, enable);
        auto removal = run(returning, returning.initial(), 11);
        if (enable)
        {
            rejects([&] { returning.prepareReturn(removal.state, 11); }, "Enable admitted without content/life binding");
            for (uint64_t life : {0u, 2u, 4u})
                rejects([&] { returning.prepareReturn(removal.state, 11, &fixture.content, life); },
                    "Enable revived a stale or unavailable origin life");
            require(returning.encode(removal.state) == removal.image && fixture.actor.getPtr().getRefData().isEnabled(),
                "Rejected Enable changed private/public state");
            PersonalScripts oldReturn(7, fixture.definitions, crypto, true, true, true, true);
            rejects([&] { returning.restore(oldReturn.encode(oldReturn.initial())); }, "V88 silently migrated V87");
        }
        auto returned = returning.prepareReturn(removal.state, 11, &fixture.content, 3);
        auto expected = removal.state;
        expected[0].encounterReturned = true;
        expected[0].encounterEnabled = enable;
        require(returned.result.executed && returned.image == returning.encode(expected)
            && PersonalScripts::physicalScope(returned.state, 11) == 1
            && PersonalScripts::physicalScope(returned.state, 22) == 1,
            "Return changed story state/origin/receipt instead of only physical occupancy");
        if (enable)
            require(returned.invocations == 1 && returned.result.trace.executed > 0 && returned.result.trace.executed <= 16
                && returned.response.empty() && returned.result.presentation.empty()
                && fixture.actor.getPtr().getRefData().isEnabled(), "Return did not execute silent bounded stock Enable");
        require(returning.encode(returning.restore(returned.image)) == returned.image,
            "Dormant encounter did not restore exactly");
        require(!returning.prepareReturn(returned.state, 11).result.executed
            && run(returning, returned.state, 11, 102).result.outcome == SharedWorldScript::Outcome::Rejected
            && run(returning, returned.state, 22).result.executed,
            "Return reopened A's removed source, repeated work or consumed B's opportunity");
        rejects([&] { returning.restore(alice.image); }, "Return policy silently migrated V86 state");
        rejects([&] { runtime.restore(returned.image); }, "V86 accepted returned encounter state");
        auto impossible = returning.initial(); impossible[0].encounterReturned = true;
        rejects([&] { returning.encode(impossible); }, "Return without a durable outcome was accepted");
        for (size_t size = 0; size < returned.image.size(); ++size)
            rejects([&] { returning.restore(std::span(returned.image).first(size)); }, "Truncated returned encounter restored");
        auto malformed = returned.image;
        // Last instance's return flag is a canonical bounded word.
        malformed[malformed.size() - 8] = 2;
        rejects([&] { returning.restore(malformed); }, "Noncanonical return flag restored");
        if (enable)
        {
            auto inconsistent = returned.state; inconsistent[0].encounterEnabled = false;
            rejects([&] { returning.encode(inconsistent); }, "Occupancy restored without durable Enable");
            inconsistent = removal.state; inconsistent[0].encounterEnabled = true;
            rejects([&] { returning.encode(inconsistent); }, "Enable restored without safe occupancy");
            // Both implicit and named stock callers dispatch through the same owned writer.
            struct Source final : MWScript::ReferenceState
            {
                MWScript::ExecutionContext expected;
                MWWorld::Ptr ptr;
                int enabled = 0;
                void enable(const MWScript::ExecutionContext& context, const MWWorld::Ptr& source) override
                {
                    require(context == expected && source == ptr, "Enable accepted foreign source/context");
                    ++enabled; ptr.getRefData().enable();
                }
                void disable(const MWScript::ExecutionContext&, const MWWorld::Ptr&) override
                { throw std::invalid_argument("Only Enable admitted"); }
                bool disabled(const MWScript::ExecutionContext&, const MWWorld::Ptr&) const override
                { return !ptr.getRefData().isEnabled(); }
            } source;
            resolver.expected = removal.state[0].last; source.expected = resolver.expected;
            MWWorld::ManualRef detached(fixture.content, id("speaker_r29")); source.ptr = detached.getPtr();
            resolver.actor = source.ptr; resolver.player = foreignPlayer.getPtr();
            MWScript::InterpreterContext context(nullptr, resolver.expected, fixture.content, resolver,
                nullptr, nullptr, nullptr, nullptr, &source);
            MWScript::InterpreterContext unowned(nullptr, resolver.expected, fixture.content, resolver);
            Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
            MWScript::Misc::installOpcodes(interpreter);
            for (const auto text : {"Enable", "speaker_r29->Enable"})
            {
                const auto program = fixture.definitions[0].script->compileResult(text, true, id("speaker_r29"), true);
                source.ptr.getRefData().disable(); interpreter.run(program, context, 16);
                require(source.ptr.getRefData().isEnabled(), "Stock Enable failed to restore staged source");
                rejects([&] { interpreter.run(program, unowned, 16); }, "Full stock installer bypassed Enable ownership");
            }
            rejects([&] { interpreter.run(fixture.definitions[0].script->compileResult(
                "player->Enable", true, id("speaker_r29"), true), context, 16); }, "Enable accepted foreign player");
            require(source.enabled == 2 && fixture.actor.getPtr().getRefData().isEnabled(),
                "Enable repeated or mutated public source");
            rejects([&] { fixture.definitions[0].script->compileResult("Enable", true, id("speaker_r29")); },
                "V86/V87 admitted Enable without restoration policy");
        }
        std::cout << "story encounter: stock Disable/GetDisabled; one source/cell, discovery rollback/retry; "
            "A removes privately, B completes independently; late failure/waits, bound receipts and silent exact restart\n";
    }

    void relocation(CredentialCrypto& crypto, bool returning = false)
    {
        Fixture fixture(crypto, {}, true);
        for (auto& definition : fixture.definitions)
        {
            definition.encounter = PersonalScripts::EncounterBinding{41, id("speaker_r29"), id("room_e87")};
            definition.dialogues[1].mInfo.front().mResultScript =
                "set calls to calls + 1\nJournal journey_t37 20\nPosition 200 -32 1 5400\n"
                "set personal_counter to speaker_r29->GetPos x\n"
                "if GetAngle z != 90\nGoodbye\nendif\nMessageBox \"Relocated for this story\"";
        }
        PersonalScripts runtime(7, fixture.definitions, crypto, true, true, true, returning, false, true);
        Resolver resolver; resolver.actor = fixture.actor.getPtr();
        MWWorld::ManualRef foreignPlayer(fixture.content, id("speaker_r29")); resolver.player = foreignPlayer.getPtr();
        const auto run = [&](const PersonalScripts& policy, const PersonalScripts::State& input,
            uint64_t character, size_t budget = 16) {
            const auto& instance = *std::ranges::find_if(input, [&](const auto& value) {
                return value.last.character == character && value.last.story == 31;
            });
            resolver.expected = instance.last; ++resolver.expected.invocation;
            resolver.expected.causalEvent = instance.eventCursor + 1;
            resolver.expected.sourceReference = 41; resolver.expected.sourceLife = 3;
            return policy.prepareIntent(input, character, 101, {31, 41, 3, instance.dialogueRevision, "passage_k61", -1},
                "Room", fixture.content, resolver, budget, nullptr, {}, nullptr, JournalDate);
        };
        const auto initial = runtime.initial(); const auto before = runtime.encode(initial);
        const auto alice = run(runtime, initial, 11);
        if (!alice.result.executed) throw std::runtime_error("Relocation: " + alice.result.diagnostic);
        require(alice.invocations == 2 && calls(alice.state[0].runtime) == 1
            && alice.state[0].runtime.runtime.globals.front().mValue.getInteger() == 200
            && alice.state[0].relocation && alice.state[0].relocation->position == std::array<float, 3>{200, -32, 1}
            && std::abs(alice.state[0].relocation->yaw - 1.5707963f) < .000001f
            && alice.state[0].encounterLife == 3 && PersonalScripts::physicalScope(alice.state, 11) == 2
            && PersonalScripts::physicalScope(alice.state, 22) == 1 && !alice.state[1].relocation
            && std::ranges::count(alice.result.presentation, "Relocated for this story") == 1,
            "Scoped pose/read/rotation, discovery rollback or B's opportunity diverged");
        require(runtime.encode(initial) == before && fixture.actor.getPtr().getRefData().getPosition().pos[0] != 200,
            "Position mutated public source/bank");
        require(run(runtime, initial, 11, 1).result.outcome == SharedWorldScript::Outcome::Rejected,
            "Position retry escaped call budget");
        const auto restored = runtime.restore(alice.image);
        require(runtime.encode(restored) == alice.image && run(runtime, restored, 22).result.executed,
            "Restart lost A's destination or B's original opportunity");
        const auto duplicate = runtime.prepareIntent(restored, 11, 101, {31, 41, 3, 0, "passage_k61", -1},
            "Room", fixture.content, resolver);
        require(duplicate.result.outcome == SharedWorldScript::Outcome::Committed && !duplicate.result.executed
            && duplicate.result.presentation.empty() && duplicate.response.empty(), "Recovery replayed relocation/output");
        if (returning)
        {
            rejects([&] { runtime.prepareReturn(restored, 11, &fixture.content, 4); },
                "Relocation return accepted a replacement source life");
            const auto returned = runtime.prepareReturn(restored, 11, &fixture.content, 3);
            auto expected = restored; expected[0].encounterReturned = true;
            require(returned.result.executed && !returned.invocations && returned.result.trace.executed == 0
                && returned.result.presentation.empty() && returned.response.empty()
                && returned.image == runtime.encode(expected) && returned.state[0].relocation == restored[0].relocation
                && !returned.state[0].encounterEnabled && PersonalScripts::physicalScope(returned.state, 11) == 1
                && !PersonalScripts::relocation(returned.state, 11),
                "Return changed the private destination/origin/bank/receipts or left an active private hull");
            require(runtime.encode(runtime.restore(returned.image)) == returned.image
                && !runtime.prepareReturn(returned.state, 11).result.executed
                && run(runtime, returned.state, 11).result.outcome == SharedWorldScript::Outcome::Rejected
                && run(runtime, returned.state, 22).result.executed,
                "Return recovery replayed work, reopened A or consumed B's opportunity");
            const auto quiet = runtime.prepareIntent(returned.state, 11, 101, {31, 41, 3, 0, "passage_k61", -1},
                "Room", fixture.content, resolver);
            require(!quiet.result.executed && quiet.result.outcome == SharedWorldScript::Outcome::Committed
                && quiet.result.presentation.empty() && quiet.response.empty(), "Dormant recovery replayed output");
            PersonalScripts v89(7, fixture.definitions, crypto, true, true, true, false, false, true);
            rejects([&] { runtime.restore(v89.encode(v89.initial())); }, "V90 silently migrated V89");
            rejects([&] { v89.restore(returned.image); }, "V89 accepted dormant relocated state");
            for (size_t size = 0; size < returned.image.size(); ++size)
                rejects([&] { runtime.restore(std::span(returned.image).first(size)); },
                    "Truncated dormant destination image accepted");
        }
        for (const std::string suffix : {"\nGoodbye", "\nDisable", "\nChoice \"Pending\" 7"})
        {
            auto definitions = fixture.definitions;
            for (auto& definition : definitions) definition.dialogues[1].mInfo.front().mResultScript += suffix;
            PersonalScripts failed(7, definitions, crypto, true, true, true, false, false, true);
            const auto result = run(failed, failed.initial(), 11);
            require(result.result.outcome == SharedWorldScript::Outcome::Rejected && result.state.empty()
                && result.image.empty() && result.response.empty() && result.result.presentation.empty(),
                "Late failure/mixed outcome leaked relocation/progression/output");
        }
        for (const std::string result : {"player->Position 200 -32 1 0", "set personal_counter to player->GetPos x",
            "PositionCell 200 -32 1 0 other_room", "Position 5000 0 1 0", "Position 200 -32 1 21601",
            "Disable\nPosition 200 -32 1 0"})
        {
            auto definitions = fixture.definitions;
            for (auto& definition : definitions) definition.dialogues[1].mInfo.front().mResultScript = result;
            PersonalScripts rejected(7, definitions, crypto, true, true, true, false, false, true);
            const auto attempt = run(rejected, rejected.initial(), 11);
            require(attempt.result.outcome == SharedWorldScript::Outcome::Rejected && attempt.state.empty(),
                "Foreign source/read, cross-cell, unbounded pose or mixed result admitted");
        }
        auto named = fixture.definitions;
        for (auto& definition : named) definition.dialogues[1].mInfo.front().mResultScript =
            "speaker_r29->PositionCell 200 -32 1 5400 room_e87\nset personal_counter to GetPos x";
        PersonalScripts cell(7, named, crypto, true, true, true, false, false, true);
        require(run(cell, cell.initial(), 11).state[0].relocation == alice.state[0].relocation,
            "Named PositionCell did not resolve the same scoped source");
        resolver.wait = true;
        require(run(runtime, initial, 11).result.outcome == SharedWorldScript::Outcome::Deferred,
            "Unavailable relocation source did not defer"); resolver.wait = false;
        auto invalid = restored; invalid[0].relocation->position[0] = std::numeric_limits<float>::infinity();
        rejects([&] { runtime.encode(invalid); }, "Nonfinite durable pose accepted");
        invalid = restored; invalid[0].relocation->position[0] = 5000;
        rejects([&] { runtime.encode(invalid); }, "Unbounded durable displacement accepted");
        invalid = initial; invalid[0].relocation = restored[0].relocation;
        rejects([&] { runtime.encode(invalid); }, "Pose without source origin accepted");
        for (size_t size = 0; size < alice.image.size(); ++size)
            rejects([&] { runtime.restore(std::span(alice.image).first(size)); }, "Truncated pose image accepted");
        std::vector<char> poseBytes;
        const auto real = [&](float value) {
            const uint64_t bits = std::bit_cast<uint32_t>(value);
            for (unsigned i = 0; i < 8; ++i) poseBytes.push_back(char(bits >> (i * 8)));
        };
        for (float value : alice.state[0].relocation->origin) real(value);
        for (float value : alice.state[0].relocation->position) real(value);
        real(alice.state[0].relocation->yaw);
        const auto pose = std::search(alice.image.begin(), alice.image.end(), poseBytes.begin(), poseBytes.end());
        require(pose != alice.image.end(), "Durable pose encoding absent");
        auto malformed = alice.image;
        const auto offset = size_t(pose - alice.image.begin()) + 3 * 8;
        const auto infinite = std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity());
        for (unsigned i = 0; i < 4; ++i) malformed[offset + i] = char(infinite >> (i * 8));
        rejects([&] { runtime.restore(malformed); }, "Nonfinite pose passed recovery preflight");
        auto old = fixture.definitions;
        for (auto& definition : old) definition.dialogues[1].mInfo.front().mResultScript = "Disable";
        PersonalScripts v88(7, old, crypto, true, true, true, true, true);
        rejects([&] { runtime.restore(v88.encode(v88.initial())); }, "V89 silently migrated V88");
        rejects([&] { v88.restore(alice.image); }, "V88 accepted relocation state");
        rejects([&] { fixture.definitions[0].script->compileResult("Position 200 -32 1 0", true, id("speaker_r29")); },
            "Previous encounter admitted Position");
        std::cout << "relocation: stock implicit/named Position/PositionCell/GetPos/GetAngle; source/cell/pose bounds; "
            "discovery retry, late rollback, private destination and B's original opportunity; exact silent recovery\n";
        if (returning) std::cout << "relocation return: immutable dormant destination/origin/bank/receipts; shared occupancy; "
            "stale-life/version/truncation rejection; silent duplicate/restart and B's independent completion\n";
    }

    void sourceWaits(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto, {}, true);
        std::vector<ESM::Script> scripts;
        for (const auto& [name, body] : std::vector<std::pair<std::string, std::string>>{
            {"ledger_v63", "set calls to calls + 1\nset timer to timer + GetSecondsPassed\n"
                "set personal_counter to personal_counter + 1\nset relay_n48.calls to relay_n48.calls + 7"},
            {"relay_n48", "set calls to calls + 1\nset blocked to speaker_r29->GetDisabled"}})
        {
            ESM::Script script; script.blank(); script.mId = id(name);
            script.mScriptText = "begin " + name + "\nlong calls\nfloat timer\nshort blocked\n" + body + "\nend";
            scripts.push_back(script);
        }
        ESM::Global global; global.blank(); global.mId = id("personal_counter");
        global.mValue.setType(ESM::VT_Long); global.mValue.setInteger(0);
        auto primary = std::make_shared<SharedWorldScript>(scripts[0], std::vector{global}, "source-waits-1", crypto,
            scripts, id("speaker_r29"));
        auto relay = std::make_shared<SharedWorldScript>(scripts[1], std::vector{global}, "source-waits-1", crypto,
            scripts, id("speaker_r29"));
        std::vector<PersonalScripts::Definition> definitions;
        for (const uint64_t character : {11, 22})
            definitions.push_back({character, 31, primary, {{relay, character == 22}}, character == 22,
                records(), PersonalScripts::EncounterBinding{41, id("speaker_r29"), id("chamber_t91")}});
        PersonalScripts policy(7, definitions, crypto, true, true, true, false, false, false, true);
        auto state = policy.initial();
        struct NoFallback final : MWScript::ReferenceResolver
        {
            MWWorld::Ptr resolve(const MWScript::ExecutionContext&, const ESM::RefId&, bool) const override
            { throw std::runtime_error("Source wait fell through to live world"); }
        } resolver;
        const auto number = [](const auto& runtime, std::string_view name) {
            for (const auto& [key, value] : runtime.runtime.script.mLocals.mVariables)
                if (key == name) return value.getFloat();
            throw std::runtime_error("Wait fixture local absent");
        };
        PersonalScripts::Source source{41, 1, 0, id("speaker_r29"), true};
        const std::array<uint64_t, 1> bob{22};
        auto step = policy.prepareScheduled(state, 1, bob, fixture.content, resolver, &source);
        if (!step.result.executed) throw std::runtime_error(step.result.diagnostic);
        require(step.result.executed && step.invocations == 2 && step.state[1].clockTicks == 1
            && number(step.state[1].runtime, "calls") == 1 && number(step.state[1].members[0], "calls") == 8,
            "Living source did not use stock bank/query/clock");
        state.swap(step.state); const auto before = policy.encode(state);
        source.living = false; source.respawnTick = 20; source.killer = 11; source.killerKind = 1;
        auto waiting = policy.prepareScheduled(state, 2, bob, fixture.content, resolver, &source);
        require(waiting.result.outcome == SharedWorldScript::Outcome::Committed && waiting.result.executed
            && waiting.invocations == 2 && waiting.state[1].sourceWait && !waiting.image.empty()
            && policy.encode(state) == before && waiting.state[1].last == state[1].last
            && waiting.state[1].runtime.tick == 1 && waiting.state[1].clockTicks == 1
            && number(waiting.state[1].runtime, "calls") == 1
            && number(waiting.state[1].runtime, "timer") == 1.f / 30
            && number(waiting.state[1].members[0], "calls") == 8
            && waiting.state[1].runtime.runtime.globals.front().mValue.getInteger() == 1
            && waiting.state[1].eventCursor == 0 && waiting.state[1].dialogue.quests.empty()
            && waiting.result.presentation.empty() && waiting.state[1].runtime.runtime.script.mRunning,
            "Foreign death leaked late bank/clock/progression effects or deactivated a waiting script");
        const auto& dependency = *waiting.state[1].sourceWait;
        require(dependency.invocation.sourceReference == 41 && dependency.invocation.sourceLife == 1
            && dependency.invocation.invocation == 2 && dependency.script == id("relay_n48")
            && dependency.reason == 1 && dependency.sinceTick == 2 && dependency.respawnTick == 20,
            "Wait lost its durable invocation/reference/life/wake identity");
        state.swap(waiting.state); const auto saved = policy.encode(state);
        PersonalScripts restarted(7, definitions, crypto, true, true, true, false, false, false, true);
        state = restarted.restore(saved);
        require(restarted.encode(state) == saved, "Source wait recovery was not exact");
        auto view = PersonalScripts::view(state[1], *SessionId::fromValue(9), SessionGeneration::initial(),
            ServerTick::initial(), CanonicalRevision::initial());
        require(view.messages.empty() && view.response.empty() && view.entries.empty(),
            "Reconnect/restart replayed notices or invented quest credit");
        auto offline = restarted.prepareScheduled(state, 9, {}, fixture.content, resolver, &source);
        require(!offline.result.executed && offline.image.empty() && restarted.encode(state) == saved,
            "Offline story accrued clock time or changed its dependency");
        const auto overdueOffline = restarted.prepareScheduled(state, 40, {}, fixture.content, resolver, &source);
        require(overdueOffline.result.outcome == SharedWorldScript::Outcome::Committed
            && !overdueOffline.result.executed && overdueOffline.image.empty(),
            "An inactive overdue respawn blocked shared time or invented a new life");
        auto paused = restarted.prepareScheduled(state, 10, bob, fixture.content, resolver, &source);
        require(paused.invocations == 1 && paused.state[1].clockTicks == 1 && paused.state[1].runtime.tick == 1,
            "Waiting probe executed bytecode or advanced the story clock");
        state.swap(paused.state);
        source.living = true; source.respawnTick = 0;
        auto wrongLife = restarted.prepareScheduled(state, 11, bob, fixture.content, resolver, &source);
        require(wrongLife.state[1].sourceWait && wrongLife.state[1].runtime.tick == 1,
            "Same-life resurrection falsely satisfied a respawn dependency");
        source.life = 2; source.living = false; source.respawnTick = 30;
        auto repeated = restarted.prepareScheduled(state, 12, bob, fixture.content, resolver, &source);
        require(repeated.state[1].sourceWait->invocation.sourceLife == 2
            && repeated.state[1].sourceWait->sinceTick == 2 && repeated.state[1].clockTicks == 1,
            "Repeated foreign death lost pause duration or pinned an obsolete life");
        state.swap(repeated.state); source.life = 1;
        require(restarted.prepareScheduled(state, 13, bob, fixture.content, resolver, &source).result.outcome
            == SharedWorldScript::Outcome::Rejected, "Regressed canonical life resumed a wait");
        source.life = 3; source.living = true; source.respawnTick = 0;
        auto resumed = restarted.prepareScheduled(state, 30, bob, fixture.content, resolver, &source);
        require(resumed.result.executed && resumed.invocations == 2 && !resumed.state[1].sourceWait
            && resumed.state[1].last.sourceLife == 3 && resumed.state[1].last.invocation == 2
            && resumed.state[1].clockTicks == 2 && number(resumed.state[1].runtime, "calls") == 2
            && number(resumed.state[1].runtime, "timer") == 2.f / 30
            && number(resumed.state[1].members[0], "calls") == 16
            && resumed.state[1].dialogue.quests.empty() && resumed.state[1].eventCursor == 0,
            "New life did not resume the same occurrence once, or wait time became story time/quest credit");
        state.swap(resumed.state);
        auto duplicate = restarted.prepareScheduled(state, 30, bob, fixture.content, resolver, &source);
        require(!duplicate.result.executed && duplicate.image.empty(), "Resumed occurrence replayed");
        auto malformed = restarted.restore(saved); malformed[1].sourceWait->reason = 2;
        rejects([&] { restarted.encode(malformed); }, "Unknown wake condition accepted");
        malformed = restarted.restore(saved); malformed[1].sourceWait->invocation.sourceReference = 42;
        rejects([&] { restarted.encode(malformed); }, "Different placement accepted a source dependency");
        rejects([&] { restarted.restore(std::span(saved).first(saved.size() - 1)); }, "Truncated source wait accepted");
        PersonalScripts previous(7, fixture.definitions, crypto, true, true, true);
        rejects([&] { previous.restore(saved); }, "Prior policy silently restored source waits");
        source = {41, 1, 20, id("speaker_r29"), false, 22, 1};
        const auto own = policy.prepareScheduled(policy.initial(), 2, bob, fixture.content, resolver, &source);
        require(own.result.outcome == SharedWorldScript::Outcome::Committed && !own.state[1].sourceWait
            && own.state[1].dialogue.entries.empty() && own.state[1].eventCursor == 0
            && number(own.state[1].members[0], "blocked") == 0,
            "Own death was invented as a foreign wait, disable result or quest credit");
        source.killer = 0; source.killerKind = 0;
        require(policy.prepareScheduled(policy.initial(), 2, bob, fixture.content, resolver, &source).result.outcome
            == SharedWorldScript::Outcome::Rejected, "Ambiguous death was invented as a foreign wait");
        auto unsupported = scripts[0]; unsupported.mScriptText = "begin ledger_v63\nshort credit\nset credit to OnDeath\nend";
        rejects([&] { SharedWorldScript invalid(unsupported, {global}, "source-waits-1", crypto, scripts, id("speaker_r29")); },
            "Unattributed death-credit API admitted");
        std::cout << "source waits: late-bank rollback; durable dependency/paused stock timer; bounded probes; "
            "foreign/own/ambiguous and stale-life checks; repeated death; correct-life single resume; silent exact recovery, no quest credit\n";
    }

    void stock(CredentialCrypto& crypto)
    {
        Fixture fixture(crypto);
        struct Journal final : MWDialogue::Journal
        {
            int index = 0, entries = 0;
            MWWorld::Ptr actor;
            void addEntry(const ESM::RefId& quest, int value, const MWWorld::Ptr& speaker) override
            { require(quest == id("journey_t37"), "Stock Journal ID changed"); index = value; ++entries; actor = speaker; }
            void setJournalIndex(const ESM::RefId& quest, int value) override
            { require(quest == id("journey_t37"), "Stock SetJournalIndex ID changed"); index = value; }
            int getJournalIndex(const ESM::RefId& quest) const override
            { require(quest == id("journey_t37"), "Stock GetJournalIndex ID changed"); return index; }
        } journal;
        MWBase::Environment environment; environment.setJournal(journal);
        const auto& runtime = *fixture.definitions.front().script;
        auto initial = runtime.initial();
        auto locals = MWScript::Locals::restore(initial.runtime.script.mLocals, initial.runtime.script.mId, runtime.declarations());
        MWScript::InterpreterContext context(&locals, fixture.actor.getPtr());
        environment.setFrameDuration(.125f);
        require(context.getSecondsPassed() == .125f, "Stock clock stopped reading the current frame duration");
        require(context.getDialogueState() == nullptr, "Stock caller unexpectedly acquired staged dialogue");
        const auto program = runtime.compileResult("Journal journey_t37 10\nSetJournalIndex journey_t37 5\n"
            "set calls to GetJournalIndex journey_t37");
        Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter); MWScript::Dialogue::installOpcodes(interpreter);
        interpreter.run(program, context, 4096);
        require(journal.entries == 1 && journal.index == 5 && locals.mLongs.front() == 5 && journal.actor == fixture.actor.getPtr(),
            "Stock Journal/SetJournalIndex/GetJournalIndex caller dispatch changed");
        Resolver resolver; resolver.expected = {MWScript::ExecutionContext::Ownership::Character, 7, 31, 1, 11, 41, 3, 1, 54};
        resolver.actor = fixture.actor.getPtr();
        MWScript::InterpreterContext scoped(&locals, resolver.expected, fixture.content, resolver);
        rejects([&] { (void)scoped.getSecondsPassed(); }, "Unadmitted scoped clock fell through to shared frame time");
        rejects([&] { interpreter.run(runtime.compileResult("FillJournal"), scoped, 4096); },
            "Full stock installer let a scoped caller bypass staged dialogue services");
        MWScript::Stats::installOpcodes(interpreter);
        rejects([&] { interpreter.run(runtime.compileResult("ModDisposition 17"), scoped, 4096); },
            "Full stock installer bypassed staged NPC result ownership");
        MWScript::Misc::installOpcodes(interpreter);
        rejects([&] { interpreter.run(runtime.compileResult("Disable", true, id("speaker_r29")), scoped, 4096); },
            "Full stock installer bypassed staged physical ownership");
        rejects([&] { interpreter.run(runtime.compileResult("set calls to GetDisabled", true), scoped, 4096); },
            "Full stock installer read unscoped physical state for a scoped caller");
        fixture.actor.getPtr().getRefData().disable();
        interpreter.run(runtime.compileResult("set calls to GetDisabled", true), context, 4096);
        require(locals.mLongs.front() == 1, "Stock GetDisabled stopped reading source RefData");
        fixture.actor.getPtr().getRefData().enable();
        ESM::Creature creature; creature.blank(); creature.mId = id("speaker_creature_y72");
        fixture.content.insertStatic(creature);
        MWWorld::ManualRef nonNpc(fixture.content, creature.mId);
        MWScript::InterpreterContext creatureContext(&locals, nonNpc.getPtr());
        interpreter.run(runtime.compileResult("ModDisposition 17"), creatureContext, 4096);
        require(journal.entries == 1 && journal.index == 5, "Unsupported scoped stock service mutated journal");
        std::cout << "dialogue stock: unchanged opcodes route unscoped callers to the stock journal and source actor\n";
    }
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: tes3mp_native_dialogue_tests isolation|rollback|recovery|stock|intents|client|player-stats|disposition|npc-state|encounter|enable|substitutions|journal");
        MWClass::registerClasses(); auto crypto = TES3MP::makeProductionCredentialCrypto(); require(bool(crypto), "Crypto unavailable");
        const std::string_view filter = argv[1];
        if (filter == "isolation") isolation(*crypto);
        else if (filter == "rollback") rollback(*crypto);
        else if (filter == "recovery") recovery(*crypto);
        else if (filter == "intents")
            intents(*crypto);
        else if (filter == "client")
            client(*crypto);
        else if (filter == "player-stats")
            playerStats(*crypto);
        else if (filter == "disposition")
            disposition(*crypto);
        else if (filter == "npc-state") disposition(*crypto, true);
        else if (filter == "encounter") encounter(*crypto);
        else if (filter == "enable") encounter(*crypto, true);
        else if (filter == "relocation") relocation(*crypto);
        else if (filter == "relocation-return") relocation(*crypto, true);
        else if (filter == "source-waits") sourceWaits(*crypto);
        else if (filter == "substitutions")
            substitutions(*crypto);
        else if (filter == "journal") journal(*crypto);
        else if (filter == "stock") stock(*crypto);
        else throw std::invalid_argument("Unknown dialogue filter");
        std::cout << "PASS dialogue " << filter << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
