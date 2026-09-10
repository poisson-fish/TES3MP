#ifndef TES3MP_SERVER_MELEE_CONTACT_HISTORY_HPP
#define TES3MP_SERVER_MELEE_CONTACT_HISTORY_HPP

#include "content_collision.hpp"

#include <tes3mp/combat_world.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    inline constexpr std::uint64_t MaximumMeleeHistoryFrames = 64;
    inline constexpr std::uint32_t MaximumAuthoritativeMeleeReachQuanta = 1024 * 1024;

    class MeleeContactHistory final : public ServerMeleeContactHistory
    {
    public:
        static std::optional<MeleeContactHistory> create(
            MeleeAuthorityPolicy policy, const ContentCollisionProvider& collision) noexcept;

        bool capture(ServerTick tick, const CanonicalServerState& players,
            const CanonicalActorWorld& actors) noexcept override;
        MeleeContactValidation validate(const ServerMeleeContactRequest& request,
            const CanonicalPlayerEntityState& attacker,
            const CanonicalActorEntityState& target) noexcept override;

        std::size_t frameCount() const noexcept { return mFrames.size(); }

    private:
        struct PlayerSample
        {
            PlayerId id;
            Transform root;
        };

        struct ActorSample
        {
            ActorId id;
            Transform root;
        };

        struct Frame
        {
            ServerTick tick;
            std::vector<PlayerSample> players;
            std::vector<ActorSample> actors;
        };

        MeleeContactHistory(MeleeAuthorityPolicy policy,
            const ContentCollisionProvider& collision) noexcept
            : mPolicy(policy), mCollision(collision)
        {
        }

        MeleeAuthorityPolicy mPolicy;
        const ContentCollisionProvider& mCollision;
        std::vector<Frame> mFrames;
    };
}

#endif
