#include "inventory_transfer_command.hpp"

#include "test_allocations.hpp"
#include "transfer_file_sink.hpp"

#include <utility>

namespace MWWorld::Testing
{
    bool executeInventoryTransfer(DisposableTransferRehearsal& fixture, InventoryTransferCaller caller,
        InventoryTransferCommand command, const SaveBindings& bindings, TransferFileSink& sink, FileFaults& faults,
        std::unique_ptr<const InventoryTransferSuccess>& output)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (fixture.failedClosed() || sink.failedClosed())
            throw TestDurabilityUncertain{};
        const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
        const auto ownedId = [](ESM::RefNum value) {
            return InventoryInstanceId{ value.mIndex, value.mContentFile };
        };
        const auto& envelope = bindings.mEnvelope;
        const bool reverse = validateInventoryTransferIntent(caller, command,
            { ownedId(envelope.mSourceOwner), ownedId(envelope.mDestinationOwner) },
            fixture.mModel.getPtrRegistryRevision(), ownedId(fixture.mModel.getLastGeneratedRefNum())) != 0;

        // Resolve afresh. Never follow a caller-supplied pointer or an old context
        // before checking its lifetime; full stock validation remains mandatory.
        const auto matches = [&](InventoryInstanceId value, const Ptr& expected) {
            const auto current = fixture.mModel.getPtr(id(value));
            return current.hasLiveReference() && expected.hasLiveReference() && current == expected
                && current.getReferenceLifetime() == expected.getReferenceLifetime();
        };
        const auto sourceOwner = reverse ? command.mDestinationOwner : command.mSourceOwner;
        const auto destinationOwner = reverse ? command.mSourceOwner : command.mDestinationOwner;
        if (!matches(sourceOwner, fixture.mSourceOwner.getPtr())
            || !matches(sourceOwner, fixture.mRemoval.mContainer)
            || !matches(destinationOwner, fixture.mDestinationOwner.getPtr())
            || !matches(destinationOwner, fixture.mDestinationAdd.mContainer)
            || !matches(ownedId(envelope.mInitiator), fixture.mDestinationAdd.mPlayer))
            throw std::invalid_argument("Inventory command current context mismatch");
        // Both eligible owners were checked against the current registry and
        // their lifetime witnesses. Authorization comes from caller, not the
        // fixed format-4 envelope or the command's claimed identity.
        const auto initiator = fixture.mModel.getPtr(id(caller.mInitiator));
        const auto item = fixture.mModel.getPtr(id(command.mItem));
        auto& source = reverse ? fixture.mDestination : fixture.mSource;
        auto& destination = reverse ? fixture.mSource : fixture.mDestination;
        if (!item.hasLiveReference() || item.getContainerStore() != &source)
            throw std::invalid_argument("Inventory command item ownership mismatch");

        phase.set(Allocations::Phase::Preparation);
        const auto contexts = fixture.transferContexts(reverse, initiator);
        const std::array resolved{ ContainerStoreResolution(fixture.mOther, fixture.mOtherOwner.getPtr()) };
        auto pair = source.prepareTransfer(
            item, command.mQuantity, destination, contexts.mRemoval, contexts.mAddition, resolved);
        if (!source.validateTransfer(pair, destination, contexts.mRemoval, contexts.mAddition).isComplete())
            throw std::invalid_argument("Inventory command requires complete resolution");

        phase.set(Allocations::Phase::Result);
        // Stage all fallible result storage while the pair is detached. Result
        // values contain no engine objects, iterators or borrowed lifetimes.
        const auto destinationItem = ownedId(pair.getDestinationIdentity());
        const auto revision = pair.getRelocation().mRegistry.mRevision;
        auto staged = std::make_unique<const InventoryTransferSuccess>(inventoryTransferSuccess(command,
            destinationItem, pair.getSourceItem().getCellRef().getCount(false),
            pair.getDestinationItem().getCellRef().getCount(false), revision,
            ownedId(pair.getSourceSelection()), ownedId(pair.getDestinationSelection()),
            pair.hasRemovalNotification(), pair.hasAdditionNotification()));
        phase.set(Allocations::Phase::Setup);
        const bool installed = fixture.commitDurably(
            std::move(pair), bindings.mContent.mDeclarations, [&](const SerializedPair& saved) {
                TransferSaveBytes bytes;
                encodeTransferSave(saved, bindings, bytes);
                return sink.write(bytes, faults); // No fallible work after possible replacement.
            },
            reverse, initiator);
        if (!installed)
            return false;
        phase.set(Allocations::Phase::Publication);
        static_assert(noexcept(output.swap(staged)));
        output.swap(staged);
        return true;
    }

}
