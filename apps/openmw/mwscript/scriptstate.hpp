#ifndef OPENMW_MWSCRIPT_SCRIPTSTATE_H
#define OPENMW_MWSCRIPT_SCRIPTSTATE_H

#include "executioncontext.hpp"
#include "globalscripts.hpp"

#include <components/compiler/locals.hpp>
#include <cstddef>

namespace MWScript
{
    // A borrowed view of the caller's staged, explicitly admitted script bank.
    // Resolve must revalidate every execution identity, reject foreign/unknown
    // scripts, and throw ScriptContextUnavailable for temporary dependencies.
    // It must never borrow live state, lazily allocate an instance, or execute it.
    // The caller commits locals/running flags with invocation receipts/gameplay.
    class ScriptState
    {
    public:
        static constexpr std::size_t MaximumLocals = 64;
        static constexpr std::size_t MaximumNameBytes = 128;
        struct Binding
        {
            GlobalScriptDesc& instance;
            const Compiler::Locals& declarations;
        };
        virtual ~ScriptState() = default;
        virtual Binding resolve(const ExecutionContext&, const ESM::RefId& script) = 0;
    };
}

#endif
