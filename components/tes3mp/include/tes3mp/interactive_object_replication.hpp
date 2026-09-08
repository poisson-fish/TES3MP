#ifndef TES3MP_INTERACTIVE_OBJECT_REPLICATION_HPP
#define TES3MP_INTERACTIVE_OBJECT_REPLICATION_HPP

#include "command_primitives.hpp"
#include "content_identity.hpp"
#include "interactive_object_catalog.hpp"
#include "interactive_object_world.hpp"
#include "session_types.hpp"
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
    inline constexpr std::size_t MaximumInteractiveObjectInterestMembers = 512;

    enum class InteractiveObjectReplicationDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        MissingHeader,
        InvalidStrongValue,
        TooManyEntries,
        EntriesNotStrictlySorted,
        InvalidDoorState,
        InvalidLockState,
        InvalidTrapState,
        InvalidInteractionKind,
        InvalidCellKind,
        InvalidInteriorGrid,
    };

    struct InteractiveObjectReplicationDecodeError
    {
        InteractiveObjectReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;

        friend constexpr bool operator==(InteractiveObjectReplicationDecodeError,
            InteractiveObjectReplicationDecodeError) noexcept = default;
    };

    struct InteractiveObjectInterestMember
    {
        InteractiveObjectId objectId;
        ObjectRevision revision;
        DoorState doorState;
        LockState lockState;
        TrapState trapState;

        friend constexpr bool operator==(const InteractiveObjectInterestMember&,
            const InteractiveObjectInterestMember&) noexcept = default;
        friend constexpr auto operator<=>(const InteractiveObjectInterestMember&,
            const InteractiveObjectInterestMember&) noexcept = default;
    };

    class ReliableInteractiveObjectInterestBaseline
    {
    public:
        static std::variant<ReliableInteractiveObjectInterestBaseline, InteractiveObjectReplicationDecodeError> create(
            SessionId targetSessionId, SessionGeneration targetSessionGeneration, ServerTick serverTick,
            CanonicalRevision canonicalRevision, std::span<const InteractiveObjectInterestMember> members);

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const InteractiveObjectInterestMember> members() const noexcept { return mMembers; }

        friend bool operator==(const ReliableInteractiveObjectInterestBaseline&,
            const ReliableInteractiveObjectInterestBaseline&) noexcept = default;

    private:
        ReliableInteractiveObjectInterestBaseline(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, std::vector<InteractiveObjectInterestMember> members)
            : mTargetSessionId(session)
            , mTargetSessionGeneration(generation)
            , mServerTick(tick)
            , mCanonicalRevision(canonicalRevision)
            , mMembers(std::move(members))
        {
        }

        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        ServerTick mServerTick;
        CanonicalRevision mCanonicalRevision;
        std::vector<InteractiveObjectInterestMember> mMembers;
    };

    struct ClientInteractObjectCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        InteractiveObjectId objectId;
        CellId targetCell;
        Position3 interactionOrigin;
        ObjectRevision expectedRevision;
        ObjectInteractionKind kind = ObjectInteractionKind::Activate;
        std::optional<KeyPrototypeId> requestedKey = std::nullopt;

        friend bool operator==(const ClientInteractObjectCommand&,
            const ClientInteractObjectCommand&) noexcept = default;
    };

    using ReliableInteractiveObjectInterestBaselineDecodeResult
        = std::variant<ReliableInteractiveObjectInterestBaseline, InteractiveObjectReplicationDecodeError>;
    using ClientInteractObjectCommandDecodeResult
        = std::variant<ClientInteractObjectCommand, InteractiveObjectReplicationDecodeError>;

    std::vector<std::byte> encodeReliableInteractiveObjectInterestBaseline(
        const ReliableInteractiveObjectInterestBaseline& value);
    std::vector<std::byte> encodeClientInteractObjectCommand(
        const ClientInteractObjectCommand& value);

    ReliableInteractiveObjectInterestBaselineDecodeResult decodeReliableInteractiveObjectInterestBaseline(
        std::span<const std::byte> payload);
    ClientInteractObjectCommandDecodeResult decodeClientInteractObjectCommand(
        std::span<const std::byte> payload);
}

#endif
