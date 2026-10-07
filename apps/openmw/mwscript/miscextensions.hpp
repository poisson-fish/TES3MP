#ifndef GAME_SCRIPT_MISCEXTENSIONS_H
#define GAME_SCRIPT_MISCEXTENSIONS_H

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
    namespace Misc
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
        void registerScriptOpcodes(Compiler::Extensions& extensions);
        void installScriptOpcodes(Interpreter::Interpreter& interpreter);
    }
}

#endif
