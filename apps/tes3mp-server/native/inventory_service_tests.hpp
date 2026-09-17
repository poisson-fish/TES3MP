#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#include <filesystem>
namespace TES3MP::Native::Testing
{
    void checkInventoryService(const std::filesystem::path& scratch, bool durability);
    void checkCanonicalInventory(const std::filesystem::path& scratch);
    void checkInventoryApplication(const std::filesystem::path& scratch);
    void checkInventoryHost(const std::filesystem::path& scratch, const std::filesystem::path& config);
}
#endif
