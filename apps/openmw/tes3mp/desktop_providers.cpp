#include "desktop_providers.hpp"
#include "movement_mapping.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/journal.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwgui/containeritemmodel.hpp"
#include "../mwgui/inventoryitemmodel.hpp"
#include "../mwgui/inventorywindow.hpp"
#include "../mwgui/itemmodel.hpp"
#include "../mwgui/worlditemmodel.hpp"
#include "../mwinput/actions.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/security.hpp"
#include "../mwrender/replicatedactor.hpp"
#include "../mwworld/cell.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/placedrefid.hpp"
#include "../mwworld/inventoryrecordid.hpp"
#include "../mwworld/esmstore.hpp"
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
#include <components/esm3/loadregn.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadweap.hpp>
#include <tes3mp/fixed_tick_scheduler.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
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

        std::int64_t roundTiesToEven(double value) noexcept
        {
            const double lower = std::floor(value);
            const double fraction = value - lower;
            if (fraction < 0.5)
                return static_cast<std::int64_t>(lower);
            if (fraction > 0.5)
                return static_cast<std::int64_t>(lower + 1.0);
            const auto lowerInteger = static_cast<std::int64_t>(lower);
            return lowerInteger % 2 == 0 ? lowerInteger : lowerInteger + 1;
        }

        Turn32 turnFromOpenMW(float rad) noexcept
        {
            constexpr double Tau = 2.0 * std::numbers::pi;
            double turns = std::fmod(-static_cast<double>(rad) / Tau, 1.0);
            if (turns < 0.0)
                turns += 1.0;
            return Turn32::fromUnnormalized(static_cast<std::uint64_t>(std::floor(turns * TurnScale + 0.5)));
        }

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
            // Local input may briefly enter a known cell space outside the server's exact-cell catalog while
            // OpenMW finishes new-game startup. Preserve that request for authoritative server rejection and
            // correction; only genuinely unmapped local records are content-mapping failures.
            if (!cell.isExterior())
            {
                const auto found = std::ranges::find_if(mapping.cellSpaces, [&](const auto& value) {
                    return value.kind == CellSpaceKind::Interior && cell.getId() == refId(value.record);
                });
                if (found == mapping.cellSpaces.end())
                    return std::nullopt;
                return CellId::interior(found->id);
            }
            const auto found = std::ranges::find_if(mapping.cellSpaces, [&](const auto& value) {
                return value.kind == CellSpaceKind::Exterior && cell.getWorldSpace() == refId(value.record);
            });
            if (found == mapping.cellSpaces.end())
                return std::nullopt;
            return CellId::exterior(found->id, cell.getGridX(), cell.getGridY());
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
        std::span<const DesktopContainerMapping> containers, std::span<const DesktopQuestMapping> quests,
        std::span<const DesktopDialogueChoiceMapping> dialogueChoices,
        std::span<const DesktopWeatherRegionMapping> weatherRegions, std::span<const DesktopWeatherMapping> weather,
        std::span<const DesktopSpellMapping> spells)
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
        std::vector<DesktopQuestMapping> questMappings(quests.begin(), quests.end());
        std::ranges::sort(questMappings, {}, &DesktopQuestMapping::id);
        for (std::size_t index = 0; index < questMappings.size(); ++index)
        {
            if (questMappings[index].record.empty()
                || (index != 0 && questMappings[index - 1].id == questMappings[index].id))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (refId(questMappings[prior].record) == refId(questMappings[index].record))
                    return std::nullopt;
        }
        std::vector<DesktopDialogueChoiceMapping> choiceMappings(dialogueChoices.begin(), dialogueChoices.end());
        std::ranges::sort(choiceMappings, {}, &DesktopDialogueChoiceMapping::id);
        for (std::size_t index = 0; index < choiceMappings.size(); ++index)
        {
            if (index != 0 && choiceMappings[index - 1].id == choiceMappings[index].id)
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (choiceMappings[prior].localChoice == choiceMappings[index].localChoice)
                    return std::nullopt;
        }
        std::vector<DesktopWeatherRegionMapping> regionMappings(weatherRegions.begin(), weatherRegions.end());
        std::ranges::sort(regionMappings, {}, &DesktopWeatherRegionMapping::id);
        for (std::size_t index = 0; index < regionMappings.size(); ++index)
        {
            if (regionMappings[index].record.empty()
                || (index != 0 && regionMappings[index - 1].id == regionMappings[index].id))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (refId(regionMappings[prior].record) == refId(regionMappings[index].record))
                    return std::nullopt;
        }
        std::vector<DesktopWeatherMapping> weatherMappings(weather.begin(), weather.end());
        std::ranges::sort(weatherMappings, {}, &DesktopWeatherMapping::id);
        for (std::size_t index = 0; index < weatherMappings.size(); ++index)
        {
            if (weatherMappings[index].record.empty()
                || (index != 0 && weatherMappings[index - 1].id == weatherMappings[index].id))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (refId(weatherMappings[prior].record) == refId(weatherMappings[index].record))
                    return std::nullopt;
        }
        std::vector<DesktopSpellMapping> spellMappings(spells.begin(), spells.end());
        std::ranges::sort(spellMappings, {}, &DesktopSpellMapping::id);
        for (std::size_t index = 0; index < spellMappings.size(); ++index)
        {
            if (spellMappings[index].record.empty()
                || (index != 0 && spellMappings[index - 1].id == spellMappings[index].id))
                return std::nullopt;
            for (std::size_t prior = 0; prior < index; ++prior)
                if (refId(spellMappings[prior].record) == refId(spellMappings[index].record))
                    return std::nullopt;
        }
        return DesktopContentMapping{ std::move(manifest), std::move(mappings), appearanceId, std::move(avatarNpc),
            std::move(prototypes), std::move(objects), std::move(items), std::move(containerMappings),
            std::move(questMappings), std::move(choiceMappings), std::move(regionMappings), std::move(weatherMappings),
            std::move(spellMappings) };
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
        std::optional<MagicUseCapture> pendingMagicUse;
        bool interceptorInstalled = false;
        std::optional<osg::Vec3f> lastPosition;
        std::optional<std::chrono::steady_clock::time_point> lastSampleTime;
        LinearVelocity3 lastVelocity{ 0, 0, 0 };

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
                player.setMagicCastInterceptor([self](bool release, const ESM::RefId& spell, const MWWorld::Ptr& item,
                                                   const MWWorld::Ptr& target) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (!presentation)
                        return false;
                    const bool supported = presentation->captureMagicUse(spell, item, {}).has_value();
                    if (!supported || !release)
                        return supported;
                    auto capture = presentation->captureMagicUse(spell, item, target);
                    if (capture && !self->mImpl->pendingMagicUse)
                        self->mImpl->pendingMagicUse = std::move(*capture);
                    return true;
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
                MWGui::ItemModel::setTakeAllInterceptor([self](MWGui::ItemModel& source, MWGui::ItemModel& target) {
                    auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                    if (!presentation || self->mImpl->pendingInventoryTransaction || source.getItemCount() == 0)
                        return;
                    // One observed source stack witnesses the owner. The server
                    // enumerates the entire inventory at this revision.
                    auto capture = presentation->inventoryTransfer(source, source.getItem(0).mBase, 1, target);
                    if (capture && capture->kind == InventoryTransactionKind::TakeFromContainer)
                    {
                        capture->kind = InventoryTransactionKind::TakeAllFromContainer;
                        self->mImpl->pendingInventoryTransaction = std::move(*capture);
                    }
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
                MWMechanics::Security::setAttemptInterceptor(
                    [self](const MWWorld::Ptr& target, const MWWorld::Ptr& tool, bool disarm) {
                        auto* presentation = dynamic_cast<const DesktopPresentation*>(self->mImpl->presentation);
                        if (!presentation)
                            return false;
                        auto capture = presentation->captureSecurityAttempt(target, tool, disarm);
                        if (capture && !self->mImpl->pendingInteraction)
                            self->mImpl->pendingInteraction = std::move(*capture);
                        return capture.has_value();
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
                    world->getPlayer().clearMagicCastInterceptor();
                }
                MWGui::ItemModel::clearTransferInterceptor();
                MWGui::InventoryWindow::clearUseItemInterceptor();
                MWMechanics::Security::clearAttemptInterceptor();
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
        mImpl->pendingMagicUse.reset();
        mImpl->lastPosition.reset();
        mImpl->lastSampleTime.reset();
        mImpl->lastVelocity = LinearVelocity3(0, 0, 0);
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

    std::optional<MagicUseCapture> DesktopSemanticInput::captureMagicUse() noexcept
    {
        mImpl->ensureInterceptor(this);
        if (!mImpl->pendingMagicUse)
            return std::nullopt;
        auto captured = std::move(mImpl->pendingMagicUse);
        mImpl->pendingMagicUse.reset();
        return captured;
    }

    std::optional<DialogueChoiceId> DesktopSemanticInput::mapDialogueChoice(int localChoice) const noexcept
    {
        if (!mImpl->mapping)
            return std::nullopt;
        const auto found = std::ranges::find(
            mImpl->mapping->dialogueChoices, localChoice, &DesktopDialogueChoiceMapping::localChoice);
        return found == mImpl->mapping->dialogueChoices.end() ? std::nullopt : std::optional(found->id);
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
            mImpl->pendingMagicUse.reset();
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
            auto world = MWBase::Environment::get().getWorld();
            const auto player = world->getPlayerPtr();
            const auto& pos = player.getRefData().getPosition();
            if (!std::isfinite(pos.pos[0]) || !std::isfinite(pos.pos[1]) || !std::isfinite(pos.pos[2])
                || !std::isfinite(pos.rot[0]) || !std::isfinite(pos.rot[1]) || !std::isfinite(pos.rot[2]))
                return LocomotionIntent(LocomotionMode::Walk, Turn32::fromValue(0), LinearVelocity3(0, 0, 0));

            const auto position = Position3(static_cast<std::int64_t>(std::round(pos.pos[0] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[1] * PositionScale)),
                static_cast<std::int64_t>(std::round(pos.pos[2] * PositionScale)));

            const auto orientation
                = Orientation3(turnFromOpenMW(pos.rot[0]), turnFromOpenMW(pos.rot[1]), turnFromOpenMW(pos.rot[2]));
            const auto rootFacing = orientation.z();

            auto mechanics = MWBase::Environment::get().getMechanicsManager();
            LocomotionMode mode = LocomotionMode::Walk;
            if (!world->isOnGround(player))
                mode = LocomotionMode::Jump;
            else if (mechanics->isSneaking(player))
                mode = LocomotionMode::Sneak;
            else if (mechanics->isRunning(player))
                mode = LocomotionMode::Run;

            const auto now = std::chrono::steady_clock::now();
            if (mImpl->lastPosition && mImpl->lastSampleTime)
            {
                const double dt = std::chrono::duration<double>(now - *mImpl->lastSampleTime).count();
                if (dt > 0.5)
                {
                    mImpl->lastVelocity = LinearVelocity3(0, 0, 0);
                    mImpl->lastPosition = osg::Vec3f(pos.pos[0], pos.pos[1], pos.pos[2]);
                    mImpl->lastSampleTime = now;
                }
                else if (dt >= 0.01)
                {
                    const double dx = static_cast<double>(pos.pos[0]) - mImpl->lastPosition->x();
                    const double dy = static_cast<double>(pos.pos[1]) - mImpl->lastPosition->y();
                    const double dz = static_cast<double>(pos.pos[2]) - mImpl->lastPosition->z();
                    constexpr double ServerTickRate = 30.0;
                    const double vx = (dx / dt) / ServerTickRate * PositionScale;
                    const double vy = (dy / dt) / ServerTickRate * PositionScale;
                    const double vz = (dz / dt) / ServerTickRate * PositionScale;
                    mImpl->lastVelocity
                        = LinearVelocity3(roundTiesToEven(vx), roundTiesToEven(vy), roundTiesToEven(vz));
                    mImpl->lastPosition = osg::Vec3f(pos.pos[0], pos.pos[1], pos.pos[2]);
                    mImpl->lastSampleTime = now;
                }
            }
            else
            {
                mImpl->lastPosition = osg::Vec3f(pos.pos[0], pos.pos[1], pos.pos[2]);
                mImpl->lastSampleTime = now;
                mImpl->lastVelocity = LinearVelocity3(0, 0, 0);
            }

            return LocomotionIntent(mode, rootFacing, mImpl->lastVelocity, position, orientation);
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
        MWWorld::InventoryRecordMap nativeItemRecords, nativeSoulRecords;
        std::map<ItemStackId, MWWorld::Ptr> presentedGroundItems;
        std::optional<InventoryRevision> observedPlayerInventoryRevision;
        std::optional<CanonicalRevision> observedInventoryCanonicalRevision;
        std::optional<LatestWinsCombatSnapshot> combatSnapshot;
        std::map<ActiveMagicEffectId, ActiveMagicEffectSnapshot> activeMagicEffects;
        bool sessionBootstrapPending = true;

        void clear() noexcept
        {
            sessionBootstrapPending = true;
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
            activeMagicEffects.clear();
            try
            {
                auto world = MWBase::Environment::get().getWorld();
                if (world)
                {
                    world->setWeatherAuthority(false);
                    world->setWorldTimeAuthority(false);
                }
            }
            catch (...)
            {
            }
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
                const auto native = nativeItemRecords.find(prototype->value());
                if (!local && native == nativeItemRecords.end())
                    return false;
                records.push_back(native != nativeItemRecords.end() ? native->second : refId(local->record));
            }
            return true;
        }

        ProviderResult applyPublicEquipment(const LatestWinsEquipmentSnapshot& snapshot)
        {
            auto world = MWBase::Environment::get().getWorld();
            for (const auto& member : snapshot.actors)
            {
                const auto ref = MWWorld::localPlacedRef(member.actor.value(), world->getContentFiles());
                if (!ref) return ProviderResult::ContentMappingFailed;
                auto ptr = findActiveContainer(ref->mIndex, ref->mContentFile);
                // A not-yet-active reference is retried on subsequent snapshots.
                if (ptr.isEmpty()) continue;
                if (!ptr.getClass().isActor() || ptr.getCell() != world->getPlayerPtr().getCell()
                    || ptr.getClass().getCreatureStats(ptr).isDead())
                    return ProviderResult::ContentMappingFailed;
                std::vector<std::pair<int, ESM::RefId>> records;
                for (size_t slot = 0; slot < member.slots.size(); ++slot)
                    if (member.slots[slot])
                    {
                        const auto record = nativeItemRecords.find(member.slots[slot]->value());
                        if (record == nativeItemRecords.end()) return ProviderResult::ContentMappingFailed;
                        records.emplace_back(static_cast<int>(slot), record->second);
                    }
                if (ptr.getClass().hasInventoryStore(ptr))
                    ptr.getClass().getInventoryStore(ptr).applyAuthoritativeAppearance(records,
                        *MWBase::Environment::get().getESMStore(), world->getLocalScripts(),
                        *MWBase::Environment::get().getWorldModel());
                else
                {
                    if (!records.empty()) return ProviderResult::ContentMappingFailed;
                    ptr.getClass().getContainerStore(ptr).clearAuthoritative(world->getLocalScripts());
                }
                // Never register appearance items as observed inventory stacks
                // or grant the living owner a container transfer revision.
            }
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
                // With adjustPlayerPos disabled, changing cells retains the old
                // position. Install the confirmed pose before creating remotes;
                // otherwise exterior streaming unloads their new cell again.
                world->moveObject(player, selfPosition.asVec3());
                world->rotateObject(player, selfPosition.asRotationVec3());
                targetCell = player.getCell();
                sessionBootstrapPending = false;
            }
            else if (player.getCell() == targetCell)
            {
                if (sessionBootstrapPending)
                {
                    world->moveObject(player, selfPosition.asVec3());
                    world->rotateObject(player, selfPosition.asRotationVec3());
                    sessionBootstrapPending = false;
                }
                const auto& local = player.getRefData().getPosition();
                (void)metrics.tryRecord({ MovementMetricKey::LocalCorrectionDistanceQuanta,
                    movementCorrectionDistanceQuanta(localRoot.position(),
                        static_cast<double>(local.pos[0]) * PositionScale,
                        static_cast<double>(local.pos[1]) * PositionScale,
                        static_cast<double>(local.pos[2]) * PositionScale) });
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
                const auto combat
                    = std::ranges::lower_bound(combatSnapshot->actors(), *target, {}, &ActorCombatSnapshot::actorId);
                if (combat == combatSnapshot->actors().end() || combat->actorId != *target || combat->dead)
                    return std::nullopt;
                targetRevision = combat->combatRevision;
            }
            return MeleeAttackCapture{ target, combatSnapshot->serverTick(), combatSnapshot->selfCombatRevision(),
                targetRevision, type, attackStrength };
        }

        std::optional<MagicUseCapture> captureMagicUse(
            const ESM::RefId& spell, const MWWorld::Ptr& item, const MWWorld::Ptr& target) const
        {
            if (!mapping || !combatSnapshot)
                return std::nullopt;
            MagicUseCapture result;
            if (!spell.empty())
            {
                const auto source = std::ranges::find_if(
                    mapping->spells, [&](const auto& value) { return refId(value.record) == spell; });
                if (source == mapping->spells.end())
                    return std::nullopt;
                result.sourceKind = MagicUseSourceKind::Spell;
                result.sourceId = source->id.value();
            }
            else
            {
                const auto* source = item.isEmpty() ? nullptr : observedStack(item);
                if (!source || source->ground || source->container || !observedPlayerInventoryRevision)
                    return std::nullopt;
                result.sourceKind = MagicUseSourceKind::EnchantedItem;
                result.sourceId = source->stack.stackId.value();
            }
            result.sourceTick = combatSnapshot->serverTick();
            result.expectedCasterRevision = combatSnapshot->selfCombatRevision();
            result.expectedTargetRevision = combatSnapshot->selfCombatRevision();
            result.expectedInventoryRevision = observedPlayerInventoryRevision.value_or(InventoryRevision::initial());
            if (target.isEmpty() || target == MWBase::Environment::get().getWorld()->getPlayerPtr())
                return result;

            const auto actor = std::ranges::find_if(actorRemotes,
                [&](const auto& entry) { return entry.second.actor && entry.second.actor->ptr() == target; });
            if (actor != actorRemotes.end() && actor->second.lastObserved)
            {
                const auto id = actor->second.lastObserved->actorId();
                const auto state
                    = std::ranges::lower_bound(combatSnapshot->actors(), id, {}, &ActorCombatSnapshot::actorId);
                if (state == combatSnapshot->actors().end() || state->actorId != id || state->dead)
                    return std::nullopt;
                result.targetKind = MagicUseTargetKind::Actor;
                result.targetId = id.value();
                result.expectedTargetRevision = state->combatRevision;
                return result;
            }
            const auto player = std::ranges::find_if(
                remotes, [&](const auto& entry) { return entry.second.actor && entry.second.actor->ptr() == target; });
            if (player != remotes.end() && player->second.lastObserved)
            {
                const auto id = player->second.lastObserved->playerId();
                const auto state
                    = std::ranges::lower_bound(combatSnapshot->players(), id, {}, &PlayerCombatSnapshot::playerId);
                if (state == combatSnapshot->players().end() || state->playerId != id || state->dead)
                    return std::nullopt;
                result.targetKind = MagicUseTargetKind::Player;
                result.targetId = id.value();
                result.expectedTargetRevision = state->combatRevision;
                return result;
            }
            return std::nullopt;
        }

        std::optional<ObjectInteractionCapture> captureSecurityAttempt(
            const MWWorld::Ptr& target, const MWWorld::Ptr& tool, bool disarm) const
        {
            if (!mapping || !combatSnapshot || !observedPlayerInventoryRevision || target.isEmpty() || tool.isEmpty())
                return std::nullopt;
            const auto refNum = target.getCellRef().getRefNum();
            std::optional<InteractiveObjectId> objectId;
            for (const auto& candidate : mapping->interactiveObjects)
                if (candidate.refNumIndex == refNum.mIndex
                    && (candidate.refNumContentFile == -1 || candidate.refNumContentFile == refNum.mContentFile))
                {
                    objectId = candidate.id;
                    break;
                }
            const auto observedObject = objectId ? observedDoors.find(*objectId) : observedDoors.end();
            const auto* observedTool = observedStack(tool);
            if (!objectId || observedObject == observedDoors.end() || !observedTool || observedTool->ground
                || observedTool->container)
                return std::nullopt;
            auto* cell = target.getCell();
            if (!cell)
                return std::nullopt;
            const auto canonicalCell = toCanonical(*cell->getCell(), *mapping);
            if (!canonicalCell)
                return std::nullopt;
            return ObjectInteractionCapture{ .objectId = *objectId,
                .targetCell = *canonicalCell,
                .interactionOrigin = playerOrigin(),
                .expectedRevision = observedObject->second.lastRevision,
                .kind = disarm ? ObjectInteractionKind::DisarmTrap : ObjectInteractionKind::PickLock,
                .requestedTool = observedTool->stack.stackId,
                .expectedInventoryRevision = *observedPlayerInventoryRevision,
                .expectedCombatRevision = combatSnapshot->selfCombatRevision() };
        }

        ProviderResult applyQuestJournal(
            const QuestJournalCatalog& catalog, const CanonicalPlayerQuestJournalState& state)
        {
            if (!mapping || mapping->manifest.id() != catalog.manifest()
                || mapping->quests.size() != catalog.quests().size())
                return ProviderResult::ContentMappingFailed;
            const auto questMapping = [&](QuestId id) -> const DesktopQuestMapping* {
                const auto found = std::ranges::lower_bound(mapping->quests, id, {}, &DesktopQuestMapping::id);
                return found == mapping->quests.end() || found->id != id ? nullptr : &*found;
            };
            const auto journalDeclaration = [&](JournalEntryId id) -> const JournalCatalogEntry* {
                const auto journal = catalog.journal();
                const auto found = std::ranges::find(journal, id, &JournalCatalogEntry::id);
                return found == journal.end() ? nullptr : &*found;
            };
            for (const auto& quest : catalog.quests())
                if (!questMapping(quest.id))
                    return ProviderResult::ContentMappingFailed;
            for (const auto& quest : state.quests)
                if (!questMapping(quest.id) || quest.stage.value() > std::numeric_limits<int>::max())
                    return ProviderResult::ContentMappingFailed;
            for (const auto& entry : state.journal)
            {
                const auto* declaration = journalDeclaration(entry.id);
                if (!declaration || !questMapping(declaration->quest)
                    || declaration->stage.value() > std::numeric_limits<int>::max())
                    return ProviderResult::ContentMappingFailed;
            }
            for (const auto& quest : catalog.quests())
                if (quest.initialStage.value() > std::numeric_limits<int>::max())
                    return ProviderResult::ContentMappingFailed;

            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return ProviderResult::PresentationFailed;
            auto journal = MWBase::Environment::get().getJournal();
            journal->clear();
            const auto player = world->getPlayerPtr();
            for (const auto& entry : state.journal)
            {
                const auto* declaration = journalDeclaration(entry.id);
                const auto* local = questMapping(declaration->quest);
                journal->addEntry(refId(local->record), static_cast<int>(declaration->stage.value()), player);
            }
            for (const auto& quest : catalog.quests())
            {
                const auto current = std::ranges::find(state.quests, quest.id, &CanonicalQuestState::id);
                const auto stage = current == state.quests.end() ? quest.initialStage : current->stage;
                journal->setJournalIndex(refId(questMapping(quest.id)->record), static_cast<int>(stage.value()));
            }
            return ProviderResult::Accepted;
        }

        ProviderResult applyWeather(std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick)
        {
            if (!mapping || mapping->weatherRegions.size() != regions.size())
                return ProviderResult::ContentMappingFailed;
            struct Mapped
            {
                ESM::RefId region;
                ESM::RefId current;
                ESM::RefId target;
                float transitionFactor;
                float transitionDelta;
            };
            std::vector<Mapped> mapped;
            mapped.reserve(regions.size());
            auto world = MWBase::Environment::get().getWorld();
            auto store = MWBase::Environment::get().getESMStore();
            if (!world)
                return ProviderResult::PresentationFailed;
            for (const auto& state : regions)
            {
                const auto region = std::ranges::lower_bound(
                    mapping->weatherRegions, state.region, {}, &DesktopWeatherRegionMapping::id);
                const auto current
                    = std::ranges::lower_bound(mapping->weather, state.currentWeather, {}, &DesktopWeatherMapping::id);
                const auto target
                    = std::ranges::lower_bound(mapping->weather, state.targetWeather, {}, &DesktopWeatherMapping::id);
                if (region == mapping->weatherRegions.end() || region->id != state.region
                    || current == mapping->weather.end() || current->id != state.currentWeather
                    || target == mapping->weather.end() || target->id != state.targetWeather)
                    return ProviderResult::ContentMappingFailed;
                const auto localRegion = refId(region->record);
                const auto localCurrent = refId(current->record);
                const auto localTarget = refId(target->record);
                if (!store->get<ESM::Region>().search(localRegion) || !world->getWeather(localCurrent)
                    || !world->getWeather(localTarget))
                    return ProviderResult::ContentMappingFailed;
                float factor = 1.f;
                float delta = 0.f;
                if (state.currentWeather != state.targetWeather && state.transitionEndTick > state.transitionStartTick
                    && serverTick < state.transitionEndTick)
                {
                    delta = static_cast<float>(ServerTicksPerSecond)
                        / static_cast<float>(state.transitionEndTick.value() - state.transitionStartTick.value());
                    if (serverTick <= state.transitionStartTick)
                        factor = 0.f;
                    else
                    {
                        const double elapsed
                            = static_cast<double>(serverTick.value() - state.transitionStartTick.value());
                        const double duration
                            = static_cast<double>(state.transitionEndTick.value() - state.transitionStartTick.value());
                        factor = static_cast<float>(elapsed / duration);
                    }
                }
                mapped.push_back({ localRegion, localCurrent, localTarget, factor, delta });
            }
            world->setWeatherAuthority(true);
            for (const auto& state : mapped)
                if (!world->applyAuthoritativeWeather(
                        state.region, state.current, state.target, state.transitionFactor, state.transitionDelta))
                    return ProviderResult::PresentationFailed;
            return ProviderResult::Accepted;
        }

        ProviderResult applyWorldTime(const ReliableWorldTimeState& state)
        {
            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return ProviderResult::PresentationFailed;
            world->setWorldTimeAuthority(true);
            const auto& time = state.time;
            return world->applyAuthoritativeWorldTime(
                       time.day, time.month, time.year, time.millisecondsSinceMidnight, time.timeScaleUnits)
                ? ProviderResult::Accepted
                : ProviderResult::PresentationFailed;
        }

        ProviderResult applyCombat(
            const LatestWinsCombatSnapshot& snapshot, std::span<const ReliableCombatEventBatch> events)
        {
            if (combatSnapshot && snapshot.serverTick() < combatSnapshot->serverTick())
                return ProviderResult::Accepted;
            if (combatSnapshot && snapshot.serverTick() == combatSnapshot->serverTick() && snapshot != *combatSnapshot)
            {
                Log(Debug::Error) << "TES3MP combat presentation: conflicting snapshot at the same tick";
                return ProviderResult::PresentationFailed;
            }
            auto world = MWBase::Environment::get().getWorld();
            if (!world)
                return ProviderResult::PresentationFailed;
            auto player = world->getPlayerPtr();
            auto& playerStats = player.getClass().getCreatureStats(player);
            auto sound = MWBase::Environment::get().getSoundManager();
            if (!snapshot.selfDead() && playerStats.isDead())
                MWBase::Environment::get().getMechanicsManager()->resurrect(player);
            auto playerHealth = playerStats.getHealth();
            playerHealth.setBase(snapshot.selfMaximumHealth());
            playerHealth.setCurrent(snapshot.selfHealth());
            playerStats.setHealth(playerHealth);
            auto fatigue = playerStats.getFatigue();
            fatigue.setBase(snapshot.selfMaximumFatigue());
            fatigue.setCurrent(snapshot.selfFatigue(), true, true);
            playerStats.setFatigue(fatigue);
            auto magicka = playerStats.getMagicka();
            magicka.setBase(snapshot.selfMaximumMagicka());
            magicka.setCurrent(snapshot.selfMagicka(), true, true);
            playerStats.setMagicka(magicka);
            const std::array skillIds{ ESM::Skill::Block, ESM::Skill::ShortBlade, ESM::Skill::LongBlade,
                ESM::Skill::BluntWeapon, ESM::Skill::Axe, ESM::Skill::Spear, ESM::Skill::HandToHand,
                ESM::Skill::LightArmor, ESM::Skill::MediumArmor, ESM::Skill::HeavyArmor, ESM::Skill::Unarmored,
                ESM::Skill::Security, ESM::Skill::Alteration, ESM::Skill::Conjuration, ESM::Skill::Destruction,
                ESM::Skill::Illusion, ESM::Skill::Mysticism, ESM::Skill::Restoration, ESM::Skill::Enchant };
            auto& npcStats = player.getClass().getNpcStats(player);
            for (const auto& confirmed : snapshot.selfSkills())
            {
                const auto index = static_cast<std::size_t>(confirmed.skill);
                if (index >= skillIds.size())
                    return ProviderResult::PresentationFailed;
                auto skill = npcStats.getSkill(skillIds[index]);
                skill.setBase(confirmed.value);
                skill.setProgress(confirmed.progress);
                npcStats.setSkill(skillIds[index], skill);
            }

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
                health.setBase(combat->maximumHealth);
                health.setCurrent(combat->health);
                stats.setHealth(health);
                auto actorFatigue = stats.getFatigue();
                actorFatigue.setBase(combat->maximumFatigue);
                actorFatigue.setCurrent(combat->fatigue, true, true);
                stats.setFatigue(actorFatigue);
                auto actorMagicka = stats.getMagicka();
                actorMagicka.setBase(combat->maximumMagicka);
                actorMagicka.setCurrent(combat->magicka, true, true);
                stats.setMagicka(actorMagicka);
                const auto deathResult = remote.actor->setDead(combat->dead);
                if (!replicatedActorResultAccepted(deathResult))
                {
                    Log(Debug::Error) << "TES3MP combat actor death presentation: "
                                      << replicatedActorResultName(deathResult);
                    return ProviderResult::PresentationFailed;
                }
            }
            for (auto& [entity, remote] : remotes)
            {
                (void)entity;
                if (!remote.actor || !remote.lastObserved)
                    continue;
                const auto combat = std::ranges::lower_bound(
                    snapshot.players(), remote.lastObserved->playerId(), {}, &PlayerCombatSnapshot::playerId);
                if (combat == snapshot.players().end() || combat->playerId != remote.lastObserved->playerId())
                    continue;
                auto ptr = remote.actor->ptr();
                auto& stats = ptr.getClass().getCreatureStats(ptr);
                if (!combat->dead && stats.isDead())
                    stats.resurrect();
                auto health = stats.getHealth();
                health.setBase(combat->maximumHealth);
                health.setCurrent(combat->health);
                stats.setHealth(health);
                auto remoteFatigue = stats.getFatigue();
                remoteFatigue.setBase(combat->maximumFatigue);
                remoteFatigue.setCurrent(combat->fatigue, true, true);
                stats.setFatigue(remoteFatigue);
                auto remoteMagicka = stats.getMagicka();
                remoteMagicka.setBase(combat->maximumMagicka);
                remoteMagicka.setCurrent(combat->magicka, true, true);
                stats.setMagicka(remoteMagicka);
                const auto deathResult = remote.actor->setDead(combat->dead);
                if (!replicatedActorResultAccepted(deathResult))
                {
                    Log(Debug::Error) << "TES3MP combat player death presentation: "
                                      << replicatedActorResultName(deathResult);
                    return ProviderResult::PresentationFailed;
                }
            }
            for (const auto& batch : events)
            {
                for (const auto& event : batch.events())
                {
                    if (!event.hit)
                        continue;
                    const auto remote = std::ranges::find_if(actorRemotes, [&](const auto& entry) {
                        return entry.second.actor && entry.second.lastObserved
                            && entry.second.lastObserved->actorId() == event.targetActorId;
                    });
                    if (remote != actorRemotes.end()
                        && !replicatedActorResultAccepted(
                            remote->second.actor->playAction(MWRender::ReplicatedActorAction::Hit)))
                        return ProviderResult::PresentationFailed;
                    if (remote != actorRemotes.end() && event.damage > 0.f)
                    {
                        const auto soundId = event.damagedStat == MeleeDamageStat::Fatigue
                            ? ESM::RefId::stringRefId("Hand To Hand Hit")
                            : ESM::RefId::stringRefId("Health Damage");
                        sound->playSound3D(remote->second.actor->ptr(), soundId, 1.f, 1.f);
                    }
                }
                for (const auto& event : batch.actorEvents())
                {
                    const auto remote = std::ranges::find_if(actorRemotes, [&](const auto& entry) {
                        return entry.second.actor && entry.second.lastObserved
                            && entry.second.lastObserved->actorId() == event.attackerActorId;
                    });
                    if (remote != actorRemotes.end()
                        && !replicatedActorResultAccepted(
                            remote->second.actor->playAction(MWRender::ReplicatedActorAction::Attack)))
                        return ProviderResult::PresentationFailed;
                    if (event.targetPlayerId == snapshot.selfPlayerId() && event.hit)
                    {
                        if (event.blocked)
                        {
                            playerStats.setBlock(true);
                            auto& inventory = player.getClass().getInventoryStore(player);
                            const auto shield = inventory.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
                            if (shield != inventory.end())
                            {
                                const auto skill = shield->getClass().getEquipmentSkill(*shield);
                                const auto soundId = skill == ESM::Skill::LightArmor
                                    ? ESM::RefId::stringRefId("Light Armor Hit")
                                    : skill == ESM::Skill::MediumArmor ? ESM::RefId::stringRefId("Medium Armor Hit")
                                                                       : ESM::RefId::stringRefId("Heavy Armor Hit");
                                sound->playSound3D(player, soundId, 1.f, 1.f);
                            }
                            else
                                sound->playSound3D(player, ESM::RefId::stringRefId("Light Armor Hit"), 1.f, 1.f);
                        }
                        else if (event.damage > 0.f)
                        {
                            playerStats.setHitRecovery(true);
                            const auto soundId = event.damagedStat == MeleeDamageStat::Fatigue
                                ? ESM::RefId::stringRefId("Hand To Hand Hit")
                                : ESM::RefId::stringRefId("Health Damage");
                            sound->playSound3D(player, soundId, 1.f, 1.f);
                            if (event.damagedStat == MeleeDamageStat::Health)
                                MWBase::Environment::get().getWindowManager()->activateHitOverlay();
                        }
                    }
                }
                for (const auto& event : batch.magicEvents())
                {
                    if (!event.castSucceeded)
                        continue;
                    if (event.targetKind == MagicUseTargetKind::Actor && event.targetHealthDelta < 0.f)
                    {
                        const auto target = ActorId::fromValue(event.targetId);
                        const auto remote = target ? std::ranges::find_if(actorRemotes,
                                                         [&](const auto& entry) {
                                                             return entry.second.actor && entry.second.lastObserved
                                                                 && entry.second.lastObserved->actorId() == *target;
                                                         })
                                                   : actorRemotes.end();
                        if (remote != actorRemotes.end()
                            && !replicatedActorResultAccepted(
                                remote->second.actor->playAction(MWRender::ReplicatedActorAction::Hit)))
                            return ProviderResult::PresentationFailed;
                    }
                    if (event.targetKind == MagicUseTargetKind::Player && event.targetHealthDelta < 0.f)
                    {
                        const auto target = PlayerId::fromValue(event.targetId);
                        if (target && *target == snapshot.selfPlayerId())
                        {
                            playerStats.setHitRecovery(true);
                            MWBase::Environment::get().getWindowManager()->activateHitOverlay();
                        }
                        const auto remote = target ? std::ranges::find_if(remotes,
                                                         [&](const auto& entry) {
                                                             return entry.second.actor && entry.second.lastObserved
                                                                 && entry.second.lastObserved->playerId() == *target;
                                                         })
                                                   : remotes.end();
                        if (remote != remotes.end()
                            && !replicatedActorResultAccepted(
                                remote->second.actor->playAction(MWRender::ReplicatedActorAction::Hit)))
                        return ProviderResult::PresentationFailed;
                    }
                }
                for (const auto& event : batch.magicEffectEvents())
                {
                    if (event.eventKind == MagicEffectCombatEventKind::Ended)
                        activeMagicEffects.erase(event.instanceId);
                    if (event.appliedDelta >= 0.f)
                        continue;
                    if (event.targetKind == MagicUseTargetKind::Actor)
                    {
                        const auto target = ActorId::fromValue(event.targetId);
                        const auto remote = target ? std::ranges::find_if(actorRemotes,
                                                         [&](const auto& entry) {
                                                             return entry.second.actor && entry.second.lastObserved
                                                                 && entry.second.lastObserved->actorId() == *target;
                                                         })
                                                   : actorRemotes.end();
                        if (remote != actorRemotes.end()
                            && !replicatedActorResultAccepted(
                                remote->second.actor->playAction(MWRender::ReplicatedActorAction::Hit)))
                            return ProviderResult::PresentationFailed;
                    }
                    else if (event.targetKind == MagicUseTargetKind::Player)
                    {
                        const auto target = PlayerId::fromValue(event.targetId);
                        if (target && *target == snapshot.selfPlayerId()
                            && event.effectKind <= DirectMagicEffectKind::DamageHealth)
                        {
                            playerStats.setHitRecovery(true);
                            MWBase::Environment::get().getWindowManager()->activateHitOverlay();
                        }
                        const auto remote = target ? std::ranges::find_if(remotes,
                                                         [&](const auto& entry) {
                                                             return entry.second.actor && entry.second.lastObserved
                                                                 && entry.second.lastObserved->playerId() == *target;
                                                         })
                                                   : remotes.end();
                        if (remote != remotes.end()
                            && !replicatedActorResultAccepted(
                                remote->second.actor->playAction(MWRender::ReplicatedActorAction::Hit)))
                            return ProviderResult::PresentationFailed;
                    }
                }
            }
            std::map<ActiveMagicEffectId, ActiveMagicEffectSnapshot> confirmedEffects;
            for (const auto& effect : snapshot.activeEffects())
                if (!confirmedEffects.emplace(effect.instanceId, effect).second)
                    return ProviderResult::PresentationFailed;
            activeMagicEffects = std::move(confirmedEffects);
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
            const auto visit = [&](const MWWorld::Ptr& ptr) {
                const auto refNum = ptr.getCellRef().getRefNum();
                if (refNum.mIndex == refNumIndex && (contentFile < 0 || refNum.mContentFile == contentFile))
                {
                    found = ptr;
                    return false;
                }
                return true;
            };
            cell.forEachType<ESM::Container>(visit);
            if (found.isEmpty()) cell.forEachType<ESM::NPC>(visit);
            if (found.isEmpty()) cell.forEachType<ESM::Creature>(visit);
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
            if (found != mapping->itemPrototypes.end()) return found->id;
            const auto id = MWWorld::inventoryRecordId(ptr.getCellRef().getRefId());
            return nativeItemRecords.contains(id) ? ItemPrototypeId::fromValue(id) : std::nullopt;
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
            const auto placed = MWWorld::placedRefId(refNum, MWBase::Environment::get().getWorld()->getContentFiles());
            if (placed)
                if (const auto id = ContainerId::fromValue(*placed); id && observedContainerRevisions.contains(*id))
                    return id;
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
            const auto native = nativeItemRecords.find(stack.prototypeId.value());
            const bool isNative = native != nativeItemRecords.end();
            if ((!local && !isNative) || stack.count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                || (!isNative && stack.condition > static_cast<std::uint32_t>(std::numeric_limits<int>::max())))
                return std::nullopt;
            MWWorld::ManualRef reference(
                *MWBase::Environment::get().getESMStore(), isNative ? native->second : refId(local->record), static_cast<int>(stack.count));
            auto ptr = reference.getPtr();
            ptr.getCellRef().setCount(static_cast<int>(stack.count));
            if (isNative)
            {
                if (!ptr.getClass().getScript(ptr).empty()) return std::nullopt;
                const auto charge = std::bit_cast<float>(stack.enchantmentCharge);
                const auto condition = std::bit_cast<int32_t>(stack.condition);
                if (!std::isfinite(charge) || charge < -1 || condition < -1) return std::nullopt;
                ptr.getCellRef().setCharge(condition);
                ptr.getCellRef().setEnchantmentCharge(charge);
            }
            else if (ptr.getClass().hasItemHealth(ptr))
                ptr.getCellRef().setCharge(static_cast<int>(stack.condition));
            if (!isNative && !ptr.getClass().getEnchantment(ptr).empty())
                ptr.getCellRef().setEnchantmentCharge(static_cast<float>(stack.enchantmentCharge));
            if (stack.soulPrototype)
            {
                if (isNative)
                {
                    const auto soul = nativeSoulRecords.find(stack.soulPrototype->value());
                    if (soul == nativeSoulRecords.end()) return std::nullopt;
                    ptr.getCellRef().setSoul(soul->second);
                }
                else
                {
                    if (!mapping)
                        return std::nullopt;
                    const auto soul = std::ranges::lower_bound(
                        mapping->actorPrototypes, *stack.soulPrototype, {}, &DesktopActorPrototypeMapping::id);
                    if (soul == mapping->actorPrototypes.end() || soul->id != *stack.soulPrototype)
                        return std::nullopt;
                    ptr.getCellRef().setSoul(refId(soul->record));
                }
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

            if (nativeItemRecords.empty() && std::ranges::any_of(containers,
                    [](const auto& container) { return (container.container.value() & MWWorld::PlacedRefTag) != 0; }))
            {
                nativeItemRecords = MWWorld::inventoryRecords(*MWBase::Environment::get().getESMStore());
                nativeSoulRecords = MWWorld::inventorySoulRecords(*MWBase::Environment::get().getESMStore());
            }

            if (!observedPlayerInventoryRevision || *observedPlayerInventoryRevision != player.revision)
            {
                auto playerPtr = world->getPlayerPtr();
                auto& inventory = playerPtr.getClass().getInventoryStore(playerPtr);
                const bool native = !nativeItemRecords.empty();
                // Rebuilding can reuse a presentation node for a different
                // remote stack. End any drag while its old identity still holds.
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->cancelDrag();
                // Stock add merges an equipped copy with a newly received copy
                // while the slots are temporarily empty. A native baseline owns
                // both stack identities and slots; install it without restacking.
                if (native)
                    inventory.clearAuthoritative(world->getLocalScripts());
                else
                {
                    inventory.unequipAll();
                    inventory.clear();
                }
                std::erase_if(observedInventoryStacks,
                    [](const auto& value) { return !value.second.container && !value.second.ground; });
                for (const auto& stack : player.stacks)
                {
                    auto local = materializeItem(stack, [&](const MWWorld::Ptr& ptr) {
                        if (native)
                            return *inventory.addAuthoritative(ptr, static_cast<int>(stack.count),
                                *MWBase::Environment::get().getWorldModel());
                        return *inventory.add(ptr, static_cast<int>(stack.count), false);
                    });
                    if (!local)
                        return ProviderResult::ContentMappingFailed;
                    const auto [stored, inserted] = observedInventoryStacks.emplace(stack.stackId,
                        ObservedInventoryStack{ stack, *local, std::nullopt, false, std::nullopt, std::nullopt });
                    if (!inserted)
                    {
                        Log(Debug::Error) << "TES3MP player inventory has duplicate stack ID=" << stack.stackId.value();
                        return ProviderResult::ContentMappingFailed;
                    }
                    if (std::ranges::any_of(observedInventoryStacks, [&](const auto& value) {
                            return value.first != stored->first && value.second.ptr == stored->second.ptr;
                        }))
                    {
                        Log(Debug::Error) << "TES3MP player inventory merged distinct stacks for record="
                                          << local->getCellRef().getRefId();
                        return ProviderResult::ContentMappingFailed;
                    }
                }
                std::vector<std::pair<int, MWWorld::Ptr>> slots;
                for (const auto& binding : player.equipment)
                {
                    const auto stored = observedInventoryStacks.find(binding.stackId);
                    if (stored == observedInventoryStacks.end() || stored->second.container || stored->second.ground)
                        return ProviderResult::PresentationFailed;
                    if (native)
                    {
                        slots.emplace_back(static_cast<int>(binding.slot), stored->second.ptr);
                        continue;
                    }
                    auto iter = inventory.begin();
                    for (; iter != inventory.end() && *iter != stored->second.ptr; ++iter)
                    {
                    }
                    if (iter == inventory.end())
                        return ProviderResult::PresentationFailed;
                    inventory.equip(static_cast<int>(binding.slot), iter);
                }
                if (native)
                    inventory.applyAuthoritativeEquipment(slots);
                // An empty baseline has no add/equip callbacks to refresh the
                // GUI. Notify once the complete committed inventory is installed.
                MWBase::Environment::get().getWindowManager()->inventoryUpdated(playerPtr);
                observedPlayerInventoryRevision = player.revision;
                Log(Debug::Verbose) << "TES3MP player inventory installed: revision=" << player.revision.value()
                                    << " stacks=" << player.stacks.size() << " equipped=" << player.equipment.size();
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
                MWWorld::Ptr ptr;
                if (localMapping != mapping->containers.end() && localMapping->id == baseline.container)
                    ptr = findActiveContainer(localMapping->refNumIndex, localMapping->refNumContentFile);
                else if (baseline.container.value() & MWWorld::PlacedRefTag)
                {
                    const auto ref = MWWorld::localPlacedRef(baseline.container.value(), world->getContentFiles());
                    if (!ref) return ProviderResult::ContentMappingFailed;
                    ptr = findActiveContainer(ref->mIndex, ref->mContentFile);
                    if (!ptr.isEmpty() && ptr.getCell() != resolveCell(baseline.cell, *mapping))
                        return ProviderResult::ContentMappingFailed;
                    if (!ptr.isEmpty())
                    {
                        const auto& p = ptr.getCellRef().getPosition().pos;
                        for (float v : p)
                            if (!std::isfinite(v) || std::abs(double(v)) >= double(INT64_MAX) / PositionScale)
                                return ProviderResult::ContentMappingFailed;
                        if (Position3(std::llround(double(p[0]) * PositionScale), std::llround(double(p[1]) * PositionScale),
                                std::llround(double(p[2]) * PositionScale)) != baseline.position)
                            return ProviderResult::ContentMappingFailed;
                    }
                }
                else
                    return ProviderResult::ContentMappingFailed;
                if (ptr.isEmpty())
                    continue;
                auto& store = ptr.getClass().getContainerStore(ptr);
                // Actor baselines currently cover content-defined corpses only.
                // A live local actor must not be converted into a loot container.
                if (ptr.getClass().isActor() && !ptr.getClass().getCreatureStats(ptr).isDead())
                    return ProviderResult::PresentationFailed;
                if (!baseline.equipment.empty() && !ptr.getClass().hasInventoryStore(ptr))
                    return ProviderResult::ContentMappingFailed;
                store.clearAuthoritative(world->getLocalScripts());
                std::erase_if(observedInventoryStacks,
                    [&](const auto& value) { return value.second.container == baseline.container; });
                for (const auto& stack : baseline.stacks)
                {
                    auto local = materializeItem(stack,
                        [&](const MWWorld::Ptr& item) {
                            if (!(baseline.container.value() & MWWorld::PlacedRefTag))
                                return *store.add(item, static_cast<int>(stack.count), false);
                            return *store.addAuthoritative(item, static_cast<int>(stack.count),
                                *MWBase::Environment::get().getWorldModel());
                        });
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
                if (ptr.getClass().hasInventoryStore(ptr))
                {
                    auto& inventory = ptr.getClass().getInventoryStore(ptr);
                    std::vector<std::pair<int, MWWorld::Ptr>> slots;
                    for (const auto& binding : baseline.equipment)
                    {
                        const auto stored = observedInventoryStacks.find(binding.stackId);
                        if (stored == observedInventoryStacks.end() || stored->second.container != baseline.container)
                            return ProviderResult::ContentMappingFailed;
                        slots.emplace_back(static_cast<int>(binding.slot), stored->second.ptr);
                    }
                    inventory.applyAuthoritativeEquipment(slots);
                }
                // Authoritative store installation bypasses stock mutation
                // callbacks; an already-open loot window still needs a refresh.
                MWBase::Environment::get().getWindowManager()->inventoryUpdated(ptr);
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
            // The visible inventory uses TradeItemModel/SortFilterItemModel
            // proxies. Ask the model for its owner instead of requiring its leaf type.
            const bool sourcePlayer = source.usesContainer(player) && !stack->container && !stack->ground;
            const bool targetPlayer = target.usesContainer(player);
            if (stack->container && targetPlayer)
            {
                result.kind = InventoryTransactionKind::TakeFromContainer;
                result.containerId = stack->container;
                result.expectedContainerRevision = stack->containerRevision;
                return result;
            }
            if (sourcePlayer)
            {
                // Corpse windows use InventoryItemModel for NPCs and armed
                // creatures, and ContainerItemModel for other creatures/chests.
                // Quick transfer supplies the view's SortFilterItemModel proxy.
                auto& targetModel = target.getTransferTarget();
                MWWorld::Ptr targetOwner;
                if (auto* containerModel = dynamic_cast<MWGui::ContainerItemModel*>(&targetModel))
                    targetOwner = containerModel->primarySource();
                else if (auto* inventoryModel = dynamic_cast<MWGui::InventoryItemModel*>(&targetModel))
                    targetOwner = inventoryModel->actor();
                if (!targetOwner.isEmpty())
                {
                    const auto id = containerId(targetOwner);
                    const auto revision = id ? observedContainerRevisions.find(*id) : observedContainerRevisions.end();
                    if (!id || revision == observedContainerRevisions.end())
                        return std::nullopt;
                    result.kind = InventoryTransactionKind::PutIntoContainer;
                    result.containerId = *id;
                    result.expectedContainerRevision = revision->second;
                    return result;
                }
                if (dynamic_cast<MWGui::WorldItemModel*>(&targetModel))
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
        (void)receivedAt;
        try
        {
            const auto result = mImpl->applyCombat(snapshot, events);
            if (result != ProviderResult::Accepted)
                mImpl->clear();
            return result;
        }
        catch (const std::exception& error)
        {
            Log(Debug::Error) << "TES3MP combat presentation exception: " << error.what();
            mImpl->clear();
            return ProviderResult::PresentationFailed;
        }
        catch (...)
        {
            Log(Debug::Error) << "TES3MP combat presentation: unknown exception";
            mImpl->clear();
            return ProviderResult::PresentationFailed;
        }
    }

    ProviderResult DesktopPresentation::applyQuestJournal(const QuestJournalCatalog& catalog,
        const CanonicalPlayerQuestJournalState& state, MonotonicInstant receivedAt) noexcept
    {
        (void)receivedAt;
        try
        {
            return mImpl->applyQuestJournal(catalog, state);
        }
        catch (...)
        {
            return ProviderResult::PresentationFailed;
        }
    }

    ProviderResult DesktopPresentation::applyWeather(
        std::span<const WeatherRegionSnapshot> regions, ServerTick serverTick, MonotonicInstant receivedAt) noexcept
    {
        (void)receivedAt;
        try
        {
            const auto result = mImpl->applyWeather(regions, serverTick);
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

    ProviderResult DesktopPresentation::applyWorldTime(
        const ReliableWorldTimeState& state, MonotonicInstant receivedAt) noexcept
    {
        (void)receivedAt;
        try
        {
            const auto result = mImpl->applyWorldTime(state);
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

    std::optional<MagicUseCapture> DesktopPresentation::captureMagicUse(
        const ESM::RefId& spell, const MWWorld::Ptr& item, const MWWorld::Ptr& target) const noexcept
    {
        try
        {
            return mImpl->captureMagicUse(spell, item, target);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ObjectInteractionCapture> DesktopPresentation::captureSecurityAttempt(
        const MWWorld::Ptr& target, const MWWorld::Ptr& tool, bool disarm) const noexcept
    {
        try
        {
            return mImpl->captureSecurityAttempt(target, tool, disarm);
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
