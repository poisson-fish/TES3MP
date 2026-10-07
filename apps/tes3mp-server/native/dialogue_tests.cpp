#include "personal_script.hpp"
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwdialogue/journalimp.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/dialogueextensions.hpp>
#include <apps/openmw/mwscript/locals.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadscpt.hpp>
#include <iostream>

namespace
{
    using namespace TES3MP;
    using namespace TES3MP::Native;
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
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
        static MWWorld::ESMStore& populate(MWWorld::ESMStore& content)
        {
            ESM::NPC npc; npc.blank(); npc.mId = id("speaker_r29"); npc.mScript = id("ledger_v63");
            content.insertStatic(npc); return content;
        }
        Fixture(CredentialCrypto& crypto, std::string late = {}) : actor(populate(content), id("speaker_r29"))
        {
            ESM::Global global; global.blank(); global.mId = id("personal_counter");
            global.mValue.setType(ESM::VT_Long); global.mValue.setInteger(0);
            std::vector<ESM::Script> bank;
            for (const char* name : {"ledger_v63", "relay_n48"})
            {
                ESM::Script script; script.blank(); script.mId = id(name);
                script.mScriptText = std::string("begin ") + name + "\nlong calls\nend\n";
                bank.push_back(script); content.insertStatic(script);
            }
            auto first = std::make_shared<SharedWorldScript>(bank[0], std::vector{global}, "dialogue-content-1", crypto, bank);
            auto second = std::make_shared<SharedWorldScript>(bank[1], std::vector{global}, "dialogue-content-1", crypto, bank);
            for (const auto [character, story] : {std::pair{11u, 31u}, {22u, 31u}, {11u, 32u}})
                definitions.push_back({character, story, first, {{second, false}}, false, records(late)});
        }
    };
    struct Resolver final : MWScript::ReferenceResolver
    {
        MWScript::ExecutionContext expected;
        MWWorld::Ptr actor;
        bool wait = false;
        MWWorld::Ptr resolve(const MWScript::ExecutionContext& execution, const ESM::RefId& reference, bool) const override
        {
            require(execution == expected, "Dialogue lookup lost invocation identity");
            require(execution.sourceReference == 41 && execution.sourceLife == 3, "Stale source/life accepted");
            if (reference == id("ledger_v63") && wait) throw MWScript::ScriptContextUnavailable();
            if (reference.empty() || reference == id("ledger_v63")) return actor;
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
        rejects([&] { interpreter.run(runtime.compileResult("FillJournal"), scoped, 4096); },
            "Full stock installer let a scoped caller bypass staged dialogue services");
        require(journal.entries == 1 && journal.index == 5, "Unsupported scoped stock service mutated journal");
        std::cout << "dialogue stock: unchanged opcodes route unscoped callers to the stock journal and source actor\n";
    }
}
int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: tes3mp_native_dialogue_tests isolation|rollback|recovery|stock");
        MWClass::registerClasses(); auto crypto = TES3MP::makeProductionCredentialCrypto(); require(bool(crypto), "Crypto unavailable");
        const std::string_view filter = argv[1];
        if (filter == "isolation") isolation(*crypto);
        else if (filter == "rollback") rollback(*crypto);
        else if (filter == "recovery") recovery(*crypto);
        else if (filter == "stock") stock(*crypto);
        else throw std::invalid_argument("Unknown dialogue filter");
        std::cout << "PASS dialogue " << filter << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
