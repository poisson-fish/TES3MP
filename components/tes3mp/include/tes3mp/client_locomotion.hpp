#ifndef TES3MP_CLIENT_LOCOMOTION_HPP
#define TES3MP_CLIENT_LOCOMOTION_HPP

#include "protocol_exchange.hpp"

#include <optional>
#include <vector>

namespace TES3MP
{
    struct LocalLocomotionReconciliation
    {
        Transform root;
        LinearVelocity3 velocity;
        std::size_t replayedInputCount = 0;
        bool hardDiscontinuity = false;

        friend constexpr bool operator==(const LocalLocomotionReconciliation&,
            const LocalLocomotionReconciliation&) noexcept = default;
    };

    class ClientLocomotionHistory
    {
    public:
        ClientLocomotionHistory();

        bool retain(CommandSequence commandSequence, PlayerLocomotionInput input) noexcept;
        LocalLocomotionReconciliation reconcile(const SpatialEntitySnapshot& authoritative,
            std::optional<CommandSequence> acknowledgedCommand, bool hardDiscontinuity = false) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept { return mInputs.size(); }

    private:
        struct RetainedInput
        {
            CommandSequence commandSequence;
            PlayerLocomotionInput input;
        };

        struct BaselineIdentity
        {
            PlayerId player;
            EntityId entity;
            AuthorityEpoch authorityEpoch;
            CellId cell;
            ServerTick tick;
        };

        std::vector<RetainedInput> mInputs;
        std::optional<BaselineIdentity> mBaseline;
    };
}

#endif
