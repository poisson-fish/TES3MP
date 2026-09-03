#ifndef TES3MP_PROTOCOL_ENVELOPE_HPP
#define TES3MP_PROTOCOL_ENVELOPE_HPP

#include "command_primitives.hpp"

#include <compare>
#include <optional>

namespace TES3MP
{
    class ReliableOperationHeader
    {
    public:
        constexpr ReliableOperationHeader(
            ClientCommandHeader commandHeader, std::optional<EntityPrecondition> entityPrecondition) noexcept
            : mCommandHeader(commandHeader)
            , mEntityPrecondition(entityPrecondition)
        {
        }

        constexpr const ClientCommandHeader& commandHeader() const noexcept { return mCommandHeader; }
        constexpr const std::optional<EntityPrecondition>& entityPrecondition() const noexcept
        {
            return mEntityPrecondition;
        }

        friend constexpr bool operator==(const ReliableOperationHeader&, const ReliableOperationHeader&) noexcept
            = default;
        friend constexpr auto operator<=>(const ReliableOperationHeader&, const ReliableOperationHeader&) noexcept
            = default;

    private:
        ClientCommandHeader mCommandHeader;
        std::optional<EntityPrecondition> mEntityPrecondition;
    };

    class LatestWinsSnapshotHeader
    {
    public:
        constexpr LatestWinsSnapshotHeader(SessionId targetSessionId, SessionGeneration targetSessionGeneration,
            CanonicalRevision canonicalRevision, std::optional<CommandSequence> acknowledgedCommandSequence) noexcept
            : mTargetSessionId(targetSessionId)
            , mTargetSessionGeneration(targetSessionGeneration)
            , mCanonicalRevision(canonicalRevision)
            , mAcknowledgedCommandSequence(acknowledgedCommandSequence)
        {
        }

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        constexpr const std::optional<CommandSequence>& acknowledgedCommandSequence() const noexcept
        {
            return mAcknowledgedCommandSequence;
        }

        friend constexpr bool operator==(LatestWinsSnapshotHeader, LatestWinsSnapshotHeader) noexcept = default;
        friend constexpr auto operator<=>(LatestWinsSnapshotHeader, LatestWinsSnapshotHeader) noexcept = default;

    private:
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        CanonicalRevision mCanonicalRevision;
        std::optional<CommandSequence> mAcknowledgedCommandSequence;
    };
}

#endif
