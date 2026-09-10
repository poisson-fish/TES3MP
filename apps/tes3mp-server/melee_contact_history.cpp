#include "melee_contact_history.hpp"

#include <algorithm>
#include <cmath>

namespace TES3MP::ServerApp
{
    std::optional<MeleeContactHistory> MeleeContactHistory::create(
        MeleeAuthorityPolicy policy, const ContentCollisionProvider& collision) noexcept
    try
    {
        if (policy.minimumAttackIntervalTicks == 0 || policy.maximumRewindTicks == 0
            || policy.maximumRewindTicks >= MaximumMeleeHistoryFrames || policy.baseReachQuanta == 0
            || policy.baseReachQuanta > MaximumAuthoritativeMeleeReachQuanta)
            return std::nullopt;
        MeleeContactHistory result(policy, collision);
        result.mFrames.reserve(static_cast<std::size_t>(policy.maximumRewindTicks + 1));
        return result;
    }
    catch (...)
    {
        return std::nullopt;
    }

    bool MeleeContactHistory::capture(ServerTick tick, const CanonicalServerState& players,
        const CanonicalActorWorld& actors) noexcept
    try
    {
        if (!mFrames.empty() && tick < mFrames.back().tick)
            return false;
        Frame frame{ tick };
        frame.players.reserve(players.players().size());
        frame.actors.reserve(actors.actors().size());
        for (const auto& player : players.players())
            frame.players.push_back({ player.playerId(), player.transform() });
        for (const auto& actor : actors.actors())
            frame.actors.push_back({ actor.actorId(), actor.root() });
        if (!mFrames.empty() && tick == mFrames.back().tick)
            mFrames.back() = std::move(frame);
        else
            mFrames.push_back(std::move(frame));
        const auto capacity = static_cast<std::size_t>(mPolicy.maximumRewindTicks + 1);
        if (mFrames.size() > capacity)
            mFrames.erase(mFrames.begin(), mFrames.begin() + static_cast<std::ptrdiff_t>(mFrames.size() - capacity));
        return true;
    }
    catch (...)
    {
        return false;
    }

    MeleeContactValidation MeleeContactHistory::validate(const ServerMeleeContactRequest& request,
        const CanonicalPlayerEntityState& attacker, const CanonicalActorEntityState& target) noexcept
    {
        if (attacker.playerId() != request.attacker || target.actorId() != request.target)
            return MeleeContactValidation::NoContact;
        const auto frame = std::ranges::lower_bound(mFrames, request.sourceTick, {}, &Frame::tick);
        if (frame == mFrames.end() || frame->tick != request.sourceTick)
            return MeleeContactValidation::HistoryUnavailable;
        const auto player = std::ranges::lower_bound(frame->players, request.attacker, {}, &PlayerSample::id);
        const auto actor = std::ranges::lower_bound(frame->actors, request.target, {}, &ActorSample::id);
        if (player == frame->players.end() || player->id != request.attacker
            || actor == frame->actors.end() || actor->id != request.target)
            return MeleeContactValidation::HistoryUnavailable;
        if (player->root.cell() != actor->root.cell())
            return MeleeContactValidation::NoContact;

        const float multiplier = request.weaponReach.value_or(1.f);
        if (!std::isfinite(multiplier) || multiplier <= 0.f)
            return MeleeContactValidation::NoContact;
        const double scaled = std::ceil(static_cast<double>(mPolicy.baseReachQuanta) * multiplier);
        if (scaled <= 0.0 || scaled > MaximumAuthoritativeMeleeReachQuanta)
            return MeleeContactValidation::NoContact;
        const auto maximumReach = static_cast<std::uint32_t>(scaled);
        if (!positionsWithinReach(player->root.position(), actor->root.position(), maximumReach)
            || !mCollision.segmentClear(
                player->root.cell(), player->root.position(), actor->root.position()))
            return MeleeContactValidation::NoContact;
        return MeleeContactValidation::Accepted;
    }
}
