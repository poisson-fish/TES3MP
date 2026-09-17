#include "inventory_command_binding.hpp"

namespace TES3MP::ServerApp
{
    std::optional<InventoryCommandBinding> InventoryCommandBinding::resolve(const CanonicalServerState& state,
        SessionId connectionSession, SessionGeneration connectionGeneration,
        const ClientInventoryTransactionCommand& command)
    {
        if (command.sessionId != connectionSession || command.sessionGeneration != connectionGeneration)
            return std::nullopt;
        const auto* session = state.findActiveSession(connectionSession);
        const auto* player = session ? state.findPlayer(session->playerId()) : nullptr;
        if (!session || session->sessionGeneration() != connectionGeneration || !player)
            return std::nullopt;
        InventoryTransactionCommand transaction{ .player = session->playerId(),
            .kind = command.kind, .containerId = command.containerId, .prototypeId = command.prototypeId,
            .stackId = command.stackId, .count = command.count, .slot = command.slot,
            .expectedInventoryRevision = command.expectedInventoryRevision,
            .expectedContainerRevision = command.expectedContainerRevision,
            .expectedWorldItemRevision = command.expectedWorldItemRevision,
            .interactionOrigin = command.interactionOrigin };
        return InventoryCommandBinding(session->playerId(), ServerCommandProposal(connectionSession,
            connectionGeneration, command.commandSequence, command.commandId, command.observedCanonicalRevision,
            EntityPrecondition(session->entityId(), player->entityRevision(), player->authorityEpoch()),
            InventoryCommandProposal(std::move(transaction))));
    }

    bool InventoryCommandBinding::current(const CanonicalServerState& state) const noexcept
    {
        const auto* session = state.findActiveSession(mProposal.sessionId());
        const auto* player = state.findPlayer(mPlayer);
        return session && player && session->playerId() == mPlayer
            && session->sessionGeneration() == mProposal.sessionGeneration()
            && mProposal.entityPrecondition() == EntityPrecondition(
                session->entityId(), player->entityRevision(), player->authorityEpoch());
    }
}
