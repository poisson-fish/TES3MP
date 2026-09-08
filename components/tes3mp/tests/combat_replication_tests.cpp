#include <tes3mp/combat_replication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace
{
    template <class T> T value(std::uint64_t raw) { return *T::fromValue(raw); }

    bool command_round_trips_and_is_bounded()
    {
        const TES3MP::ClientMeleeAttackCommand input{ value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::CommandSequence::initial(), value<TES3MP::CommandId>(3),
            value<TES3MP::CanonicalRevision>(4), value<TES3MP::ActorId>(5), value<TES3MP::ServerTick>(6),
            TES3MP::CombatRevision::initial(), value<TES3MP::CombatRevision>(2),
            TES3MP::MeleeAttackType::Thrust, 0.75f };
        const auto bytes = TES3MP::encodeClientMeleeAttackCommand(input);
        const auto decoded = TES3MP::decodeClientMeleeAttackCommand(bytes);
        auto truncated = bytes; truncated.pop_back();
        auto empty = input;
        empty.targetActorId.reset();
        empty.expectedTargetRevision = TES3MP::CombatRevision::initial();
        const auto decodedEmpty = TES3MP::decodeClientMeleeAttackCommand(
            TES3MP::encodeClientMeleeAttackCommand(empty));
        return std::get<TES3MP::ClientMeleeAttackCommand>(decoded) == input
            && std::get<TES3MP::ClientMeleeAttackCommand>(decodedEmpty) == empty
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(
                TES3MP::decodeClientMeleeAttackCommand(truncated));
    }

    bool snapshots_and_events_round_trip()
    {
        const std::array actors{ TES3MP::ActorCombatSnapshot{ value<TES3MP::ActorId>(2),
            value<TES3MP::CombatRevision>(3), 40.f, 20.f, false } };
        auto created = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), value<TES3MP::ServerTick>(5),
            value<TES3MP::CanonicalRevision>(6), value<TES3MP::PlayerId>(7),
            value<TES3MP::CombatRevision>(8), 90.f, actors);
        const auto snapshot = std::get<TES3MP::LatestWinsCombatSnapshot>(created);
        const auto decodedSnapshot = TES3MP::decodeLatestWinsCombatSnapshot(
            TES3MP::encodeLatestWinsCombatSnapshot(snapshot));
        const std::array events{ TES3MP::MeleeCombatEvent{ value<TES3MP::PlayerId>(7),
            value<TES3MP::ActorId>(2), value<TES3MP::CombatRevision>(8), value<TES3MP::CombatRevision>(3),
            10.f, TES3MP::MeleeDamageStat::Health, true, false, false } };
        auto batch = std::get<TES3MP::ReliableCombatEventBatch>(TES3MP::ReliableCombatEventBatch::create(
            value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(), value<TES3MP::ServerTick>(5),
            value<TES3MP::CanonicalRevision>(6), events));
        const auto decodedBatch = TES3MP::decodeReliableCombatEventBatch(
            TES3MP::encodeReliableCombatEventBatch(batch));
        return std::get<TES3MP::LatestWinsCombatSnapshot>(decodedSnapshot) == snapshot
            && std::get<TES3MP::ReliableCombatEventBatch>(decodedBatch) == batch;
    }

    bool semantic_validation_rejects_nonfinite_and_unsorted()
    {
        const std::array unsorted{ TES3MP::ActorCombatSnapshot{ value<TES3MP::ActorId>(2),
            TES3MP::CombatRevision::initial(), 1.f, 1.f, false },
            TES3MP::ActorCombatSnapshot{ value<TES3MP::ActorId>(1), TES3MP::CombatRevision::initial(),
                1.f, 1.f, false } };
        const auto invalid = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(),
            TES3MP::CanonicalRevision::initial(), value<TES3MP::PlayerId>(1),
            TES3MP::CombatRevision::initial(), std::numeric_limits<float>::quiet_NaN(), {});
        const auto order = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(),
            TES3MP::CanonicalRevision::initial(), value<TES3MP::PlayerId>(1),
            TES3MP::CombatRevision::initial(), 1.f, unsorted);
        return std::holds_alternative<TES3MP::CombatReplicationDecodeError>(invalid)
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(order);
    }

    bool frame_classes_are_pinned()
    {
        const auto command = TES3MP::messageDescriptor(TES3MP::MessageKind::ClientMeleeAttackCommand);
        const auto event = TES3MP::messageDescriptor(TES3MP::MessageKind::ReliableCombatEventBatch);
        const auto snapshot = TES3MP::messageDescriptor(TES3MP::MessageKind::LatestWinsCombatSnapshot);
        return command && event && snapshot && command->messageClass == TES3MP::MessageClass::ReliableOperation
            && event->messageClass == TES3MP::MessageClass::ReliableOperation
            && snapshot->messageClass == TES3MP::MessageClass::LatestWinsSnapshot;
    }
}

int main()
{
    return command_round_trips_and_is_bounded() && snapshots_and_events_round_trip()
            && semantic_validation_rejects_nonfinite_and_unsorted() && frame_classes_are_pinned()
        ? 0
        : 1;
}
