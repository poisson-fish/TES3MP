#ifndef OPENMW_SPELLPRESENTATIONTRACE_H
#define OPENMW_SPELLPRESENTATIONTRACE_H

// Bounded evidence for the desktop test build, shared by stock and committed paths.
#ifdef TES3MP_OPENMW_DESKTOP_AUTOMATION
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string_view>
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

namespace MWMechanics
{
    inline void traceSpellPresentation(std::string_view kind, const MWWorld::Ptr& target,
        std::string_view resource, bool attached = false)
    {
        static std::ofstream output([] {
            const char* path = std::getenv("TES3MP_SPELL_PRESENTATION_TRACE");
            return path ? path : "";
        }(), std::ios::out);
        static size_t count = 0;
        if (!output || count >= 4096) return;
        // Resources/record IDs can contain arbitrary content bytes; keep JSON valid.
        const auto quoted = [&](std::string_view value) {
            output << '"';
            for (const unsigned char c : value)
                if (c == '"' || c == '\\') output << '\\' << char(c);
                else if (c < 32) output << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
                else output << char(c);
            output << '"';
        };
        output << "{\"event\":"; quoted(kind);
        output << ",\"target\":"; quoted(target.isEmpty() ? "" : target.getCellRef().getRefId().serializeText());
        output << ",\"resource\":"; quoted(resource);
        output << ",\"attached\":" << (attached ? "true" : "false") << "}\n";
        output.flush(); ++count;
    }
}
#else
#include <string_view>
namespace MWWorld { class Ptr; }
namespace MWMechanics
{
    inline void traceSpellPresentation(std::string_view, const MWWorld::Ptr&, std::string_view, bool = false) {}
}
#endif
#endif
