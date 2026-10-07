#ifndef OPENMW_MWSCRIPT_WORLDSCRIPT_H
#define OPENMW_MWSCRIPT_WORLDSCRIPT_H

#include <components/compiler/locals.hpp>
#include <components/esm3/globalscript.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/interpreter/program.hpp>

namespace ESM { class Script; }
namespace Interpreter { struct ExecutionTrace; }

namespace MWScript
{
    // Context-free subset of the stock compiler/interpreter. Callers supply
    // explicitly owned globals and stage the returned state in their transaction.
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

        WorldScript(const ESM::Script& script, std::vector<ESM::Global> ownedGlobals);
        const ESM::RefId& id() const { return mId; }
        State initial() const;
        void validate(const State& state) const;
        Invocation execute(const State& before, Interpreter::ExecutionTrace* trace = nullptr) const;

    private:
        ESM::RefId mId;
        Compiler::Locals mDeclarations;
        Interpreter::Program mProgram;
        std::vector<ESM::Global> mGlobals;
    };
}

#endif
