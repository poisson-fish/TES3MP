#ifndef TES3MP_NATIVE_PLACEMENT_TESTS_HPP
#define TES3MP_NATIVE_PLACEMENT_TESTS_HPP
#include <tes3mp/inventory_world.hpp>
#include <filesystem>
namespace TES3MP::Native::Testing
{
    void checkItemPlacement();
    void writePlacementFixtureModels(const std::filesystem::path& scratch);
    DropPlacementView placementTestView(float x, float z, bool miss = false);
}
#endif
