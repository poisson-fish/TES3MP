#ifndef GAME_SCRIPT_STATSEXTENSIONS_H
#define GAME_SCRIPT_STATSEXTENSIONS_H

namespace Compiler
{
    class Extensions;
}

namespace Interpreter
{
    class Interpreter;
}

namespace MWScript
{
    /// \brief stats-related script functionality (creatures and NPCs)
    namespace Stats
    {
        void registerDispositionResultOpcode(Compiler::Extensions& extensions);
        void installDispositionResultOpcode(Interpreter::Interpreter& interpreter);
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}

#endif
