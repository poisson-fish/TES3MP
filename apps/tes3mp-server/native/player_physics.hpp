#ifndef TES3MP_NATIVE_PLAYER_PHYSICS_HPP
#define TES3MP_NATIVE_PLAYER_PHYSICS_HPP
#include "actor_spawns.hpp"
#include <tes3mp/movement_policy.hpp>
#include <tes3mp/canonical_persistence.hpp>
#include <array>
#include <optional>
#include <bit>

namespace TES3MP::Native
{
    inline void putActorProcessingCell(std::vector<char>& bytes, CellId cell)
    {
        putAreaWord(bytes, uint64_t(cell.kind()));
        putAreaWord(bytes, cell.asInterior() ? cell.asInterior()->cellSpace().value() : cell.asExterior()->worldspace().value());
        putAreaWord(bytes, cell.asExterior() ? uint32_t(cell.asExterior()->gridX()) : 0);
        putAreaWord(bytes, cell.asExterior() ? uint32_t(cell.asExterior()->gridY()) : 0);
    }
    inline CellId getActorProcessingCell(std::span<const char> bytes, size_t& offset)
    {
        const auto kind = getAreaWord(bytes, offset), space = getAreaWord(bytes, offset);
        const auto x = getAreaWord(bytes, offset), y = getAreaWord(bytes, offset);
        const auto id = CellSpaceId::fromValue(space);
        if (!id || x > UINT32_MAX || y > UINT32_MAX
            || (kind == uint64_t(CellId::Kind::Interior) ? x || y : kind != uint64_t(CellId::Kind::Exterior)))
            throw std::invalid_argument("Actor processing cell invalid");
        if (kind == uint64_t(CellId::Kind::Interior)) return CellId::interior(*id);
        const auto gx = std::bit_cast<int32_t>(uint32_t(x)), gy = std::bit_cast<int32_t>(uint32_t(y));
        if (gx < -32768 || gx > 32767 || gy < -32768 || gy > 32767)
            throw std::invalid_argument("Actor processing coordinates invalid");
        return CellId::exterior(*id, gx, gy);
    }
    inline constexpr uint64_t PlayerPhysicsCampaignMagic = 0x3950435054335354;
    inline constexpr uint64_t PlayerHullBase = 0x4000000000000001ull;
    inline constexpr uint64_t PlayerControlExpiryTicks = 12;
    struct PlayerPhysicsState
    {
        uint64_t player = 0, epoch = 0, inputTick = 0, sequence = 0;
        // Authentication bindings are live only and never serialized.
        uint64_t session = 0, generation = 0;
        std::optional<Transform> root;
        PlayerMovementControls controls;
        Turn32 facing = Turn32::fromValue(0);
        LocomotionMode mode = LocomotionMode::Walk;
        std::vector<char> image;
    };

    inline std::vector<char> sealPlayerPhysics(std::span<const char> inner,
        const std::array<PlayerPhysicsState, 2>& states)
    {
        std::vector<char> bytes;
        putAreaWord(bytes, PlayerPhysicsCampaignMagic);
        for (const auto& state : states)
        {
            putAreaWord(bytes, state.root.has_value());
            if (!state.root) continue;
            if (!state.player || !state.epoch || state.image.empty() || state.image.size() > 65536
                || !state.controls.valid()) throw std::invalid_argument("Player physics image outside bounds");
            for (uint64_t word : {state.player, state.epoch, state.inputTick, state.sequence})
                putAreaWord(bytes, word);
            putActorProcessingCell(bytes, state.root->cell());
            for (auto axis : {state.root->position().x(), state.root->position().y(), state.root->position().z()})
                putAreaWord(bytes, std::bit_cast<uint64_t>(axis));
            for (auto turn : {state.root->orientation().x(), state.root->orientation().y(), state.root->orientation().z()})
                putAreaWord(bytes, turn.value());
            for (auto axis : {state.controls.right, state.controls.forward, state.controls.vertical})
                putAreaWord(bytes, uint64_t(int(axis) + 1024));
            for (uint64_t word : {uint64_t(state.controls.pitch.value()), uint64_t(state.controls.jump),
                    uint64_t(state.facing.value()), uint64_t(state.mode), uint64_t(state.image.size())})
                putAreaWord(bytes, word);
            bytes.insert(bytes.end(), state.image.begin(), state.image.end());
        }
        if (bytes.size() > MaximumNativeInventoryImageBytes
            || inner.size() > MaximumNativeInventoryImageBytes - bytes.size())
            throw std::invalid_argument("Player physics campaign exceeds budget");
        bytes.insert(bytes.end(), inner.begin(), inner.end());
        return bytes;
    }

    inline std::pair<std::array<PlayerPhysicsState, 2>, std::span<const char>> readPlayerPhysics(
        std::span<const char> bytes)
    {
        if (bytes.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Player physics campaign exceeds budget");
        size_t offset = 0;
        if (getAreaWord(bytes, offset) != PlayerPhysicsCampaignMagic)
            throw std::invalid_argument("Player physics campaign version mismatch");
        std::array<PlayerPhysicsState, 2> states;
        for (auto& state : states)
        {
            const auto present = getAreaWord(bytes, offset);
            if (present > 1) throw std::invalid_argument("Player physics presence invalid");
            if (!present) continue;
            for (auto* field : {&state.player, &state.epoch, &state.inputTick, &state.sequence})
                *field = getAreaWord(bytes, offset);
            const auto cell = getActorProcessingCell(bytes, offset);
            std::array<int64_t, 3> position;
            for (auto& axis : position)
            {
                axis = std::bit_cast<int64_t>(getAreaWord(bytes, offset));
                if (axis < -10000000ll * 1024 || axis > 10000000ll * 1024)
                    throw std::invalid_argument("Player physics position invalid");
            }
            const auto turn = [&]() {
                const auto value = getAreaWord(bytes, offset);
                if (value > UINT32_MAX) throw std::invalid_argument("Player physics turn invalid");
                return Turn32::fromValue(uint32_t(value));
            };
            const auto pitch = turn(), roll = turn(), yaw = turn();
            state.root.emplace(cell, Position3(position[0], position[1], position[2]), Orientation3(pitch, roll, yaw));
            for (auto* axis : {&state.controls.right, &state.controls.forward, &state.controls.vertical})
            {
                const auto value = getAreaWord(bytes, offset);
                if (value > 2048) throw std::invalid_argument("Player physics control invalid");
                *axis = int16_t(int(value) - 1024);
            }
            state.controls.pitch = turn();
            const auto jump = getAreaWord(bytes, offset);
            if (jump > 1) throw std::invalid_argument("Player physics jump invalid");
            state.controls.jump = jump;
            state.facing = turn();
            const auto mode = getAreaWord(bytes, offset), length = getAreaWord(bytes, offset);
            if (!state.player || !state.epoch || state.sequence > MaximumLocomotionInputOrdinal
                || mode > uint64_t(LocomotionMode::Jump)
                || !length || length > 65536 || length > bytes.size() - offset)
                throw std::invalid_argument("Player physics domain invalid");
            state.mode = LocomotionMode(mode);
            state.image.assign(bytes.begin() + offset, bytes.begin() + offset + size_t(length));
            offset += size_t(length);
        }
        if (states[0].player && states[0].player == states[1].player)
            throw std::invalid_argument("Player physics duplicate identity");
        return {std::move(states), bytes.subspan(offset)};
    }
}
#endif
