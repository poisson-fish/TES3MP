#ifndef TES3MP_SERVER_INVENTORY_COMMAND_BINDING_HPP
#define TES3MP_SERVER_INVENTORY_COMMAND_BINDING_HPP

#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/server_command_intake.hpp>
#include <optional>
#include <utility>

namespace TES3MP::ServerApp
{
    // The connection session/generation come from established authentication,
    // never from the decoded command. Retaining this value does not authorize a
    // later session: current() must pass again at deferred preparation. This
    // binding does not replace intake ordering or reducer replay/finalization.
    class InventoryCommandBinding
    {
        PlayerId mPlayer;
        ServerCommandProposal mProposal;
        InventoryCommandBinding(PlayerId player, ServerCommandProposal proposal)
            : mPlayer(player), mProposal(std::move(proposal)) {}
    public:
        static std::optional<InventoryCommandBinding> resolve(const CanonicalServerState& state,
            SessionId connectionSession, SessionGeneration connectionGeneration,
            const ClientInventoryTransactionCommand& command);
        bool current(const CanonicalServerState& state) const noexcept;
        PlayerId player() const noexcept { return mPlayer; }
        const ServerCommandProposal& proposal() const noexcept { return mProposal; }
        const InventoryTransactionCommand& transaction() const noexcept
        {
            return std::get<InventoryCommandProposal>(mProposal.payload()).command();
        }
    };
}
#endif
