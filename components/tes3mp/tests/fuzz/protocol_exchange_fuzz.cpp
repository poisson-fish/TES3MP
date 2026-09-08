#include <tes3mp/actor_replication.hpp>
#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/protocol_exchange.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    const auto reliable = TES3MP::decodeReliableOperation(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableOperation>(&reliable))
    {
        const auto normalized = TES3MP::decodeReliableOperation(TES3MP::encodeReliableOperation(*value));
        const auto* normalizedValue = std::get_if<TES3MP::ReliableOperation>(&normalized);
        if (normalizedValue == nullptr || *normalizedValue != *value)
            std::abort();
    }

    const auto latestWins = TES3MP::decodeLatestWinsSnapshot(bytes);
    if (const auto* value = std::get_if<TES3MP::LatestWinsSnapshot>(&latestWins))
    {
        const auto normalized = TES3MP::decodeLatestWinsSnapshot(TES3MP::encodeLatestWinsSnapshot(*value));
        const auto* normalizedValue = std::get_if<TES3MP::LatestWinsSnapshot>(&normalized);
        if (normalizedValue == nullptr || *normalizedValue != *value)
            std::abort();
    }
    const auto observations = TES3MP::decodeReliableObservationBatch(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableObservationBatch>(&observations))
        if (TES3MP::decodeReliableObservationBatch(TES3MP::encodeReliableObservationBatch(*value)) != observations)
            std::abort();
    const auto baseline = TES3MP::decodeReliableInterestBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableInterestBaseline>(&baseline))
        if (TES3MP::decodeReliableInterestBaseline(TES3MP::encodeReliableInterestBaseline(*value)) != baseline)
            std::abort();
    const auto resync = TES3MP::decodeSessionResyncRequest(bytes);
    if (const auto* value = std::get_if<TES3MP::SessionResyncRequest>(&resync))
        if (TES3MP::decodeSessionResyncRequest(TES3MP::encodeSessionResyncRequest(*value)) != resync)
            std::abort();
    const auto actorBaseline = TES3MP::decodeReliableActorInterestBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableActorInterestBaseline>(&actorBaseline))
        if (TES3MP::decodeReliableActorInterestBaseline(TES3MP::encodeReliableActorInterestBaseline(*value))
            != actorBaseline)
            std::abort();
    const auto actorSnapshot = TES3MP::decodeLatestWinsActorSnapshot(bytes);
    if (const auto* value = std::get_if<TES3MP::LatestWinsActorSnapshot>(&actorSnapshot))
        if (TES3MP::decodeLatestWinsActorSnapshot(TES3MP::encodeLatestWinsActorSnapshot(*value)) != actorSnapshot)
            std::abort();
    const auto playerInventory = TES3MP::decodeReliablePlayerInventoryBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliablePlayerInventoryBaseline>(&playerInventory))
        if (TES3MP::decodeReliablePlayerInventoryBaseline(TES3MP::encodeReliablePlayerInventoryBaseline(*value))
            != playerInventory)
            std::abort();
    const auto containerInventory = TES3MP::decodeReliableContainerInventoryBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableContainerInventoryBaseline>(&containerInventory))
        if (TES3MP::decodeReliableContainerInventoryBaseline(TES3MP::encodeReliableContainerInventoryBaseline(*value))
            != containerInventory)
            std::abort();
    const auto groundItems = TES3MP::decodeReliableGroundItemBaseline(bytes);
    if (const auto* value = std::get_if<TES3MP::ReliableGroundItemBaseline>(&groundItems))
        if (TES3MP::decodeReliableGroundItemBaseline(TES3MP::encodeReliableGroundItemBaseline(*value)) != groundItems)
            std::abort();
    const auto equipment = TES3MP::decodeLatestWinsEquipmentSnapshot(bytes);
    if (const auto* value = std::get_if<TES3MP::LatestWinsEquipmentSnapshot>(&equipment))
        if (TES3MP::decodeLatestWinsEquipmentSnapshot(TES3MP::encodeLatestWinsEquipmentSnapshot(*value)) != equipment)
            std::abort();
    const auto inventoryCommand = TES3MP::decodeClientInventoryTransactionCommand(bytes);
    if (const auto* value = std::get_if<TES3MP::ClientInventoryTransactionCommand>(&inventoryCommand))
        if (TES3MP::decodeClientInventoryTransactionCommand(TES3MP::encodeClientInventoryTransactionCommand(*value))
            != inventoryCommand)
            std::abort();
    return 0;
}
