#include "faction_scripts.hpp"

#include <stdexcept>
#include <sstream>

#include <components/compiler/context.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/fileparser.hpp>
#include <components/compiler/opcodes.hpp>
#include <components/compiler/scanner.hpp>
#include <components/compiler/streamerrorhandler.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>

#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/statsextensions.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace TES3MP::Native
{
    namespace
    {
        class FactionCompilerContext final : public Compiler::Context
        {
        public:
            bool canDeclareLocals() const override { return false; }
            char getGlobalType(const std::string&) const override { return ' '; }
            std::pair<char, bool> getMemberType(const std::string&, const ESM::RefId&) const override
            { return {' ', false}; }
            bool isId(const ESM::RefId&) const override { return false; }
        };

        void registerFactionInstructions(Compiler::Extensions& extensions)
        {
            using namespace Compiler::Stats;
            extensions.registerInstruction("pcjoinfaction", "/S", opcodePCJoinFaction);
            extensions.registerInstruction("pcraiserank", "/S", opcodePCRaiseRank);
            extensions.registerInstruction("pclowerrank", "/S", opcodePCLowerRank);
            extensions.registerFunction("getpcrank", 'l', "/S", opcodeGetPCRank);
            extensions.registerFunction("pcexpelled", 'l', "/S", opcodePcExpelled);
            extensions.registerInstruction("pcexpell", "/S", opcodePcExpell);
            extensions.registerInstruction("pcclearexpelled", "/S", opcodePcClearExpelled);
        }
        class SetLevitation final : public Interpreter::Opcode0
        {
            bool& mEnabled;
            bool mValue;
        public:
            SetLevitation(bool& enabled, bool value) : mEnabled(enabled), mValue(value) {}
            void execute(Interpreter::Runtime&) override { mEnabled = mValue; }
        };
    }

    ActorCampaignCombat::PlayerAi runFactionScript(const MWWorld::ESMStore& content,
        const ESM::RefId& scriptId, const MWWorld::Ptr& actor, const MWWorld::Ptr& player,
        const ActorCampaignCombat::PlayerAi& before)
    {
        if (scriptId.empty() || scriptId.serializeText().size() > 256
            || actor.isEmpty() || player.isEmpty())
            throw std::invalid_argument("Faction script context is incomplete");
        const auto* script = content.get<ESM::Script>().search(scriptId);
        if (!script || script->mScriptText.empty() || script->mScriptText.size() > 4096)
            throw std::invalid_argument("Faction script missing or exceeds bound");

        Compiler::Extensions extensions;
        registerFactionInstructions(extensions);
        FactionCompilerContext compilerContext;
        compilerContext.setExtensions(&extensions);
        Compiler::StreamErrorHandler errors;
        Compiler::FileParser parser(errors, compilerContext);
        std::istringstream source(script->mScriptText);
        Compiler::Scanner scanner(errors, source, compilerContext.getExtensions());
        scanner.scan(parser);
        if (!errors.isGood())
            throw std::invalid_argument("Unsupported or invalid faction script");
        auto program = parser.getProgram();
        if (program.mInstructions.size() > 256)
            throw std::invalid_argument("Faction script program exceeds bound");

        MWMechanics::NpcStats stats(content);
        for (const auto& faction : before.factions)
        {
            if (!content.get<ESM::Faction>().search(faction.id))
                throw std::invalid_argument("Faction script player state differs from content");
            stats.joinFaction(faction.id);
            stats.setFactionRank(faction.id, faction.rank, content);
            if (stats.getFactionRank(faction.id) != faction.rank)
                throw std::invalid_argument("Faction script player rank differs from content");
            if (faction.expelled) stats.expell(faction.id, false);
        }

        MWScript::InterpreterContext context(nullptr, actor, player, content, &stats);
        Interpreter::Interpreter interpreter;
        Interpreter::installOpcodes(interpreter);
        MWScript::Stats::installOpcodes(interpreter);
        interpreter.run(program, context, 256);

        auto result = before;
        result.factions.clear();
        if (stats.getFactionRanks().size() > 256)
            throw std::invalid_argument("Faction script membership exceeds bound");
        for (const auto& [id, rank] : stats.getFactionRanks())
        {
            if (rank < 0 || rank > 9 || !content.get<ESM::Faction>().search(id))
                throw std::invalid_argument("Faction script result invalid");
            result.factions.push_back({id, rank, stats.getExpelled(id)});
        }
        for (const auto& id : stats.getExpelled())
            if (!stats.isInFaction(id))
                throw std::invalid_argument("Faction script expelled an absent member");
        return result;
    }

    bool runMovementRuleScript(const MWWorld::ESMStore& content, const ESM::RefId& scriptId,
        const MWWorld::Ptr& actor, bool before)
    {
        if (scriptId.empty() || scriptId.serializeText().size() > 256 || actor.isEmpty())
            throw std::invalid_argument("Movement rule script context incomplete");
        const auto* script = content.get<ESM::Script>().search(scriptId);
        if (!script || script->mScriptText.empty() || script->mScriptText.size() > 4096)
            throw std::invalid_argument("Movement rule script missing or exceeds bound");
        Compiler::Extensions extensions;
        extensions.registerInstruction("disablelevitation", "", Compiler::Misc::opcodeDisableLevitation);
        extensions.registerInstruction("enablelevitation", "", Compiler::Misc::opcodeEnableLevitation);
        FactionCompilerContext compilerContext;
        compilerContext.setExtensions(&extensions);
        Compiler::StreamErrorHandler errors;
        Compiler::FileParser parser(errors, compilerContext);
        std::istringstream source(script->mScriptText);
        Compiler::Scanner scanner(errors, source, compilerContext.getExtensions());
        scanner.scan(parser);
        if (!errors.isGood()) throw std::invalid_argument("Unsupported or invalid movement rule script");
        auto program = parser.getProgram();
        if (program.mInstructions.empty() || program.mInstructions.size() > 256)
            throw std::invalid_argument("Movement rule script instruction count invalid");
        bool result = before;
        MWScript::InterpreterContext context(nullptr, actor, actor, content);
        Interpreter::Interpreter interpreter;
        Interpreter::installOpcodes(interpreter);
        interpreter.installSegment5<SetLevitation>(Compiler::Misc::opcodeDisableLevitation, result, false);
        interpreter.installSegment5<SetLevitation>(Compiler::Misc::opcodeEnableLevitation, result, true);
        interpreter.run(program, context, 256);
        return result;
    }
}
