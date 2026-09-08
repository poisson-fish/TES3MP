#ifndef TES3MP_INTERACTIVE_OBJECT_WORLD_HPP
#define TES3MP_INTERACTIVE_OBJECT_WORLD_HPP

#include "canonical_state.hpp"
#include "interactive_object_catalog.hpp"
#include "spatial_types.hpp"
#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    class CanonicalInteractiveObjectWorld;
    class InteractiveObjectCatalog;
    struct InteractObjectCommand;
    struct ObjectInteractionValidationContext;
    struct StagedObjectInteractionResult;

    enum class DoorState : std::uint8_t
    {
        Closed = 0,
        Open = 1,
    };

    enum class LockState : std::uint8_t
    {
        Unlocked = 0,
        Locked = 1,
    };

    enum class TrapState : std::uint8_t
    {
        Disarmed = 0,
        Armed = 1,
    };

    class CanonicalInteractiveObjectState
    {
    public:
        constexpr CanonicalInteractiveObjectState(InteractiveObjectId objectId, CellId cell, DoorState doorState,
            LockState lockState, std::uint32_t lockLevel, std::optional<KeyPrototypeId> keyId, TrapState trapState,
            std::optional<TrapPrototypeId> trapId, ObjectRevision revision, ServerTick lastChangeTick) noexcept
            : mObjectId(objectId)
            , mCell(cell)
            , mDoorState(doorState)
            , mLockState(lockState)
            , mLockLevel(lockLevel)
            , mKeyId(keyId)
            , mTrapState(trapState)
            , mTrapId(trapId)
            , mRevision(revision)
            , mLastChangeTick(lastChangeTick)
        {
        }

        constexpr InteractiveObjectId objectId() const noexcept { return mObjectId; }
        constexpr const CellId& cell() const noexcept { return mCell; }
        constexpr DoorState doorState() const noexcept { return mDoorState; }
        constexpr LockState lockState() const noexcept { return mLockState; }
        constexpr std::uint32_t lockLevel() const noexcept { return mLockLevel; }
        constexpr std::optional<KeyPrototypeId> keyId() const noexcept { return mKeyId; }
        constexpr TrapState trapState() const noexcept { return mTrapState; }
        constexpr std::optional<TrapPrototypeId> trapId() const noexcept { return mTrapId; }
        constexpr ObjectRevision revision() const noexcept { return mRevision; }
        constexpr ServerTick lastChangeTick() const noexcept { return mLastChangeTick; }

        friend constexpr bool operator==(
            const CanonicalInteractiveObjectState&, const CanonicalInteractiveObjectState&) noexcept
            = default;

    private:
        InteractiveObjectId mObjectId;
        CellId mCell;
        DoorState mDoorState;
        LockState mLockState;
        std::uint32_t mLockLevel;
        std::optional<KeyPrototypeId> mKeyId;
        TrapState mTrapState;
        std::optional<TrapPrototypeId> mTrapId;
        ObjectRevision mRevision;
        ServerTick mLastChangeTick;
    };

    enum class CanonicalInteractiveObjectWorldErrorCode : std::uint8_t
    {
        LimitExceeded,
        ObjectIdsNotStrictlyOrdered,
        AllocationFailure,
    };

    struct CanonicalInteractiveObjectWorldError
    {
        CanonicalInteractiveObjectWorldErrorCode code;
        std::size_t index = 0;
        std::uint64_t value = 0;
        std::uint64_t relatedValue = 0;

        friend constexpr bool operator==(
            CanonicalInteractiveObjectWorldError, CanonicalInteractiveObjectWorldError) noexcept
            = default;
    };

    class CanonicalInteractiveObjectWorld
    {
    public:
        std::span<const CanonicalInteractiveObjectState> objects() const noexcept { return mObjects; }
        const CanonicalInteractiveObjectState* find(InteractiveObjectId id) const noexcept;

        friend bool operator==(const CanonicalInteractiveObjectWorld&, const CanonicalInteractiveObjectWorld&) noexcept
            = default;

    private:
        friend std::variant<CanonicalInteractiveObjectWorld, CanonicalInteractiveObjectWorldError>
            createCanonicalInteractiveObjectWorld(std::span<const CanonicalInteractiveObjectState>);
        friend StagedObjectInteractionResult applyObjectInteractionToCandidate(CanonicalInteractiveObjectWorld&,
            const InteractiveObjectCatalog&, const CanonicalServerState&, const InteractObjectCommand&, ServerTick,
            ObjectInteractionValidationContext) noexcept;
        friend bool restoreInteractiveObjectCandidate(
            CanonicalInteractiveObjectWorld&, CanonicalInteractiveObjectState) noexcept;

        explicit CanonicalInteractiveObjectWorld(std::vector<CanonicalInteractiveObjectState> objects) noexcept
            : mObjects(std::move(objects))
        {
        }

        std::vector<CanonicalInteractiveObjectState> mObjects;
    };

    using CanonicalInteractiveObjectWorldResult
        = std::variant<CanonicalInteractiveObjectWorld, CanonicalInteractiveObjectWorldError>;

    CanonicalInteractiveObjectWorldResult createCanonicalInteractiveObjectWorld(
        std::span<const CanonicalInteractiveObjectState> objects);

    CanonicalInteractiveObjectWorldResult createInitialCanonicalInteractiveObjectWorld(
        const InteractiveObjectCatalog& catalog);

    enum class ObjectInteractionKind : std::uint8_t
    {
        Activate,
        UnlockWithKey,
    };

    struct InteractObjectCommand
    {
        PlayerId player;
        InteractiveObjectId objectId;
        CellId cell;
        Position3 interactionOrigin;
        ObjectRevision expectedRevision;
        ObjectInteractionKind kind = ObjectInteractionKind::Activate;
        std::optional<KeyPrototypeId> requestedKey = std::nullopt;

        friend bool operator==(const InteractObjectCommand&, const InteractObjectCommand&) noexcept = default;
    };

    struct ObjectInteractionValidationContext
    {
        std::uint32_t maxReach = 384;
        // Derived from server-owned state for the command's authenticated player.
        std::span<const KeyPrototypeId> verifiedPlayerKeys{};
    };

    enum class ObjectInteractionResultCode : std::uint8_t
    {
        Success,
        PlayerNotFound,
        ObjectNotFound,
        CellMismatch,
        PlayerOutOfReach,
        Locked,
        StaleRevision,
        TrapSprung,
        CatalogMismatch,
        TickRegression,
        RevisionExhausted,
        InternalError,
    };

    struct ObjectInteractionOutcome
    {
        ObjectInteractionResultCode code = ObjectInteractionResultCode::ObjectNotFound;
        InteractiveObjectId objectId = *InteractiveObjectId::fromValue(1);
        ObjectRevision newRevision = ObjectRevision::initial();
        DoorState newDoorState = DoorState::Closed;
        LockState newLockState = LockState::Unlocked;
        TrapState newTrapState = TrapState::Disarmed;
        std::optional<TeleportDestination> playerTeleport = std::nullopt;
        std::optional<TrapPrototypeId> sprungTrap = std::nullopt;

        friend bool operator==(const ObjectInteractionOutcome&, const ObjectInteractionOutcome&) noexcept = default;
    };

    struct ObjectInteractionResult
    {
        ObjectInteractionOutcome outcome;
        std::optional<CanonicalInteractiveObjectWorld> updatedWorld;

        friend bool operator==(const ObjectInteractionResult&, const ObjectInteractionResult&) noexcept = default;
    };

    struct StagedObjectInteractionResult
    {
        ObjectInteractionOutcome outcome;
        bool worldChanged = false;
        std::optional<CanonicalInteractiveObjectState> previousState;

        friend bool operator==(const StagedObjectInteractionResult&, const StagedObjectInteractionResult&) noexcept
            = default;
    };

    // Mutates only a caller-owned prepared candidate. This avoids rebuilding the
    // complete bounded world for every command in a server tick.
    StagedObjectInteractionResult applyObjectInteractionToCandidate(CanonicalInteractiveObjectWorld& candidate,
        const InteractiveObjectCatalog& catalog, const CanonicalServerState& players,
        const InteractObjectCommand& command, ServerTick currentTick,
        ObjectInteractionValidationContext validation = {}) noexcept;
    bool restoreInteractiveObjectCandidate(
        CanonicalInteractiveObjectWorld& candidate, CanonicalInteractiveObjectState previous) noexcept;

    ObjectInteractionResult applyObjectInteraction(const CanonicalInteractiveObjectWorld& current,
        const InteractiveObjectCatalog& catalog, const CanonicalServerState& players,
        const InteractObjectCommand& command, ServerTick currentTick,
        ObjectInteractionValidationContext validation = {}) noexcept;
}

#endif
