#ifndef TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#define TES3MP_NATIVE_INVENTORY_SERVICE_TESTS_HPP
#include <filesystem>
namespace TES3MP::Native::Testing
{
    void checkTravelerNeighborhood(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&);
    void checkNpcDoors(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&,
        bool avoidance = false, bool traveler = false, bool melee = false, bool combat = false,
        bool lifecycle = false, bool spell = false, bool projectile = false, bool timed = false,
        bool area = false, bool playerTarget = false, bool collection = false, bool strike = false,
        bool knockout = false, bool defense = false, bool shield = false,
        bool effectLifecycle = false);
    void checkNavigatingActor(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&);
    void checkDoorService(const std::filesystem::path& scratch, bool streaming = false);
    void checkAreaCrossings(const std::filesystem::path& scratch);
    void checkCellLifecycle(const std::filesystem::path& scratch);
    void checkTeleportTraversal(const std::filesystem::path& scratch);
    void checkExteriorReferences(const std::filesystem::path& scratch);
    void checkPlayerAreas(const std::filesystem::path& scratch);
    void checkPlacedContainers(const std::filesystem::path& scratch);
    void checkPlacedActors(const std::filesystem::path& scratch);
    void checkLeveledActors(const std::filesystem::path& scratch);
    void checkLeveledActorPersistence(const std::filesystem::path& scratch);
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
    void checkInventoryApplication(const std::filesystem::path& scratch, bool environment = false);
    void checkInventoryHost(const std::filesystem::path& scratch, const std::filesystem::path& config,
        bool wholeInterior = false, bool baseInventory = false, bool worldActors = false, bool worldItems = false,
        bool stockPlacement = false, bool door = false, bool twoCells = false, bool teleports = false, bool environment = false, bool exterior = false);
}
#endif
