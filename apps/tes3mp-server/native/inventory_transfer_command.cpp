#include "inventory_transfer_command.hpp"

#include "test_allocations.hpp"
#include "transfer_file_sink.hpp"

#include <limits>

namespace MWWorld::Testing
{
    bool executeInventoryTransfer(DisposableTransferRehearsal& fixture, InventoryTransferCommand command,
        const SaveBindings& bindings, TransferFileSink& sink, FileFaults& faults,
        std::unique_ptr<const InventoryTransferSuccess>& output)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (fixture.failedClosed() || sink.failedClosed())
            throw TestDurabilityUncertain{};
        const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
        const auto ownedId = [](ESM::RefNum value) {
            return InventoryInstanceId{ value.mIndex, value.mContentFile };
        };
        for (auto value : { command.mSourceOwner, command.mDestinationOwner, command.mInitiator, command.mItem })
            if (!id(value).isSet() || value.mContentFile < -1)
                throw std::invalid_argument("Inventory command requires valid instance IDs");
        if (command.mQuantity <= 0 || command.mSourceOwner == command.mDestinationOwner
            || command.mExpectedRevision != fixture.mModel.getPtrRegistryRevision())
            throw std::invalid_argument("Inventory command quantity, owners or revision invalid");
        // Version 4 preserves only the first generated-ID namespace. Close this
        // bounded command at either saved counter's limit, including stacking:
        // never wrap the revision or let stock generation enter content slot -2.
        const auto counter = fixture.mModel.getLastGeneratedRefNum();
        if (command.mExpectedRevision == std::numeric_limits<size_t>::max() || counter.mContentFile != -1
            || counter.mIndex == std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Inventory command saved counter exhausted or unsupported");
        const auto& envelope = bindings.mEnvelope;
        if (id(command.mSourceOwner) != envelope.mSourceOwner
            || id(command.mDestinationOwner) != envelope.mDestinationOwner
            || id(command.mInitiator) != envelope.mInitiator)
            throw std::invalid_argument("Inventory command save owner/initiator mismatch");

        // Resolve afresh. Never follow a caller-supplied pointer or an old context
        // before checking its lifetime; full stock validation remains mandatory.
        const auto matches = [&](InventoryInstanceId value, const Ptr& expected) {
            const auto current = fixture.mModel.getPtr(id(value));
            return current.hasLiveReference() && expected.hasLiveReference() && current == expected
                && current.getReferenceLifetime() == expected.getReferenceLifetime();
        };
        if (!matches(command.mSourceOwner, fixture.mSourceOwner.getPtr())
            || !matches(command.mSourceOwner, fixture.mRemoval.mContainer)
            || !matches(command.mDestinationOwner, fixture.mDestinationOwner.getPtr())
            || !matches(command.mDestinationOwner, fixture.mDestinationAdd.mContainer)
            || !matches(command.mInitiator, fixture.mDestinationAdd.mPlayer))
            throw std::invalid_argument("Inventory command current context mismatch");
        const auto item = fixture.mModel.getPtr(id(command.mItem));
        if (!item.hasLiveReference() || item.getContainerStore() != &fixture.mSource)
            throw std::invalid_argument("Inventory command item ownership mismatch");

        phase.set(Allocations::Phase::Preparation);
        const std::array resolved{ ContainerStoreResolution(fixture.mOther, fixture.mOtherOwner.getPtr()) };
        auto pair = fixture.mSource.prepareTransfer(
            item, command.mQuantity, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd, resolved);
        if (!fixture.mSource.validateTransfer(pair, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd)
                .isComplete())
            throw std::invalid_argument("Inventory command requires complete resolution");

        phase.set(Allocations::Phase::Result);
        // Stage all fallible result storage while the pair is detached. Result
        // values contain no engine objects, iterators or borrowed lifetimes.
        auto staged = std::make_unique<const InventoryTransferSuccess>(InventoryTransferSuccess{ command,
            ownedId(pair.getDestinationIdentity()), pair.getSourceItem().getCellRef().getCount(false),
            pair.getDestinationItem().getCellRef().getCount(false), pair.getRelocation().mRegistry.mRevision });
        phase.set(Allocations::Phase::Setup);
        const bool installed = fixture.commitDurably(
            std::move(pair), bindings.mContent.mDeclarations, [&](const SerializedPair& saved) {
                TransferSaveBytes bytes;
                encodeTransferSave(saved, bindings, bytes);
                return sink.write(bytes, faults); // No fallible work after possible replacement.
            });
        if (!installed)
            return false;
        phase.set(Allocations::Phase::Publication);
        static_assert(noexcept(output.swap(staged)));
        output.swap(staged);
        return true;
    }
}
