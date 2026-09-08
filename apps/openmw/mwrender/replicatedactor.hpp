#ifndef OPENMW_MWRENDER_REPLICATEDACTOR_H
#define OPENMW_MWRENDER_REPLICATEDACTOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace ESM
{
    class RefId;
    struct Position;
}

namespace MWWorld
{
    class CellStore;
    class ESMStore;
    class Ptr;
}

namespace MWRender
{
    class RenderingManager;

    inline constexpr std::size_t MaximumReplicatedActors = 255;

    enum class ReplicatedActorLocomotion : std::uint8_t
    {
        Idle,
        SneakIdle,
        WalkForward,
        WalkBack,
        WalkLeft,
        WalkRight,
        RunForward,
        RunBack,
        RunLeft,
        RunRight,
        SneakForward,
        SneakBack,
        SneakLeft,
        SneakRight,
        Jump,
    };

    std::string_view replicatedActorAnimationGroup(ReplicatedActorLocomotion locomotion) noexcept;

    enum class ReplicatedActorResult : std::uint8_t
    {
        Accepted,
        AnimationFallback,
        InvalidAppearanceRecord,
        MissingAppearanceDependency,
        ResourceLoadFailed,
        CapacityExceeded,
        InvalidPose,
        LifecycleViolation,
    };

    constexpr bool replicatedActorResultAccepted(ReplicatedActorResult result) noexcept
    {
        return result == ReplicatedActorResult::Accepted || result == ReplicatedActorResult::AnimationFallback;
    }

    constexpr ReplicatedActorResult replicatedActorCapacityResult(std::size_t currentCount) noexcept
    {
        return currentCount < MaximumReplicatedActors ? ReplicatedActorResult::Accepted
                                                      : ReplicatedActorResult::CapacityExceeded;
    }

    bool isValidReplicatedActorPose(const ESM::Position& position) noexcept;

    class ReplicatedActor
    {
    public:
        using CreateResult = std::pair<ReplicatedActorResult, std::unique_ptr<ReplicatedActor>>;

        ~ReplicatedActor();
        ReplicatedActor(const ReplicatedActor&) = delete;
        ReplicatedActor& operator=(const ReplicatedActor&) = delete;

        ReplicatedActorResult update(
            const ESM::Position& position, ReplicatedActorLocomotion locomotion, float animationSeconds) noexcept;
        ReplicatedActorResult setDead(bool dead) noexcept;
        const MWWorld::Ptr& ptr() const noexcept;

        static CreateResult create(RenderingManager& rendering, const MWWorld::ESMStore& store,
            const ESM::RefId& npcRecord, MWWorld::CellStore& cell, const ESM::Position& position,
            std::optional<std::span<const ESM::RefId>> equipment = std::nullopt) noexcept;

    private:
        class Impl;
        explicit ReplicatedActor(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
