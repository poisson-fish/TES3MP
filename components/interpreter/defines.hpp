#ifndef INTERPRETER_DEFINES_H_INCLUDED
#define INTERPRETER_DEFINES_H_INCLUDED

#include "context.hpp"
#include <cstddef>
#include <string>

namespace Interpreter
{
    std::string fixDefinesDialog(std::string_view text, Context& context);
    // Transactional callers bound expansion and propagate failed context reads.
    // Globals are looked up per call, never through the stock process-wide cache.
    std::string fixDefinesDialog(std::string_view text, Context& context, size_t maximumBytes);
    std::string fixDefinesMsgBox(std::string_view text, Context& context);
    std::string fixDefinesBook(std::string_view text, Context& context);
}

#endif
