#include "interactive_object_interest_projection.hpp"

#include <tes3mp/protocol_frame.hpp>

#include <algorithm>

namespace TES3MP::ServerApp
{
    namespace
    {
        const CanonicalPlayerEntityState* targetPlayer(
            const CanonicalServerState& players, SessionId target) noexcept
        {
            const auto* session = players.findActiveSession(target);
            return session ? players.findPlayer(session->playerId()) : nullptr;
        }

        std::vector<InteractiveObjectInterestMember> membersForCell(
            const CellId& cell, const CanonicalInteractiveObjectWorld& objects)
        {
            std::vector<InteractiveObjectInterestMember> members;
            for (const auto& obj : objects.objects())
            {
                if (obj.cell() == cell)
                {
                    members.push_back(InteractiveObjectInterestMember{
                        .objectId = obj.objectId(),
                        .revision = obj.revision(),
                        .doorState = obj.doorState(),
                        .lockState = obj.lockState(),
                        .trapState = obj.trapState()
                    });
                }
            }
            std::ranges::sort(members, [](const auto& a, const auto& b) {
                return a.objectId < b.objectId;
            });
            return members;
        }
    }

    std::optional<InteractiveObjectInterestBaselineDelivery> projectInteractiveObjectInterestBaseline(
        const CanonicalServerState& players, const CanonicalInteractiveObjectWorld& objects,
        SessionId target, ServerTick tick, CanonicalRevision canonicalRevision)
    try
    {
        const auto* session = players.findActiveSession(target);
        const auto* player = targetPlayer(players, target);
        if (!session || !player)
            return std::nullopt;

        const auto members = membersForCell(player->transform().cell(), objects);
        auto baseline = ReliableInteractiveObjectInterestBaseline::create(
            target, session->sessionGeneration(), tick, canonicalRevision, members);
        if (!std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(baseline))
            return std::nullopt;

        return InteractiveObjectInterestBaselineDelivery{
            target,
            std::get<ReliableInteractiveObjectInterestBaseline>(std::move(baseline))
        };
    }
    catch (...)
    {
        return std::nullopt;
    }

    std::optional<ReliableInteractiveObjectInterestBaseline> projectCellInteractiveObjectBaseline(
        const CellId& cell, const CanonicalInteractiveObjectWorld& objects,
        SessionId target, SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision)
    try
    {
        const auto members = membersForCell(cell, objects);
        auto baseline = ReliableInteractiveObjectInterestBaseline::create(
            target, generation, tick, canonicalRevision, members);
        if (!std::holds_alternative<ReliableInteractiveObjectInterestBaseline>(baseline))
            return std::nullopt;
        return std::get<ReliableInteractiveObjectInterestBaseline>(std::move(baseline));
    }
    catch (...)
    {
        return std::nullopt;
    }

    bool admitInteractiveObjectInterestBaseline(OutboundQueueSet& queues, TransportConnectionId connection,
        const InteractiveObjectInterestBaselineDelivery& delivery)
    {
        auto frame = encodeProtocolFrame(MessageClass::ReliableOperation,
            MessageKind::ReliableInteractiveObjectInterestBaseline,
            encodeReliableInteractiveObjectInterestBaseline(delivery.baseline));
        if (!std::holds_alternative<std::vector<std::byte>>(frame))
            return false;
        return queues.enqueue(connection, TransportChannel::ReliableOrdered,
                   std::get<std::vector<std::byte>>(frame)) == TransportResult::Accepted;
    }
}
