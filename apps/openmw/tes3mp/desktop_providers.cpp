#include "desktop_providers.hpp"
#include "movement_mapping.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwgui/containeritemmodel.hpp"
#include "../mwgui/inventoryitemmodel.hpp"
#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/itemmodel.hpp"
#include "../mwgui/worlditemmodel.hpp"
#include "../mwinput/actions.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwrender/replicatedactor.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/scene.hpp"
#include "../mwworld/worldmodel.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadweap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>
#include <utility>

namespace TES3MP::OpenMWAdapter
{
    namespace
    {
        constexpr double PositionScale = 1024.0;
        constexpr double TurnScale = 4294967296.0;

        ESM::RefId refId(std::string_view value)
        {
            return ESM::RefId::stringRefId(value);
        }

        const DesktopCellSpaceMapping* mappingFor(
            const DesktopContentMapping& mapping, CellSpaceId id, CellSpaceKind kind)
        {
            const auto found = std::ranges::find_if(
                mapping.cellSpaces, [&](const auto& value) { return value.id == id && value.kind == kind; });
            return found == mapping.cellSpaces.end() ? nullptr : &*found;
        }

        std::optional<CellId> toCanonical(const MWWorld::Cell& cell, const DesktopContentMapping& mapping)
        {
            if (!cell.isExterior())
            {
                const auto found = std::ranges::find_if(mapping.cellSpaces, [&](const auto& value) {
                    return value.kind == CellSpaceKind::Interior && cell.getId() == refId(value.record);
                });
                if (found == mapping.cellSpaces.end())
                    return std::nullopt;
                const auto result = CellId::interior(found->id);
                return mapping.manifest.contains(result) ? std::optional(result) : std::nullopt;
            }
            const auto found = std::ranges::find_if(mapping.cellSpaces, [&](const auto& value) {
                return value.kind == CellSpaceKind::Exterior && cell.getWorldSpace() == refId(value.record);
            });
            if (found == mapping.cellSpaces.end())
                return std::nullopt;
            const auto result = CellId::exterior(found->id, cell.getGridX(), cell.getGridY());
            return mapping.manifest.contains(result) ? std::optional(result) : std::nullopt;
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

        MWWorld::CellStore* resolveCell(const CellId& cell, const DesktopContentMapping& mapping)
        {
            if (!mapping.manifest.contains(cell))
                return nullptr;
            auto worldModel = MWBase::Environment::get().getWorldModel();
            if (const auto* interior = cell.asInterior())
            {
                const auto* local = mappingFor(mapping, interior->cellSpace(), CellSpaceKind::Interior);
                return local ? worldModel->findCell(refId(local->record)) : nullptr;
            }
            const auto* exterior = cell.asExterior();
            const auto* local
                = exterior ? mappingFor(mapping, exterior->worldspace(), CellSpaceKind::Exterior) : nullptr;
            if (!local)
                return nullptr;
            return &worldModel->getExterior(
                ESM::ExteriorCellLocation(exterior->gridX(), exterior->gridY(), refId(local->record)));
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

        MWRender::ReplicatedActorLocomotion toOpenMW(RemoteLocomotionAnimation animation) noexcept
        {
            using Source = RemoteLocomotionAnimation;
            using Target = MWRender::ReplicatedActorLocomotion;
            switch (animation)
            {
                case Source::Idle:
                    return Target::Idle;
                case Source::SneakIdle:
                    return Target::SneakIdle;
                case Source::WalkForward:
                    return Target::WalkForward;
                case Source::WalkBack:
                    return Target::WalkBack;
                case Source::WalkLeft:
                    return Target::WalkLeft;
                case Source::WalkRight:
                    return Target::WalkRight;
                case Source::RunForward:
                    return Target::RunForward;
                case Source::RunBack:
                    return Target::RunBack;
                case Source::RunLeft:
                    return Target::RunLeft;
                case Source::RunRight:
                    return Target::RunRight;
                case Source::SneakForward:
                    return Target::SneakForward;
                case Source::SneakBack:
                    return Target::SneakBack;
                case Source::SneakLeft:
                    return Target::SneakLeft;
                case Source::SneakRight:
                    return Target::SneakRight;
                case Source::Jump:
                    return Target::Jump;
            }
            return Target::Idle;
        }

        bool sameReplicatedState(const SpatialEntitySnapshot& left, const SpatialEntitySnapshot& right) noexcept
        {
            return left.playerId() == right.playerId() && left.entityId() == right.entityId()
                && left.entityRevision() == right.entityRevision() && left.authorityEpoch() == right.authorityEpoch()
                && left.transform() == right.transform() && left.linearVelocity() == right.linearVelocity()
                && left.locomotionMode() == right.locomotionMode();
        }

        bool sameReplicatedState(const ActorSpatialSnapshot& left, const ActorSpatialSnapshot& right) noexcept
        {
            return left.actorId() == right.actorId() && left.entityId() == right.entityId()
                && left.prototypeId() == right.prototypeId() && left.entityRevision() == right.entityRevision()
                && left.authorityEpoch() == right.authorityEpoch() && left.transform() == right.transform()
                && left.linearVelocity() == right.linearVelocity() && left.activity() == right.activity();
        }
    }

    std::optional<DesktopContentMapping> DesktopContentMapping::create(ContentManifest manifest,
        std::span<const DesktopCellSpaceMapping> cellSpaces, AppearanceId appearanceId, std::string avatarNpc,
        std::span<const DesktopActorPrototypeMapping> actorPrototypes,
        std::span<const DesktopInteractiveObjectMapping> interactiveObjects,
        std::span<const DesktopItemPrototypeMapping> itemPrototypes,
        std::span<const DesktopContainerMapping> containers)
    try
    {
        if (appearanceId != manifest.defaultAppearance() || avatarNpc.empty()
            || cellSpaces.size() != manifest.cellSpaces().size())
            return std::nullopt;
        std::vector<DesktopCellSpaceMapping> mappings(cellSpaces.begin(), cellSpaces.end());
        std::ranges::sort(mappings, {}, &DesktopCellSpaceMapping::id);
        for (std::size_t index = 0; index < mappings.size(); ++index)
        {
            const auto& mapping = mappings[index];
            const auto declaration
                = std::ranges::lower_bound(manifest.cellSpaces(), mapping.id, {}, &CellSpaceDeclaration::id);
            if (mapping.record.empty() || declaration == manifest.cellSpaces().end() || declaration->id != mapping.id
                || declaration->kind != mapping.kind || (index != 0 && mappings[index - 1].id == mapping.id))
                return std::nullopt;
            const auto local = refId(mapping.record);
            if (local == refId(avatarNpc))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (local == refId(mappings[prior].record))
                    return std::nullopt;
        }
        std::vector<DesktopActorPrototypeMapping> prototypes(actorPrototypes.begin(), actorPrototypes.end());
        std::ranges::sort(prototypes, {}, &DesktopActorPrototypeMapping::id);
        for (std::size_t index = 0; index < prototypes.size(); ++index)
        {
            if (prototypes[index].record.empty() || (index != 0 && prototypes[index - 1].id == prototypes[index].id))
                return std::nullopt;
            const auto local = refId(prototypes[index].record);
            if (local == refId(avatarNpc))
                return std::nullopt;
            for (const auto& mapping : mappings)
                if (local == refId(mapping.record))
                    return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (local == refId(prototypes[prior].record))
                    return std::nullopt;
        }
        std::vector<DesktopInteractiveObjectMapping> objects(interactiveObjects.begin(), interactiveObjects.end());
        std::ranges::sort(objects, {}, &DesktopInteractiveObjectMapping::id);
        std::vector<std::pair<std::uint32_t, std::int32_t>> objectRefs;
        objectRefs.reserve(objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            if (objects[index].refNumContentFile < -1 || (index != 0 && objects[index - 1].id == objects[index].id))
                return std::nullopt;
            objectRefs.emplace_back(objects[index].refNumIndex, objects[index].refNumContentFile);
        }
        std::ranges::sort(objectRefs);
        for (std::size_t index = 1; index < objectRefs.size(); ++index)
        {
            if (objectRefs[index - 1].first == objectRefs[index].first
                && (objectRefs[index - 1].second == -1 || objectRefs[index].second == -1
                    || objectRefs[index - 1].second == objectRefs[index].second))
                return std::nullopt;
        }
        std::vector<DesktopItemPrototypeMapping> items(itemPrototypes.begin(), itemPrototypes.end());
        std::ranges::sort(items, {}, &DesktopItemPrototypeMapping::id);
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (items[index].record.empty() || (index != 0 && items[index - 1].id == items[index].id))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (refId(items[prior].record) == refId(items[index].record))
                    return std::nullopt;
        }
        std::vector<DesktopContainerMapping> containerMappings(containers.begin(), containers.end());
        std::ranges::sort(containerMappings, {}, &DesktopContainerMapping::id);
        std::vector<std::pair<std::uint32_t, std::int32_t>> containerRefs;
        for (std::size_t index = 0; index < containerMappings.size(); ++index)
        {
            if (containerMappings[index].refNumContentFile < -1
                || (index != 0 && containerMappings[index - 1].id == containerMappings[index].id))
                return std::nullopt;
            containerRefs.emplace_back(
                containerMappings[index].refNumIndex, containerMappings[index].refNumContentFile);
        }
        std::ranges::sort(containerRefs);
        for (std::size_t index = 1; index < containerRefs.size(); ++index)
            if (containerRefs[index - 1].first == containerRefs[index].first
                && (containerRefs[index - 1].second == -1 || containerRefs[index].second == -1
                    || containerRefs[index - 1].second == containerRefs[index].second))
                return std::nullopt;
        return DesktopContentMapping{ std::move(manifest), std::move(mappings), appearanceId, std::move(avatarNpc),
            std::move(prototypes), std::move(objects), std::move(items), std::move(containerMappings) };
    }
    catch (...)
    {
        return std::nullopt;
    }

    class DesktopSemanticInput::Impl
    {
    public:
        std::optional<DesktopContentMapping> mapping;
        const PresentationProvider* presentation = nullptr;
        std::optional<ObjectInteractionCapture> pendingInteraction;
        std::optional<InventoryTransactionCapture> pendingInventoryTransaction;
        std::optional<MeleeAttackCapture> pendingMeleeAttack;
        bool interceptorInstalled = false;

        void ensureInterceptor(DesktopSemanticInput* self)
        {
            if (interceptorInstalled)
                return;
            try
            {
                auto world = MWBase::Environment::get().getWorld();
                if (!world)
                    return;
                MWWorld::Player& player = world->getPlayer();
                player.setActivationInterceptor([self](const MWWorld::Ptr& toActivate, const MWWorld::Ptr& actor) {
                    return self->handleActivation(toActivate, actor);
                });
                player.setMeleeTargetProvider([self](std::vector<MWWorld::Ptr>& targets) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (presentation)
                        presentation->appendMeleeTargets(targets);
                });
                player.setMeleeHitInterceptor([self](float strength, int type, const MWWorld::Ptr& victim) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (!presentation)
                        return false;
                    auto capture = presentation->captureMeleeAttack(victim, strength, type);
                    if (capture && !self->mImpl->pendingMeleeAttack)
                        self->mImpl->pendingMeleeAttack = std::move(*capture);
                    return capture.has_value();
                });
                MWGui::ItemModel::setTransferInterceptor([self](MWGui::ItemModel& source, const MWGui::ItemStack& item,
                                                             std::size_t count, MWGui::ItemModel& target) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (!presentation)
                        return false;
                    auto capture = presentation->inventoryTransfer(source, item.mBase, count, target);
                    if (capture && !self->mImpl->pendingInventoryTransaction)
                        self->mImpl->pendingInventoryTransaction = std::move(*capture);
                    return capture.has_value() || presentation->observesInventoryItem(item.mBase);
                });
                MWGui::InventoryWindow::setUseItemInterceptor([self](const MWWorld::Ptr& item) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (!presentation)
                        return false;
                    auto capture = presentation->inventoryUse(item);
                    if (capture && !self->mImpl->pendingInventoryTransaction)
                        self->mImpl->pendingInventoryTransaction = std::move(*capture);
                    return capture.has_value() || presentation->observesInventoryItem(item);
                });
                interceptorInstalled = true;
            }
            catch (...)
            {
            }
        }

        void clearInterceptor()
        {
            if (!interceptorInstalled)
                return;
            try
            {
                auto world = MWBase::Environment::get().getWorld();
                if (world)
                {
                    world->getPlayer().clearActivationInterceptor();
                    world->getPlayer().clearMeleeCombatInterceptors();
                }
                MWGui::ItemModel::clearTransferInterceptor();
                MWGui::InventoryWindow::clearUseItemInterceptor();
            }
            catch (...)
            {
            }
            interceptorInstalled = false;
        }
    };

    DesktopSemanticInput::DesktopSemanticInput()
        : mImpl(std::make_unique<Impl>())
    {
    }

    DesktopSemanticInput::~DesktopSemanticInput() = default;

    void DesktopSemanticInput::configure(DesktopContentMapping mapping, const PresentationProvider* presentation)
    {
        mImpl->mapping = std::move(mapping);
        mImpl->presentation = presentation;
    }

    void DesktopSemanticInput::clearSessionState() noexcept
    {
        mImpl->clearInterceptor();
        mImpl->pendingInteraction.reset();
        mImpl->pendingInventoryTransaction.reset();
        mImpl->pendingMeleeAttack.reset();
    }

    bool DesktopSemanticInput::handleActivation(const MWWorld::Ptr& toActivate, const MWWorld::Ptr& player) noexcept
    {
        try
        {
            (void)player;
            if (toActivate.isEmpty() || !mImpl->mapping)
                return false;

            if (toActivate.getType() == ESM::Door::sRecordId)
                return queueObjectActivation(toActivate);
            auto* presentation = dynamic_cast<const DesktopPresentation*>(mImpl->presentation);
            auto capture = presentation ? presentation->inventoryPickup(toActivate) : std::nullopt;
            if (!capture)
                return false;
            if (!mImpl->pendingInventoryTransaction)
                mImpl->pendingInventoryTransaction = std::move(*capture);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool DesktopSemanticInput::queueObjectActivation(const MWWorld::Ptr& doorPtr) noexcept
    {
        try
        {
            if (doorPtr.isEmpty() || !mImpl->mapping)
                return false;
            if (doorPtr.getType() != ESM::Door::sRecordId)
                return false;

            const auto refNum = doorPtr.getCellRef().getRefNum();
            std::optional<InteractiveObjectId> objectId;
            bool explicitlyMapped = false;
            for (const auto& objMap : mImpl->mapping->interactiveObjects)
            {
                if (objMap.refNumIndex == refNum.mIndex
                    && (objMap.refNumContentFile == -1 || objMap.refNumContentFile == refNum.mContentFile))
                {
                    objectId = objMap.id;
                    explicitlyMapped = true;
                    break;
                }
            }
            if (!objectId)
            {
                objectId = InteractiveObjectId::fromValue(refNum.mIndex);
            }
            if (!objectId)
                return false;

            auto* cellStore = doorPtr.getCell();
            if (!cellStore)
            {
                auto scene = MWBase::Environment::get().getWorldScene();
                if (scene)
                    cellStore = scene->getCurrentCell();
            }
            if (!cellStore)
                return false;
            auto canonicalCell = toCanonical(*cellStore->getCell(), *mImpl->mapping);
            if (!canonicalCell)
                return false;

            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return false;
            const auto playerPtr = world->getPlayerPtr();
            const auto& pos = playerPtr.getRefData().getPosition();
            const auto origin = Position3(static_cast<std::int64_t>(std::round(pos.pos[0] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[1] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[2] * PositionScale)));

            if (!mImpl->presentation)
                return false;
            const auto expectedRev = mImpl->presentation->observedObjectRevision(*objectId);
            if (!expectedRev)
                return explicitlyMapped;

            mImpl->pendingInteraction = ObjectInteractionCapture{ .objectId = *objectId,
                .targetCell = *canonicalCell,
                .interactionOrigin = origin,
                .expectedRevision = *expectedRev,
                .kind = ObjectInteractionKind::Activate,
                .requestedKey = std::nullopt };
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<ObjectInteractionCapture> DesktopSemanticInput::captureObjectInteraction() noexcept
    {
        mImpl->ensureInterceptor(this);
        if (!mImpl->pendingInteraction)
            return std::nullopt;
        auto captured = std::move(mImpl->pendingInteraction);
        mImpl->pendingInteraction.reset();
        return captured;
    }

    std::optional<InventoryTransactionCapture> DesktopSemanticInput::captureInventoryTransaction() noexcept
    {
        mImpl->ensureInterceptor(this);
        if (!mImpl->pendingInventoryTransaction)
            return std::nullopt;
        auto captured = std::move(mImpl->pendingInventoryTransaction);
        mImpl->pendingInventoryTransaction.reset();
        return captured;
    }

    std::optional<MeleeAttackCapture> DesktopSemanticInput::captureMeleeAttack() noexcept
    {
        mImpl->ensureInterceptor(this);
        if (!mImpl->pendingMeleeAttack)
            return std::nullopt;
        auto captured = std::move(mImpl->pendingMeleeAttack);
        mImpl->pendingMeleeAttack.reset();
        return captured;
    }

    CellTransitionCapture DesktopSemanticInput::captureCellTransition() noexcept
    {
        try
        {
            auto scene = MWBase::Environment::get().getWorldScene();
            auto* current = scene->getCurrentCell();
            if (!scene->hasCellChanged())
                return {};
            mImpl->pendingInteraction.reset();
            mImpl->pendingInventoryTransaction.reset();
            mImpl->pendingMeleeAttack.reset();
            if (!current)
                return {};
            if (!mImpl->mapping)
                return { ProviderResult::ContentMappingFailed, std::nullopt };
            auto cell = toCanonical(*current->getCell(), *mImpl->mapping);
            if (!cell)
                return { ProviderResult::ContentMappingFailed, std::nullopt };
            return { ProviderResult::Accepted, CellTransition(*cell) };
        }
        catch (...)
        {
            return { ProviderResult::ContentMappingFailed, std::nullopt };
        }
    }

    std::optional<LocomotionIntent> DesktopSemanticInput::sampleCurrentIntent() noexcept
    {
        try
        {
            auto input = MWBase::Environment::get().getInputManager();
            if (input->controlsDisabled() || !input->getControlSwitch("playercontrols"))
                return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));
            const double right = static_cast<double>(input->getActionValue(MWInput::A_MoveRight))
                - static_cast<double>(input->getActionValue(MWInput::A_MoveLeft));
            const double forward = static_cast<double>(input->getActionValue(MWInput::A_MoveForward))
                - static_cast<double>(input->getActionValue(MWInput::A_MoveBackward));
            const auto player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            return mapPlanarMovement(right, forward, player.getRefData().getPosition().rot[2]);
        }
        catch (...)
        {
            return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));
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
            std::array<std::optional<ItemPrototypeId>, static_cast<std::size_t>(EquipmentSlot::Count)> equipment{};
            bool authoritativeEquipment = false;

            Remote(MWWorld::CellStore* targetCell, std::unique_ptr<MWRender::ReplicatedActor> targetActor,
                RemoteMotionMetricSink& metrics)
                : cell(targetCell)
                , actor(std::move(targetActor))
                , motion(metrics)
            {
            }
        };

        struct ActorRemote
        {
            MWWorld::CellStore* cell = nullptr;
            std::unique_ptr<MWRender::ReplicatedActor> actor;
            RemoteMotionBuffer motion;
            std::optional<ActorSpatialSnapshot> lastObserved;
            std::optional<MonotonicInstant> lastAdvance;

            ActorRemote(MWWorld::CellStore* targetCell, std::unique_ptr<MWRender::ReplicatedActor> targetActor,
                RemoteMotionMetricSink& metrics)
                : cell(targetCell)
                , actor(std::move(targetActor))
                , motion(metrics)
            {
            }
        };

        struct ObservedDoorPresentation
        {
            TES3MP::DoorState lastDoorState = TES3MP::DoorState::Closed;
            TES3MP::LockState lastLockState = TES3MP::LockState::Unlocked;
            TES3MP::TrapState lastTrapState = TES3MP::TrapState::Disarmed;
            ObjectRevision lastRevision = ObjectRevision::initial();
        };

        struct ObservedInventoryStack
        {
            CanonicalItemStack stack;
            MWWorld::Ptr ptr;
            std::optional<ContainerId> container;
            bool ground = false;
            std::optional<ContainerRevision> containerRevision;
            std::optional<WorldItemRevision> worldItemRevision;
        };

        explicit Impl(RemoteMotionMetricSink& targetMetrics)
            : metrics(targetMetrics)
        {
        }

        std::optional<DesktopContentMapping> mapping;
        RemoteMotionMetricSink& metrics;
        std::map<EntityId, Remote> remotes;
        std::map<EntityId, ActorRemote> actorRemotes;
        std::map<InteractiveObjectId, ObservedDoorPresentation> observedDoors;
        std::map<ItemStackId, ObservedInventoryStack> observedInventoryStacks;
        std::map<ContainerId, ContainerRevision> observedContainerRevisions;
        std::map<ItemStackId, MWWorld::Ptr> presentedGroundItems;
        std::optional<InventoryRevision> observedPlayerInventoryRevision;
        std::optional<CanonicalRevision> observedInventoryCanonicalRevision;
        std::optional<LatestWinsCombatSnapshot> combatSnapshot;

        void clear() noexcept
        {
            for (auto& [entity, remote] : remotes)
            {
                (void)entity;
                remote.motion.clear();
            }
            remotes.clear();
            for (auto& [entity, remote] : actorRemotes)
            {
                (void)entity;
                remote.motion.clear();
            }
            actorRemotes.clear();
            observedDoors.clear();
            try
            {
                auto world = MWBase::Environment::get().getWorld();
                if (world)
                    for (const auto& [stack, ptr] : presentedGroundItems)
                    {
                        (void)stack;
                        if (!ptr.isEmpty())
                            world->deleteObject(ptr);
                    }
            }
            catch (...)
            {
            }
            presentedGroundItems.clear();
            observedInventoryStacks.clear();
            observedContainerRevisions.clear();
            observedPlayerInventoryRevision.reset();
            observedInventoryCanonicalRevision.reset();
            combatSnapshot.reset();
        }

        void erase(std::map<EntityId, ActorRemote>::iterator iter) noexcept
        {
            iter->second.motion.clear();
            actorRemotes.erase(iter);
        }

        void erase(std::map<EntityId, Remote>::iterator iter) noexcept
        {
            iter->second.motion.clear();
            remotes.erase(iter);
        }

        bool equipmentRecords(const LatestWinsEquipmentSnapshot& snapshot, PlayerId player,
            std::vector<ESM::RefId>& records,
            std::array<std::optional<ItemPrototypeId>, static_cast<std::size_t>(EquipmentSlot::Count)>& slots) const
        {
            slots = {};
            const auto member = std::ranges::lower_bound(snapshot.members, player, {}, &PublicEquipmentMember::player);
            if (member == snapshot.members.end() || member->player != player)
                return true;
            slots = member->slots;
            for (std::size_t index = 0; index < slots.size(); ++index)
            {
                const auto& prototype = slots[index];
                if (!prototype)
                    continue;
                if (index == static_cast<std::size_t>(EquipmentSlot::Ammunition))
                    continue;
                const auto* local = itemMapping(*prototype);
                if (!local)
                    return false;
                records.push_back(refId(local->record));
            }
            return true;
        }

        ProviderResult applyPublicEquipment(const LatestWinsEquipmentSnapshot& snapshot)
        {
            for (auto& [entity, remote] : remotes)
            {
                (void)entity;
                if (!remote.lastObserved)
                    continue;
                std::vector<ESM::RefId> records;
                std::array<std::optional<ItemPrototypeId>, static_cast<std::size_t>(EquipmentSlot::Count)> slots;
                if (!equipmentRecords(snapshot, remote.lastObserved->playerId(), records, slots))
                    return ProviderResult::ContentMappingFailed;
                if (remote.authoritativeEquipment && remote.equipment == slots)
                    continue;
                auto [actorResult, actor]
                    = MWRender::ReplicatedActor::create(*MWBase::Environment::get().getWorld()->getRenderingManager(),
                        *MWBase::Environment::get().getESMStore(), refId(mapping->avatarNpc), *remote.cell,
                        toOpenMW(remote.lastObserved->transform()), std::span<const ESM::RefId>(records));
                const auto mapped = mapReplicatedActorResult(actorResult);
                if (mapped != ProviderResult::Accepted || !actor)
                    return mapped;
                remote.actor = std::move(actor);
                remote.equipment = slots;
                remote.authoritativeEquipment = true;
            }
            return ProviderResult::Accepted;
        }

        ProviderResult apply(const LatestWinsSnapshot& snapshot, std::span<const ObservedPlayer> observedPlayers,
            bool allowLocalCellCorrection, MonotonicInstant receivedAt,
            const std::optional<LocalLocomotionReconciliation>& localReconciliation)
        {
            if (!mapping)
                return ProviderResult::ContentMappingFailed;
            const auto& content = *mapping;
            const auto self = std::ranges::find_if(snapshot.view().entries(), [&](const auto& entry) {
                return entry.playerId() == snapshot.header().targetPlayerId()
                    && entry.entityId() == snapshot.header().targetEntityId();
            });
            if (self == snapshot.view().entries().end())
                return ProviderResult::PresentationFailed;

            const Transform& localRoot = localReconciliation ? localReconciliation->root : self->transform();
            auto* targetCell = resolveCell(localRoot.cell(), content);
            if (!targetCell)
                return ProviderResult::ContentMappingFailed;

            auto world = MWBase::Environment::get().getWorld();
            const auto selfPosition = toOpenMW(localRoot);
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
            {
                const auto& local = player.getRefData().getPosition();
                (void)metrics.tryRecord({ MovementMetricKey::LocalCorrectionDistanceQuanta,
                    movementCorrectionDistanceQuanta(localRoot.position(),
                        static_cast<double>(local.pos[0]) * PositionScale,
                        static_cast<double>(local.pos[1]) * PositionScale,
                        static_cast<double>(local.pos[2]) * PositionScale) });
                world->moveObject(player, selfPosition.asVec3());
            }

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
                if (entry->appearanceId() != content.appearanceId)
                    return ProviderResult::ContentMappingFailed;
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
                        *MWBase::Environment::get().getESMStore(), refId(content.avatarNpc), *targetCell, position);
                    const ProviderResult mappedResult = mapReplicatedActorResult(actorResult);
                    if (mappedResult != ProviderResult::Accepted || !actor)
                    {
                        Log(Debug::Error)
                            << "TES3MP replicated actor create failed: entity=" << observed.entityId.value()
                            << " result=" << replicatedActorResultName(actorResult);
                        return mappedResult;
                    }
                    found = remotes.try_emplace(observed.entityId, targetCell, std::move(actor), metrics).first;
                }
                if (found->second.lastObserved
                    && entry->entityRevision() == found->second.lastObserved->entityRevision())
                {
                    if (!sameReplicatedState(*entry, *found->second.lastObserved))
                    {
                        Log(Debug::Error)
                            << "TES3MP replicated actor contradictory same-revision observation: entity="
                            << observed.entityId.value() << " revision=" << entry->entityRevision().value();
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

        ProviderResult applyActors(const LatestWinsActorSnapshot& snapshot,
            std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt)
        {
            if (!mapping)
                return ProviderResult::ContentMappingFailed;
            const auto& content = *mapping;
            std::array<std::optional<EntityId>, MaximumActorInterestMembers> desired;
            std::size_t desiredCount = 0;
            for (const auto& observed : observedActors)
            {
                const auto entry = std::ranges::find_if(snapshot.view().entries(), [&](const auto& candidate) {
                    return candidate.actorId() == observed.actorId && candidate.entityId() == observed.entityId
                        && candidate.prototypeId() == observed.prototypeId;
                });
                if (entry == snapshot.view().entries().end())
                    return ProviderResult::PresentationFailed;
                const auto prototype = std::ranges::lower_bound(
                    content.actorPrototypes, observed.prototypeId, {}, &DesktopActorPrototypeMapping::id);
                if (prototype == content.actorPrototypes.end() || prototype->id != observed.prototypeId)
                    return ProviderResult::ContentMappingFailed;
                auto* targetCell = resolveCell(entry->transform().cell(), content);
                if (!targetCell)
                    return ProviderResult::ContentMappingFailed;
                if (desiredCount == desired.size())
                    return ProviderResult::PresentationFailed;
                desired[desiredCount++].emplace(observed.entityId);
                auto found = actorRemotes.find(observed.entityId);
                const auto position = toOpenMW(entry->transform());
                if (found == actorRemotes.end() || found->second.cell != targetCell)
                {
                    if (found != actorRemotes.end())
                        erase(found);
                    if (remotes.size() + actorRemotes.size() >= MWRender::MaximumReplicatedActors)
                        continue;
                    auto [actorResult, actor] = MWRender::ReplicatedActor::create(
                        *MWBase::Environment::get().getWorld()->getRenderingManager(),
                        *MWBase::Environment::get().getESMStore(), refId(prototype->record), *targetCell, position);
                    const auto mapped = mapReplicatedActorResult(actorResult);
                    if (mapped != ProviderResult::Accepted || !actor)
                        return mapped;
                    found = actorRemotes.try_emplace(observed.entityId, targetCell, std::move(actor), metrics).first;
                }
                if (found == actorRemotes.end())
                    continue;
                if (found->second.lastObserved
                    && entry->entityRevision() == found->second.lastObserved->entityRevision())
                {
                    if (!sameReplicatedState(*entry, *found->second.lastObserved))
                        return ProviderResult::PresentationFailed;
                    continue;
                }
                if (!found->second.motion.observe(*entry, receivedAt))
                    return ProviderResult::PresentationFailed;
                found->second.lastObserved = *entry;
                if (combatSnapshot)
                {
                    const auto combat = std::ranges::lower_bound(
                        combatSnapshot->actors(), observed.actorId, {}, &ActorCombatSnapshot::actorId);
                    if (combat != combatSnapshot->actors().end() && combat->actorId == observed.actorId
                        && !replicatedActorResultAccepted(found->second.actor->setDead(combat->dead)))
                        return ProviderResult::PresentationFailed;
                }
            }
            if (snapshot.view().entries().size() != observedActors.size())
                return ProviderResult::PresentationFailed;
            for (auto iter = actorRemotes.begin(); iter != actorRemotes.end();)
                if (std::find_if(desired.begin(), desired.begin() + desiredCount,
                        [&](const auto& value) { return value && *value == iter->first; })
                    == desired.begin() + desiredCount)
                {
                    iter->second.motion.clear();
                    iter = actorRemotes.erase(iter);
                }
                else
                    ++iter;
            return ProviderResult::Accepted;
        }

        void appendMeleeTargets(std::vector<MWWorld::Ptr>& targets) const
        {
            for (const auto& [entity, remote] : actorRemotes)
            {
                (void)entity;
                if (remote.actor)
                    targets.push_back(remote.actor->ptr());
            }
        }

        std::optional<MeleeAttackCapture> captureMeleeAttack(
            const MWWorld::Ptr& victim, float attackStrength, int attackType) const
        {
            if (!combatSnapshot || !std::isfinite(attackStrength) || attackStrength < 0.f || attackStrength > 1.f)
                return std::nullopt;
            MeleeAttackType type;
            if (attackType == ESM::Weapon::AT_Chop)
                type = MeleeAttackType::Chop;
            else if (attackType == ESM::Weapon::AT_Slash)
                type = MeleeAttackType::Slash;
            else if (attackType == ESM::Weapon::AT_Thrust)
                type = MeleeAttackType::Thrust;
            else
                return std::nullopt;

            std::optional<ActorId> target;
            CombatRevision targetRevision = CombatRevision::initial();
            if (!victim.isEmpty())
            {
                const auto remote = std::ranges::find_if(actorRemotes,
                    [&](const auto& entry) { return entry.second.actor && entry.second.actor->ptr() == victim; });
                if (remote == actorRemotes.end() || !remote->second.lastObserved)
                    return std::nullopt;
                target = remote->second.lastObserved->actorId();
                const auto combat = std::ranges::lower_bound(
                    combatSnapshot->actors(), *target, {}, &ActorCombatSnapshot::actorId);
                if (combat == combatSnapshot->actors().end() || combat->actorId != *target || combat->dead)
                    return std::nullopt;
                targetRevision = combat->combatRevision;
            }
            return MeleeAttackCapture{ target, combatSnapshot->serverTick(),
                combatSnapshot->selfCombatRevision(), targetRevision, type, attackStrength };
        }

        ProviderResult applyCombat(const LatestWinsCombatSnapshot& snapshot)
        {
            if (combatSnapshot && snapshot.serverTick() < combatSnapshot->serverTick())
                return ProviderResult::Accepted;
            if (combatSnapshot && snapshot.serverTick() == combatSnapshot->serverTick()
                && snapshot != *combatSnapshot)
                return ProviderResult::PresentationFailed;
            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return ProviderResult::PresentationFailed;
            auto player = world->getPlayerPtr();
            auto& playerStats = player.getClass().getCreatureStats(player);
            auto fatigue = playerStats.getFatigue();
            fatigue.setCurrent(snapshot.selfFatigue());
            playerStats.setFatigue(fatigue);

            for (auto& [entity, remote] : actorRemotes)
            {
                (void)entity;
                if (!remote.actor || !remote.lastObserved)
                    continue;
                const auto combat = std::ranges::lower_bound(
                    snapshot.actors(), remote.lastObserved->actorId(), {}, &ActorCombatSnapshot::actorId);
                if (combat == snapshot.actors().end() || combat->actorId != remote.lastObserved->actorId())
                    continue;
                auto& stats = remote.actor->ptr().getClass().getCreatureStats(remote.actor->ptr());
                if (!combat->dead && stats.isDead())
                    stats.resurrect();
                auto health = stats.getHealth();
                health.setCurrent(combat->health);
                stats.setHealth(health);
                auto actorFatigue = stats.getFatigue();
                actorFatigue.setCurrent(combat->fatigue);
                stats.setFatigue(actorFatigue);
                if (!replicatedActorResultAccepted(remote.actor->setDead(combat->dead)))
                    return ProviderResult::PresentationFailed;
            }
            combatSnapshot = snapshot;
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
                const MWRender::ReplicatedActorResult actorResult = remote.actor->update(
                    toOpenMW(*pose), toOpenMW(remoteLocomotionAnimation(*pose)), animationSeconds);
                const ProviderResult result = mapReplicatedActorResult(actorResult);
                if (result != ProviderResult::Accepted)
                {
                    Log(Debug::Error) << "TES3MP replicated actor update failed: entity=" << entity.value()
                                      << " result=" << replicatedActorResultName(actorResult);
                    return result;
                }
            }
            for (auto& [entity, remote] : actorRemotes)
            {
                auto pose = remote.motion.advance(now);
                if (!pose)
                    return ProviderResult::PresentationFailed;
                float animationSeconds = 0.f;
                if (remote.lastAdvance && now >= *remote.lastAdvance)
                    animationSeconds = static_cast<float>(now.nanoseconds() - remote.lastAdvance->nanoseconds()) / 1e9f;
                remote.lastAdvance = now;
                const auto actorResult = remote.actor->update(
                    toOpenMW(*pose), toOpenMW(remoteLocomotionAnimation(*pose)), animationSeconds);
                const auto result = mapReplicatedActorResult(actorResult);
                if (result != ProviderResult::Accepted)
                {
                    Log(Debug::Error) << "TES3MP replicated content actor update failed: entity=" << entity.value()
                                      << " result=" << replicatedActorResultName(actorResult);
                    return result;
                }
            }
            return ProviderResult::Accepted;
        }

        static MWWorld::Ptr findDoorInCell(
            MWWorld::CellStore& cell, std::uint32_t refNumIndex, std::int32_t contentFile)
        {
            MWWorld::Ptr found;
            cell.forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
                const auto refNum = ptr.getCellRef().getRefNum();
                if (refNum.mIndex == refNumIndex && (contentFile < 0 || refNum.mContentFile == contentFile))
                {
                    found = ptr;
                    return false;
                }
                return true;
            });
            return found;
        }

        static MWWorld::Ptr findActiveDoor(std::uint32_t refNumIndex, std::int32_t contentFile)
        {
            auto scene = MWBase::Environment::get().getWorldScene();
            auto* current = scene->getCurrentCell();
            if (current)
            {
                auto ptr = findDoorInCell(*current, refNumIndex, contentFile);
                if (!ptr.isEmpty())
                    return ptr;
            }
            for (auto* cell : scene->getActiveCells())
            {
                if (cell && cell != current)
                {
                    auto ptr = findDoorInCell(*cell, refNumIndex, contentFile);
                    if (!ptr.isEmpty())
                        return ptr;
                }
            }
            return {};
        }

        static MWWorld::Ptr findContainerInCell(
            MWWorld::CellStore& cell, std::uint32_t refNumIndex, std::int32_t contentFile)
        {
            MWWorld::Ptr found;
            cell.forEachType<ESM::Container>([&](const MWWorld::Ptr& ptr) {
                const auto refNum = ptr.getCellRef().getRefNum();
                if (refNum.mIndex == refNumIndex && (contentFile < 0 || refNum.mContentFile == contentFile))
                {
                    found = ptr;
                    return false;
                }
                return true;
            });
            return found;
        }

        static MWWorld::Ptr findActiveContainer(std::uint32_t refNumIndex, std::int32_t contentFile)
        {
            auto scene = MWBase::Environment::get().getWorldScene();
            if (!scene)
                return {};
            auto* current = scene->getCurrentCell();
            if (current)
            {
                auto ptr = findContainerInCell(*current, refNumIndex, contentFile);
                if (!ptr.isEmpty())
                    return ptr;
            }
            for (auto* cell : scene->getActiveCells())
                if (cell && cell != current)
                {
                    auto ptr = findContainerInCell(*cell, refNumIndex, contentFile);
                    if (!ptr.isEmpty())
                        return ptr;
                }
            return {};
        }

        const DesktopItemPrototypeMapping* itemMapping(ItemPrototypeId id) const noexcept
        {
            if (!mapping)
                return nullptr;
            const auto found
                = std::ranges::lower_bound(mapping->itemPrototypes, id, {}, &DesktopItemPrototypeMapping::id);
            return found != mapping->itemPrototypes.end() && found->id == id ? &*found : nullptr;
        }

        std::optional<ItemPrototypeId> itemPrototype(const MWWorld::Ptr& ptr) const noexcept
        {
            if (!mapping || ptr.isEmpty())
                return std::nullopt;
            const auto found = std::ranges::find_if(mapping->itemPrototypes,
                [&](const auto& item) { return refId(item.record) == ptr.getCellRef().getRefId(); });
            return found == mapping->itemPrototypes.end() ? std::nullopt : std::optional(found->id);
        }

        std::optional<ContainerId> containerId(const MWWorld::Ptr& ptr) const noexcept
        {
            if (!mapping || ptr.isEmpty())
                return std::nullopt;
            const auto refNum = ptr.getCellRef().getRefNum();
            const auto found = std::ranges::find_if(mapping->containers, [&](const auto& container) {
                return container.refNumIndex == refNum.mIndex
                    && (container.refNumContentFile < 0 || container.refNumContentFile == refNum.mContentFile);
            });
            return found == mapping->containers.end() ? std::nullopt : std::optional(found->id);
        }

        const ObservedInventoryStack* observedStack(const MWWorld::Ptr& ptr) const noexcept
        {
            const auto found = std::ranges::find_if(
                observedInventoryStacks, [&](const auto& value) { return value.second.ptr == ptr; });
            return found == observedInventoryStacks.end() ? nullptr : &found->second;
        }

        static Position3 playerOrigin()
        {
            const auto& pos = MWBase::Environment::get().getWorld()->getPlayerPtr().getRefData().getPosition();
            return Position3(static_cast<std::int64_t>(std::round(pos.pos[0] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[1] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[2] * PositionScale)));
        }

        template <class Sink>
        std::optional<MWWorld::Ptr> materializeItem(const CanonicalItemStack& stack, Sink&& sink) const
        {
            const auto* local = itemMapping(stack.prototypeId);
            if (!local || stack.count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                || stack.condition > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                return std::nullopt;
            MWWorld::ManualRef reference(
                *MWBase::Environment::get().getESMStore(), refId(local->record), static_cast<int>(stack.count));
            auto ptr = reference.getPtr();
            ptr.getCellRef().setCount(static_cast<int>(stack.count));
            if (ptr.getClass().hasItemHealth(ptr))
                ptr.getCellRef().setCharge(static_cast<int>(stack.condition));
            if (!ptr.getClass().getEnchantment(ptr).empty())
                ptr.getCellRef().setEnchantmentCharge(static_cast<float>(stack.enchantmentCharge));
            if (stack.soulPrototype)
            {
                if (!mapping)
                    return std::nullopt;
                const auto soul = std::ranges::lower_bound(
                    mapping->actorPrototypes, *stack.soulPrototype, {}, &DesktopActorPrototypeMapping::id);
                if (soul == mapping->actorPrototypes.end() || soul->id != *stack.soulPrototype)
                    return std::nullopt;
                ptr.getCellRef().setSoul(refId(soul->record));
            }
            return std::forward<Sink>(sink)(ptr);
        }

        ProviderResult applyInventory(const ReliablePlayerInventoryBaseline& player,
            std::span<const ReliableContainerInventoryBaseline> containers,
            const ReliableGroundItemBaseline& groundItems, const LatestWinsEquipmentSnapshot& equipment)
        {
            if (!mapping)
                return ProviderResult::ContentMappingFailed;
            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return ProviderResult::PresentationFailed;

            if (!observedPlayerInventoryRevision || *observedPlayerInventoryRevision != player.revision)
            {
                auto playerPtr = world->getPlayerPtr();
                auto& inventory = playerPtr.getClass().getInventoryStore(playerPtr);
                inventory.unequipAll();
                inventory.clear();
                std::erase_if(observedInventoryStacks,
                    [](const auto& value) { return !value.second.container && !value.second.ground; });
                for (const auto& stack : player.stacks)
                {
                    auto local = materializeItem(stack, [&](const MWWorld::Ptr& ptr) {
                        return *inventory.add(ptr, static_cast<int>(stack.count), false);
                    });
                    if (!local)
                        return ProviderResult::ContentMappingFailed;
                    const auto [stored, inserted] = observedInventoryStacks.emplace(stack.stackId,
                        ObservedInventoryStack{ stack, *local, std::nullopt, false, std::nullopt, std::nullopt });
                    if (!inserted || std::ranges::any_of(observedInventoryStacks, [&](const auto& value) {
                            return value.first != stored->first && value.second.ptr == stored->second.ptr;
                        }))
                        return ProviderResult::ContentMappingFailed;
                }
                for (const auto& binding : player.equipment)
                {
                    const auto stored = observedInventoryStacks.find(binding.stackId);
                    if (stored == observedInventoryStacks.end())
                        return ProviderResult::PresentationFailed;
                    auto iter = inventory.begin();
                    for (; iter != inventory.end() && *iter != stored->second.ptr; ++iter)
                    {
                    }
                    if (iter == inventory.end())
                        return ProviderResult::PresentationFailed;
                    inventory.equip(static_cast<int>(binding.slot), iter);
                }
                observedPlayerInventoryRevision = player.revision;
            }

            std::map<ContainerId, ContainerRevision> desiredContainers;
            for (const auto& baseline : containers)
            {
                desiredContainers.emplace(baseline.container, baseline.revision);
                const auto prior = observedContainerRevisions.find(baseline.container);
                if (prior != observedContainerRevisions.end() && prior->second == baseline.revision)
                    continue;
                const auto localMapping = std::ranges::lower_bound(
                    mapping->containers, baseline.container, {}, &DesktopContainerMapping::id);
                if (localMapping == mapping->containers.end() || localMapping->id != baseline.container)
                    return ProviderResult::ContentMappingFailed;
                auto ptr = findActiveContainer(localMapping->refNumIndex, localMapping->refNumContentFile);
                if (ptr.isEmpty())
                    continue;
                auto& store = ptr.getClass().getContainerStore(ptr);
                store.clear();
                std::erase_if(observedInventoryStacks,
                    [&](const auto& value) { return value.second.container == baseline.container; });
                for (const auto& stack : baseline.stacks)
                {
                    auto local = materializeItem(stack,
                        [&](const MWWorld::Ptr& ptr) { return *store.add(ptr, static_cast<int>(stack.count), false); });
                    if (!local)
                        return ProviderResult::ContentMappingFailed;
                    const auto [stored, inserted] = observedInventoryStacks.emplace(stack.stackId,
                        ObservedInventoryStack{
                            stack, *local, baseline.container, false, baseline.revision, std::nullopt });
                    if (!inserted || std::ranges::any_of(observedInventoryStacks, [&](const auto& value) {
                            return value.first != stored->first && value.second.ptr == stored->second.ptr;
                        }))
                        return ProviderResult::ContentMappingFailed;
                }
                observedContainerRevisions.insert_or_assign(baseline.container, baseline.revision);
            }
            std::erase_if(observedContainerRevisions,
                [&](const auto& value) { return !desiredContainers.contains(value.first); });

            if (!observedInventoryCanonicalRevision
                || *observedInventoryCanonicalRevision != groundItems.header.canonicalRevision)
            {
                for (const auto& [stack, ptr] : presentedGroundItems)
                {
                    (void)stack;
                    if (!ptr.isEmpty())
                        world->deleteObject(ptr);
                }
                presentedGroundItems.clear();
                std::erase_if(observedInventoryStacks, [](const auto& value) { return value.second.ground; });
                auto* cell = resolveCell(groundItems.cell, *mapping);
                if (!cell)
                    return ProviderResult::ContentMappingFailed;
                for (const auto& member : groundItems.items)
                {
                    ESM::Position position{};
                    position.pos[0] = static_cast<float>(member.position.x()) / static_cast<float>(PositionScale);
                    position.pos[1] = static_cast<float>(member.position.y()) / static_cast<float>(PositionScale);
                    position.pos[2] = static_cast<float>(member.position.z()) / static_cast<float>(PositionScale);
                    auto local = materializeItem(
                        member.stack, [&](const MWWorld::Ptr& ptr) { return world->placeObject(ptr, cell, position); });
                    if (!local)
                        return ProviderResult::ContentMappingFailed;
                    auto ptr = *local;
                    presentedGroundItems.emplace(member.stack.stackId, ptr);
                    if (!observedInventoryStacks
                            .emplace(member.stack.stackId,
                                ObservedInventoryStack{
                                    member.stack, ptr, std::nullopt, true, std::nullopt, member.revision })
                            .second)
                        return ProviderResult::PresentationFailed;
                }
                observedInventoryCanonicalRevision = groundItems.header.canonicalRevision;
            }
            return applyPublicEquipment(equipment);
        }

        std::optional<InventoryTransactionCapture> inventoryTransfer(
            MWGui::ItemModel& source, const MWWorld::Ptr& item, std::size_t count, MWGui::ItemModel& target) const
        {
            if (!observedPlayerInventoryRevision || count == 0 || count > MaximumTransferCount)
                return std::nullopt;
            const auto* stack = observedStack(item);
            if (!stack || count > stack->stack.count)
                return std::nullopt;
            InventoryTransactionCapture result{ .prototypeId = stack->stack.prototypeId,
                .stackId = stack->stack.stackId,
                .count = static_cast<std::uint32_t>(count),
                .expectedInventoryRevision = *observedPlayerInventoryRevision,
                .interactionOrigin = playerOrigin() };
            const auto player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            const auto sourceInventory = dynamic_cast<MWGui::InventoryItemModel*>(&source);
            const auto targetInventory = dynamic_cast<MWGui::InventoryItemModel*>(&target);
            const bool sourcePlayer
                = sourceInventory && sourceInventory->actor() == player && !stack->container && !stack->ground;
            const bool targetPlayer = targetInventory && targetInventory->actor() == player;
            if (stack->container && targetPlayer)
            {
                result.kind = InventoryTransactionKind::TakeFromContainer;
                result.containerId = stack->container;
                result.expectedContainerRevision = stack->containerRevision;
                return result;
            }
            if (sourcePlayer)
            {
                if (auto* containerModel = dynamic_cast<MWGui::ContainerItemModel*>(&target))
                {
                    const auto id = containerId(containerModel->primarySource());
                    const auto revision = id ? observedContainerRevisions.find(*id) : observedContainerRevisions.end();
                    if (!id || revision == observedContainerRevisions.end())
                        return std::nullopt;
                    result.kind = InventoryTransactionKind::PutIntoContainer;
                    result.containerId = *id;
                    result.expectedContainerRevision = revision->second;
                    return result;
                }
                if (dynamic_cast<MWGui::WorldItemModel*>(&target))
                {
                    result.kind = InventoryTransactionKind::DropItem;
                    return result;
                }
            }
            return std::nullopt;
        }

        std::optional<InventoryTransactionCapture> inventoryUse(const MWWorld::Ptr& item) const
        {
            if (!observedPlayerInventoryRevision)
                return std::nullopt;
            const auto* stack = observedStack(item);
            if (!stack || stack->container || stack->ground)
                return std::nullopt;
            InventoryTransactionCapture result{ .prototypeId = stack->stack.prototypeId,
                .stackId = stack->stack.stackId,
                .count = 1,
                .expectedInventoryRevision = *observedPlayerInventoryRevision,
                .interactionOrigin = playerOrigin() };
            const auto playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();
            auto& inventory = playerPtr.getClass().getInventoryStore(playerPtr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                const auto equipped = inventory.getSlot(slot);
                if (equipped != inventory.end() && *equipped == item)
                {
                    result.kind = InventoryTransactionKind::UnequipItem;
                    result.slot = static_cast<EquipmentSlot>(slot);
                    return result;
                }
            }
            const auto slots = item.getClass().getEquipmentSlots(item).first;
            if (slots.empty() || slots.front() < 0 || slots.front() >= MWWorld::InventoryStore::Slots)
                return std::nullopt;
            int selectedSlot = slots.front();
            for (const int slot : slots)
            {
                if (slot < 0 || slot >= MWWorld::InventoryStore::Slots)
                    return std::nullopt;
                if (inventory.getSlot(slot) == inventory.end())
                {
                    selectedSlot = slot;
                    break;
                }
            }
            result.kind = InventoryTransactionKind::EquipItem;
            result.slot = static_cast<EquipmentSlot>(selectedSlot);
            return result;
        }

        std::optional<InventoryTransactionCapture> inventoryPickup(const MWWorld::Ptr& item) const
        {
            if (!observedPlayerInventoryRevision)
                return std::nullopt;
            const auto* stack = observedStack(item);
            if (!stack || !stack->ground || !stack->worldItemRevision)
                return std::nullopt;
            return InventoryTransactionCapture{ .kind = InventoryTransactionKind::PickupItem,
                .prototypeId = stack->stack.prototypeId,
                .stackId = stack->stack.stackId,
                .count = stack->stack.count,
                .expectedInventoryRevision = *observedPlayerInventoryRevision,
                .interactionOrigin = playerOrigin(),
                .expectedWorldItemRevision = stack->worldItemRevision };
        }

        ProviderResult applyInteractiveObjects(
            const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt)
        {
            (void)receivedAt;
            if (!mapping)
                return ProviderResult::ContentMappingFailed;
            const auto& content = *mapping;
            auto world = MWBase::Environment::get().getWorld();
            auto soundManager = MWBase::Environment::get().getSoundManager();

            for (const auto& member : baseline.members())
            {
                std::uint32_t refNumIndex = 0;
                std::int32_t refNumContentFile = -1;
                const auto it = std::ranges::lower_bound(
                    content.interactiveObjects, member.objectId, {}, &DesktopInteractiveObjectMapping::id);
                if (it != content.interactiveObjects.end() && it->id == member.objectId)
                {
                    refNumIndex = it->refNumIndex;
                    refNumContentFile = it->refNumContentFile;
                }
                else
                {
                    if (member.objectId.value() > std::numeric_limits<std::uint32_t>::max())
                        return ProviderResult::ContentMappingFailed;
                    refNumIndex = static_cast<std::uint32_t>(member.objectId.value());
                }

                auto doorPtr = findActiveDoor(refNumIndex, refNumContentFile);
                if (doorPtr.isEmpty())
                    continue;
                const bool teleportDoor = doorPtr.getCellRef().getTeleport();
                if (teleportDoor && member.doorState != TES3MP::DoorState::Closed)
                    return ProviderResult::ContentMappingFailed;

                auto found = observedDoors.find(member.objectId);
                if (found != observedDoors.end())
                {
                    if (member.revision < found->second.lastRevision)
                        return ProviderResult::PresentationFailed;
                    if (member.revision == found->second.lastRevision
                        && (member.doorState != found->second.lastDoorState
                            || member.lockState != found->second.lastLockState
                            || member.trapState != found->second.lastTrapState))
                        return ProviderResult::PresentationFailed;
                }

                if (member.lockState == TES3MP::LockState::Locked)
                    doorPtr.getCellRef().lock(100);
                else
                    doorPtr.getCellRef().unlock();

                if (member.trapState == TES3MP::TrapState::Disarmed)
                    doorPtr.getCellRef().setTrap(ESM::RefId());

                const float minRot = doorPtr.getCellRef().getPosition().rot[2];
                const float maxRot = minRot + static_cast<float>(std::numbers::pi / 2.0);

                if (found == observedDoors.end())
                {
                    if (!teleportDoor)
                    {
                        auto newRot = doorPtr.getRefData().getPosition().asRotationVec3();
                        newRot.z() = (member.doorState == TES3MP::DoorState::Open) ? maxRot : minRot;
                        world->activateDoor(doorPtr, MWWorld::DoorState::Idle);
                        world->rotateObject(doorPtr, newRot, MWBase::RotationFlag_none);
                        doorPtr.getClass().setDoorState(doorPtr, MWWorld::DoorState::Idle);
                    }

                    observedDoors.emplace(member.objectId,
                        ObservedDoorPresentation{
                            member.doorState, member.lockState, member.trapState, member.revision });
                }
                else
                {
                    if (!teleportDoor && member.doorState != found->second.lastDoorState)
                    {
                        const auto* ref = doorPtr.get<ESM::Door>()->mBase;
                        if (member.doorState == TES3MP::DoorState::Open)
                        {
                            world->activateDoor(doorPtr, MWWorld::DoorState::Opening);
                            if (ref && !ref->mOpenSound.empty() && soundManager)
                                soundManager->playSound3D(doorPtr, ref->mOpenSound, 1.0f, 1.0f);
                        }
                        else
                        {
                            world->activateDoor(doorPtr, MWWorld::DoorState::Closing);
                            if (ref && !ref->mCloseSound.empty() && soundManager)
                                soundManager->playSound3D(doorPtr, ref->mCloseSound, 1.0f, 1.0f);
                        }
                    }
                    found->second.lastDoorState = member.doorState;
                    found->second.lastLockState = member.lockState;
                    found->second.lastTrapState = member.trapState;
                    found->second.lastRevision = member.revision;
                }
            }
            return ProviderResult::Accepted;
        }

        std::optional<ObjectRevision> observedObjectRevision(InteractiveObjectId id) const noexcept
        {
            const auto found = observedDoors.find(id);
            return found != observedDoors.end() ? std::optional(found->second.lastRevision) : std::nullopt;
        }
    };

    DesktopPresentation::DesktopPresentation(RemoteMotionMetricSink& metrics)
        : mImpl(std::make_unique<Impl>(metrics))
    {
    }

    DesktopPresentation::~DesktopPresentation() = default;

    void DesktopPresentation::configure(DesktopContentMapping mapping)
    {
        mImpl->mapping = std::move(mapping);
    }

    ProviderResult DesktopPresentation::applyAuthoritative(const LatestWinsSnapshot& snapshot,
        std::span<const ObservedPlayer> observedPlayers, bool allowLocalCellCorrection, MonotonicInstant receivedAt,
        const std::optional<LocalLocomotionReconciliation>& localReconciliation) noexcept
    {
        try
        {
            const auto result
                = mImpl->apply(snapshot, observedPlayers, allowLocalCellCorrection, receivedAt, localReconciliation);
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

    ProviderResult DesktopPresentation::applyActors(const LatestWinsActorSnapshot& snapshot,
        std::span<const ActorInterestMember> observedActors, MonotonicInstant receivedAt) noexcept
    {
        try
        {
            const auto result = mImpl->applyActors(snapshot, observedActors, receivedAt);
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

    ProviderResult DesktopPresentation::applyInteractiveObjects(
        const ReliableInteractiveObjectInterestBaseline& baseline, MonotonicInstant receivedAt) noexcept
    {
        try
        {
            const auto result = mImpl->applyInteractiveObjects(baseline, receivedAt);
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

    std::optional<ObjectRevision> DesktopPresentation::observedObjectRevision(InteractiveObjectId id) const noexcept
    {
        return mImpl->observedObjectRevision(id);
    }

    ProviderResult DesktopPresentation::applyInventory(const ReliablePlayerInventoryBaseline& player,
        std::span<const ReliableContainerInventoryBaseline> containers, const ReliableGroundItemBaseline& groundItems,
        const LatestWinsEquipmentSnapshot& equipment, MonotonicInstant receivedAt) noexcept
    {
        (void)receivedAt;
        try
        {
            const auto result = mImpl->applyInventory(player, containers, groundItems, equipment);
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

    ProviderResult DesktopPresentation::applyCombat(const LatestWinsCombatSnapshot& snapshot,
        std::span<const ReliableCombatEventBatch> events, MonotonicInstant receivedAt) noexcept
    {
        (void)events;
        (void)receivedAt;
        try
        {
            const auto result = mImpl->applyCombat(snapshot);
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

    void DesktopPresentation::appendMeleeTargets(std::vector<MWWorld::Ptr>& targets) const
    {
        mImpl->appendMeleeTargets(targets);
    }

    std::optional<MeleeAttackCapture> DesktopPresentation::captureMeleeAttack(
        const MWWorld::Ptr& victim, float attackStrength, int attackType) const noexcept
    {
        try
        {
            return mImpl->captureMeleeAttack(victim, attackStrength, attackType);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<InventoryTransactionCapture> DesktopPresentation::inventoryTransfer(
        MWGui::ItemModel& source, const MWWorld::Ptr& item, std::size_t count, MWGui::ItemModel& target) const noexcept
    {
        try
        {
            return mImpl->inventoryTransfer(source, item, count, target);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<InventoryTransactionCapture> DesktopPresentation::inventoryUse(
        const MWWorld::Ptr& item) const noexcept
    {
        try
        {
            return mImpl->inventoryUse(item);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<InventoryTransactionCapture> DesktopPresentation::inventoryPickup(
        const MWWorld::Ptr& item) const noexcept
    {
        try
        {
            return mImpl->inventoryPickup(item);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool DesktopPresentation::observesInventoryItem(const MWWorld::Ptr& item) const noexcept
    {
        try
        {
            return mImpl->observedStack(item) != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    void DesktopPresentation::clear() noexcept
    {
        mImpl->clear();
    }
}
