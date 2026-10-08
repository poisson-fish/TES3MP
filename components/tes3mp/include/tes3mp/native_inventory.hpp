#ifndef TES3MP_NATIVE_INVENTORY_HPP
#define TES3MP_NATIVE_INVENTORY_HPP

#include "canonical_persistence.hpp"
#include "server_command_intake.hpp"
#include "native_door.hpp"
#include "wait_rest.hpp"
#include <functional>

namespace TES3MP
{
    class CharacterProfile;
    // The engine implementation owns detached candidates and its existing image.
    // Only the reducer supplies this serialized durability callback. Committed
    // authorizes nonthrowing engine installation before canonical publication.
    using NativeInventoryCommit = std::function<CanonicalDurabilityResult(std::span<const std::byte>)>;

    struct NativePlayerMotion
    {
        PlayerId player;
        Transform root;
        LinearVelocity3 velocity;
        LocomotionMode mode;
        PlayerPhysicsBaseline physics;
    };

    class PreparedNativeInventory
    {
    public:
        virtual ~PreparedNativeInventory() = default;
        // A door may stage relocation of the authenticated requester. The
        // reducer persists it with the unchanged native image before publication.
        virtual std::optional<Transform> playerDestination() const { return {}; }
        virtual std::span<const std::pair<PlayerId, Transform>> playerRelocations() const { return {}; }
        virtual std::span<const NativePlayerMotion> playerMotions() const { return {}; }
        virtual bool changesInventory() const noexcept { return true; }
        virtual bool waitRestApplied() const noexcept { return false; }
        virtual std::optional<std::pair<PlayerId, std::uint64_t>> requestedPlayerRespawn() const { return {}; }
        virtual CanonicalDurabilityResult commit(const NativeInventoryCommit& durability) noexcept = 0;
    };

    class NativeInventoryAuthority
    {
    public:
        // Application composition keeps this service and its loaded content
        // alive until the reducer and all preparations have been destroyed.
        virtual ~NativeInventoryAuthority() = default;
        // Completion must stage inventory, actor stats/spells and resource
        // bindings together. A missing implementation rejects fresh characters;
        // the profile/safe point must never commit against an unchanged image.
        virtual std::unique_ptr<PreparedNativeInventory> prepareCharacterCompletion(
            const CanonicalServerState&, PlayerId, const CharacterProfile&, ServerTick) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareInventory(
            const CanonicalServerState& players, const ServerCommandProposal& command) = 0;
        virtual std::unique_ptr<PreparedNativeInventory> prepareMeleeAttack(
            const CanonicalServerState&, const ServerCommandProposal&, ServerTick) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> preparePlayerRespawn(
            const CanonicalServerState&, const ServerCommandProposal&) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareMagicUse(
            const CanonicalServerState&, const ServerCommandProposal&, ServerTick) { return {}; }
        // Append a bounded independent caster to an existing detached candidate.
        // False leaves the candidate unchanged; neither result publishes state.
        virtual bool appendDialogueIntent(
            const CanonicalServerState&, const ServerCommandProposal&, std::unique_ptr<PreparedNativeInventory>&)
        {
            return false;
        }
        virtual bool appendMagicUse(const CanonicalServerState&, const ServerCommandProposal&,
            ServerTick, PreparedNativeInventory&) { return false; }
        virtual std::span<const std::byte> inventoryImage() const noexcept = 0;
        virtual bool hasNativeDoor() const noexcept { return false; }
        virtual bool ownsNativeDoor(InteractiveObjectId) const noexcept { return false; }
        virtual bool requiresDoorTraversal() const noexcept { return false; }
        virtual bool streamsPlayerAreas() const noexcept { return false; }
        virtual bool hasLeveledActors() const noexcept { return false; }
        virtual bool hasActorMotion() const noexcept { return false; }
        virtual bool ownsPlayerPhysics() const noexcept { return false; }
        virtual std::optional<PlayerPhysicsBaseline> projectPlayerPhysics(
            const CanonicalServerState&, PlayerId, const PreparedNativeInventory* = nullptr) const { return {}; }
        virtual bool hasNativeCombat() const noexcept { return false; }
        virtual bool hasNativeMagicUse() const noexcept { return false; }
        // Resolve inherited player motion against the native area domain. This
        // never grants interior travel; the engine adapter may permit contiguous
        // exterior crossings while explicit relocations remain transactional.
        virtual bool allowsPlayerMovement(PlayerId) const { return true; }
        virtual std::optional<CellId> movementCell(CellId current, Position3) const { return current; }
        virtual bool allowsCellTransition(CellId, CellId, Position3) const { return false; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareDoorActivation(
            const CanonicalServerState&, const ServerCommandProposal&) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareDoorStep(
            const CanonicalServerState&, ServerTick, float) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareNativeTick(const CanonicalServerState& players,
            ServerTick tick, float seconds, std::unique_ptr<PreparedNativeInventory> command,
            const CanonicalWorldState* world = nullptr)
        { return command ? std::move(command) : prepareDoorStep(players, tick, seconds); }
        // Admit elapsed menu time before effect/resource/membership cleanup.
        // The candidate flag couples the world clock to the same durable tick.
        virtual std::unique_ptr<PreparedNativeInventory> prepareNativeScheduledTick(
            const CanonicalServerState& players, ServerTick tick, float seconds,
            std::unique_ptr<PreparedNativeInventory> command, const CanonicalWorldState* world,
            std::optional<WaitRestRequest> waitRest)
        { return prepareNativeTick(players, tick, seconds, std::move(command), world); }
        virtual std::unique_ptr<PreparedNativeInventory> prepareNativePlayerTick(
            const CanonicalServerState& players, ServerTick tick, float seconds,
            std::unique_ptr<PreparedNativeInventory> command, const CanonicalWorldState* world,
            std::optional<WaitRestRequest> waitRest, std::span<const ServerCommandProposal>)
        { return prepareNativeScheduledTick(players, tick, seconds, std::move(command), world, waitRest); }
        virtual void reportDoorObstruction(const CanonicalServerState&, const ClientDoorObstruction&, ServerTick) {}
    };
}
#endif
