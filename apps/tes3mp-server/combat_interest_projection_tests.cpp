#include "actor_interest_projection.hpp"
#include "combat_interest_projection.hpp"

#include <array>
#include <string_view>
#include <iostream>
#include <variant>
#include <vector>

namespace
{
    using namespace TES3MP;
    template <class T>
    T id(std::uint64_t value)
    {
        return *T::fromValue(value);
    }
    Transform root(std::uint64_t cell)
    {
        const auto zero = Turn32::fromValue(0);
        return Transform(CellId::interior(id<CellSpaceId>(cell)), Position3(0, 0, 0), Orientation3(zero, zero, zero));
    }

    bool combat_projection_is_private_and_cell_scoped()
    {
        const std::array players{ CanonicalPlayerEntityState(id<PlayerId>(1), id<EntityId>(10), id<AppearanceId>(1),
            root(7), LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(),
            ServerTick::initial()) };
        const std::array sessions{ CanonicalSessionProgress(
            id<SessionId>(2), SessionGeneration::initial(), id<PlayerId>(1), id<EntityId>(10), std::nullopt) };
        const auto canonical = std::get<CanonicalServerState>(createCanonicalServerState(players, sessions));
        const std::array actorStates{ CanonicalActorEntityState(id<ActorId>(3), id<EntityId>(30),
                                          id<ActorPrototypeId>(1), root(7), LinearVelocity3(0, 0, 0),
                                          EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial(),
                                          ActorActivity::Idle, 0),
            CanonicalActorEntityState(id<ActorId>(4), id<EntityId>(40), id<ActorPrototypeId>(1), root(8),
                LinearVelocity3(0, 0, 0), EntityRevision::initial(), AuthorityEpoch::initial(), ServerTick::initial(),
                ActorActivity::Idle, 0) };
        const auto spatial = std::get<CanonicalActorWorld>(createCanonicalActorWorld(actorStates));
        OpenMwMeleeAttacker attacker;
        attacker.fatigue = 75.f;
        OpenMwMeleeVictim playerVictim;
        playerVictim.health = 60.f;
        playerVictim.fatigue = 75.f;
        CanonicalPlayerCombatState combatPlayer{ .playerId = id<PlayerId>(1),
            .revision = id<CombatRevision>(5),
            .stats = attacker,
            .maximumEncumbranceWeightUnits = 100,
            .victim = playerVictim,
            .respawnVictim = playerVictim,
            .maximumHealth = 80.f,
            .maximumFatigue = 100.f };
        combatPlayer.magicka = 40.f;
        combatPlayer.maximumMagicka = 50.f;
        combatPlayer.blockSkill = 15.f;
        combatPlayer.armorSkills = { 21.f, 22.f, 23.f, 24.f };
        combatPlayer.securitySkill = 42.f;
        combatPlayer.activeMagicEffects.push_back(CanonicalActiveMagicEffect{ id<ActiveMagicEffectId>(1),
            id<PlayerId>(1), MagicUseSourceKind::Spell, 20, 0, DirectMagicEffectKind::RestoreHealth, 2.f,
            id<ServerTick>(8), id<ServerTick>(20), id<ServerTick>(9) });
        combatPlayer.skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Block)].progress = 0.25f;
        combatPlayer.skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Unarmored)].progress = 0.75f;
        combatPlayer.skillProgression[static_cast<std::size_t>(CombatProgressionSkill::Security)].progress = 0.5f;
        const std::array combatPlayers{ combatPlayer };
        OpenMwMeleeVictim first;
        first.health = 40.f;
        OpenMwMeleeVictim second;
        second.health = 90.f;
        CanonicalActorCombatState firstActor{ .actorId = id<ActorId>(3),
                                           .revision = id<CombatRevision>(6),
                                           .stats = first,
                                           .respawnStats = first,
                                           .maximumHealth = 50.f,
                                           .maximumFatigue = 0.f };
        firstActor.activeMagicEffects.push_back(CanonicalActiveMagicEffect{ id<ActiveMagicEffectId>(2),
            id<PlayerId>(1), MagicUseSourceKind::Spell, 20, 0, DirectMagicEffectKind::DamageHealth, 3.f,
            id<ServerTick>(8), id<ServerTick>(20), id<ServerTick>(9) });
        CanonicalActorCombatState secondActor{ .actorId = id<ActorId>(4),
                .revision = id<CombatRevision>(7),
                .stats = second,
                .respawnStats = second,
                .maximumHealth = 100.f,
                .maximumFatigue = 0.f };
        secondActor.activeMagicEffects.push_back(CanonicalActiveMagicEffect{ id<ActiveMagicEffectId>(3),
            id<PlayerId>(1), MagicUseSourceKind::Spell, 20, 0, DirectMagicEffectKind::DamageHealth, 4.f,
            id<ServerTick>(8), id<ServerTick>(20), id<ServerTick>(9) });
        const std::array combatActors{ firstActor, secondActor };
        const auto random = Xoshiro256StarStar::fromWorldSeed(1, *RandomStreamKey::fromValues(1, 1)).snapshot();
        const auto combat
            = std::get<CanonicalCombatWorld>(createCanonicalCombatWorld(combatPlayers, combatActors, random,
                std::nullopt, id<ActiveMagicEffectId>(4)));
        const AuthoritativeMeleeEvent visibleEvent{ id<ServerTick>(9), id<PlayerId>(1), id<ActorId>(3),
            id<CombatRevision>(5), id<CombatRevision>(6), OpenMwMeleeResolution{ .damage = 10.f, .hit = true } };
        const AuthoritativeMeleeEvent hiddenEvent{ id<ServerTick>(9), id<PlayerId>(1), id<ActorId>(4),
            id<CombatRevision>(5), id<CombatRevision>(7), OpenMwMeleeResolution{ .damage = 5.f, .hit = true } };
        const std::array events{ visibleEvent, hiddenEvent };
        const std::array actorEvents{ AuthoritativeActorMeleeEvent{ id<ServerTick>(9), id<ActorId>(3), id<PlayerId>(1),
            id<CombatRevision>(8), id<CombatRevision>(9), OpenMwMeleeResolution{ .damage = 4.f, .hit = true } } };
        const std::array magicEffectEvents{
            AuthoritativeMagicEffectEvent{ id<ServerTick>(9), id<ActiveMagicEffectId>(2),
                AuthoritativeMagicEffectEventKind::Updated, AuthoritativeMagicEffectEndReason::None,
                MagicUseTargetKind::Actor, 3, DirectMagicEffectKind::DamageHealth, 3.f, -3.f,
                id<ServerTick>(8), id<ServerTick>(20), id<CombatRevision>(8) },
            AuthoritativeMagicEffectEvent{ id<ServerTick>(9), id<ActiveMagicEffectId>(3),
                AuthoritativeMagicEffectEventKind::Updated, AuthoritativeMagicEffectEndReason::None,
                MagicUseTargetKind::Actor, 4, DirectMagicEffectKind::DamageHealth, 4.f, -4.f,
                id<ServerTick>(8), id<ServerTick>(20), id<CombatRevision>(9) } };
        auto snapshot = TES3MP::ServerApp::projectCombatSnapshot(
            canonical, spatial, combat, id<SessionId>(2), id<ServerTick>(9), id<CanonicalRevision>(4));
        auto batch = TES3MP::ServerApp::projectCombatEvents(
            canonical, spatial, id<SessionId>(2), id<ServerTick>(9), id<CanonicalRevision>(4), events, actorEvents, {},
            magicEffectEvents);
        return snapshot && snapshot->selfPlayerId() == id<PlayerId>(1) && snapshot->selfHealth() == 60.f
            && snapshot->selfMaximumHealth() == 80.f && snapshot->selfFatigue() == 75.f
            && snapshot->selfMaximumFatigue() == 100.f && snapshot->selfMagicka() == 40.f
            && snapshot->selfMaximumMagicka() == 50.f && snapshot->selfSkills().size() == ReplicatedCombatSkillCount
            && snapshot->selfSkills()[0] == CombatSkillSnapshot{ ReplicatedCombatSkill::Block, 15.f, 0.25f }
        && snapshot->selfSkills()[10] == CombatSkillSnapshot{ ReplicatedCombatSkill::Unarmored, 24.f, 0.75f }
        && snapshot->selfSkills()[11] == CombatSkillSnapshot{ ReplicatedCombatSkill::Security, 42.f, 0.5f }
        && !snapshot->selfDead() && snapshot->actors().size() == 1 && snapshot->actors()[0].actorId == id<ActorId>(3)
            && snapshot->actors()[0].maximumHealth == 50.f && snapshot->activeEffects().size() == 2
            && snapshot->activeEffects()[0].instanceId == id<ActiveMagicEffectId>(1)
            && snapshot->activeEffects()[1].instanceId == id<ActiveMagicEffectId>(2) && batch
            && batch->events().size() == 1
            && batch->events()[0].targetActorId == id<ActorId>(3) && batch->actorEvents().size() == 1
            && batch->actorEvents()[0].attackerActorId == id<ActorId>(3) && batch->magicEffectEvents().size() == 1
            && batch->magicEffectEvents()[0].instanceId == id<ActiveMagicEffectId>(2);
    }

    class RecordingTransport final : public TransportRuntime
    {
    public:
        TransportAdmission<ListenerId> startListener(const ListenerEndpoint&) override { return {}; }
        TransportResult stopListener(ListenerId) override { return TransportResult::Accepted; }
        TransportAdmission<ConnectAttemptId> connect(const ConnectionEndpoint&) override { return {}; }
        TransportResult cancelConnect(ConnectAttemptId) override { return TransportResult::Accepted; }
        TransportResult send(TransportConnectionId, TransportChannel channel, std::span<const std::byte> bytes) override
        {
            sent.push_back({ channel, { bytes.begin(), bytes.end() } });
            return TransportResult::Accepted;
        }
        TransportReceiveResult receive(TransportConnectionId, std::span<TransportMessage>) override { return {}; }
        TransportResult close(TransportConnectionId, TransportCloseMode) override { return TransportResult::Accepted; }
        TransportPollResult poll(std::span<TransportEvent>) override { return {}; }
        TransportResult shutdown() override { return TransportResult::Accepted; }
        std::vector<TransportMessage> sent;
    };

    bool dialogue_result_admission_is_typed_atomic_and_backpressured()
    {
        const auto policy = *OutboundQueuePolicy::create(1, 4096, 1, 1, 1, 1, 1, 1, 8, 250);
        const auto connection = TransportConnectionId::initial();
        auto queues = *OutboundQueueSet::create(policy, 1);
        if (queues.attach(connection) != TransportResult::Accepted)
            return false;
        const ReliableDialogueChoiceResult result{ id<SessionId>(2), SessionGeneration::initial(),
            CommandSequence::initial(), id<CommandId>(1), id<DialogueChoiceId>(40),
            DialogueChoiceDisposition::Ineligible, false, id<CanonicalRevision>(3) };
        const std::vector<std::pair<TransportConnectionId, ReliableDialogueChoiceResult>> delivery{ { connection,
            result } };
        if (!ServerApp::admitCombinedInterestTickAtomically(queues, {}, {}, {}, {}, {}, {}, {}, {}, delivery))
            return false;
        RecordingTransport transport;
        if (queues.pump(transport, connection, 0) != OutboundPumpResult::Progress || transport.sent.size() != 1)
            return false;
        const auto frame = decodeProtocolFrame(transport.sent.front().bytes);
        const auto* decodedFrame = std::get_if<DecodedFrame>(&frame);
        const auto decoded = decodedFrame ? decodeReliableDialogueChoiceResult(decodedFrame->payload())
                                          : ReliableDialogueChoiceResultDecodeResult{ DialogueChoiceProtocolError{} };
        if (!decodedFrame || decodedFrame->messageKind() != MessageKind::ReliableDialogueChoiceResult
            || !std::holds_alternative<ReliableDialogueChoiceResult>(decoded)
            || std::get<ReliableDialogueChoiceResult>(decoded) != result)
            return false;

        const std::array<std::byte, 1> occupied{ std::byte{ 1 } };
        if (queues.enqueue(connection, TransportChannel::ReliableOrdered, occupied) != TransportResult::Accepted
            || ServerApp::admitCombinedInterestTickAtomically(queues, {}, {}, {}, {}, {}, {}, {}, {}, delivery))
            return false;
        return queues.hasPending(connection) == std::optional<bool>(true);
    }

    bool magic_only_combat_batch_is_admitted(bool presentationOnly = false, size_t cueCount = 1)
    {
        const auto policy = *OutboundQueuePolicy::create(cueCount > 1 ? 16 : 1,
            cueCount > 1 ? 128 * 1024 : 4096, cueCount > 1 ? 16 : 1,
            cueCount > 1 ? 8 : 1, cueCount > 1 ? 16 : 1, 1, 1, 1, 16, 250);
        const auto connection = TransportConnectionId::initial();
        auto queues = *OutboundQueueSet::create(policy, 1);
        if (queues.attach(connection) != TransportResult::Accepted)
            return false;
        const std::array magicEvents{ MagicUseCombatEvent{ id<PlayerId>(1), MagicUseSourceKind::EnchantedItem, 3,
            MagicUseTargetKind::Actor, 4, id<CombatRevision>(5), id<CombatRevision>(6), true, 0.f, 0.f, 0.f, 0.f, -9.f,
            0.f, false } };
        MagicImpactCue cue{1, 0, 1, 1, 9, "fireball", {}, 3};
        cue.presentation = 3;
        if (cueCount > 1) cue.record = std::string(256, 'x');
        const std::vector<MagicImpactCue> cues(cueCount, cue);
        auto created = ReliableCombatEventBatch::create(id<SessionId>(2), SessionGeneration::initial(),
            id<ServerTick>(7), id<CanonicalRevision>(8), {}, {},
            presentationOnly ? std::span<const MagicUseCombatEvent>{} : magicEvents, {},
            presentationOnly ? std::span<const MagicImpactCue>{cues} : std::span<const MagicImpactCue>{});
        const auto* batch = std::get_if<ReliableCombatEventBatch>(&created);
        if (!batch)
            return false;
        const std::vector<std::pair<TransportConnectionId, ReliableCombatEventBatch>> delivery{ { connection,
            *batch } };
        if (!ServerApp::admitCombinedInterestTickAtomically(queues, {}, {}, {}, {}, {}, {}, {}, delivery))
            return false;
        RecordingTransport transport;
        if (queues.pump(transport, connection, 0) != OutboundPumpResult::Progress
            || transport.sent.size() != (cueCount > 1 ? 8 : 1))
            return false;
        if (cueCount > 1)
        {
            size_t received = 0;
            for (const auto& message : transport.sent)
            {
                const auto frame = decodeProtocolFrame(message.bytes);
                const auto* typed = std::get_if<DecodedFrame>(&frame);
                if (!typed || typed->payload().size() > ReliableOperationMaximumPayloadBytes) return false;
                const auto parsed = decodeReliableCombatEventBatch(typed->payload());
                const auto* chunk = std::get_if<ReliableCombatEventBatch>(&parsed);
                if (!chunk || !chunk->magicEvents().empty()) return false;
                for (const auto& actual : chunk->magicImpactCues()) if (actual != cue) return false;
                received += chunk->magicImpactCues().size();
            }
            return received == cueCount;
        }
        const auto frame = decodeProtocolFrame(transport.sent.front().bytes);
        const auto* decodedFrame = std::get_if<DecodedFrame>(&frame);
        const auto decoded = decodedFrame
            ? decodeReliableCombatEventBatch(decodedFrame->payload())
            : ReliableCombatEventBatch::create(
                  id<SessionId>(2), SessionGeneration::initial(), id<ServerTick>(7), id<CanonicalRevision>(8), {});
        const auto* decodedBatch = std::get_if<ReliableCombatEventBatch>(&decoded);
        return decodedFrame && decodedFrame->messageKind() == MessageKind::ReliableCombatEventBatch && decodedBatch
            && (presentationOnly ? decodedBatch->magicEvents().empty() && decodedBatch->magicImpactCues().size() == 1
                    && decodedBatch->magicImpactCues()[0] == cue
                : decodedBatch->magicEvents().size() == 1 && decodedBatch->magicEvents()[0] == magicEvents[0]);
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "personal-dialogue")
    {
        const auto a = TransportConnectionId::initial(), b = *TransportConnectionId::fromValue(2);
        auto queues = *OutboundQueueSet::create(*OutboundQueuePolicy::create(1, 4096, 1, 1, 1, 1, 1, 1, 8, 250), 2);
        if (queues.attach(a) != TransportResult::Accepted || queues.attach(b) != TransportResult::Accepted)
            return 1;
        ReliablePersonalDialogue alice{ id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(7),
            id<CanonicalRevision>(8) };
        alice.story = 71;
        alice.revision = 1;
        alice.command = 3;
        alice.speaker = 41;
        alice.life = 3;
        alice.topic = "passage";
        alice.response = "Alice only";
        auto bob = alice;
        bob.targetSessionId = id<SessionId>(2);
        bob.response = "Bob only";
        const std::vector<std::pair<TransportConnectionId, ReliablePersonalDialogue>> both{ { a, alice }, { b, bob } };
        const auto admit = [&] {
            return ServerApp::admitCombinedInterestTickAtomically(
                queues, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, both);
        };
        const std::array occupied{ std::byte{ 1 } };
        if (queues.enqueue(b, TransportChannel::ReliableOrdered, occupied) != TransportResult::Accepted || admit()
            || queues.hasPending(a) != std::optional(false))
            return 1;
        RecordingTransport transport;
        queues.pump(transport, b, 0);
        transport.sent.clear();
        if (!admit())
            return 1;
        queues.pump(transport, a, 1);
        queues.pump(transport, b, 1);
        if (transport.sent.size() != 2)
            return 1;
        for (size_t i = 0; i < 2; ++i)
        {
            const auto frame = decodeProtocolFrame(transport.sent[i].bytes);
            const auto* value = std::get_if<DecodedFrame>(&frame);
            if (!value || decodeReliablePersonalDialogue(value->payload()) != (i ? bob : alice))
                return 1;
        }
        std::cout << "PASS personal dialogue: private two-recipient reliable admission and atomic backpressure\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "ambient")
    {
        const auto a = TransportConnectionId::initial(), b = *TransportConnectionId::fromValue(2);
        auto queues = *OutboundQueueSet::create(*OutboundQueuePolicy::create(1, 4096, 1, 1, 1, 1, 1, 1, 8, 250), 2);
        if (queues.attach(a) != TransportResult::Accepted || queues.attach(b) != TransportResult::Accepted) return 1;
        const ReliableAmbientBatch batch{id<SessionId>(1), SessionGeneration::initial(), id<ServerTick>(7),
            id<CanonicalRevision>(8), 24, "arbitrary_bell", {"Committed bell"}};
        const std::vector<std::pair<TransportConnectionId, ReliableAmbientBatch>> both{{a, batch}, {b, batch}};
        const auto admit = [&] { return ServerApp::admitCombinedInterestTickAtomically(queues, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, both); };
        const std::array occupied{std::byte{1}};
        if (queues.enqueue(b, TransportChannel::ReliableOrdered, occupied) != TransportResult::Accepted
            || admit() || queues.hasPending(a) != std::optional<bool>(false)) return 1;
        RecordingTransport transport;
        queues.pump(transport, b, 0); transport.sent.clear();
        if (!admit()) return 1;
        queues.pump(transport, a, 1); queues.pump(transport, b, 1);
        if (transport.sent.size() != 2) return 1;
        for (const auto& message : transport.sent)
        {
            const auto frame = decodeProtocolFrame(message.bytes);
            const auto* value = std::get_if<DecodedFrame>(&frame);
            if (!value || value->messageKind() != MessageKind::ReliableAmbientBatch
                || decodeReliableAmbientBatch(value->payload()) != batch) return 1;
        }
        std::cout << "PASS ambient: two-recipient reliable admission and atomic backpressure\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "spell-cues")
    {
        if (!magic_only_combat_batch_is_admitted(true))
        { std::cerr << "FAIL presentation-only admission\n"; return 1; }
        if (!magic_only_combat_batch_is_admitted(true, MaximumReplicatedMagicImpactCues))
        { std::cerr << "FAIL bounded presentation chunk admission\n"; return 1; }
        std::cout << "PASS presentation-only and maximum-size chunked admission\n";
        return 0;
    }
    return combat_projection_is_private_and_cell_scoped()
            && dialogue_result_admission_is_typed_atomic_and_backpressured() && magic_only_combat_batch_is_admitted()
        ? 0
        : 1;
}
