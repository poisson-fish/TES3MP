#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwlua/context.hpp>
#include <apps/openmw/mwlua/globalscripts.hpp>
#include <apps/openmw/mwlua/localscripts.hpp>
#include <apps/openmw/mwlua/luaevents.hpp>
#include <apps/openmw/mwlua/mwscriptbindings.hpp>
#include <apps/openmw/mwlua/objectbindings.hpp>
#include <apps/openmw/mwlua/userdataserializer.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/locals.hpp>
#include <apps/openmw/mwscript/miscextensions.hpp>
#include <apps/openmw/mwscript/ref.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwscript/statsextensions.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <components/compiler/context.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/compiler/scanner.hpp>
#include <components/esm/luascripts.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/locals.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/globalscript.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/localopcodes.hpp>
#include <components/interpreter/runtime.hpp>
#include <components/lua/asyncpackage.hpp>
#include <components/lua/configuration.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/scriptscontainer.hpp>
#include <components/lua/serialization.hpp>
#include <components/lua/storage.hpp>
#include <components/lua/utilpackage.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/file.hpp>
#include <components/vfs/manager.hpp>

#include <array>
#include <iostream>
#include <limits>
#include <sstream>

namespace
{
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class F> void rejects(F&& operation, const char* message)
    {
        try { operation(); } catch (const std::exception&) { return; }
        throw std::runtime_error(message);
    }
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
    MWScript::ExecutionContext execution(uint64_t character)
    { return {MWScript::ExecutionContext::Ownership::Character, 7, character + 100, character + 200,
        character, 23, 2, 41, 53}; }

    struct Content
    {
        MWWorld::ESMStore store;
        Content()
        {
            ESM::Race race; race.blank(); race.mId = id("fixture_race"); race.mName = "Race";
            store.insertStatic(race);
            ESM::Class characterClass; characterClass.blank(); characterClass.mId = id("fixture_class");
            characterClass.mName = "Class"; store.insertStatic(characterClass);
            ESM::Faction faction; faction.blank(); faction.mId = id("fixture_faction"); faction.mName = "Faction";
            for (int i = 0; i < 10; ++i) faction.mRanks[i] = "Rank " + std::to_string(i);
            store.insertStatic(faction);
            for (const char* name : {"player_a", "player_b", "fixture_actor"})
            {
                ESM::NPC npc; npc.blank(); npc.mId = id(name); npc.mName = name;
                npc.mRace = race.mId; npc.mClass = characterClass.mId; npc.mFaction = faction.mId;
                store.insertStatic(npc);
            }
        }
    };

    struct Resolver final : MWScript::ReferenceResolver
    {
        MWScript::ExecutionContext expected;
        MWWorld::Ptr player, source;
        bool unavailable = false;
        mutable bool lastActiveOnly = false;
        mutable unsigned calls = 0;
        Resolver(MWScript::ExecutionContext context, MWWorld::Ptr character, MWWorld::Ptr actor)
            : expected(context), player(character), source(actor) {}
        MWWorld::Ptr resolve(const MWScript::ExecutionContext& context, const ESM::RefId& reference,
            bool activeOnly) const override
        {
            ++calls; lastActiveOnly = activeOnly;
            require(context == expected, "Lookup lost campaign/story/scope/character/source/life/invocation/event");
            if (unavailable) throw MWScript::ScriptContextUnavailable();
            if (reference == "player") return player;
            if (reference.empty() || reference == "fixture_actor") return source;
            return {};
        }
    };

    void lookup()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b"));
        // Identical record names can denote different physical instances.
        MWWorld::ManualRef actorA(content.store, id("fixture_actor")), actorB(content.store, id("fixture_actor"));
        Resolver resolverA(execution(1), a.getPtr(), actorA.getPtr()), resolverB(execution(2), b.getPtr(), actorB.getPtr());
        MWMechanics::NpcStats statsA(content.store), statsB(content.store);
        statsA.setBounty(11); statsB.setBounty(29);
        MWScript::InterpreterContext contextA(nullptr, execution(1), content.store, resolverA, &statsA);
        MWScript::InterpreterContext contextB(nullptr, execution(2), content.store, resolverB, &statsB);
        Interpreter::Program program; program.mStrings = {"player", "fixture_actor", "absent", "", std::string(129, 'x')};
        Interpreter::Runtime runtime;
        for (auto* context : {&contextA, &contextB})
        {
            const bool first = context == &contextA;
            runtime.configure(program, *context);
            runtime.push(0);
            require(MWScript::ExplicitRef()(runtime) == (first ? a.getPtr() : b.getPtr()), "Explicit player crossed characters");
            runtime.push(1);
            require(MWScript::ExplicitRef()(runtime, true, true) == (first ? actorA.getPtr() : actorB.getPtr()),
                "Explicit source crossed physical scopes");
            require((first ? resolverA : resolverB).lastActiveOnly, "Explicit lookup lost active-only policy");
            require(MWScript::ImplicitRef()(runtime, true, false) == (first ? actorA.getPtr() : actorB.getPtr())
                && !(first ? resolverA : resolverB).lastActiveOnly, "Implicit lookup bypassed scoped resolver");
            runtime.push(2); require(MWScript::ExplicitRef()(runtime, false).isEmpty(), "Optional absence fabricated a reference");
            runtime.push(2); rejects([&] { MWScript::ExplicitRef()(runtime); }, "Required absent reference accepted");
            runtime.push(3); require(MWScript::ExplicitRef()(runtime, false).isEmpty(), "Empty explicit name became implicit source");
            runtime.push(4); rejects([&] { MWScript::ExplicitRef()(runtime); }, "Oversized literal lookup admitted");
        }
        require(contextA.getPCName() == "player_a" && contextB.getPCName() == "player_b"
            && contextA.getPCRace() == "Race" && contextB.getPCClass() == "Class"
            && contextA.getPCBounty() == 11 && contextB.getPCBounty() == 29, "Personal text used the stock protagonist");
        statsA.joinFaction(id("fixture_faction")); statsA.setFactionRank(id("fixture_faction"), 3, content.store);
        require(contextA.getPCRank() == "Rank 3" && contextB.getPCRank() == "Rank 0"
            && contextA.getPCNextRank() == "Rank 4" && contextB.getPCNextRank() == "Rank 0",
            "Personal rank text ignored staged stats or stock defaults");
        rejects([&] { contextA.getGlobalLong("foreign"); }, "Scoped globals fell back to world state");
        rejects([&] { contextA.setGlobalLong("foreign", 9); }, "Scoped globals wrote world state");
        rejects([&] { contextA.getMemberLong(id("foreign"), "value", true); }, "Member locals fell back to global scripts");
        rejects([&] { contextA.setMemberLong(id("fixture_actor"), "value", 9, false); }, "Member locals wrote unstaged references");
        rejects([&] { contextA.messageBox("unstaged", {}); }, "Scoped output bypassed transaction");
        rejects([&] { contextA.executeActivation(actorA.getPtr(), a.getPtr()); }, "Scoped activation bypassed transaction");
        resolverA.unavailable = true;
        bool deferred = false;
        try { contextA.resolveReference(id("absent"), false); }
        catch (const MWScript::ScriptContextUnavailable&) { deferred = true; }
        require(deferred, "Unavailable optional lookup became absence");
        resolverA.unavailable = false;
        auto staleLife = execution(1); ++staleLife.sourceLife;
        MWScript::InterpreterContext stale(nullptr, staleLife, content.store, resolverA);
        rejects([&] { stale.getReference(); }, "Stale source life accepted");
        rejects([&] { contextA.resolveReference(id(std::string(129, 'x'))); }, "Oversized reference lookup admitted");
        {
            MWWorld::ManualRef temporary(content.store, id("fixture_actor"));
            resolverA.source = temporary.getPtr();
        }
        rejects([&] { contextA.getReference(false); }, "Destroyed scoped source returned to caller");

        auto shared = execution(1); shared.ownership = MWScript::ExecutionContext::Ownership::Shared;
        shared.character = shared.story = 0;
        Resolver sharedResolver(shared, {}, actorA.getPtr());
        MWScript::InterpreterContext sharedContext(nullptr, shared, content.store, sharedResolver);
        rejects([&] { sharedContext.getPlayer(); }, "Shared script acquired a character");
        rejects([&] { sharedContext.getPlayerStats(); }, "Shared script acquired player stats");
        rejects([&] { sharedContext.getPCRank(); }, "Shared script acquired personal rank text");
        rejects([&] { MWScript::InterpreterContext invalid(nullptr, shared, content.store, sharedResolver, &statsA); },
            "Shared script admitted personal stats");
        require(sharedResolver.calls == 0, "Shared player rejection reached the resolver");
        auto global = execution(1); global.sourceReference = global.sourceLife = 0;
        Resolver globalResolver(global, a.getPtr(), {});
        MWScript::InterpreterContext globalContext(nullptr, global, content.store, globalResolver);
        require(globalContext.getReference(false).isEmpty() && globalContext.getPlayer() == a.getPtr(),
            "Personal global script lost source-free character binding");
        rejects([&] { globalContext.getPlayerStats(); }, "Scoped stats borrowed a live character writer");
        for (int field = 0; field < 6; ++field)
        {
            auto invalid = execution(1);
            if (field == 0) invalid.campaign = 0;
            if (field == 1) invalid.story = 0;
            if (field == 2) invalid.physicalScope = 0;
            if (field == 3) invalid.character = 0;
            if (field == 4) invalid.sourceLife = 0;
            if (field == 5) invalid.invocation = 0;
            rejects([&] { MWScript::InterpreterContext bad(nullptr, invalid, content.store, resolverA); },
                "Incomplete context admitted");
        }
    }

    class CompilerContext final : public Compiler::Context
    {
        const Compiler::Locals* mMembers;
    public:
        explicit CompilerContext(const Compiler::Locals* members = nullptr) : mMembers(members) {}
        bool canDeclareLocals() const override { return true; }
        char getGlobalType(const std::string&) const override { return ' '; }
        std::pair<char, bool> getMemberType(const std::string& name, const ESM::RefId& script) const override
        {
            return {mMembers && (script == "ledger_x73" || script == "relay_q19") ? mMembers->getType(name) : ' ', false};
        }
        bool isId(const ESM::RefId& name) const override
        {
            return name == "player" || name == "fixture_actor" || name == "absent"
                || (mMembers && (name == "ledger_x73" || name == "relay_q19"));
        }
    };
    class Errors final : public Compiler::ErrorHandler
    {
        void report(const std::string&, const Compiler::TokenLoc&, Type) override {}
        void report(const std::string&, Type) override {}
    };
    struct Compiled
    {
        Interpreter::Program program;
        Compiler::Locals declarations;
    };
    Compiled compile(std::string_view body, const Compiler::Locals* members = nullptr)
    {
        Compiler::Extensions extensions;
        extensions.registerInstruction("pcjoinfaction", "/S", Compiler::Stats::opcodePCJoinFaction,
            Compiler::Stats::opcodePCJoinFactionExplicit);
        extensions.registerInstruction("pcraiserank", "/S", Compiler::Stats::opcodePCRaiseRank,
            Compiler::Stats::opcodePCRaiseRankExplicit);
        extensions.registerFunction("getpcrank", 'l', "/S", Compiler::Stats::opcodeGetPCRank,
            Compiler::Stats::opcodeGetPCRankExplicit);
        extensions.registerFunction("scriptrunning", 'l', "c", Compiler::Misc::opcodeScriptRunning);
        extensions.registerInstruction("startscript", "c", Compiler::Misc::opcodeStartScript,
            Compiler::Misc::opcodeStartScriptExplicit);
        extensions.registerInstruction("stopscript", "c", Compiler::Misc::opcodeStopScript);
        CompilerContext context(members); context.setExtensions(&extensions);
        Errors errors; errors.setWarningsMode(2);
        Compiler::FileParser parser(errors, context);
        std::istringstream input("Begin arbitrary_context\nshort phase\n" + std::string(body) + "\nEnd arbitrary_context\n");
        Compiler::Scanner scanner(errors, input, &extensions); scanner.scan(parser);
        require(errors.isGood() && errors.countWarnings() == 0, "Fixture script did not compile strictly");
        return {parser.getProgram(), parser.getLocals()};
    }
    struct StagedContext final : MWScript::InterpreterContext
    {
        std::vector<std::string>& output;
        StagedContext(MWScript::Locals& locals, MWScript::ExecutionContext execution, const MWWorld::ESMStore& content,
            const Resolver& resolver, MWMechanics::NpcStats& stats, std::vector<std::string>& messages)
            : InterpreterContext(&locals, execution, content, resolver, &stats), output(messages) {}
        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
        { require(buttons.empty() && output.size() < 8 && message.size() <= 256, "Fixture output exceeded bounds"); output.emplace_back(message); }
    };

    void rollback()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b")), actor(content.store, id("fixture_actor"));
        Resolver resolver(execution(1), a.getPtr(), actor.getPtr());
        auto committedA = std::make_unique<MWMechanics::NpcStats>(content.store);
        MWMechanics::NpcStats committedB(content.store);
        std::vector<std::string> published;
        const auto good = compile("set phase to phase + 1\nplayer->PCJoinFaction \"fixture_faction\"\n"
            "fixture_actor->PCRaiseRank \"fixture_faction\"\nMessageBox \"committed\"");
        const auto bad = compile("set phase to phase + 1\nPCJoinFaction \"fixture_faction\"\n"
            "MessageBox \"discarded\"\nset phase to absent->GetPCRank");
        ESM::Variant zero; zero.setType(ESM::VT_Int); zero.setInteger(0);
        ESM::Locals initial; initial.mVariables.emplace_back("phase", zero);
        auto committedLocals = MWScript::Locals::restore(initial, id("arbitrary_context"), good.declarations);
        enum class Outcome { Committed, Deferred, Rejected };
        auto transact = [&](const Compiled& script, bool durable) {
            auto locals = committedLocals;
            auto stats = std::make_unique<MWMechanics::NpcStats>(*committedA);
            std::vector<std::string> messages;
            StagedContext context(locals, execution(1), content.store, resolver, *stats, messages);
            Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
            MWScript::Stats::installOpcodes(interpreter);
            try { interpreter.run(script.program, context, 256); }
            catch (const MWScript::ScriptContextUnavailable&) { return Outcome::Deferred; }
            catch (const std::exception&) { return Outcome::Rejected; }
            if (!durable) return Outcome::Deferred;
            committedLocals = std::move(locals); committedA.swap(stats); published.swap(messages);
            return Outcome::Committed;
        };
        require(transact(bad, true) == Outcome::Rejected && committedLocals.mShorts[0] == 0
            && committedA->getFactionRanks().empty() && published.empty(), "Rejected stock caller leaked staged changes/output");
        require(transact(good, false) == Outcome::Deferred && committedLocals.mShorts[0] == 0
            && committedA->getFactionRanks().empty() && published.empty(), "Failed durability leaked staged changes/output");
        resolver.unavailable = true;
        require(transact(good, true) == Outcome::Deferred && committedLocals.mShorts[0] == 0
            && published.empty(), "Dependency wait leaked staged locals");
        resolver.unavailable = false;
        require(transact(good, true) == Outcome::Committed && committedLocals.mShorts[0] == 1
            && committedA->getFactionRank(id("fixture_faction")) == 1 && published == std::vector<std::string>{"committed"}
            && committedB.getFactionRanks().empty(), "Retry did not publish one isolated stock result");
        ESM::Locals saved;
        require(committedLocals.write(saved, good.declarations), "Committed locals did not serialize with stock declarations");
        auto recovered = MWScript::Locals::restore(saved, id("arbitrary_context"), good.declarations);
        require(recovered.mShorts == committedLocals.mShorts, "Scoped locals failed stock recovery");
    }

    void stock()
    {
        Content content;
        MWWorld::ManualRef actor(content.store, id("fixture_actor")), player(content.store, id("player_a"));
        const auto script = compile("set phase to phase + 1");
        MWScript::Locals locals; locals.mShorts = {4};
        MWScript::InterpreterContext context(&locals, actor.getPtr());
        Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
        interpreter.run(script.program, context); // Stock unlimited execution remains supported.
        require(!context.getExecutionContext() && !context.hasExplicitPlayer() && locals.mShorts[0] == 5
            && context.getReference() == actor.getPtr() && context.getTarget() == id("fixture_actor"), "Stock local caller changed");
        MWMechanics::NpcStats stats(content.store);
        MWScript::InterpreterContext detached(&locals, actor.getPtr(), player.getPtr(), content.store, &stats);
        require(detached.resolveReference(id("player")) == player.getPtr() && detached.getReference() == actor.getPtr(),
            "Detached migration lost player/source binding");
        rejects([&] { detached.resolveReference(id("fixture_actor")); }, "Detached lookup reached an unscoped world");
        MWScript::InterpreterContext empty(nullptr, {});
        require(empty.getReference(false).isEmpty() && empty.getTarget().empty(), "Stock optional implicit reference changed");
        rejects([&] { empty.getReference(); }, "Stock missing implicit reference accepted");
    }

    MWScript::ExecutionContext sourceFree(uint64_t character)
    {
        auto result = execution(character); result.sourceReference = result.sourceLife = 0;
        return result;
    }
    struct Bank final : MWScript::ScriptState
    {
        MWScript::ExecutionContext expected;
        Compiler::Locals declarations;
        std::array<MWScript::GlobalScriptDesc, 2> scripts;
        bool unavailable = false;
        unsigned calls = 0;
        explicit Bank(MWScript::ExecutionContext context) : expected(context)
        {
            declarations.declare('s', "token"); declarations.declare('l', "calls"); declarations.declare('f', "phase");
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                ESM::Locals values;
                for (const char type : {'s', 'l', 'f'})
                    for (const auto& name : std::as_const(declarations).get(type))
                    {
                        ESM::Variant zero; zero.setType(type == 'f' ? ESM::VT_Float : ESM::VT_Int);
                        values.mVariables.emplace_back(name, zero);
                    }
                scripts[i].mLocals = MWScript::Locals::restore(values, id(i ? "relay_q19" : "ledger_x73"), declarations);
            }
        }
        Binding resolve(const MWScript::ExecutionContext& context, const ESM::RefId& script) override
        {
            ++calls;
            require(context == expected, "Script state lost an execution identity or crossed ownership");
            if (unavailable) throw MWScript::ScriptContextUnavailable();
            require(script == "ledger_x73" || script == "relay_q19", "Script lacks explicit bank ownership");
            return {scripts[script == "relay_q19" ? 1 : 0], declarations};
        }
        std::string save() const
        {
            std::ostringstream output(std::ios::binary);
            ESM::ESMWriter writer; writer.setVersion(ESM::VER_130); writer.setType(0);
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion); writer.setRecordCount(2); writer.save(output);
            for (const auto& entry : scripts)
            {
                ESM::GlobalScript value; value.mId = entry.mLocals.getScriptId(); value.mRunning = entry.mRunning;
                require(entry.mLocals.write(value.mLocals, declarations), "Bank locals failed stock serialization");
                writer.startRecord("GSCR"); value.save(writer); writer.endRecord("GSCR");
            }
            writer.close(); return output.str();
        }
        void restore(const std::string& saved)
        {
            ESM::ESMReader reader; reader.open(std::make_unique<std::istringstream>(saved, std::ios::binary), {});
            auto staged = scripts;
            for (auto& entry : staged)
            {
                require(reader.getRecName() == ESM::fourCC("GSCR"), "Missing stock bank record"); reader.getRecHeader();
                ESM::GlobalScript value; value.load(reader);
                require(value.mId == entry.mLocals.getScriptId() && value.mTargetId.empty() && !value.mTargetRef.isSet(),
                    "Bank recovery lost identity or acquired a physical target");
                entry.mLocals = MWScript::Locals::restore(value.mLocals, value.mId, declarations);
                entry.mRunning = value.mRunning;
            }
            require(!reader.hasMoreRecs(), "Unexpected bank recovery record"); scripts.swap(staged);
        }
    };
    struct BankContext final : MWScript::InterpreterContext
    {
        std::vector<std::string> messages;
        BankContext(MWScript::Locals* locals, Bank& bank, const Content& content, const Resolver& resolver)
            : InterpreterContext(locals, bank.expected, content.store, resolver, nullptr, &bank) {}
        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
        { require(buttons.empty() && messages.size() < 8 && message.size() <= 256, "Bank output exceeded bounds"); messages.emplace_back(message); }
    };
    void run(const Compiled& script, MWScript::InterpreterContext& context)
    {
        Interpreter::Interpreter interpreter; Interpreter::installOpcodes(interpreter);
        MWScript::Misc::installOpcodes(interpreter); interpreter.run(script.program, context, 256);
    }
    constexpr std::string_view BankCalls = "set phase to phase + 1\n"
        "set ledger_x73.token to ledger_x73.token + 2\nset ledger_x73.calls to ledger_x73.calls + 5\n"
        "set ledger_x73.phase to ledger_x73.phase + 0.25\nStartScript ledger_x73\n"
        "set phase to ScriptRunning ledger_x73\nStopScript ledger_x73\nStartScript relay_q19\n"
        "set phase to phase + ScriptRunning relay_q19\nMessageBox \"committed\"";

    void crossScript()
    {
        Content content;
        Bank alice(sourceFree(1)), bob(sourceFree(2)), otherStory(sourceFree(1)); ++otherStory.expected.story;
        Resolver resolver(alice.expected, {}, {});
        MWScript::Locals caller; caller.mShorts = {0};
        BankContext context(&caller, alice, content, resolver), peer(nullptr, bob, content, resolver),
            story(nullptr, otherStory, content, resolver);
        const auto peerBefore = bob.save(), storyBefore = otherStory.save();
        const auto script = compile(BankCalls, &alice.declarations); run(script, context);
        require(caller.mShorts[0] == 2 && context.getMemberShort(id("ledger_x73"), "token", true) == 2
            && context.getMemberLong(id("ledger_x73"), "calls", true) == 5
            && context.getMemberFloat(id("ledger_x73"), "phase", true) == .25f
            && !context.scriptRunning("ledger_x73") && context.scriptRunning("relay_q19")
            && !peer.scriptRunning("relay_q19") && !story.scriptRunning("relay_q19")
            && bob.save() == peerBefore && otherStory.save() == storyBefore && resolver.calls == 0,
            "Stock cross-script calls executed inline, resolved physics, or crossed character/story ownership");
        context.startScript("ledger_x73", {}); context.startScript("ledger_x73", {});
        context.stopScript("ledger_x73"); context.stopScript("ledger_x73");
        require(context.getMemberLong(id("ledger_x73"), "calls", true) == 5, "Start/stop reset script locals");
        context.setMemberShort(id("ledger_x73"), "token", 65537, true);
        require(context.getMemberShort(id("ledger_x73"), "token", true) == 1, "Member short lost stock conversion");
        const auto before = alice.save();
        for (const std::string& name : {std::string(), std::string(129, 'x'), std::string("bad\0name", 8), std::string("foreign")})
        {
            const auto calls = alice.calls;
            rejects([&] { context.scriptRunning(name); }, "Unbounded/foreign running query accepted");
            rejects([&] { context.startScript(name, {}); }, "Unbounded/foreign start accepted");
            rejects([&] { context.stopScript(name); }, "Unbounded/foreign stop accepted");
            rejects([&] { context.getMemberLong(id("ledger_x73"), name, true); }, "Unbounded/unknown member accepted");
            if (name != "foreign") require(alice.calls == calls, "Unbounded name reached the bank resolver");
        }
        rejects([&] { context.getMemberLong(id("ledger_x73"), "token", true); }, "Mistyped member accepted");
        rejects([&] { context.setMemberFloat(id("ledger_x73"), "phase", std::numeric_limits<float>::infinity(), true); },
            "Nonfinite member accepted");
        rejects([&] { context.setMemberLong(id("ledger_x73"), "calls", 9, false); }, "Physical member operation accepted");
        for (int field = 0; field < 9; ++field)
        {
            auto foreign = alice.expected;
            if (field == 0) ++foreign.campaign;
            if (field == 1) ++foreign.story;
            if (field == 2) ++foreign.physicalScope;
            if (field == 3) ++foreign.character;
            if (field == 4) foreign.sourceReference = foreign.sourceLife = 9;
            if (field == 5) ++foreign.invocation;
            if (field == 6) ++foreign.causalEvent;
            if (field == 7) { foreign.ownership = MWScript::ExecutionContext::Ownership::Shared; foreign.character = foreign.story = 0; }
            if (field == 8) { foreign.sourceReference = 9; foreign.sourceLife = 10; }
            MWScript::InterpreterContext invalid(nullptr, foreign, content.store, resolver, nullptr, &alice);
            rejects([&] { invalid.setMemberLong(id("ledger_x73"), "calls", 9, true); }, "Foreign member write accepted");
            rejects([&] { invalid.startScript("ledger_x73", {}); }, "Foreign lifecycle write accepted");
        }
        require(alice.save() == before, "Rejected cross-script operation changed the bank");
        // Exercise core literal conversion, rather than interning malformed IDs
        // in the fixture before calling the scoped context.
        Interpreter::Program literals; literals.mStrings = {"calls", std::string(129, 'x')};
        Interpreter::Runtime runtime; const auto calls = alice.calls;
        runtime.configure(literals, context); runtime.push(0); runtime.push(1);
        rejects([&] { Interpreter::OpFetchMemberLong<true>().execute(runtime); }, "Oversized fetch literal accepted");
        runtime.configure(literals, context); runtime.push(0); runtime.push(1); runtime.push(9);
        rejects([&] { Interpreter::OpStoreMemberLong<true>().execute(runtime); }, "Oversized store literal accepted");
        require(alice.calls == calls && alice.save() == before, "Unbounded core literal reached script bank");
        Bank maximum(alice.expected);
        for (size_t i = 3; i < MWScript::ScriptState::MaximumLocals; ++i)
        { maximum.declarations.declare('l', "extra" + std::to_string(i)); maximum.scripts[0].mLocals.mLongs.push_back(0); }
        BankContext bounded(nullptr, maximum, content, resolver);
        const std::string maximumName(MWScript::ScriptState::MaximumNameBytes, 'x');
        require(bounded.getMemberId(maximumName).getRefIdString() == maximumName, "Maximum script name rejected");
        bounded.setMemberLong(id("ledger_x73"), "extra63", 9, true);
        require(bounded.getMemberLong(id("ledger_x73"), "extra63", true) == 9, "Maximum local count rejected");
        for (int fault = 0; fault < 5; ++fault)
        {
            Bank malformed = alice;
            if (fault == 0) malformed.scripts[0].mLocals = malformed.scripts[1].mLocals;
            if (fault == 1) malformed.scripts[0].mLocals.mLongs.clear();
            if (fault == 2)
                for (size_t i = 1; i <= MWScript::ScriptState::MaximumLocals; ++i)
                { malformed.declarations.declare('l', "extra" + std::to_string(i)); malformed.scripts[0].mLocals.mLongs.push_back(0); }
            if (fault == 3) malformed.scripts[0].mTarget = std::pair<ESM::RefNum, ESM::RefId>{};
            if (fault == 4) malformed.scripts[0].mLocals.mFloats[0] = std::numeric_limits<float>::quiet_NaN();
            BankContext invalid(nullptr, malformed, content, resolver);
            rejects([&] { invalid.startScript("ledger_x73", {}); }, "Malformed staged script binding accepted");
        }
        alice.unavailable = true;
        bool deferred = false;
        try { context.scriptRunning("ledger_x73"); } catch (const MWScript::ScriptContextUnavailable&) { deferred = true; }
        require(deferred, "Waiting script became a stopped script");
        alice.unavailable = false;
        MWScript::InterpreterContext closed(nullptr, alice.expected, content.store, resolver);
        rejects([&] { closed.scriptRunning("ledger_x73"); }, "Unprovided bank fell back to stock scripts");
        rejects([&] { closed.startScript("ledger_x73", {}); }, "Unprovided bank started a stock script");
        rejects([&] { closed.stopScript("ledger_x73"); }, "Unprovided bank stopped a stock script");
        std::cout << "cross-script: stock typed members/lifecycle, character/story isolation, all identities, bounds, waits, closed services\n";
    }

    void crossScriptRollback()
    {
        Content content; MWWorld::ManualRef actor(content.store, id("fixture_actor"));
        Bank committed(sourceFree(1)); Resolver resolver(committed.expected, {}, actor.getPtr());
        const auto initial = committed.save(); const auto good = compile(BankCalls, &committed.declarations);
        const auto bad = compile(std::string(BankCalls) + "\nStartScript foreign", &committed.declarations);
        const auto targeted = compile(std::string(BankCalls) + "\nfixture_actor->StartScript ledger_x73", &committed.declarations);
        MWScript::Locals caller; caller.mShorts = {0}; std::vector<std::string> published;
        enum class Outcome { Committed, Deferred, Rejected };
        auto transact = [&](const Compiled& script, bool durable) {
            auto staged = committed; auto locals = caller;
            BankContext context(&locals, staged, content, resolver);
            try { run(script, context); }
            catch (const MWScript::ScriptContextUnavailable&) { return Outcome::Deferred; }
            catch (const std::exception&) { return Outcome::Rejected; }
            if (!durable) return Outcome::Deferred;
            committed = std::move(staged); caller = std::move(locals); published = std::move(context.messages);
            return Outcome::Committed;
        };
        for (const auto* script : {&bad, &targeted})
            require(transact(*script, true) == Outcome::Rejected && committed.save() == initial
                && caller.mShorts[0] == 0 && published.empty(), "Late failure leaked cross-script locals/lifecycle/output");
        require(transact(good, false) == Outcome::Deferred && committed.save() == initial
            && caller.mShorts[0] == 0 && published.empty(), "Failed durability leaked script bank");
        committed.unavailable = true;
        require(transact(good, true) == Outcome::Deferred && committed.save() == initial && caller.mShorts[0] == 0,
            "Dependency wait leaked caller locals");
        committed.unavailable = false;
        require(transact(good, true) == Outcome::Committed && committed.scripts[0].mLocals.mLongs[0] == 5
            && !committed.scripts[0].mRunning && committed.scripts[1].mRunning
            && caller.mShorts[0] == 2 && published == std::vector<std::string>{"committed"}, "Retry lost or duplicated script effects");
        const auto saved = committed.save(); Bank recovered(committed.expected); recovered.restore(saved);
        require(recovered.save() == saved && recovered.scripts[0].mLocals.mLongs[0] == 5
            && !recovered.scripts[0].mRunning && recovered.scripts[1].mRunning, "Stock GSCR recovery lost member state/lifecycle");
        std::cout << "cross-script rollback: late/targeted rejection, durability/wait rollback, retry, stock GSCR locals/running recovery\n";
    }

    void crossScriptStock()
    {
        Content content; MWBase::Environment environment; environment.setESMStore(content.store);
        Compiler::Locals declarations; declarations.declare('s', "token"); declarations.declare('l', "calls");
        declarations.declare('f', "phase");
        for (const char* name : {"ledger_x73", "relay_q19"})
        {
            ESM::Script value; value.blank(); value.mId = id(name);
            value.mScriptText = "Begin " + std::string(name) + "\nshort token\nlong calls\nfloat phase\nEnd " + name + "\n";
            content.store.insertStatic(value);
        }
        CompilerContext compiler(&declarations); Compiler::Extensions extensions; compiler.setExtensions(&extensions);
        MWScript::ScriptManager manager(content.store, compiler, 2); environment.setScriptManager(manager);
        MWScript::Locals caller; caller.mShorts = {0};
        MWScript::InterpreterContext context(&caller, {});
        run(compile(BankCalls.substr(0, BankCalls.find("\nMessageBox")), &declarations), context);
        require(caller.mShorts[0] == 2 && context.getMemberShort(id("ledger_x73"), "token", true) == 2
            && context.getMemberLong(id("ledger_x73"), "calls", true) == 5
            && context.getMemberFloat(id("ledger_x73"), "phase", true) == .25f
            && !context.scriptRunning("ledger_x73") && context.scriptRunning("relay_q19")
            && !context.scriptRunning("foreign"), "Stock global-script members/lifecycle changed");
        context.startScript("foreign", {}); context.stopScript("foreign");
        require(manager.getGlobalScripts().getScripts().size() == 2, "Stock missing script created an instance");
        MWWorld::ManualRef target(content.store, id("fixture_actor"));
        context.startScript("ledger_x73", target.getPtr());
        const auto* entry = manager.getGlobalScripts().getScriptIfPresent(id("ledger_x73"));
        require(entry && entry->mRunning && entry->getPtrIfPresent() && *entry->getPtrIfPresent() == target.getPtr()
            && entry->mLocals.mLongs[0] == 5, "Stock targeted restart lost target or reset locals");
        std::cout << "cross-script stock: unchanged compiled member calls, running flags, absent scripts, targeted restart/local retention\n";
    }

    class MemoryFile final : public VFS::File
    {
        std::string mContent;
    public:
        explicit MemoryFile(std::string content) : mContent(std::move(content)) {}
        Files::IStreamPtr open() override { return std::make_unique<std::istringstream>(mContent, std::ios_base::in); }
        std::filesystem::file_time_type getLastModified() const override { return {}; }
        std::string getStem() const override { return "MemoryFile"; }
    };
    class MemoryArchive final : public VFS::Archive
    {
        VFS::FileMap mFiles;
    public:
        explicit MemoryArchive(VFS::FileMap files) : mFiles(std::move(files)) {}
        void listResources(VFS::FileMap& out) override { out = mFiles; }
        bool contains(VFS::Path::NormalizedView file) const override { return mFiles.contains(file); }
        std::string getDescription() const override { return "MemoryArchive"; }
    };

    struct ScopedLuaContext final : MWScript::InterpreterContext
    {
        int sharedPulse = 0;
        float ambientPhase = 0;
        std::vector<std::string>& output;
        ScopedLuaContext(MWScript::Locals* locals, MWScript::ExecutionContext execution, const MWWorld::ESMStore& content,
            const Resolver& resolver, MWMechanics::NpcStats* stats, Bank* bank, std::vector<std::string>& messages)
            : InterpreterContext(locals, execution, content, resolver, stats, bank), output(messages) {}
        char getGlobalType(std::string_view name) const override
        { return name == "sharedpulse" ? 'l' : name == "ambientphase" ? 'f' : ' '; }
        std::vector<std::string> getGlobals() const override { return {"sharedpulse", "ambientphase"}; }
        int getGlobalShort(std::string_view name) const override { return getGlobalLong(name); }
        int getGlobalLong(std::string_view name) const override
        { require(name == "sharedpulse", "Unknown scoped global"); return sharedPulse; }
        float getGlobalFloat(std::string_view name) const override
        { require(name == "ambientphase", "Unknown scoped global"); return ambientPhase; }
        void setGlobalShort(std::string_view name, int value) override { setGlobalLong(name, value); }
        void setGlobalLong(std::string_view name, int value) override
        { require(name == "sharedpulse", "Unknown scoped global"); sharedPulse = value; }
        void setGlobalFloat(std::string_view name, float value) override
        { require(name == "ambientphase" && std::isfinite(value), "Invalid scoped global"); ambientPhase = value; }
        void messageBox(std::string_view message, const std::vector<std::string>& buttons) override
        {
            require(buttons.empty() && output.size() < 8 && message.size() <= 256, "Scoped Lua output exceeded bounds");
            output.emplace_back(message);
        }
    };

    void luaContext()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b")),
            actorA(content.store, id("fixture_actor")), actorB(content.store, id("fixture_actor"));
        a.getPtr().getCellRef().setRefNum({1, 1});
        b.getPtr().getCellRef().setRefNum({2, 1});
        actorA.getPtr().getCellRef().setRefNum({101, 1});
        actorB.getPtr().getCellRef().setRefNum({102, 1});

        MemoryFile storyLua(R"lua(
local storage = require('openmw.storage')
local async = require('openmw.async')
local world = require('openmw.world')
local step = 0
local timerCb = async:registerTimerCallback('on_timer', function(arg)
    step = step + arg.delta
    local ledger = world.mwscript.getGlobalScript('ledger_x73')
    ledger.variables.calls = ledger.variables.calls + arg.delta
end)
local sec = storage.playerSection('story_state')
sec:subscribe(async:callback(function(section, key)
    if key == 'trigger' then
        local globals = world.mwscript.getGlobalVariables()
        globals.sharedpulse = globals.sharedpulse + 1
    end
end))
return {
    engineHandlers = {
        onSave = function() return { step = step } end,
        onLoad = function(state) if state then step = state.step end end,
    },
    eventHandlers = {
        Configure = function(data)
            step = step + data.step
            sec:set('stage', data.stage)
            sec:set('trigger', data.stage)
            local globals = world.mwscript.getGlobalVariables(data.player)
            globals.ambientphase = globals.ambientphase + 0.5
            local ledger = world.mwscript.getGlobalScript('ledger_x73', data.player)
            ledger.variables.token = 65537
            ledger.variables.calls = ledger.variables.calls + 5
            ledger.variables.phase = ledger.variables.phase + 0.25
            ledger.isRunning = false
            local relay = world.mwscript.getGlobalScript('relay_q19', data.player)
            relay.isRunning = true
            async:newGameTimer(2.0, timerCb, { delta = 3 })
            data.target:sendEvent('Ping', { from = tostring(ledger.player.recordId), step = step })
        end,
        WaitBank = function()
            pcall(function() return world.mwscript.getGlobalScript('ledger_x73') end)
            step = step + 999
        end,
        WaitSource = function()
            xpcall(function() sec:set('trigger', 99) end, function(e) return e end)
            step = step + 999
        end,
    }
}
)lua");
        MemoryFile actorLua(R"lua(
local received = 0
return {
    engineHandlers = {
        onSave = function() return { received = received } end,
        onLoad = function(state) if state then received = state.received end end,
    },
    eventHandlers = {
        Ping = function(data)
            received = received + data.step
        end,
    }
}
)lua");
        VFS::FileMap files;
        files.emplace(VFS::Path::Normalized("story.lua"), &storyLua);
        files.emplace(VFS::Path::Normalized("actor.lua"), &actorLua);
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<MemoryArchive>(std::move(files)));
        vfs.buildIndex();

        ESM::LuaScriptsCfg rawCfg;
        LuaUtil::parseOMWScripts(rawCfg, "GLOBAL: story.lua\nCUSTOM, NPC: actor.lua\n");
        LuaUtil::ScriptsConfiguration cfg;
        cfg.init(std::move(rawCfg), false);
        LuaUtil::LuaState lua(&vfs, &cfg);

        double simTime = 1.0, gameTime = 10.0;
        MWScript::InterpreterContext* activeScoped = nullptr;
        MWScript::ExecutionContext expectedExecution = execution(1);
        bool storageUnavailable = false;

        auto globalSerializer = MWLua::createUserdataSerializer(false);
        auto localSerializer = MWLua::createUserdataSerializer(true);

        MWLua::GlobalScripts globalScripts(&lua);
        globalScripts.setSerializer(globalSerializer.get());
        globalScripts.setAutoStartConf(cfg.getGlobalConf());
        MWLua::LocalScripts localA(&lua, MWLua::LObject(actorA.getPtr()));
        localA.setSerializer(localSerializer.get());
        localA.setAutoStartConf(cfg.getLocalConf(ESM::REC_NPC_, id("fixture_actor"), actorA.getPtr().getCellRef().getRefNum()));
        MWLua::LocalScripts localB(&lua, MWLua::LObject(actorB.getPtr()));
        localB.setSerializer(localSerializer.get());
        localB.setAutoStartConf(cfg.getLocalConf(ESM::REC_NPC_, id("fixture_actor"), actorB.getPtr().getCellRef().getRefNum()));

        MWLua::LuaEvents events(globalScripts);
        auto validateActive = [&]() -> MWScript::ExecutionContext {
            require(activeScoped && activeScoped->getExecutionContext(), "Missing active Lua execution context");
            const auto& actual = *activeScoped->getExecutionContext();
            actual.validate();
            require(actual == expectedExecution, "Lua operation lost or crossed execution context");
            return actual;
        };
        globalScripts.setValidator([&] { (void)validateActive(); });
        localA.setValidator([&] { (void)validateActive(); });
        localB.setValidator([&] {
            require(activeScoped && activeScoped->getExecutionContext()
                && *activeScoped->getExecutionContext() == execution(2), "Local actor B crossed execution context");
        });
        events.setScopedResolver(
            [&](const std::optional<MWScript::ExecutionContext>& ctx, ESM::RefNum dest) -> MWLua::LocalScripts* {
                const auto active = validateActive();
                require(!ctx || *ctx == active, "Local event lost execution context");
                if (active == execution(1) && dest == actorA.getPtr().getCellRef().getRefNum())
                    return &localA;
                if (active == execution(2) && dest == actorB.getPtr().getCellRef().getRefNum())
                    return &localB;
                return nullptr;
            },
            validateActive);

        LuaUtil::LuaStorage globalStorage, storageA, storageB, storageOtherStory;
        globalStorage.setActive(true);
        storageA.setActive(true);
        storageB.setActive(true);
        storageOtherStory.setActive(true);
        globalStorage.setValidator([&](bool readOnly) {
            require(activeScoped && activeScoped->getExecutionContext(), "Global storage lacked execution context");
            const auto& actual = *activeScoped->getExecutionContext();
            actual.validate();
            require(readOnly || actual.ownership == MWScript::ExecutionContext::Ownership::Shared,
                "Character context mutated shared global storage");
        });
        storageA.setValidator([&](bool readOnly) {
            const auto actual = validateActive();
            require(actual.ownership == MWScript::ExecutionContext::Ownership::Character && actual == execution(1),
                "Player storage A crossed character/story context");
            if (!readOnly && storageUnavailable)
            {
                try { throw MWScript::ScriptSourceWait(23, 2, 900); }
                catch (...) { lua.capturePendingException(std::current_exception()); throw; }
            }
        });
        storageB.setValidator([&](bool) {
            require(activeScoped && activeScoped->getExecutionContext()
                && *activeScoped->getExecutionContext() == execution(2), "Player storage B crossed character/story context");
        });
        storageOtherStory.setValidator([&](bool) {
            auto expected = execution(1); ++expected.story;
            require(activeScoped && activeScoped->getExecutionContext()
                && *activeScoped->getExecutionContext() == expected, "Other-story storage crossed story context");
        });

        MWLua::Context mwCtx;
        mwCtx.mType = MWLua::Context::Global;
        mwCtx.mLua = &lua;
        mwCtx.mSerializer = globalSerializer.get();
        mwCtx.mLuaEvents = &events;
        mwCtx.mScopedContext = &activeScoped;

        lua.protectedCall([&](LuaUtil::LuaView& view) {
            LuaUtil::LuaStorage::initLuaBindings(view);
            lua.addCommonPackage("openmw.async",
                LuaUtil::getAsyncPackageInitializer(view.sol(), [&] { return simTime; }, [&] { return gameTime; }));
            MWLua::initObjectBindingsForGlobalScripts(mwCtx);
            MWLua::initObjectBindingsForLocalScripts(mwCtx);
            sol::table worldPkg(view.sol(), sol::create);
            worldPkg["mwscript"] = MWLua::initMWScriptBindings(mwCtx);
            globalScripts.addPackage("openmw.world", LuaUtil::makeReadOnly(worldPkg));
            globalScripts.addPackage("openmw.storage",
                LuaUtil::LuaStorage::initPlayerPackage(view, &globalStorage, &storageA));
        });

        Bank bankA(execution(1)), bankB(execution(2)), bankOther(execution(1));
        ++bankOther.expected.story;
        Resolver resolverA(execution(1), a.getPtr(), actorA.getPtr()),
            resolverB(execution(2), b.getPtr(), actorB.getPtr());
        MWMechanics::NpcStats statsA(content.store), statsB(content.store);
        std::vector<std::string> messagesA;
        ScopedLuaContext scopedA(nullptr, execution(1), content.store, resolverA, &statsA, &bankA, messagesA);
        activeScoped = &scopedA;

        globalScripts.addAutoStartedScripts();
        localA.addAutoStartedScripts();

        std::string payload;
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::table data = view.newTable();
            data["step"] = 4;
            data["stage"] = 7;
            data["player"] = MWLua::GObject(a.getPtr());
            data["target"] = MWLua::GObject(actorA.getPtr());
            payload = LuaUtil::serialize(data, globalSerializer.get());
        });
        events.addGlobalEvent({"Configure", payload});
        events.finalizeEventBatch();
        events.callEventHandlers();
        events.finalizeEventBatch();
        events.callEventHandlers();
        gameTime = 12.0;
        globalScripts.processTimers(simTime, gameTime);

        require(scopedA.sharedPulse == 1 && scopedA.ambientPhase == 0.5f
            && scopedA.getMemberShort(id("ledger_x73"), "token", true) == 1
            && scopedA.getMemberLong(id("ledger_x73"), "calls", true) == 8
            && scopedA.getMemberFloat(id("ledger_x73"), "phase", true) == 0.25f
            && !scopedA.scriptRunning("ledger_x73") && scopedA.scriptRunning("relay_q19")
            && bankB.scripts[0].mLocals.mLongs[0] == 0 && bankOther.scripts[0].mLocals.mLongs[0] == 0,
            "Scoped Lua bindings/storage/events/timers did not update isolated character/story state");

        ESM::LuaScripts savedLocalA;
        localA.save(savedLocalA);
        require(savedLocalA.mScripts.size() == 1, "Local actor script state missing");
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::table state = LuaUtil::deserialize(view.sol(), savedLocalA.mScripts[0].mData);
            require(state.get<int>("received") == 4, "Scoped local Lua event did not deliver payload to target instance");
            sol::object stage = storageA.getReadOnlySection(view.sol(), "story_state").as<sol::table>()["get"](
                storageA.getReadOnlySection(view.sol(), "story_state"), "stage");
            require(stage.as<int>() == 7, "Scoped player storage lost value");
        });

        // Foreign player object, foreign target object, and all 9 execution context mutations must reject.
        sol::table world;
        lua.protectedCall([&](LuaUtil::LuaView& view) { world = MWLua::initMWScriptBindings(mwCtx); });
        auto rejectsLua = [&](auto&& fn, const char* message) {
            rejects([&] { lua.protectedCall([&](LuaUtil::LuaView& view) { fn(view); }); }, message);
        };
        rejectsLua([&](LuaUtil::LuaView&) { world["getGlobalScript"]("ledger_x73", MWLua::GObject(b.getPtr())); },
            "Foreign player accepted in getGlobalScript");
        rejectsLua([&](LuaUtil::LuaView&) { world["getGlobalVariables"](MWLua::GObject(b.getPtr())); },
            "Foreign player accepted in getGlobalVariables");
        rejectsLua([&](LuaUtil::LuaView&) { world["getLocalScript"](MWLua::GObject(actorA.getPtr())); },
            "Unstaged local script accepted");
        rejectsLua([&](LuaUtil::LuaView&) {
            sol::object ref = world["getGlobalScript"]("ledger_x73");
            sol::table vars = ref.as<sol::table>()["variables"];
            vars["unknown"] = 1;
        }, "Unknown scoped member accepted in Lua");
        rejectsLua([&](LuaUtil::LuaView&) {
            sol::object ref = world["getGlobalScript"]("ledger_x73");
            sol::table vars = ref.as<sol::table>()["variables"];
            vars["phase"] = std::numeric_limits<double>::infinity();
        }, "Nonfinite member accepted in Lua");
        rejectsLua([&](LuaUtil::LuaView&) {
            sol::object globals = world["getGlobalVariables"]();
            globals.as<sol::table>()["unknown"] = 1;
        }, "Unknown scoped global accepted in Lua");
        rejectsLua([&](LuaUtil::LuaView&) {
            sol::object globals = world["getGlobalVariables"]();
            globals.as<sol::table>()["ambientphase"] = std::numeric_limits<double>::quiet_NaN();
        }, "Nonfinite scoped global accepted in Lua");
        rejectsLua([&](LuaUtil::LuaView& view) {
            sol::object roGlobal = storageA.getReadOnlySection(view.sol(), "story_state");
            roGlobal.as<sol::table>()["set"](roGlobal, "stage", 9);
        }, "Read-only storage section mutated");
        rejectsLua([&](LuaUtil::LuaView& view) {
            sol::object mutSec = storageA.getMutableSection(view.sol(), "story_state");
            mutSec.as<sol::table>()["set"](mutSec, "", 1);
        }, "Empty storage key accepted");
        rejectsLua([&](LuaUtil::LuaView& view) {
            sol::object mutSec = storageA.getMutableSection(view.sol(), "story_state");
            mutSec.as<sol::table>()["set"](mutSec, std::string(129, 'x'), 1);
        }, "Oversized storage key accepted");
        rejectsLua([&](LuaUtil::LuaView& view) {
            storageA.getMutableSection(view.sol(), std::string(129, 'x'));
        }, "Oversized storage section accepted");
        rejects([&] { events.addLocalEvent({actorB.getPtr().getCellRef().getRefNum(), "Ping", {}}); },
            "Cross-scope local event target accepted");
        rejects([&] { events.addGlobalEvent({"", {}}); }, "Empty global event name accepted");
        rejects([&] { events.addGlobalEvent({std::string(129, 'x'), {}}); }, "Oversized global event name accepted");
        rejects([&] { events.addGlobalEvent({" oversize ", std::string(MWLua::LuaEvents::MaximumEventDataBytes + 1, 'x')}); },
            "Oversized global event payload accepted");
        rejects([&] { events.addMenuEvent({"Menu", {}}); }, "Scoped menu event accepted");
        rejects([&] { globalScripts.setupUnsavableTimer(LuaUtil::ScriptsContainer::TimerType::GAME_TIME, 15.0, 0, {}); },
            "Unsavable timer accepted in scoped container");
        rejects([&] { globalScripts.setupSerializableTimer(LuaUtil::ScriptsContainer::TimerType::GAME_TIME, 15.0, 0, "unregistered", sol::nil); },
            "Unregistered serializable timer callback accepted");

        for (int field = 0; field < 9; ++field)
        {
            auto foreign = execution(1);
            if (field == 0) ++foreign.campaign;
            if (field == 1) ++foreign.story;
            if (field == 2) ++foreign.physicalScope;
            if (field == 3) ++foreign.character;
            if (field == 4) foreign.sourceReference = foreign.sourceLife = 9;
            if (field == 5) ++foreign.invocation;
            if (field == 6) ++foreign.causalEvent;
            if (field == 7) { foreign.ownership = MWScript::ExecutionContext::Ownership::Shared; foreign.character = foreign.story = 0; }
            if (field == 8) { foreign.sourceReference = 9; foreign.sourceLife = 10; }
            ScopedLuaContext bad(nullptr, foreign, content.store, resolverA, nullptr, &bankA, messagesA);
            activeScoped = &bad;
            rejects([&] { globalScripts.processTimers(simTime, gameTime); }, "Timer accepted mismatched execution context");
            rejects([&] { events.addGlobalEvent({"Configure", payload}); }, "Event accepted mismatched execution context");
            rejectsLua([&](LuaUtil::LuaView& view) {
                storageA.getMutableSection(view.sol(), "story_state");
            }, "Player storage accepted mismatched execution context");
        }
        activeScoped = nullptr;
        rejects([&] { globalScripts.processTimers(simTime, gameTime); }, "Scoped Lua timer ran without active execution context");

        // Shared ownership rejects personal player access without calling character resolver.
        auto shared = execution(1);
        shared.ownership = MWScript::ExecutionContext::Ownership::Shared;
        shared.character = shared.story = 0;
        Resolver sharedResolver(shared, {}, actorA.getPtr());
        ScopedLuaContext sharedScoped(nullptr, shared, content.store, sharedResolver, nullptr, nullptr, messagesA);
        activeScoped = &sharedScoped;
        rejectsLua([&](LuaUtil::LuaView&) { world["getGlobalVariables"](MWLua::GObject(a.getPtr())); },
            "Shared Lua accessed character globals");
        rejectsLua([&](LuaUtil::LuaView& view) { storageA.getReadOnlySection(view.sol(), "story_state"); },
            "Shared Lua accessed player storage");
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::object gsec = globalStorage.getMutableSection(view.sol(), "world_state");
            gsec.as<sol::table>()["set"](gsec, "weather", 3);
        });
        require(sharedResolver.calls == 0, "Shared Lua player rejection reached reference resolver");

        // Destroyed reference lifetime witness rejects in GObject.
        activeScoped = &scopedA;
        expectedExecution = execution(1);
        {
            MWLua::GObject dangling;
            {
                MWWorld::ManualRef temp(content.store, id("fixture_actor"));
                temp.getPtr().getCellRef().setRefNum({999, 1});
                dangling = MWLua::GObject(temp.getPtr());
            }
            require(dangling.ptrOrEmpty().isEmpty(), "Destroyed reference remained live in GObject");
            rejects([&] { (void)dangling.ptr(); }, "Destroyed GObject dereferenced");
        }

        // Dependency waits (including ScriptSourceWait) survive Lua pcall/xpcall without being swallowed.
        events.addGlobalEvent({"WaitBank", {}});
        events.finalizeEventBatch();
        bankA.unavailable = true;
        bool waited = false;
        try
        {
            events.callEventHandlers();
        }
        catch (const MWScript::ScriptContextUnavailable&) { waited = true; }
        require(waited, "Lua pcall swallowed ScriptContextUnavailable");
        bankA.unavailable = false;

        events.addGlobalEvent({"WaitSource", {}});
        events.finalizeEventBatch();
        storageUnavailable = true;
        uint64_t waitRef = 0, waitLife = 0, waitTick = 0;
        try
        {
            events.callEventHandlers();
        }
        catch (const MWScript::ScriptSourceWait& wait)
        {
            waitRef = wait.reference; waitLife = wait.life; waitTick = wait.respawnTick;
        }
        require(waitRef == 23 && waitLife == 2 && waitTick == 900, "Lua xpcall lost ScriptSourceWait identity");
        storageUnavailable = false;
        {
            ESM::LuaScripts afterWait;
            globalScripts.save(afterWait);
            lua.protectedCall([&](LuaUtil::LuaView& view) {
                sol::table state = LuaUtil::deserialize(view.sol(), afterWait.mScripts[0].mData);
                require(state.get<int>("step") == 7, "Lua script continued past pcall/xpcall after dependency wait");
            });
        }
        std::cout << "lua-context: scoped bindings/storage/events/timers, character/story/scope isolation, all identities, waits\n";
    }

    void luaRollback()
    {
        Content content;
        MWWorld::ManualRef a(content.store, id("player_a")), b(content.store, id("player_b")),
            actor(content.store, id("fixture_actor"));
        a.getPtr().getCellRef().setRefNum({1, 1});
        b.getPtr().getCellRef().setRefNum({2, 1});
        actor.getPtr().getCellRef().setRefNum({101, 1});

        MemoryFile questLua(R"lua(
local storage = require('openmw.storage')
local async = require('openmw.async')
local world = require('openmw.world')
local step = 0
local events = 0
local sec = storage.playerSection('quest_73')
local timerCb
timerCb = async:registerTimerCallback('quest_tick', function(arg)
    step = step + arg.delta
    sec:set('stage', (sec:get('stage') or 0) + arg.delta)
    local globals = world.mwscript.getGlobalVariables()
    globals.sharedpulse = globals.sharedpulse + arg.delta
    globals.ambientphase = globals.ambientphase + 0.25
    local ledger = world.mwscript.getGlobalScript('ledger_x73')
    ledger.variables.token = ledger.variables.token + 2
    ledger.variables.calls = ledger.variables.calls + 5
    ledger.variables.phase = ledger.variables.phase + 0.25
    ledger.isRunning = false
    local relay = world.mwscript.getGlobalScript('relay_q19')
    relay.isRunning = true
    if arg.followup then
        async:newGameTimer(5.0, timerCb, { delta = 10, followup = false })
    end
    if arg.target then
        arg.target:sendEvent('SyncActor', { count = step })
    end
    if arg.fail then
        world.mwscript.getGlobalScript('foreign')
    end
end)
sec:subscribe(async:callback(function(section, key)
    if key == 'fail_sub' then
        error('subscriber failure')
    end
end))
return {
    engineHandlers = {
        onInit = function()
            sec:set('stage', 1)
            async:newGameTimer(5.0, timerCb, { delta = 1, followup = true })
        end,
        onSave = function() return { step = step, events = events, cachedStage = sec:get('stage') } end,
        onLoad = function(state)
            if state then
                step = state.step
                events = state.events
            end
        end,
    },
    eventHandlers = {
        Advance = function(data)
            events = events + data.amount
            sec:set('last_event', events)
            if data.target then
                data.target:sendEvent('SyncActor', { count = events })
            end
            if data.failSub then
                sec:set('fail_sub', 1)
            end
            if data.failUnsavable then
                async:newUnsavableGameTimer(1.0, function() end)
            end
            if data.failScript then
                world.mwscript.getGlobalScript('foreign')
            end
        end,
    }
}
)lua");
        MemoryFile actorLua(R"lua(
local synced = 0
return {
    engineHandlers = {
        onSave = function() return { synced = synced } end,
        onLoad = function(state) if state then synced = state.synced end end,
    },
    eventHandlers = {
        SyncActor = function(data)
            synced = synced + data.count
        end,
    }
}
)lua");
        VFS::FileMap files;
        files.emplace(VFS::Path::Normalized("quest.lua"), &questLua);
        files.emplace(VFS::Path::Normalized("actor.lua"), &actorLua);
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<MemoryArchive>(std::move(files)));
        vfs.buildIndex();

        ESM::LuaScriptsCfg rawCfg;
        LuaUtil::parseOMWScripts(rawCfg, "GLOBAL: quest.lua\nCUSTOM, NPC: actor.lua\n");
        LuaUtil::ScriptsConfiguration cfg;
        cfg.init(std::move(rawCfg), false);
        LuaUtil::LuaState lua(&vfs, &cfg);

        double simTime = 1.0, gameTime = 0.0;
        MWScript::InterpreterContext* activeScoped = nullptr;
        const auto expected = execution(1);

        auto globalSerializer = MWLua::createUserdataSerializer(false);
        auto localSerializer = MWLua::createUserdataSerializer(true);

        MWLua::GlobalScripts globalScripts(&lua);
        globalScripts.setSerializer(globalSerializer.get());
        globalScripts.setAutoStartConf(cfg.getGlobalConf());
        MWLua::LocalScripts localActor(&lua, MWLua::LObject(actor.getPtr()));
        localActor.setSerializer(localSerializer.get());
        localActor.setAutoStartConf(cfg.getLocalConf(ESM::REC_NPC_, id("fixture_actor"), actor.getPtr().getCellRef().getRefNum()));

        MWLua::LuaEvents events(globalScripts);
        auto validateActive = [&]() -> MWScript::ExecutionContext {
            require(activeScoped && activeScoped->getExecutionContext()
                && *activeScoped->getExecutionContext() == expected, "Rollback test lost execution context");
            return expected;
        };
        globalScripts.setValidator([&] { (void)validateActive(); });
        localActor.setValidator([&] { (void)validateActive(); });
        events.setScopedResolver(
            [&](const std::optional<MWScript::ExecutionContext>& ctx, ESM::RefNum dest) -> MWLua::LocalScripts* {
                const auto active = validateActive();
                require(!ctx || *ctx == active, "Rollback local event lost execution context");
                return dest == actor.getPtr().getCellRef().getRefNum() ? &localActor : nullptr;
            },
            validateActive);

        LuaUtil::LuaStorage globalStorage, playerStorage, peerStorage;
        globalStorage.setActive(true);
        playerStorage.setActive(true);
        peerStorage.setActive(true);
        playerStorage.setValidator([&](bool) { (void)validateActive(); });
        peerStorage.setStrict(true);

        MWLua::Context mwCtx;
        mwCtx.mType = MWLua::Context::Global;
        mwCtx.mLua = &lua;
        mwCtx.mSerializer = globalSerializer.get();
        mwCtx.mLuaEvents = &events;
        mwCtx.mScopedContext = &activeScoped;

        lua.protectedCall([&](LuaUtil::LuaView& view) {
            LuaUtil::LuaStorage::initLuaBindings(view);
            lua.addCommonPackage("openmw.async",
                LuaUtil::getAsyncPackageInitializer(view.sol(), [&] { return simTime; }, [&] { return gameTime; }));
            MWLua::initObjectBindingsForGlobalScripts(mwCtx);
            MWLua::initObjectBindingsForLocalScripts(mwCtx);
            sol::table worldPkg(view.sol(), sol::create);
            worldPkg["mwscript"] = MWLua::initMWScriptBindings(mwCtx);
            globalScripts.addPackage("openmw.world", LuaUtil::makeReadOnly(worldPkg));
            globalScripts.addPackage("openmw.storage",
                LuaUtil::LuaStorage::initPlayerPackage(view, &globalStorage, &playerStorage));
        });

        Bank committedBank(expected), peerBank(execution(2));
        Resolver resolver(expected, a.getPtr(), actor.getPtr());
        auto committedStats = std::make_unique<MWMechanics::NpcStats>(content.store);
        int committedPulse = 0;
        float committedPhase = 0.0f;
        std::vector<std::string> published;

        {
            std::vector<std::string> initMessages;
            ScopedLuaContext initCtx(nullptr, expected, content.store, resolver, committedStats.get(), &committedBank, initMessages);
            activeScoped = &initCtx;
            globalScripts.addAutoStartedScripts();
            localActor.addAutoStartedScripts();
            activeScoped = nullptr;
        }

        auto makeAdvancePayload = [&](bool failSub, bool failUnsavable, bool failScript) {
            std::string out;
            lua.protectedCall([&](LuaUtil::LuaView& view) {
                sol::table t = view.newTable();
                t["amount"] = 3;
                t["target"] = MWLua::GObject(actor.getPtr());
                t["failSub"] = failSub;
                t["failUnsavable"] = failUnsavable;
                t["failScript"] = failScript;
                out = LuaUtil::serialize(t, globalSerializer.get());
            });
            return out;
        };

        // Pre-seed a pending global event before the transaction so rollback must preserve it without re-consuming.
        {
            std::vector<std::string> seedMessages;
            ScopedLuaContext seedCtx(nullptr, expected, content.store, resolver, committedStats.get(), &committedBank, seedMessages);
            activeScoped = &seedCtx;
            events.addGlobalEvent({"Advance", makeAdvancePayload(false, false, false)});
            events.finalizeEventBatch();
            activeScoped = nullptr;
        }

        const auto serializeScripts = [&](LuaUtil::ScriptsContainer& container) {
            std::vector<std::string> tmpMessages;
            ScopedLuaContext tmpCtx(nullptr, expected, content.store, resolver, committedStats.get(), &committedBank, tmpMessages);
            activeScoped = &tmpCtx;
            ESM::LuaScripts data;
            container.save(data);
            activeScoped = nullptr;
            std::ostringstream stream(std::ios::binary);
            ESM::ESMWriter writer;
            writer.setVersion(ESM::VER_130);
            writer.setType(0);
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.setRecordCount(1);
            writer.save(stream);
            writer.startRecord("LUAM");
            data.save(writer);
            writer.endRecord("LUAM");
            writer.close();
            return stream.str();
        };

        const auto initialGlobalLua = serializeScripts(globalScripts);
        const auto initialLocalLua = serializeScripts(localActor);
        const auto initialStorage = playerStorage.snapshot();
        const auto initialEvents = events.snapshot();
        const auto initialBank = committedBank.save();
        const auto peerBankBefore = peerBank.save();

        enum class Outcome { Committed, Deferred, Rejected };
        auto transact = [&](double targetGameTime, const std::optional<std::string>& extraEvent, bool durable) {
            ESM::LuaScripts beforeGlobal, beforeLocal;
            auto beforeStorage = playerStorage.snapshot();
            auto beforeEvents = events.snapshot();
            auto stagedBank = committedBank;
            auto stagedStats = std::make_unique<MWMechanics::NpcStats>(*committedStats);
            std::vector<std::string> stagedMessages;
            ScopedLuaContext scoped(nullptr, expected, content.store, resolver, stagedStats.get(), &stagedBank, stagedMessages);
            scoped.sharedPulse = committedPulse;
            scoped.ambientPhase = committedPhase;
            activeScoped = &scoped;
            globalScripts.save(beforeGlobal);
            localActor.save(beforeLocal);
            const double prevGameTime = gameTime;
            auto rollbackAll = [&]() {
                gameTime = prevGameTime;
                const bool wasUnavailable = stagedBank.unavailable;
                const bool wasResolverUnavailable = resolver.unavailable;
                stagedBank.unavailable = false;
                resolver.unavailable = false;
                playerStorage.restore(beforeStorage);
                events.restore(beforeEvents);
                globalScripts.load(beforeGlobal);
                localActor.load(beforeLocal);
                stagedBank.unavailable = wasUnavailable;
                resolver.unavailable = wasResolverUnavailable;
                activeScoped = nullptr;
            };
            try
            {
                gameTime = targetGameTime;
                globalScripts.processTimers(simTime, gameTime);
                if (extraEvent)
                    events.addGlobalEvent({"Advance", *extraEvent});
                events.callEventHandlers();
                events.finalizeEventBatch();
                events.callEventHandlers();
                scoped.getPlayerStats().joinFaction(id("fixture_faction"));
                scoped.getPlayerStats().setFactionRank(id("fixture_faction"), 1, content.store);
                scoped.messageBox("committed", {});
            }
            catch (const MWScript::ScriptContextUnavailable&)
            {
                rollbackAll();
                return Outcome::Deferred;
            }
            catch (const std::exception&)
            {
                rollbackAll();
                return Outcome::Rejected;
            }
            if (!durable)
            {
                rollbackAll();
                return Outcome::Deferred;
            }
            activeScoped = nullptr;
            committedBank = std::move(stagedBank);
            committedStats = std::move(stagedStats);
            committedPulse = scoped.sharedPulse;
            committedPhase = scoped.ambientPhase;
            published = std::move(stagedMessages);
            return Outcome::Committed;
        };

        // 1. Late failures in subscriber callback, unsavable timer, and cross-script call roll back everything.
        for (const auto& badPayload : {
                 makeAdvancePayload(true, false, false),
                 makeAdvancePayload(false, true, false),
                 makeAdvancePayload(false, false, true),
             })
        {
            require(transact(5.0, badPayload, true) == Outcome::Rejected, "Bad payload did not reject");
            require(serializeScripts(globalScripts) == initialGlobalLua, "Rejected transaction leaked global Lua state");
            require(serializeScripts(localActor) == initialLocalLua, "Rejected transaction leaked local Lua state");
            require(playerStorage.snapshot() == initialStorage, "Rejected transaction leaked player storage");
            require(events.snapshot() == initialEvents, "Rejected transaction leaked Lua events");
            require(committedBank.save() == initialBank, "Rejected transaction leaked bank state");
            require(committedStats->getFactionRanks().empty()
                && committedPulse == 0 && committedPhase == 0.0f && published.empty(),
                "Rejected Lua transaction leaked stats or output");
        }

        // 2. Failed durability rolls back everything without consuming due timers or pre-seeded events.
        const auto deferredOutcome = transact(5.0, std::nullopt, false);
        require(deferredOutcome == Outcome::Deferred, "Failed durability did not return Deferred");
        require(serializeScripts(globalScripts) == initialGlobalLua, "Failed durability leaked global Lua state");
        require(serializeScripts(localActor) == initialLocalLua, "Failed durability leaked local Lua state");
        require(playerStorage.snapshot() == initialStorage, "Failed durability leaked player storage");
        require(events.snapshot() == initialEvents, "Failed durability leaked Lua events");
        require(committedBank.save() == initialBank, "Failed durability leaked bank state");
        require(committedStats->getFactionRanks().empty() && published.empty(), "Failed durability leaked stats/output");

        // 3. Dependency wait (bank or resolver unavailable) rolls back and defers cleanly.
        committedBank.unavailable = true;
        require(transact(5.0, std::nullopt, true) == Outcome::Deferred
            && serializeScripts(globalScripts) == initialGlobalLua
            && playerStorage.snapshot() == initialStorage
            && events.snapshot() == initialEvents
            && committedBank.save() == initialBank, "Bank wait leaked Lua state or consumed timers/events");
        committedBank.unavailable = false;

        resolver.unavailable = true;
        require(transact(5.0, std::nullopt, true) == Outcome::Deferred
            && serializeScripts(globalScripts) == initialGlobalLua
            && playerStorage.snapshot() == initialStorage
            && events.snapshot() == initialEvents
            && committedBank.save() == initialBank, "Resolver wait leaked Lua state or consumed timers/events");
        resolver.unavailable = false;

        // 4. Retry after wait/defer commits once without double-consuming due timer or event.
        require(transact(5.0, std::nullopt, true) == Outcome::Committed
            && committedPulse == 1 && committedPhase == 0.25f
            && committedBank.scripts[0].mLocals.mShorts[0] == 2
            && committedBank.scripts[0].mLocals.mLongs[0] == 5
            && committedBank.scripts[0].mLocals.mFloats[0] == 0.25f
            && !committedBank.scripts[0].mRunning && committedBank.scripts[1].mRunning
            && committedStats->getFactionRank(id("fixture_faction")) == 1
            && published == std::vector<std::string>{"committed"}
            && peerBank.save() == peerBankBefore, "Retry did not commit exact isolated Lua/MWScript state once");

        // 5. Coherent recovery of Lua scripts, timers, storage, queued events, and bank state.
        ScopedLuaContext recoveryCtx(nullptr, expected, content.store, resolver, committedStats.get(), &committedBank, published);
        activeScoped = &recoveryCtx;
        events.addGlobalEvent({"Advance", makeAdvancePayload(false, false, false)});
        ESM::LuaScripts savedGlobal, savedLocal;
        globalScripts.save(savedGlobal);
        localActor.save(savedLocal);
        std::string savedStorageBytes;
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::table gState = LuaUtil::deserialize(view.sol(), savedGlobal.mScripts[0].mData);
            sol::table lState = LuaUtil::deserialize(view.sol(), savedLocal.mScripts[0].mData);
            require(gState.get<int>("step") == 1 && gState.get<int>("events") == 3
                && gState.get<int>("cachedStage") == 2 && lState.get<int>("synced") == 3
                && savedGlobal.mScripts[0].mTimers.size() == 1
                && savedGlobal.mScripts[0].mTimers[0].mTime == 10.0,
                "Committed Lua script/timer state did not match expected values");
            savedStorageBytes = playerStorage.save(view.sol());
        });

        std::ostringstream eventStream(std::ios::binary);
        {
            ESM::ESMWriter writer;
            writer.setVersion(ESM::VER_130);
            writer.setType(0);
            writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
            writer.setRecordCount(1);
            writer.save(eventStream);
            writer.startRecord("LUAM");
            events.save(writer);
            writer.endRecord("LUAM");
            writer.close();
        }
        const std::string savedEventsBytes = eventStream.str();

        // Malformed recovery inputs must reject atomically without mutating committed state.
        {
            const auto storageBefore = playerStorage.snapshot();
            lua.protectedCall([&](LuaUtil::LuaView& view) {
                rejects([&] { playerStorage.load(view.sol(), std::string_view{}); }, "Empty storage bytes accepted");
                std::string corruptStorage = savedStorageBytes;
                corruptStorage.push_back('x');
                rejects([&] { playerStorage.load(view.sol(), std::string_view(corruptStorage)); }, "Trailing storage bytes accepted");
                corruptStorage = savedStorageBytes.substr(0, savedStorageBytes.size() - 2);
                rejects([&] { playerStorage.load(view.sol(), std::string_view(corruptStorage)); }, "Truncated storage bytes accepted");
            });
            require(playerStorage.snapshot() == storageBefore, "Rejected storage load mutated state");

            const auto globalBefore = serializeScripts(globalScripts);
            activeScoped = &recoveryCtx;
            ESM::LuaScripts badScripts = savedGlobal;
            badScripts.mScripts[0].mScriptId = 99;
            rejects([&] { globalScripts.load(badScripts); }, "Unknown saved Lua script id accepted");
            badScripts = savedGlobal;
            badScripts.mScripts[0].mTimers[0].mCallbackName = "missing_cb";
            rejects([&] { globalScripts.load(badScripts); }, "Unregistered saved timer callback accepted");
            badScripts = savedGlobal;
            badScripts.mScripts[0].mTimers[0].mTime = std::numeric_limits<double>::infinity();
            rejects([&] { globalScripts.load(badScripts); }, "Nonfinite saved timer time accepted");
            badScripts = savedGlobal;
            badScripts.mScripts[0].mData.push_back('x');
            rejects([&] { globalScripts.load(badScripts); }, "Corrupt saved Lua script data accepted");
            activeScoped = nullptr;
            require(serializeScripts(globalScripts) == globalBefore, "Rejected Lua script load mutated container");
        }

        // Restore into a fresh runtime and verify the recovered timer and queued event execute once.
        activeScoped = &recoveryCtx;
        LuaUtil::LuaStorage recoveredStorage;
        recoveredStorage.setActive(true);
        recoveredStorage.setValidator([&](bool) { (void)validateActive(); });
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            recoveredStorage.load(view.sol(), std::string_view(savedStorageBytes));
            globalScripts.addPackage("openmw.storage",
                LuaUtil::LuaStorage::initPlayerPackage(view, &globalStorage, &recoveredStorage));
        });
        globalScripts.load(savedGlobal);
        localActor.load(savedLocal);
        {
            ESM::ESMReader reader;
            reader.open(std::make_unique<std::istringstream>(savedEventsBytes, std::ios::binary), {});
            require(reader.getRecName() == ESM::fourCC("LUAM"), "Missing LUAM record");
            reader.getRecHeader();
            std::map<int, int> mapping;
            lua.protectedCall([&](LuaUtil::LuaView& view) {
                events.load(view.sol(), reader, mapping, globalSerializer.get());
            });
        }
        gameTime = 10.0;
        globalScripts.processTimers(simTime, gameTime);
        events.callEventHandlers();
        events.finalizeEventBatch();
        events.callEventHandlers();

        ESM::LuaScripts postRecoveryGlobal, postRecoveryLocal;
        globalScripts.save(postRecoveryGlobal);
        localActor.save(postRecoveryLocal);
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::table gState = LuaUtil::deserialize(view.sol(), postRecoveryGlobal.mScripts[0].mData);
            sol::table lState = LuaUtil::deserialize(view.sol(), postRecoveryLocal.mScripts[0].mData);
            require(gState.get<int>("step") == 11 && gState.get<int>("events") == 6
                && gState.get<int>("cachedStage") == 12 && lState.get<int>("synced") == 9
                && postRecoveryGlobal.mScripts[0].mTimers.empty()
                && committedBank.scripts[0].mLocals.mLongs[0] == 10,
                "Recovered Lua runtime did not continue timers/events/storage coherently");
        });
        activeScoped = nullptr;
        std::cout << "lua-rollback: late/subscriber/timer/event rejection, durability/wait rollback, retry, LUAM/storage recovery\n";
    }

    void luaStock()
    {
        MemoryFile stockLua(R"lua(
local async = require('openmw.async')
local storage = require('openmw.storage')
local count = 0
local sec = storage.globalSection('stock_sec')
return {
    engineHandlers = {
        onInit = function()
            sec:set('count', 1)
            async:newUnsavableSimulationTimer(1.0, function()
                count = count + 4
                sec:set('count', count)
            end)
        end,
        onSave = function() return { count = count } end,
        onLoad = function(state) if state then count = state.count end end,
    },
    eventHandlers = {
        Ping = function() count = count + 2 end,
    }
}
)lua");
        VFS::FileMap files;
        files.emplace(VFS::Path::Normalized("stock.lua"), &stockLua);
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<MemoryArchive>(std::move(files)));
        vfs.buildIndex();

        ESM::LuaScriptsCfg rawCfg;
        LuaUtil::parseOMWScripts(rawCfg, "GLOBAL: stock.lua\n");
        LuaUtil::ScriptsConfiguration cfg;
        cfg.init(std::move(rawCfg), false);
        LuaUtil::LuaState lua(&vfs, &cfg);
        double simTime = 0.0, gameTime = 0.0;
        LuaUtil::LuaStorage globalStorage;
        globalStorage.setActive(true);
        MWLua::GlobalScripts globalScripts(&lua);
        globalScripts.setAutoStartConf(cfg.getGlobalConf());
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            LuaUtil::LuaStorage::initLuaBindings(view);
            lua.addCommonPackage("openmw.async",
                LuaUtil::getAsyncPackageInitializer(view.sol(), [&] { return simTime; }, [&] { return gameTime; }));
            globalScripts.addPackage("openmw.storage", LuaUtil::LuaStorage::initGlobalPackage(view, &globalStorage));
        });
        globalScripts.addAutoStartedScripts();
        simTime = 2.0;
        globalScripts.processTimers(simTime, gameTime);

        MWLua::LuaEvents events(globalScripts);
        events.addGlobalEvent({"Ping", {}});
        std::ostringstream stream(std::ios::binary);
        ESM::ESMWriter writer;
        writer.setVersion(ESM::VER_130);
        writer.setType(0);
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.setRecordCount(1);
        writer.save(stream);
        writer.startRecord("LUAM");
        events.save(writer);
        writer.endRecord("LUAM");
        writer.close();

        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(stream.str(), std::ios::binary), {});
        require(reader.getRecName() == ESM::fourCC("LUAM"), "Missing stock LUAM record");
        reader.getRecHeader();
        std::map<int, int> mapping;
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            events.load(view.sol(), reader, mapping, nullptr);
        });
        events.callEventHandlers();

        ESM::LuaScripts saved;
        globalScripts.save(saved);
        lua.protectedCall([&](LuaUtil::LuaView& view) {
            sol::table state = LuaUtil::deserialize(view.sol(), saved.mScripts[0].mData);
            sol::object sec = globalStorage.getReadOnlySection(view.sol(), "stock_sec");
            sol::object countObj = sec.as<sol::table>()["get"](sec, "count");
            require(state.get<int>("count") == 6 && countObj.as<int>() == 4,
                "Stock unscoped Lua timers, events or storage changed");
        });
        std::cout << "lua-stock: unchanged unscoped containers, unsavable timers, empty-payload LUAE events and storage\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2,
            "Usage: tes3mp_native_script_context_tests lookup|rollback|stock|cross-script|cross-script-rollback|cross-script-stock|lua-context|lua-rollback|lua-stock");
        MWClass::registerClasses();
        const std::string_view filter = argv[1];
        if (filter == "lookup") lookup();
        else if (filter == "rollback") rollback();
        else if (filter == "stock") stock();
        else if (filter == "cross-script") crossScript();
        else if (filter == "cross-script-rollback") crossScriptRollback();
        else if (filter == "cross-script-stock") crossScriptStock();
        else if (filter == "lua-context" || filter == "lua") luaContext();
        else if (filter == "lua-rollback") luaRollback();
        else if (filter == "lua-stock") luaStock();
        else throw std::invalid_argument("Unknown script context filter");
        std::cout << "PASS script-context " << filter << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
