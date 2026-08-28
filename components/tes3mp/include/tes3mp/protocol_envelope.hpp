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
            ServerTick serverTick, std::optional<CommandSequence> acknowledgedCommandSequence) noexcept
            : mTargetSessionId(targetSessionId)
            , mTargetSessionGeneration(targetSessionGeneration)
            , mServerTick(serverTick)
            , mAcknowledgedCommandSequence(acknowledgedCommandSequence)
        {
        }

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr const std::optional<CommandSequence>& acknowledgedCommandSequence() const noexcept
        {
            return mAcknowledgedCommandSequence;
        }

        friend constexpr bool operator==(LatestWinsSnapshotHeader, LatestWinsSnapshotHeader) noexcept = default;
        friend constexpr auto operator<=>(LatestWinsSnapshotHeader, LatestWinsSnapshotHeader) noexcept = default;

    private:
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        ServerTick mServerTick;
        std::optional<CommandSequence> mAcknowledgedCommandSequence;
    };
}

#endif
