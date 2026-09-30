#ifndef OPENMW_TES3MP_ACTOR_PRESENTATION_HPP
#define OPENMW_TES3MP_ACTOR_PRESENTATION_HPP

#include <tes3mp/combat_replication.hpp>
#include <tes3mp/monotonic_clock.hpp>
#include <algorithm>
#include <cmath>
#include <deque>
#include <tuple>

namespace TES3MP::OpenMWAdapter
{
    struct ActorPresentationPose : ActorPresentationSnapshot
    {
        float castFrame = 0;
    };

    // One render-time cursor for actor motion and animation. Only committed
    // history is sampled: starvation holds the latest pose, never finishes an
    // unknown action. The server's gameplay clock and callbacks remain untouched.
    class ActorPresentationTimeline
    {
        struct Frame
        {
            uint64_t tick;
            std::vector<ActorPresentationPose> actors;
            std::vector<PhysicalProjectileSnapshot> projectiles;
        };
        std::deque<Frame> mFrames;
        uint64_t mGeneration = 0;
        double mCursor = 0;
        std::optional<MonotonicInstant> mLast;
        bool mStarted = false;
    public:
        void clear() { mFrames.clear(); mLast.reset(); mStarted = false; mCursor = 0; mGeneration = 0; }
        bool empty() const { return mFrames.empty(); }
        double tick() const { return mCursor; }
        void observe(const LatestWinsCombatSnapshot& snapshot)
        {
            if (snapshot.presentation().empty() && snapshot.projectiles().empty()) return;
            const auto generation = snapshot.targetSessionGeneration().value();
            if (generation != mGeneration) { clear(); mGeneration = generation; }
            const auto tick = snapshot.serverTick().value();
            if (!mFrames.empty() && tick <= mFrames.back().tick) return;
            if (mFrames.empty()) mCursor = double(tick);
            Frame frame{tick, {}, {snapshot.projectiles().begin(), snapshot.projectiles().end()}};
            for (const auto& actor : snapshot.presentation())
            {
                ActorPresentationPose pose;
                static_cast<ActorPresentationSnapshot&>(pose) = actor;
                pose.castFrame = float(actor.castElapsed);
                if (actor.kind == 2 && !actor.cast)
                    for (const auto& combat : snapshot.actors())
                        if (combat.actorId.value() == actor.id)
                        {
                            pose.cast = combat.castId; pose.castPhase = combat.castPhase;
                            pose.castRange = combat.castRange; pose.castFrame = float(combat.castElapsed);
                            pose.castRelease = combat.castRelease; pose.castStop = combat.castStop;
                            break;
                        }
                frame.actors.push_back(std::move(pose));
            }
            mFrames.push_back(std::move(frame));
            while (mFrames.size() > 16) mFrames.pop_front();
            mCursor = std::max(mCursor, double(mFrames.front().tick));
        }
        void advance(MonotonicInstant now)
        {
            const double seconds = mLast && now >= *mLast
                ? double(now.nanoseconds() - mLast->nanoseconds()) / 1e9 : 0;
            mLast = now;
            if (empty()) return;
            if (!mStarted)
            {
                if (double(mFrames.back().tick) - mCursor < 3) return;
                mStarted = true;
            }
            mCursor = std::clamp(mCursor + seconds * 30., double(mFrames.front().tick), double(mFrames.back().tick));
        }
        std::optional<ActorPresentationPose> sample(uint8_t kind, uint64_t id) const
        {
            if (empty()) return {};
            size_t lo = 0;
            while (lo + 1 < mFrames.size() && double(mFrames[lo + 1].tick) <= mCursor) ++lo;
            const auto find = [&](const Frame& frame) -> const ActorPresentationPose* {
                const auto i = std::ranges::find_if(frame.actors, [&](const auto& p) { return p.kind == kind && p.id == id; });
                return i == frame.actors.end() ? nullptr : &*i;
            };
            const auto* lower = find(mFrames[lo]);
            if (!lower) return {};
            auto result = *lower;
            if (lo + 1 == mFrames.size()) return result;
            const auto* upper = find(mFrames[lo + 1]);
            if (!upper || upper->life != lower->life || lower->dead) return result;
            const double elapsed = mCursor - double(mFrames[lo].tick);
            const double span = double(mFrames[lo + 1].tick - mFrames[lo].tick);
            const float ratio = float(elapsed / span);
            // Release is a visual section boundary, never a local gameplay key.
            // A changed identity, interrupted cast or missing bracket holds the
            // last committed pose until its replacement reaches the cursor.
            if (lower->cast && lower->cast == upper->cast && lower->castPhase >= 3
                && lower->castRange == upper->castRange && lower->castRelease == upper->castRelease
                && lower->castStop == upper->castStop && upper->castFrame >= lower->castFrame)
            {
                result.castFrame = std::lerp(lower->castFrame, upper->castFrame, ratio);
                result.castPhase = result.castFrame >= result.castRelease ? 5 : 3;
            }
            if (lower->action && lower->action == upper->action && lower->group == upper->group
                && lower->direction == upper->direction)
            {
                if (lower->phase == upper->phase && upper->completion >= lower->completion)
                    result.completion = std::lerp(lower->completion, upper->completion, ratio);
                else if (lower->phase < upper->phase)
                    result.completion = std::min(1.f, lower->completion + float(elapsed / 30.) * lower->rate);
            }
            if (lower->bodyAction && lower->bodyAction == upper->bodyAction && lower->bodyState == upper->bodyState)
            {
                if (upper->bodyFrame >= lower->bodyFrame)
                    result.bodyFrame = std::lerp(lower->bodyFrame, upper->bodyFrame, ratio);
                else if (lower->bodyState == 2 && lower->loopStop > lower->loopStart)
                {
                    // The bracket proves a committed loop wrap. Do not blend backwards.
                    const float distance = float(lower->loopStop - lower->loopStart) + upper->bodyFrame - lower->bodyFrame;
                    result.bodyFrame = lower->bodyFrame + distance * ratio;
                    if (result.bodyFrame >= lower->loopStop)
                        result.bodyFrame -= float(lower->loopStop - lower->loopStart);
                }
            }
            return result;
        }

        std::vector<PhysicalProjectileSnapshot> sampleProjectiles() const
        {
            std::vector<PhysicalProjectileSnapshot> result;
            if (empty()) return result;
            size_t lo = 0;
            while (lo + 1 < mFrames.size() && double(mFrames[lo + 1].tick) <= mCursor) ++lo;
            const auto& lower = mFrames[lo];
            const auto* upper = lo + 1 < mFrames.size() ? &mFrames[lo + 1] : nullptr;
            for (const auto& flight : lower.projectiles)
            {
                if (flight.terminal) continue;
                auto sampled = flight;
                if (upper)
                {
                    const auto key = [](const PhysicalProjectileSnapshot& p) {
                        return std::tuple(p.casterKind, p.caster, p.casterLife, p.command);
                    };
                    const auto next = std::ranges::find_if(upper->projectiles,
                        [&](const auto& p) { return key(p) == key(flight) && p.record == flight.record; });
                    if (next != upper->projectiles.end())
                    {
                        const float ratio = float((mCursor - double(lower.tick)) / double(upper->tick - lower.tick));
                        for (size_t axis = 0; axis < 3; ++axis)
                        {
                            sampled.position[axis] = std::lerp(flight.position[axis], next->position[axis], ratio);
                            sampled.velocity[axis] = std::lerp(flight.velocity[axis], next->velocity[axis], ratio);
                        }
                    }
                }
                result.push_back(std::move(sampled));
            }
            return result;
        }
    };
}
#endif
