#ifndef TES3MP_NATIVE_EQUIPMENT_COMMAND_H
#define TES3MP_NATIVE_EQUIPMENT_COMMAND_H

#include "inventory_transfer_command.hpp"
#include "test_persistence.hpp"

#include <vector>
#include <array>
#include <optional>

namespace MWWorld::Testing
{
    class PlainEquipmentFixture;
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
        bool operator==(const EquipmentCommand&) const = default;
    };

    // Supplied separately by trusted server composition, never from the command.
    // Matching it is authorization within this fixture, not authentication.
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
        bool operator==(const EquipmentSuccess&) const = default;
    };

    // Test-target-only implementation beside PlainEquipmentFixture. Serialized,
    // synchronous access; trusted content/bindings/sink outlive the call and may
    // not mutate/reenter. Resolve current actors/items; keep all engine views
    // inside the fixture. Unsupported input throws before persistence. Safe I/O
    // failure returns Rejected; Uncertain closes the entire composition until
    // recovery into an explicitly fresh fixture. All failures preserve output
    // allocation/value and bytes. Owned success swaps only after durable install.
    // Preserve stock counters (including revision rollover), 64-node preparation
    // and 65-node saves (plain format 1; bounded Luck format 2). Revision is not a request-deduplication token.
    TestPersistenceResult executeEquipment(PlainEquipmentFixture& fixture, EquipmentCaller caller,
        EquipmentCommand command, EquipmentFileSink& file, const EquipmentBindings& bindings,
        std::unique_ptr<const EquipmentSuccess>& output, std::vector<char>& bytes, FileFaults& faults);
}

#endif
