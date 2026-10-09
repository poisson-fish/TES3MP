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
        void registerSourceWaitOpcodes(Compiler::Extensions& extensions);
        void installSourceWaitOpcodes(Interpreter::Interpreter& interpreter, bool queries = true);
        void registerEnableOpcodes(Compiler::Extensions& extensions);
        void installEnableOpcodes(Interpreter::Interpreter& interpreter);
        void registerDisableOpcodes(Compiler::Extensions& extensions);
        void installDisableOpcodes(Interpreter::Interpreter& interpreter);
    }
}

#endif
