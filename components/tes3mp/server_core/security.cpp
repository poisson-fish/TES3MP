#include <tes3mp/security.hpp>

namespace TES3MP
{
    PreparedSecurityAttempt prepareAuthoritativeSecurityAttempt(const CanonicalInteractiveObjectWorld& objects,
        const InteractiveObjectCatalog& objectCatalog, const CanonicalInventoryWorld& inventory,
        const ItemPrototypeCatalog& itemCatalog, const CanonicalCombatWorld& combat,
        const CanonicalServerState& players, const InteractObjectCommand& command,
        const OpenMwSecuritySettings& settings, ServerTick tick,
        ObjectInteractionValidationContext validation) noexcept
    try
    {
        const auto* player = players.findPlayer(command.player);
        if (!player)
            return { AuthoritativeSecurityDisposition::PlayerNotFound };
        const auto* object = objects.find(command.objectId);
        const auto* declaration = objectCatalog.find(command.objectId);
        if (!object || !declaration)
            return { AuthoritativeSecurityDisposition::ObjectNotFound };
        if (player->transform().cell() != command.cell || object->cell() != command.cell
            || declaration->cell != command.cell)
            return { AuthoritativeSecurityDisposition::CellMismatch };
        if (!positionsWithinReach(player->transform().position(), declaration->transform.position(),
                validation.maxReach)
            || !positionsWithinReach(player->transform().position(), command.interactionOrigin, validation.maxReach)
            || !positionsWithinReach(command.interactionOrigin, declaration->transform.position(),
                validation.maxReach))
            return { AuthoritativeSecurityDisposition::PlayerOutOfReach };
        if (object->revision() != command.expectedRevision)
            return { AuthoritativeSecurityDisposition::StaleObjectRevision };
        if (tick < object->lastChangeTick())
            return { AuthoritativeSecurityDisposition::TickRegression };
        if (!command.requestedTool || !command.expectedInventoryRevision || !command.expectedCombatRevision)
            return { AuthoritativeSecurityDisposition::MissingRevision };
        const bool picking = command.kind == ObjectInteractionKind::PickLock;
        if (!picking && command.kind != ObjectInteractionKind::DisarmTrap)
            return { AuthoritativeSecurityDisposition::ObjectStateMismatch };
        if ((picking && object->lockState() != LockState::Locked)
            || (!picking && object->trapState() != TrapState::Armed))
            return { AuthoritativeSecurityDisposition::ObjectStateMismatch };

        const auto* playerInventory = inventory.findPlayer(command.player);
        if (!playerInventory)
            return { AuthoritativeSecurityDisposition::PlayerNotFound };
        if (playerInventory->revision != *command.expectedInventoryRevision)
            return { AuthoritativeSecurityDisposition::StaleInventoryRevision };
        const auto* tool = playerInventory->findStack(*command.requestedTool);
        if (!tool)
            return { AuthoritativeSecurityDisposition::ToolNotFound };
        const auto* toolDeclaration = itemCatalog.find(tool->prototypeId);
        const auto expectedCategory = picking ? ItemCategory::Lockpick : ItemCategory::Probe;
        if (!toolDeclaration || toolDeclaration->category != expectedCategory)
            return { AuthoritativeSecurityDisposition::WrongTool };
        if (tool->condition == 0)
            return { AuthoritativeSecurityDisposition::ToolBroken };
        const auto* combatPlayer = combat.findPlayer(command.player);
        if (!combatPlayer)
            return { AuthoritativeSecurityDisposition::PlayerNotFound };
        if (combatPlayer->revision != *command.expectedCombatRevision)
            return { AuthoritativeSecurityDisposition::StaleCombatRevision };

        CanonicalInteractiveObjectWorld candidateObjects = objects;
        CanonicalInventoryWorld candidateInventory = inventory;
        CanonicalCombatWorld candidateCombat = combat;
        const auto securityKind = picking ? SecurityAttemptKind::PickLock : SecurityAttemptKind::DisarmTrap;
        const auto difficulty = picking ? object->lockLevel() : declaration->trap.disarmDifficulty;
        const auto combatResult = candidateCombat.applySecurityAttempt(command.player,
            *command.expectedCombatRevision, securityKind, difficulty, toolDeclaration->toolQuality, settings);
        if (combatResult == CanonicalCombatWorld::SecurityAttemptResult::StaleCombatRevision)
            return { AuthoritativeSecurityDisposition::StaleCombatRevision };
        if (combatResult == CanonicalCombatWorld::SecurityAttemptResult::RevisionExhausted)
            return { AuthoritativeSecurityDisposition::RevisionExhausted };
        if (combatResult != CanonicalCombatWorld::SecurityAttemptResult::Succeeded
            && combatResult != CanonicalCombatWorld::SecurityAttemptResult::Failed)
            return { AuthoritativeSecurityDisposition::InvalidWorld };
        const bool succeeded = combatResult == CanonicalCombatWorld::SecurityAttemptResult::Succeeded;

        const auto wear = candidateInventory.consumeSecurityToolUse(command.player, *command.requestedTool,
            *command.expectedInventoryRevision, expectedCategory, tick);
        if (wear == SecurityToolUseResult::StaleInventoryRevision)
            return { AuthoritativeSecurityDisposition::StaleInventoryRevision };
        if (wear == SecurityToolUseResult::RevisionExhausted)
            return { AuthoritativeSecurityDisposition::RevisionExhausted };
        if (wear != SecurityToolUseResult::Applied)
            return { AuthoritativeSecurityDisposition::InvalidWorld };

        const auto mutation = picking ? SecurityObjectMutation::Unlock : SecurityObjectMutation::Disarm;
        const auto mutated = candidateObjects.applySecurityResult(
            command.objectId, command.expectedRevision, mutation, succeeded, tick);
        if (mutated == SecurityObjectMutationResult::RevisionExhausted)
            return { AuthoritativeSecurityDisposition::RevisionExhausted };
        if (mutated != SecurityObjectMutationResult::Applied)
            return { AuthoritativeSecurityDisposition::InvalidWorld };

        return { succeeded ? AuthoritativeSecurityDisposition::Succeeded
                           : AuthoritativeSecurityDisposition::Failed,
            std::move(candidateObjects), std::move(candidateInventory), std::move(candidateCombat) };
    }
    catch (...)
    {
        return { AuthoritativeSecurityDisposition::InvalidWorld };
    }
}
