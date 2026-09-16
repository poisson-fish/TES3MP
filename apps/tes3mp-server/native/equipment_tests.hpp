#ifndef TES3MP_NATIVE_EQUIPMENT_TESTS_H
#define TES3MP_NATIVE_EQUIPMENT_TESTS_H

#include <filesystem>
#include <string_view>

namespace MWWorld::Testing
{
    void checkPlainEquipment(std::string_view filter, const std::filesystem::path& scratch);
}

#endif
