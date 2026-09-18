#include <tes3mp/native_door.hpp>
#include <array>

namespace TES3MP
{
    std::vector<std::byte> encodeClientDoorObstruction(const ClientDoorObstruction& report)
    {
        std::vector<std::byte> bytes{std::byte{1}};
        for (uint64_t value : {report.session.value(), report.generation.value(), report.observedTick.value(),
                 report.placement, report.motion, report.sequence})
            for (unsigned i = 0; i < 8; ++i) bytes.push_back(std::byte((value >> (8 * i)) & 255));
        bytes.push_back(std::byte(report.blocked));
        return bytes;
    }

    std::optional<ClientDoorObstruction> decodeClientDoorObstruction(std::span<const std::byte> bytes)
    {
        if (bytes.size() != 50 || bytes[0] != std::byte{1} || bytes[49] > std::byte{1}) return {};
        std::array<uint64_t, 6> values{};
        for (size_t i = 0; i < values.size(); ++i)
            for (unsigned j = 0; j < 8; ++j)
                values[i] |= uint64_t(std::to_integer<unsigned char>(bytes[1 + i * 8 + j])) << (8 * j);
        const auto session = SessionId::fromValue(values[0]);
        const auto generation = SessionGeneration::fromValue(values[1]);
        const auto tick = ServerTick::fromValue(values[2]);
        if (!session || !generation || !tick || !(values[3] >> 63) || !values[4] || !values[5]) return {};
        return ClientDoorObstruction{*session, *generation, *tick, values[3], values[4], values[5], bytes[49] == std::byte{1}};
    }
}
