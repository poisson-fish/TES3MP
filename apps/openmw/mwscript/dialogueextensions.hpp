#ifndef GAME_SCRIPT_DIALOGUEEXTENSIONS_H
#define GAME_SCRIPT_DIALOGUEEXTENSIONS_H

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
    /// \brief Dialogue/Journal-related script functionality
    namespace Dialogue
    {
        void registerJournalQueryOpcode(Compiler::Extensions& extensions);
        void installJournalQueryOpcode(Interpreter::Interpreter& interpreter);
        void installOpcodes(Interpreter::Interpreter& interpreter);
        void installJournalOpcodes(Interpreter::Interpreter& interpreter);
    }
}

#endif
