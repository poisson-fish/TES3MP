#include <tes3mp/combat_replication.hpp>
#include <tes3mp/protocol_frame.hpp>
#include "../protocol/generated/reliable_combat_event_batch_generated.h"

#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace
{
    template <class T>
    T value(std::uint64_t raw)
    {
        return *T::fromValue(raw);
    }

    std::array<TES3MP::CombatSkillSnapshot, TES3MP::ReplicatedCombatSkillCount> skills()
    {
        std::array<TES3MP::CombatSkillSnapshot, TES3MP::ReplicatedCombatSkillCount> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = { static_cast<TES3MP::ReplicatedCombatSkill>(index), 10.f + index, 0.1f };
        return result;
    }

    bool command_round_trips_and_is_bounded()
    {
        const TES3MP::ClientMeleeAttackCommand input{ value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(),
            TES3MP::CommandSequence::initial(), value<TES3MP::CommandId>(3), value<TES3MP::CanonicalRevision>(4),
            value<TES3MP::ActorId>(5), value<TES3MP::ServerTick>(6), TES3MP::CombatRevision::initial(),
            value<TES3MP::CombatRevision>(2), TES3MP::MeleeAttackType::Thrust, 0.75f };
        const auto bytes = TES3MP::encodeClientMeleeAttackCommand(input);
        const auto decoded = TES3MP::decodeClientMeleeAttackCommand(bytes);
        auto truncated = bytes;
        truncated.pop_back();
        auto empty = input;
        empty.targetActorId.reset();
        empty.expectedTargetRevision = TES3MP::CombatRevision::initial();
        const auto decodedEmpty = TES3MP::decodeClientMeleeAttackCommand(TES3MP::encodeClientMeleeAttackCommand(empty));
        return std::get<TES3MP::ClientMeleeAttackCommand>(decoded) == input
            && std::get<TES3MP::ClientMeleeAttackCommand>(decodedEmpty) == empty
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(
                TES3MP::decodeClientMeleeAttackCommand(truncated));
    }

    bool magic_command_round_trips_and_is_bounded()
    {
        const TES3MP::ClientMagicUseCommand input{ value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(),
            TES3MP::CommandSequence::initial(), value<TES3MP::CommandId>(3), value<TES3MP::CanonicalRevision>(4),
            TES3MP::MagicUseSourceKind::EnchantedItem, 12, TES3MP::MagicUseTargetKind::Player, 7,
            value<TES3MP::ServerTick>(6), value<TES3MP::CombatRevision>(2), value<TES3MP::CombatRevision>(3),
            value<TES3MP::InventoryRevision>(4) };
        const auto bytes = TES3MP::encodeClientMagicUseCommand(input);
        const auto decoded = TES3MP::decodeClientMagicUseCommand(bytes);
        auto truncated = bytes;
        truncated.pop_back();
        auto invalidTarget = input;
        invalidTarget.targetKind = TES3MP::MagicUseTargetKind::Self;
        return std::get<TES3MP::ClientMagicUseCommand>(decoded) == input
            && std::holds_alternative<TES3MP::MagicUseDecodeError>(TES3MP::decodeClientMagicUseCommand(truncated))
            && std::get<TES3MP::MagicUseDecodeError>(
                   TES3MP::decodeClientMagicUseCommand(TES3MP::encodeClientMagicUseCommand(invalidTarget)))
                   .code
            == TES3MP::MagicUseDecodeErrorCode::InvalidTarget;
    }

    bool snapshots_and_events_round_trip()
    {
        const auto selfSkills = skills();
        const std::array actors{ TES3MP::ActorCombatSnapshot{
            value<TES3MP::ActorId>(2), value<TES3MP::CombatRevision>(3), 40.f, 50.f, 20.f, 30.f, 15.f, 25.f, false } };
        const std::array players{ TES3MP::PlayerCombatSnapshot{
            value<TES3MP::PlayerId>(8), value<TES3MP::CombatRevision>(4), 60.f, 80.f, 30.f, 90.f, 20.f, 40.f, false } };
        const std::array activeEffects{ TES3MP::ActiveMagicEffectSnapshot{ value<TES3MP::ActiveMagicEffectId>(3),
            value<TES3MP::PlayerId>(7), TES3MP::MagicUseSourceKind::Spell, 22,
            TES3MP::MagicUseTargetKind::Player, 8, TES3MP::DirectMagicEffectKind::FireDamage, 4.f,
            value<TES3MP::ServerTick>(5), value<TES3MP::ServerTick>(15) } };
        auto created = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), value<TES3MP::ServerTick>(5), value<TES3MP::CanonicalRevision>(6),
            value<TES3MP::PlayerId>(7), value<TES3MP::CombatRevision>(8), 75.f, 100.f, 90.f, 120.f, 40.f, 60.f, false,
            actors, selfSkills, players, activeEffects);
        const auto snapshot = std::get<TES3MP::LatestWinsCombatSnapshot>(created);
        const auto decodedSnapshot
            = TES3MP::decodeLatestWinsCombatSnapshot(TES3MP::encodeLatestWinsCombatSnapshot(snapshot));
        const std::array events{ TES3MP::MeleeCombatEvent{ value<TES3MP::PlayerId>(7), value<TES3MP::ActorId>(2),
            value<TES3MP::CombatRevision>(8), value<TES3MP::CombatRevision>(3), 10.f, TES3MP::MeleeDamageStat::Health,
            true, false, false } };
        const std::array actorEvents{ TES3MP::ActorMeleeCombatEvent{ value<TES3MP::ActorId>(2),
            value<TES3MP::PlayerId>(7), value<TES3MP::CombatRevision>(4), value<TES3MP::CombatRevision>(9), 4.f,
            TES3MP::MeleeDamageStat::Health, true, false, false } };
        const std::array magicEvents{ TES3MP::MagicUseCombatEvent{ value<TES3MP::PlayerId>(7),
            TES3MP::MagicUseSourceKind::Spell, 22, TES3MP::MagicUseTargetKind::Player, 8,
            value<TES3MP::CombatRevision>(9), value<TES3MP::CombatRevision>(5), true, 0.f, 5.f, -10.f, -7.f, -3.f, -2.f,
            false } };
        const std::array magicEffectEvents{ TES3MP::MagicEffectCombatEvent{
            value<TES3MP::ActiveMagicEffectId>(3), TES3MP::MagicEffectCombatEventKind::Updated,
            TES3MP::MagicEffectCombatEndReason::None, TES3MP::MagicUseTargetKind::Player, 8,
            TES3MP::DirectMagicEffectKind::FireDamage, 4.f, -0.064f, value<TES3MP::ServerTick>(5),
            value<TES3MP::ServerTick>(15), value<TES3MP::CombatRevision>(5) } };
        auto batch = std::get<TES3MP::ReliableCombatEventBatch>(
            TES3MP::ReliableCombatEventBatch::create(value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(),
                value<TES3MP::ServerTick>(5), value<TES3MP::CanonicalRevision>(6), events, actorEvents, magicEvents,
                magicEffectEvents));
        const auto decodedBatch = TES3MP::decodeReliableCombatEventBatch(TES3MP::encodeReliableCombatEventBatch(batch));
        return std::get<TES3MP::LatestWinsCombatSnapshot>(decodedSnapshot) == snapshot
            && std::get<TES3MP::ReliableCombatEventBatch>(decodedBatch) == batch;
    }

    bool semantic_validation_rejects_nonfinite_and_unsorted()
    {
        const auto selfSkills = skills();
        auto invalidSkills = selfSkills;
        invalidSkills[0].progress = 1.f;
        const std::array unsorted{ TES3MP::ActorCombatSnapshot{ value<TES3MP::ActorId>(2),
                                       TES3MP::CombatRevision::initial(), 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false },
            TES3MP::ActorCombatSnapshot{
                value<TES3MP::ActorId>(1), TES3MP::CombatRevision::initial(), 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false } };
        const auto invalid = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(), TES3MP::CanonicalRevision::initial(),
            value<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), 1.f, 1.f,
            std::numeric_limits<float>::quiet_NaN(), 1.f, 1.f, 1.f, false, {}, selfSkills);
        const auto order = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(), TES3MP::CanonicalRevision::initial(),
            value<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false,
            unsorted, selfSkills);
        const auto magicka = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(), TES3MP::CanonicalRevision::initial(),
            value<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), 1.f, 1.f, 1.f, 1.f, 2.f, 1.f, false, {},
            selfSkills);
        const auto progression = TES3MP::LatestWinsCombatSnapshot::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(), TES3MP::CanonicalRevision::initial(),
            value<TES3MP::PlayerId>(1), TES3MP::CombatRevision::initial(), 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false, {},
            invalidSkills);
        const std::array invalidLifecycle{ TES3MP::MagicEffectCombatEvent{
            value<TES3MP::ActiveMagicEffectId>(3), TES3MP::MagicEffectCombatEventKind::Ended,
            TES3MP::MagicEffectCombatEndReason::None, TES3MP::MagicUseTargetKind::Player, 2,
            TES3MP::DirectMagicEffectKind::DamageHealth, 1.f, 0.f, value<TES3MP::ServerTick>(1),
            value<TES3MP::ServerTick>(2), TES3MP::CombatRevision::initial() } };
        const auto lifecycle = TES3MP::ReliableCombatEventBatch::create(value<TES3MP::SessionId>(1),
            TES3MP::SessionGeneration::initial(), TES3MP::ServerTick::initial(),
            TES3MP::CanonicalRevision::initial(), {}, {}, {}, invalidLifecycle);
        return std::holds_alternative<TES3MP::CombatReplicationDecodeError>(invalid)
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(order)
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(magicka)
            && std::holds_alternative<TES3MP::CombatReplicationDecodeError>(progression)
            && std::get<TES3MP::CombatReplicationDecodeError>(lifecycle).code
                == TES3MP::CombatReplicationDecodeErrorCode::InvalidMagicEffect;
    }

    bool actor_casts_reject_malformed_wire_identity()
    {
        namespace Wire = TES3MP::Protocol::Schema::CombatEvent;
        const auto decode = [](uint64_t caster, uint64_t life, uint8_t kind) {
            flatbuffers::FlatBufferBuilder builder;
            const auto header = Wire::CreateCombatEventHeader(builder, 1, 1, 1, 1);
            const std::vector<Wire::MagicUseCombatEvent> events{
                {caster, life, 42, 7, 1, 1, 0, 0, -5, -2, 0, 0,
                    Wire::MagicUseSourceKind::Spell, Wire::MagicUseTargetKind::Player, true, false, kind}};
            const auto root = Wire::CreateReliableCombatEventBatch(builder, header, 0, 0, builder.CreateVectorOfStructs(events));
            Wire::FinishSizePrefixedReliableCombatEventBatchBuffer(builder, root);
            return TES3MP::decodeReliableCombatEventBatch({reinterpret_cast<const std::byte*>(builder.GetBufferPointer()), builder.GetSize()});
        };
        for (const auto& [id, life, kind] : std::array<std::tuple<uint64_t, uint64_t, uint8_t>, 6>{
                {{0, 1, 2}, {7, 0, 2}, {7, 1, 0}, {7, 1, 3}, {7, 1, 255}, {7, 2, 1}}})
            if (!std::holds_alternative<TES3MP::CombatReplicationDecodeError>(decode(id, life, kind))) return false;
        const auto actor = decode(7, 3, 2);
        const auto player = decode(7, 1, 1);
        if (!std::holds_alternative<TES3MP::ReliableCombatEventBatch>(actor)
            || !std::holds_alternative<TES3MP::ReliableCombatEventBatch>(player)) return false;
        const auto& a = std::get<TES3MP::ReliableCombatEventBatch>(actor);
        const auto& b = std::get<TES3MP::ReliableCombatEventBatch>(player);
        if (!a.magicEvents()[0].actorCaster() || a.magicEvents()[0].casterLife != 3
            || b.magicEvents()[0].actorCaster() || a.magicEvents()[0].caster == b.magicEvents()[0].caster) return false;
        const std::array mixed{a.magicEvents()[0], b.magicEvents()[0]};
        auto batch = std::get<TES3MP::ReliableCombatEventBatch>(TES3MP::ReliableCombatEventBatch::create(
            value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(), value<TES3MP::ServerTick>(1),
            value<TES3MP::CanonicalRevision>(1), {}, {}, mixed));
        auto bytes = TES3MP::encodeReliableCombatEventBatch(batch);
        if (std::get<TES3MP::ReliableCombatEventBatch>(TES3MP::decodeReliableCombatEventBatch(bytes)) != batch) return false;
        // Reject the prior struct layout at the identifier, before interpreting its fields.
        bytes[11] = std::byte('E');
        if (!std::holds_alternative<TES3MP::CombatReplicationDecodeError>(TES3MP::decodeReliableCombatEventBatch(bytes))) return false;
        auto invalid = mixed;
        invalid[0].casterLife = 0;
        return std::holds_alternative<TES3MP::CombatReplicationDecodeError>(TES3MP::ReliableCombatEventBatch::create(
            value<TES3MP::SessionId>(1), TES3MP::SessionGeneration::initial(), value<TES3MP::ServerTick>(1),
            value<TES3MP::CanonicalRevision>(1), {}, {}, invalid));
    }

    bool frame_classes_are_pinned()
    {
        const auto command = TES3MP::messageDescriptor(TES3MP::MessageKind::ClientMeleeAttackCommand);
        const auto magic = TES3MP::messageDescriptor(TES3MP::MessageKind::ClientMagicUseCommand);
        const auto event = TES3MP::messageDescriptor(TES3MP::MessageKind::ReliableCombatEventBatch);
        const auto snapshot = TES3MP::messageDescriptor(TES3MP::MessageKind::LatestWinsCombatSnapshot);
        return command && magic && event && snapshot && command->messageClass == TES3MP::MessageClass::ReliableOperation
            && magic->messageClass == TES3MP::MessageClass::ReliableOperation
            && event->messageClass == TES3MP::MessageClass::ReliableOperation
            && snapshot->messageClass == TES3MP::MessageClass::LatestWinsSnapshot;
    }
}

int main()
{
    return command_round_trips_and_is_bounded() && magic_command_round_trips_and_is_bounded()
            && snapshots_and_events_round_trip() && semantic_validation_rejects_nonfinite_and_unsorted()
            && actor_casts_reject_malformed_wire_identity() && frame_classes_are_pinned()
        ? 0
        : 1;
}
