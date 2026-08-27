#ifndef TES3MP_TEST_SUPPORT_SPATIAL_ROUND_TRIP_HPP
#define TES3MP_TEST_SUPPORT_SPATIAL_ROUND_TRIP_HPP

#include <tes3mp/command_primitives.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace TES3MP::TestSupport
{
    std::vector<std::byte> encodeSpatialEntitySnapshot(const SpatialEntitySnapshot& snapshot);
    std::optional<SpatialEntitySnapshot> decodeSpatialEntitySnapshot(std::span<const std::byte> bytes);
}

#endif
