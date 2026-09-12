#ifndef TES3MP_SECURITY_HPP
#define TES3MP_SECURITY_HPP

#include "combat_world.hpp"
#include "interactive_object_catalog.hpp"
#include "interactive_object_world.hpp"
#include "inventory_world.hpp"
#include "item_catalog.hpp"

namespace TES3MP
{
    enum class AuthoritativeSecurityDisposition : std::uint8_t
    {
        Succeeded,
        Failed,
        PlayerNotFound,
        ObjectNotFound,
        CellMismatch,
        PlayerOutOfReach,
        StaleObjectRevision,
        MissingRevision,
        ToolNotFound,
        WrongTool,
        ToolBroken,
        ObjectStateMismatch,
        StaleInventoryRevision,
        StaleCombatRevision,
        TickRegression,
        RevisionExhausted,
        InvalidWorld,
    };

    struct PreparedSecurityAttempt
    {
        AuthoritativeSecurityDisposition disposition = AuthoritativeSecurityDisposition::InvalidWorld;
        std::optional<CanonicalInteractiveObjectWorld> objects;
        std::optional<CanonicalInventoryWorld> inventory;
        std::optional<CanonicalCombatWorld> combat;

        constexpr bool applied() const noexcept
        {
            return disposition == AuthoritativeSecurityDisposition::Succeeded
                || disposition == AuthoritativeSecurityDisposition::Failed;
        }
        constexpr bool succeeded() const noexcept
        {
            return disposition == AuthoritativeSecurityDisposition::Succeeded;
        }
    };

    PreparedSecurityAttempt prepareAuthoritativeSecurityAttempt(const CanonicalInteractiveObjectWorld& objects,
        const InteractiveObjectCatalog& objectCatalog, const CanonicalInventoryWorld& inventory,
        const ItemPrototypeCatalog& itemCatalog, const CanonicalCombatWorld& combat,
        const CanonicalServerState& players, const InteractObjectCommand& command,
        const OpenMwSecuritySettings& settings, ServerTick tick,
        ObjectInteractionValidationContext validation = {}) noexcept;
}

#endif
