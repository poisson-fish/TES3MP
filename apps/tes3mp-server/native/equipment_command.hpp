#ifndef TES3MP_NATIVE_EQUIPMENT_COMMAND_H
#define TES3MP_NATIVE_EQUIPMENT_COMMAND_H

#include "inventory_identity.hpp"
#include "persistence.hpp"

#include <vector>
#include <memory>
#include <array>
#include <optional>

namespace TES3MP::Native
{
    class EquipmentRuntime;
    struct FileFaults;
    class EquipmentFileSink;
    struct EquipmentBindings;

    enum class EquipmentRequestedState : std::uint8_t
    {
        Unequipped,
        Equipped
    };

    // Owned app-local intent, not a wire format or durable request identity.
    struct EquipmentCommand
    {
        InventoryInstanceId mActor, mItem;
        std::uint64_t mExpectedRevision = 0;
        EquipmentRequestedState mState = EquipmentRequestedState::Unequipped;
        int mSlot = 8; // Owned slot index; validated against stock slots in the runtime.
        bool operator==(const EquipmentCommand&) const = default;
    };

    // Supplied separately by trusted server composition, never from the command.
    // Matching it is authorization within this runtime, not authentication.
    struct EquipmentCaller
    {
        InventoryInstanceId mActor;
    };

    struct EquipmentSuccess
    {
        EquipmentCommand mCommand;
        InventoryInstanceId mShirt, mSelected, mLastGenerated;
        std::uint64_t mRevision = 0;
        std::optional<std::array<float, 3>> mLuck;
        bool mSkipped = false; // PCSkipEquip committed locals, without an equipment change.
        bool operator==(const EquipmentSuccess&) const = default;
    };

    // App-local OpenMW runtime implementation. Serialized,
    // synchronous access; trusted content/bindings/sink outlive the call and may
    // not mutate/reenter. Resolve current actors/items; keep all engine views
    // inside the runtime. Unsupported input throws before persistence. Safe I/O
    // failure returns Rejected; Uncertain closes the entire composition until
    // recovery into an explicitly fresh runtime. All failures preserve output
    // allocation/value and bytes. Owned success swaps only after durable install.
    // Preserve stock counters (including revision rollover), 64-node preparation
    // and 65-node saves (legacy 1/5/6; full slot table 7).
    // Revision is not a request-deduplication token.
    PersistenceResult executeEquipment(EquipmentRuntime& runtime, EquipmentCaller caller,
        EquipmentCommand command, EquipmentFileSink& file, const EquipmentBindings& bindings,
        std::unique_ptr<const EquipmentSuccess>& output, std::vector<char>& bytes, FileFaults& faults);
}

#endif
