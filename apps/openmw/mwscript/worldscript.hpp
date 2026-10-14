#ifndef OPENMW_MWSCRIPT_WORLDSCRIPT_H
#define OPENMW_MWSCRIPT_WORLDSCRIPT_H

#include <components/compiler/locals.hpp>
#include <components/esm3/globalscript.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/interpreter/program.hpp>
#include <span>

namespace ESM { class Script; }
namespace Interpreter { struct ExecutionTrace; }
namespace MWWorld { class ESMStore; }
namespace MWMechanics { class NpcStats; }

namespace MWScript
{
    struct ExecutionContext;
    class ReferenceResolver;
    class ScriptState;
    class DialogueState;
    class ReferenceState;
    struct GlobalScriptDesc;
    // Bounded subset of the stock compiler/interpreter. Callers supply
    // explicitly owned globals/banks and stage the returned state in their transaction.
    // No player, reference lookup, Environment, UI or script-manager fallback.
    class WorldScript
    {
    public:
        static constexpr std::size_t MaximumSourceBytes = 8192;
        static constexpr std::size_t MaximumInstructions = 2048;
        static constexpr std::size_t MaximumExecutedInstructions = 4096;
        static constexpr std::size_t MaximumLocals = 64;
        static constexpr std::size_t MaximumGlobals = 16;
        static constexpr std::size_t MaximumNameBytes = 128;
        static constexpr std::size_t MaximumMessages = 8;
        static constexpr std::size_t MaximumMessageBytes = 256;

        struct State
        {
            ESM::GlobalScript script;
            std::vector<ESM::Global> globals;
        };
        struct Invocation
        {
            State state;
            std::vector<std::string> presentation;
        };

        WorldScript(const ESM::Script& script, std::vector<ESM::Global> ownedGlobals,
            std::span<const ESM::Script> bank = {}, const ESM::RefId& source = {}, bool lifeEvents = false);
        const ESM::RefId& id() const { return mId; }
        const Compiler::Locals& declarations() const { return mDeclarations; }
        bool bankCapable() const { return mBank; }
        const ESM::RefId& sourceRecord() const { return mSource; }
        Interpreter::Program compileResult(std::string_view source, bool physical = false,
            const ESM::RefId& reference = {}, bool restoration = false, bool relocation = false, bool activity = false) const;
        State initial() const;
        void validate(const State& state) const;
        Invocation execute(const State& before, Interpreter::ExecutionTrace* trace = nullptr) const;
        Invocation execute(const State& before, const ExecutionContext& execution,
            const MWWorld::ESMStore& content, const ReferenceResolver& resolver,
            Interpreter::ExecutionTrace* trace = nullptr, ScriptState* bank = nullptr,
            GlobalScriptDesc* caller = nullptr, DialogueState* dialogue = nullptr,
            const Interpreter::Program* result = nullptr, MWMechanics::NpcStats* sourceNpcStats = nullptr,
            ReferenceState* referenceState = nullptr) const;

    private:
        Invocation executeImpl(const State&, Interpreter::ExecutionTrace*, const ExecutionContext*,
            const MWWorld::ESMStore*, const ReferenceResolver*, ScriptState*, GlobalScriptDesc*,
            DialogueState*, const Interpreter::Program*, MWMechanics::NpcStats*, ReferenceState*) const;
        ESM::RefId mId;
        Compiler::Locals mDeclarations;
        Interpreter::Program mProgram;
        std::vector<ESM::Global> mGlobals;
        bool mBank = false;
        ESM::RefId mSource;
        bool mLifeEvents = false;
        std::vector<std::pair<ESM::RefId, Compiler::Locals>> mMembers;
    };
}

#endif
