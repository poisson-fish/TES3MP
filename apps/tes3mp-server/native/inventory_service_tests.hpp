#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#include <filesystem>
namespace TES3MP::Native::Testing
{
    void checkInventoryService(const std::filesystem::path& scratch, bool durability);
}
#endif
