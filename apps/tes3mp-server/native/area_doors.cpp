#include "inventory_service.hpp"
#include <apps/openmw/mwworld/esmstore.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace TES3MP::Native
{
    namespace
    {
        constexpr uint64_t AreaMagic = 0x3153414552413354; // T3AREAS1
        void put(EquipmentBytes& bytes, uint64_t value)
        {
            for (unsigned i = 0; i < 8; ++i) bytes.push_back(char(value >> (8 * i)));
        }
        uint64_t get(std::span<const char> bytes, size_t& offset)
        {
            if (offset > bytes.size() || bytes.size() - offset < 8)
                throw std::invalid_argument("Truncated native area image");
            uint64_t value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= uint64_t(uint8_t(bytes[offset++])) << (8 * i);
            return value;
        }
    }

    void InventoryService::initializeAreaDoors()
    {
        if (!mBinding.mStreamExteriors && !mBinding.mDoors.empty())
            throw std::invalid_argument("Automatic doors require a streaming domain");
        if (mBinding.mDoors.size() > 4096) throw std::invalid_argument("Native door campaign budget exceeded");
        std::set<uint64_t> ids;
        for (const auto& door : mBinding.mDoors)
        {
            if (!(door.mId >> 63) || !ids.insert(door.mId).second || !worldDomain(door.mCell))
                throw std::invalid_argument("Native ordinary door mapping invalid");
            DoorBinding binding(*mRuntime.mStore.get<ESM::Door>().find(door.mRef.mRefID), door.mRef);
            auto state = std::make_shared<const ESM::DoorState>(binding.door().initialState());
            mAreaDoors.push_back({std::move(binding), std::move(state)});
        }
    }

    EquipmentBytes InventoryService::sealInventory(std::span<const char> core,
        std::span<const std::shared_ptr<const ESM::DoorState>> doors) const
    {
        if (!mBinding.mStreamExteriors) return {core.begin(), core.end()};
        if (core.empty() || core.size() > MaximumNativeInventoryImageBytes - 24
            || (!doors.empty() && doors.size() != mAreaDoors.size()))
            throw std::invalid_argument("Native area image shape invalid");
        EquipmentBytes result;
        put(result, mBinding.mActorSelections ? SpawnAreaMagic : AreaMagic);
        if (mBinding.mActorSelections)
        {
            validateActorSpawns(*mBinding.mActorSelections);
            put(result, mBinding.mActorSelections->size());
            for (const auto& value : *mBinding.mActorSelections)
            { put(result, value.mPlacement); put(result, value.mRecord); }
        }
        put(result, core.size()); put(result, mAreaDoors.size());
        result.insert(result.end(), core.begin(), core.end());
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
        {
            EquipmentBytes encoded;
            encodeDoor(doors.empty() ? *mAreaDoors[i].state : *doors[i], mAreaDoors[i].binding, encoded);
            if (result.size() > MaximumNativeInventoryImageBytes - 16 - encoded.size())
                throw std::invalid_argument("Native area image exceeds campaign budget");
            put(result, mBinding.mDoors[i].mId); put(result, encoded.size());
            result.insert(result.end(), encoded.begin(), encoded.end());
        }
        if (result.size() > MaximumNativeInventoryImageBytes)
            throw std::invalid_argument("Native area image exceeds campaign budget");
        return result;
    }

    EquipmentBytes InventoryService::replaceAreaCore(std::span<const char> area,
        std::span<const char> core) const
    {
        if (!mBinding.mStreamExteriors) return {core.begin(), core.end()};
        size_t offset = 0;
        if (get(area, offset) != (mBinding.mActorSelections ? SpawnAreaMagic : AreaMagic))
            throw std::invalid_argument("Native melee area image version invalid");
        if (mBinding.mActorSelections && readActorSpawns(area, offset) != *mBinding.mActorSelections)
            throw std::invalid_argument("Native melee area selections changed");
        const size_t lengthOffset = offset;
        const auto oldSize = get(area, offset), doorCount = get(area, offset);
        if (!oldSize || oldSize > area.size() - offset || doorCount != mAreaDoors.size()
            || core.size() > MaximumNativeInventoryImageBytes - (area.size() - size_t(oldSize)))
            throw std::invalid_argument("Native melee area image bounds invalid");
        EquipmentBytes result(area.begin(), area.begin() + lengthOffset);
        put(result, core.size()); put(result, doorCount);
        result.insert(result.end(), core.begin(), core.end());
        result.insert(result.end(), area.begin() + offset + size_t(oldSize), area.end());
        return result;
    }

    void InventoryService::recoverAreas(std::span<const std::byte> image, std::span<const ESM::RefId> references,
        std::span<const char> actor)
    {
        const std::span bytes(reinterpret_cast<const char*>(image.data()), image.size());
        size_t offset = 0;
        if (get(bytes, offset) != (mBinding.mActorSelections ? SpawnAreaMagic : AreaMagic))
            throw std::invalid_argument("Native area image version incompatible");
        if (mBinding.mActorSelections && readActorSpawns(bytes, offset) != *mBinding.mActorSelections)
            throw std::invalid_argument("Native actor selections differ from bound campaign");
        const auto coreSize = get(bytes, offset), count = get(bytes, offset);
        if (!coreSize || coreSize > bytes.size() - offset || count != mAreaDoors.size())
            throw std::invalid_argument("Native area image lengths or bindings invalid");
        const auto core = bytes.subspan(offset, size_t(coreSize)); offset += size_t(coreSize);
        std::vector<std::span<const char>> images;
        for (size_t i = 0; i < count; ++i)
        {
            const auto id = get(bytes, offset), size = get(bytes, offset);
            if (id != mBinding.mDoors[i].mId || !size || size > MaxDoorBytes || size > bytes.size() - offset)
                throw std::invalid_argument("Native door image identity or length invalid");
            const auto door = bytes.subspan(offset, size_t(size)); offset += size_t(size);
            mAreaDoors[i].binding.preflight(door);
            images.push_back(door);
        }
        if (offset != bytes.size()) throw std::invalid_argument("Trailing native area image bytes");
        std::vector<std::shared_ptr<const ESM::DoorState>> states;
        for (size_t i = 0; i < count; ++i) states.push_back(decodeDoor(images[i], mAreaDoors[i].binding));
        auto sealed = sealInventory(core, states);
        if (!std::ranges::equal(sealed, bytes)) throw std::invalid_argument("Noncanonical native area image");
        std::unique_ptr<InteriorActorScene::Prepared> step;
        if (mBinding.mNavigatingActor)
        {
            std::vector<ActorSceneDoor> doors;
            for (size_t i = 0; i < states.size(); ++i)
                doors.push_back({mBinding.mDoors[i].mId, states[i]->mPosition.rot[2], states[i]->mDoorState != 0});
            step = mBinding.mNavigatingActor->prepareRestore(actor, doors);
        }
        EquipmentBytes accepted(core.begin(), core.end()), restored;
        std::unique_ptr<const EquipmentSessionValues> values;
        mRuntime.restoreSession(std::move(accepted), references, values, restored);
        for (size_t i = 0; i < count; ++i) mAreaDoors[i].state.swap(states[i]);
        mCoreImage.swap(restored);
        mImage.swap(sealed);
        if (step) mBinding.mNavigatingActor->install(*step);
    }

    class InventoryService::AreaDoorTransaction final : public PreparedNativeInventory
    {
    public:
        InventoryService& service;
        EquipmentBytes before, image;
        std::unique_ptr<PreparedNativeInventory> command;
        std::vector<std::shared_ptr<const ESM::DoorState>> states;
        std::vector<uint64_t> motions;
        std::vector<bool> blocked;
        std::vector<bool> avoid;
        bool consumed = false;
        explicit AreaDoorTransaction(InventoryService& owner) : service(owner), before(owner.mImage)
        {
            for (const auto& door : owner.mAreaDoors)
            { states.push_back(door.state); motions.push_back(door.motion); blocked.push_back(door.blocked); avoid.push_back(false); }
        }
        CanonicalDurabilityResult commit(const NativeInventoryCommit& persist) noexcept override
        {
            if (consumed || service.inventoryImage().empty() || before != service.mImage)
                return CanonicalDurabilityResult::Rejected;
            try
            {
                const auto compose = [&](std::span<const std::byte> input) {
                    const std::span bytes(reinterpret_cast<const char*>(input.data()), input.size());
                    size_t offset = 0;
                    if (get(bytes, offset) != (service.mBinding.mActorSelections ? SpawnAreaMagic : AreaMagic))
                        throw std::invalid_argument("Composed door inventory version mismatch");
                    if (service.mBinding.mActorSelections && readActorSpawns(bytes, offset) != *service.mBinding.mActorSelections)
                        throw std::invalid_argument("Composed door actor selections mismatch");
                    const auto size = get(bytes, offset), count = get(bytes, offset);
                    if (size > bytes.size() - offset || count != states.size())
                        throw std::invalid_argument("Composed door inventory bounds mismatch");
                    image = service.sealInventory(bytes.subspan(offset, size_t(size)), states);
                    return persist(std::as_bytes(std::span(image)));
                };
                const auto result = command ? command->commit(compose) : persist(std::as_bytes(std::span(image)));
                if (result == CanonicalDurabilityResult::Rejected) return result;
                consumed = true;
                if (result == CanonicalDurabilityResult::Failed) service.mRuntime.mFailedClosed = true;
                else
                {
                    for (size_t i = 0; i < states.size(); ++i)
                    {
                        service.mAreaDoors[i].state.swap(states[i]);
                        service.mAreaDoors[i].motion = motions[i];
                        service.mAreaDoors[i].blocked = blocked[i];
                    }
                    service.mImage.swap(image);
                }
                return result;
            }
            catch (...) { service.mRuntime.mFailedClosed = true; return CanonicalDurabilityResult::Failed; }
        }
    };

    std::unique_ptr<PreparedNativeInventory> InventoryService::prepareAreaDoor(size_t index, bool activation,
        const CanonicalServerState& players, ServerTick tick, float seconds,
        std::unique_ptr<PreparedNativeInventory> command)
    {
        if (inventoryImage().empty()) return {};
        auto result = std::make_unique<AreaDoorTransaction>(*this);
        bool changed = bool(command);
        if (ownsAreaDoorCandidate(command.get()))
            result.reset(static_cast<AreaDoorTransaction*>(command.release()));
        else result->command = std::move(command);
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
        {
            auto& door = mAreaDoors[i];
            const auto& state = *result->states[i];
            if (activation ? i != index : !state.mDoorState) continue;
            const auto cell = mBinding.mDoors[i].mCell;
            if (!activation && (mActiveAreas.empty() || !mActiveAreas[worldIndex(cell)])) continue;
            bool blocked = false;
            for (const auto& report : door.reports)
            {
                if (!report) continue;
                const auto* session = players.findActiveSession(report->session);
                const auto* player = session ? players.findPlayer(session->playerId()) : nullptr;
                if (!player || session->sessionGeneration() != report->generation
                    || player->transform().cell() != cell || report->motion != result->motions[i]
                    || report->observedTick > tick || tick.value() - report->observedTick.value() >= DoorObstructionLifetimeTicks)
                    continue;
                blocked |= report->blocked;
            }
            if (activation && door.motion == std::numeric_limits<uint64_t>::max())
                throw std::invalid_argument("Native door motion exhausted");
            auto next = activation ? door.binding.door().activate(state)
                : door.binding.door().advance(state, seconds, [&](const auto& position, float delta) {
                    if (mBinding.mNavigatingActor)
                    {
                        const auto contact = mBinding.mNavigatingActor->doorContact(mBinding.mDoors[i].mId, position.rot[2], delta);
                        blocked |= contact.mBlocked;
                        result->avoid[i] = contact.mSelectedActor;
                    }
                    return blocked;
                });
            result->blocked[i] = !activation && blocked && state.mDoorState != 0;
            result->states[i] = std::make_shared<const ESM::DoorState>(std::move(next.mState));
            result->motions[i] += uint64_t(activation);
            changed = true;
        }
        if (!changed) return {};
        result->image = sealInventory(mCoreImage, result->states);
        return result;
    }

    bool InventoryService::ownsAreaDoorCandidate(const PreparedNativeInventory* candidate) const
    {
        const auto* prepared = dynamic_cast<const AreaDoorTransaction*>(candidate);
        return prepared && &prepared->service == this && prepared->before == mImage && !prepared->consumed;
    }

    const PreparedNativeInventory* InventoryService::areaDoorCommand(const PreparedNativeInventory* candidate) const
    {
        const auto* prepared = dynamic_cast<const AreaDoorTransaction*>(candidate);
        return prepared ? prepared->command.get() : candidate;
    }

    std::vector<ActorSceneDoor> InventoryService::actorDoorFrames(const PreparedNativeInventory* candidate) const
    {
        const auto* prepared = dynamic_cast<const AreaDoorTransaction*>(candidate);
        if (prepared && !ownsAreaDoorCandidate(candidate)) throw std::invalid_argument("Stale actor door candidate");
        std::vector<ActorSceneDoor> result;
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
        {
            const auto& state = *(prepared ? prepared->states[i] : mAreaDoors[i].state);
            result.push_back({mBinding.mDoors[i].mId, state.mPosition.rot[2], state.mDoorState != 0,
                prepared && prepared->avoid[i]});
        }
        return result;
    }

    std::vector<NativeDoorSnapshot> InventoryService::areaDoorSnapshots(CellId cell, const PreparedNativeInventory* candidate) const
    {
        const auto* prepared = dynamic_cast<const AreaDoorTransaction*>(candidate);
        if (prepared && (&prepared->service != this || prepared->before != mImage || prepared->consumed))
            throw std::invalid_argument("Stale or foreign area door projection");
        std::vector<NativeDoorSnapshot> result;
        for (size_t i = 0; i < mAreaDoors.size(); ++i)
            if (mBinding.mDoors[i].mCell == cell)
            {
                const auto& door = mAreaDoors[i];
                const auto& state = prepared ? *prepared->states[i] : *door.state;
                result.push_back({mBinding.mDoors[i].mId, prepared ? prepared->motions[i] : door.motion,
                    state.mPosition.rot[2], mDoorStepSeconds, uint8_t(state.mDoorState),
                    prepared ? prepared->blocked[i] : door.blocked});
            }
        std::ranges::sort(result, {}, &NativeDoorSnapshot::placement);
        return result;
    }
}
