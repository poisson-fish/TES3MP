#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#include <filesystem>
namespace TES3MP::Native::Testing
{
    void checkDoorService(const std::filesystem::path& scratch);
    void checkPlacedContainers(const std::filesystem::path& scratch);
    void checkPlacedActors(const std::filesystem::path& scratch);
    void checkStockedInventory(const std::filesystem::path& scratch);
    void checkCellInventories(const std::filesystem::path& scratch);
    void checkPlayerInventories(const std::filesystem::path& scratch);
    void checkStartingEquipment(const std::filesystem::path& scratch);
    void checkWorldActorInventories(const std::filesystem::path& scratch);
    void checkPlacedItems(const std::filesystem::path& scratch);
    void checkWorldItems(const std::filesystem::path& scratch);
    void checkBulkTakeAll(const std::filesystem::path& scratch);
    void checkEquipmentSlots(const std::filesystem::path& scratch);
    void checkInventoryService(const std::filesystem::path& scratch, bool durability);
    void checkCanonicalInventory(const std::filesystem::path& scratch, bool equipment = false, bool takeAll = false, bool worldItems = false);
    void checkInventoryApplication(const std::filesystem::path& scratch);
    void checkInventoryHost(const std::filesystem::path& scratch, const std::filesystem::path& config,
        bool wholeInterior = false, bool baseInventory = false, bool worldActors = false, bool worldItems = false,
        bool stockPlacement = false, bool door = false);
}
#endif
