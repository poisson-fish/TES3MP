#ifndef TES3MP_NATIVE_CELL_SELECTION_HPP
#define TES3MP_NATIVE_CELL_SELECTION_HPP

#include <components/esm/refid.hpp>
#include <stdexcept>

namespace TES3MP::Native
{
    // Typed TES3 cell identity, shared by discovery and resource activation.
    // Bound coordinates before engine grid arithmetic or float conversion.
    inline void validateCell(ESM::RefId cell)
    {
        if (const auto* exterior = cell.getIf<ESM::ESM3ExteriorCellRefId>())
        {
            if (exterior->getX() < -32768 || exterior->getX() > 32767
                || exterior->getY() < -32768 || exterior->getY() > 32767)
                throw std::invalid_argument("Native exterior coordinates outside supported bounds");
            return;
        }
        if (!cell.is<ESM::StringRefId>() || cell.empty() || cell.getRefIdString().size() > 256
            || cell.getRefIdString().find_first_of("\r\n\t") != std::string::npos
            || cell.getRefIdString().find('\0') != std::string::npos)
            throw std::invalid_argument("Native interior identity invalid");
    }

    inline ESM::RefId interiorCell(std::string_view name)
    {
        // Validate before interning externally supplied names.
        if (name.empty() || name.size() > 256 || name.find_first_of("\r\n\t") != std::string_view::npos
            || name.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Native interior name invalid");
        return ESM::RefId::stringRefId(name);
    }
}
#endif
