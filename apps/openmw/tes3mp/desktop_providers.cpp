#include "desktop_providers.hpp"
#include "movement_mapping.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwinput/actions.hpp"
#include "../mwrender/replicatedactor.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/scene.hpp"
#include "../mwworld/worldmodel.hpp"

#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/debug/debuglog.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        constexpr std::uint64_t InteriorFixture = 7;
        constexpr std::uint64_t ExteriorFixture = 8;
        constexpr double PositionScale = 1024.0;
        constexpr double TurnScale = 4294967296.0;

        ESM::RefId refId(std::string_view value)
        {
            return ESM::RefId::stringRefId(value);
        }

        std::optional<CellId> toCanonical(const MWWorld::Cell& cell, const DesktopFixtureMapping& mapping)
        {
            if (!cell.isExterior())
            {
                if (cell.getId() != refId(mapping.interiorCell))
                    return std::nullopt;
                return CellId::interior(*CellSpaceId::fromValue(InteriorFixture));
            }
            if (cell.getWorldSpace() != refId(mapping.exteriorWorldspace))
                return std::nullopt;
            return CellId::exterior(*CellSpaceId::fromValue(ExteriorFixture), cell.getGridX(), cell.getGridY());
        }

        ESM::Position toOpenMW(const Transform& transform)
        {
            ESM::Position result{};
            const auto position = transform.position();
            result.pos[0] = static_cast<float>(static_cast<double>(position.x()) / PositionScale);
            result.pos[1] = static_cast<float>(static_cast<double>(position.y()) / PositionScale);
            result.pos[2] = static_cast<float>(static_cast<double>(position.z()) / PositionScale);
            const auto orientation = transform.orientation();
            const auto radians = [](Turn32 turn) {
                return -static_cast<float>(static_cast<double>(turn.value()) / TurnScale * 2.0 * std::numbers::pi);
            };
            result.rot[0] = radians(orientation.x());
            result.rot[1] = radians(orientation.y());
            result.rot[2] = radians(orientation.z());
            return result;
        }

        ESM::Position toOpenMW(const RemoteMotionPose& pose)
        {
            ESM::Position result{};
            result.pos[0] = static_cast<float>(pose.x / PositionScale);
            result.pos[1] = static_cast<float>(pose.y / PositionScale);
            result.pos[2] = static_cast<float>(pose.z / PositionScale);
            const auto radians = [](Turn32 turn) {
                return -static_cast<float>(static_cast<double>(turn.value()) / TurnScale * 2.0 * std::numbers::pi);
            };
            result.rot[0] = radians(pose.orientation.x());
            result.rot[1] = radians(pose.orientation.y());
            result.rot[2] = radians(pose.orientation.z());
            return result;
        }

        MWWorld::CellStore* resolveCell(const CellId& cell, const DesktopFixtureMapping& mapping)
        {
            auto worldModel = MWBase::Environment::get().getWorldModel();
            if (const auto* interior = cell.asInterior())
            {
                if (interior->cellSpace().value() != InteriorFixture)
                    return nullptr;
                return worldModel->findCell(refId(mapping.interiorCell));
            }
            const auto* exterior = cell.asExterior();
            if (!exterior || exterior->worldspace().value() != ExteriorFixture)
                return nullptr;
            return &worldModel->getExterior(
                ESM::ExteriorCellLocation(exterior->gridX(), exterior->gridY(), refId(mapping.exteriorWorldspace)));
        }

        ProviderResult mapReplicatedActorResult(MWRender::ReplicatedActorResult result)
        {
            using Result = MWRender::ReplicatedActorResult;
            switch (result)
            {
                case Result::Accepted:
                case Result::AnimationFallback:
                    return ProviderResult::Accepted;
                case Result::InvalidAppearanceRecord:
                case Result::MissingAppearanceDependency:
                    return ProviderResult::ContentMappingFailed;
                case Result::ResourceLoadFailed:
                case Result::CapacityExceeded:
                case Result::InvalidPose:
                case Result::LifecycleViolation:
                    return ProviderResult::PresentationFailed;
            }
            return ProviderResult::PresentationFailed;
        }

        const char* replicatedActorResultName(MWRender::ReplicatedActorResult result) noexcept
        {
            using Result = MWRender::ReplicatedActorResult;
            switch (result)
            {
                case Result::Accepted:
                    return "accepted";
                case Result::InvalidAppearanceRecord:
                    return "invalid_appearance_record";
                case Result::MissingAppearanceDependency:
                    return "missing_appearance_dependency";
                case Result::ResourceLoadFailed:
                    return "resource_load_failed";
                case Result::CapacityExceeded:
                    return "capacity_exceeded";
                case Result::InvalidPose:
                    return "invalid_pose";
                case Result::LifecycleViolation:
                    return "lifecycle_violation";
                case Result::AnimationFallback:
                    return "animation_fallback";
            }
            return "unknown";
        }

        bool sameReplicatedState(const SpatialEntitySnapshot& left, const SpatialEntitySnapshot& right) noexcept
        {
            return left.playerId() == right.playerId() && left.entityId() == right.entityId()
                && left.entityRevision() == right.entityRevision() && left.authorityEpoch() == right.authorityEpoch()
                && left.transform() == right.transform() && left.linearVelocity() == right.linearVelocity();
        }
    }

    class DesktopSemanticInput::Impl
    {
    public:
        DesktopFixtureMapping mapping;
    };

    DesktopSemanticInput::DesktopSemanticInput()
        : mImpl(std::make_unique<Impl>())
    {
    }

    DesktopSemanticInput::~DesktopSemanticInput() = default;

    void DesktopSemanticInput::configure(DesktopFixtureMapping mapping)
    {
        mImpl->mapping = std::move(mapping);
    }

    CellTransitionCapture DesktopSemanticInput::captureCellTransition() noexcept
    {
        try
        {
            auto scene = MWBase::Environment::get().getWorldScene();
            auto* current = scene->getCurrentCell();
            if (!scene->hasCellChanged() || !current)
                return {};
            auto cell = toCanonical(*current->getCell(), mImpl->mapping);
            if (!cell)
                return { ProviderResult::ContentMappingFailed, std::nullopt };
            return { ProviderResult::Accepted, FixtureCellTransition(*cell) };
        }
        catch (...)
        {
            return { ProviderResult::ContentMappingFailed, std::nullopt };
        }
    }

    std::optional<PlayerMotionIntent> DesktopSemanticInput::sampleCurrentIntent() noexcept
    {
        try
        {
            auto input = MWBase::Environment::get().getInputManager();
            if (input->controlsDisabled() || !input->getControlSwitch("playercontrols"))
                return PlayerMotionIntent(LinearVelocity3(0, 0, 0));
            const double right = static_cast<double>(input->getActionValue(MWInput::A_MoveRight))
                - static_cast<double>(input->getActionValue(MWInput::A_MoveLeft));
            const double forward = static_cast<double>(input->getActionValue(MWInput::A_MoveForward))
                - static_cast<double>(input->getActionValue(MWInput::A_MoveBackward));
            const auto player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            return mapPlanarMovement(right, forward, player.getRefData().getPosition().rot[2]);
        }
        catch (...)
        {
            return PlayerMotionIntent(LinearVelocity3(0, 0, 0));
        }
    }

    class DesktopPresentation::Impl
    {
    public:
        struct Remote
        {
            MWWorld::CellStore* cell = nullptr;
            std::unique_ptr<MWRender::ReplicatedActor> actor;
            RemoteMotionBuffer motion;
            std::optional<SpatialEntitySnapshot> lastObserved;
            std::optional<MonotonicInstant> lastAdvance;

            Remote(MWWorld::CellStore* targetCell, std::unique_ptr<MWRender::ReplicatedActor> targetActor,
                RemoteMotionMetricSink& metrics)
                : cell(targetCell)
                , actor(std::move(targetActor))
                , motion(metrics)
            {
            }
        };

        explicit Impl(RemoteMotionMetricSink& targetMetrics)
            : metrics(targetMetrics)
        {
        }

        DesktopFixtureMapping mapping;
        RemoteMotionMetricSink& metrics;
        std::map<EntityId, Remote> remotes;

        void clear() noexcept
        {
            for (auto& [entity, remote] : remotes)
            {
                (void)entity;
                remote.motion.clear();
            }
            remotes.clear();
        }

        void erase(std::map<EntityId, Remote>::iterator iter) noexcept
        {
            iter->second.motion.clear();
            remotes.erase(iter);
        }

        ProviderResult apply(const LatestWinsSnapshot& snapshot, std::span<const ObservedPlayer> observedPlayers,
            bool allowLocalCellCorrection, MonotonicInstant receivedAt)
        {
            const auto self = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
                return entry.playerId() == snapshot.header().targetPlayerId()
                    && entry.entityId() == snapshot.header().targetEntityId();
            });
            if (self == snapshot.view().entries().end())
                return ProviderResult::PresentationFailed;

            auto* targetCell = resolveCell(self->transform().cell(), mapping);
            if (!targetCell)
                return ProviderResult::ContentMappingFailed;

            auto world = MWBase::Environment::get().getWorld();
            const auto selfPosition = toOpenMW(self->transform());
            auto player = world->getPlayerPtr();
            if (!allowLocalCellCorrection && player.getCell() != targetCell)
            {
                clear();
                return ProviderResult::Accepted;
            }
            if (allowLocalCellCorrection && player.getCell() != targetCell)
            {
                clear();
                world->changeToCell(targetCell->getCell()->getId(), selfPosition, false, false);
                player = world->getPlayerPtr();
                targetCell = player.getCell();
            }
            else if (player.getCell() == targetCell)
                world->moveObject(player, selfPosition.asVec3());

            std::array<std::optional<EntityId>, MWRender::MaximumReplicatedActors> desired;
            std::size_t desiredCount = 0;
            for (const auto& observed : observedPlayers)
            {
                if (observed.playerId == snapshot.header().targetPlayerId()
                    && observed.entityId == snapshot.header().targetEntityId())
                    continue;
                const auto entry = std::ranges::find_if(snapshot.view().entries(), [&](const auto& candidate) {
                    return candidate.playerId() == observed.playerId && candidate.entityId() == observed.entityId;
                });
                if (entry == snapshot.view().entries().end() || entry->transform().cell() != self->transform().cell())
                    continue;
                if (desiredCount == desired.size())
                    return ProviderResult::PresentationFailed;
                desired[desiredCount++].emplace(observed.entityId);
                auto found = remotes.find(observed.entityId);
                const auto position = toOpenMW(entry->transform());
                if (found == remotes.end() || found->second.cell != targetCell)
                {
                    if (found != remotes.end())
                        erase(found);
                    auto [actorResult, actor] = MWRender::ReplicatedActor::create(*world->getRenderingManager(),
                        *MWBase::Environment::get().getESMStore(), refId(mapping.avatarNpc), *targetCell, position);
                    const ProviderResult mappedResult = mapReplicatedActorResult(actorResult);
                    if (mappedResult != ProviderResult::Accepted || !actor)
                    {
                        Log(Debug::Error) << "TES3MP replicated actor create failed: entity="
                                          << observed.entityId.value() << " result="
                                          << replicatedActorResultName(actorResult);
                        return mappedResult;
                    }
                    found = remotes.try_emplace(observed.entityId, targetCell, std::move(actor), metrics).first;
                }
                if (found->second.lastObserved
                    && entry->entityRevision() == found->second.lastObserved->entityRevision())
                {
                    if (!sameReplicatedState(*entry, *found->second.lastObserved))
                    {
                        Log(Debug::Error) << "TES3MP replicated actor contradictory same-revision observation: entity="
                                          << observed.entityId.value() << " revision="
                                          << entry->entityRevision().value();
                        return ProviderResult::PresentationFailed;
                    }
                    continue;
                }
                if (!found->second.motion.observe(*entry, receivedAt))
                {
                    Log(Debug::Error) << "TES3MP replicated actor motion observation rejected: entity="
                                      << observed.entityId.value() << " revision=" << entry->entityRevision().value()
                                      << " tick=" << entry->serverTick().value();
                    return ProviderResult::PresentationFailed;
                }
                found->second.lastObserved = *entry;
            }
            for (auto iter = remotes.begin(); iter != remotes.end();)
                if (std::find_if(desired.begin(), desired.begin() + desiredCount,
                        [&](const auto& value) { return value && *value == iter->first; })
                    == desired.begin() + desiredCount)
                {
                    iter->second.motion.clear();
                    iter = remotes.erase(iter);
                }
                else
                    ++iter;
            return ProviderResult::Accepted;
        }

        ProviderResult advance(MonotonicInstant now)
        {
            for (auto& [entity, remote] : remotes)
            {
                (void)entity;
                auto pose = remote.motion.advance(now);
                if (!pose)
                {
                    Log(Debug::Error) << "TES3MP replicated actor motion resolve failed: entity=" << entity.value();
                    return ProviderResult::PresentationFailed;
                }
                float animationSeconds = 0.f;
                if (remote.lastAdvance && now >= *remote.lastAdvance)
                    animationSeconds = static_cast<float>(now.nanoseconds() - remote.lastAdvance->nanoseconds()) / 1e9f;
                remote.lastAdvance = now;
                const MWRender::ReplicatedActorResult actorResult
                    = remote.actor->update(toOpenMW(*pose), animationSeconds);
                const ProviderResult result = mapReplicatedActorResult(actorResult);
                if (result != ProviderResult::Accepted)
                {
                    Log(Debug::Error) << "TES3MP replicated actor update failed: entity=" << entity.value()
                                      << " result=" << replicatedActorResultName(actorResult);
                    return result;
                }
            }
            return ProviderResult::Accepted;
        }
    };

    DesktopPresentation::DesktopPresentation(RemoteMotionMetricSink& metrics)
        : mImpl(std::make_unique<Impl>(metrics))
    {
    }

    DesktopPresentation::~DesktopPresentation() = default;

    void DesktopPresentation::configure(DesktopFixtureMapping mapping)
    {
        mImpl->mapping = std::move(mapping);
    }

    ProviderResult DesktopPresentation::applyAuthoritative(const LatestWinsSnapshot& snapshot,
        std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection,
        MonotonicInstant receivedAt) noexcept
    {
        try
        {
            const auto result = mImpl->apply(snapshot, observedPlayers, allowLocalCellCorrection, receivedAt);
            if (result != ProviderResult::Accepted)
                mImpl->clear();
            return result;
        }
        catch (...)
        {
            mImpl->clear();
            return ProviderResult::PresentationFailed;
        }
    }

    ProviderResult DesktopPresentation::advance(MonotonicInstant now) noexcept
    {
        try
        {
            const auto result = mImpl->advance(now);
            if (result != ProviderResult::Accepted)
                mImpl->clear();
            return result;
        }
        catch (...)
        {
            mImpl->clear();
            return ProviderResult::PresentationFailed;
        }
    }

    void DesktopPresentation::clear() noexcept
    {
        mImpl->clear();
    }
}
