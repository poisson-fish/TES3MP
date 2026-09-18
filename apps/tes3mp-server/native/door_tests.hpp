#ifndef TES3MP_NATIVE_DOOR_TESTS_HPP
#define TES3MP_NATIVE_DOOR_TESTS_HPP

#include <filesystem>
#include <string_view>

namespace TES3MP::Native::Testing
{
    void checkOrdinaryDoor();
    void checkDoorCodec();
    void checkPlacedDoor(const std::filesystem::path& scratch);
    void checkDoorLoadout(const std::filesystem::path& configDirectory, std::string_view cell);
}

#endif
