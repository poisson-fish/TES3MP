#ifndef TES3MP_COMBAT_REPLICATION_HPP
#define TES3MP_COMBAT_REPLICATION_HPP

#include "command_primitives.hpp"
#include "melee_combat.hpp"
#include "session_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumCombatSnapshotActors = 248;
    inline constexpr std::size_t MaximumCombatEventsPerBatch = 256;

    enum class CombatReplicationDecodeErrorCode : std::uint8_t
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
        InvalidAttackType,
        InvalidDamageStat,
        InvalidFloat,
        InvalidAttackStrength,
    };

    struct CombatReplicationDecodeError
    {
        CombatReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;
        friend constexpr bool operator==(CombatReplicationDecodeError,
            CombatReplicationDecodeError) noexcept = default;
    };

    struct ClientMeleeAttackCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        std::optional<ActorId> targetActorId;
        ServerTick sourceServerTick;
        CombatRevision expectedAttackerRevision = CombatRevision::initial();
        CombatRevision expectedTargetRevision = CombatRevision::initial();
        MeleeAttackType attackType = MeleeAttackType::Chop;
        float attackStrength = 0.f;
        friend constexpr bool operator==(ClientMeleeAttackCommand, ClientMeleeAttackCommand) noexcept = default;
    };

    struct ActorCombatSnapshot
    {
        ActorId actorId;
        CombatRevision combatRevision = CombatRevision::initial();
        float health = 0.f;
        float fatigue = 0.f;
        bool dead = false;
        friend constexpr bool operator==(ActorCombatSnapshot, ActorCombatSnapshot) noexcept = default;
        friend constexpr auto operator<=>(const ActorCombatSnapshot& lhs, const ActorCombatSnapshot& rhs) noexcept
        { return lhs.actorId <=> rhs.actorId; }
    };

    class LatestWinsCombatSnapshot
    {
    public:
        static std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision, PlayerId self,
            CombatRevision selfRevision, float selfFatigue, std::span<const ActorCombatSnapshot> actors);
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        PlayerId selfPlayerId() const noexcept { return mSelf; }
        CombatRevision selfCombatRevision() const noexcept { return mSelfRevision; }
        float selfFatigue() const noexcept { return mSelfFatigue; }
        std::span<const ActorCombatSnapshot> actors() const noexcept { return mActors; }
        friend bool operator==(const LatestWinsCombatSnapshot&, const LatestWinsCombatSnapshot&) noexcept = default;
    private:
        LatestWinsCombatSnapshot(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, PlayerId self, CombatRevision selfRevision, float selfFatigue,
            std::vector<ActorCombatSnapshot> actors) : mSession(session), mGeneration(generation), mTick(tick),
            mCanonicalRevision(canonicalRevision), mSelf(self), mSelfRevision(selfRevision), mSelfFatigue(selfFatigue),
            mActors(std::move(actors)) {}
        SessionId mSession; SessionGeneration mGeneration; ServerTick mTick; CanonicalRevision mCanonicalRevision;
        PlayerId mSelf; CombatRevision mSelfRevision; float mSelfFatigue; std::vector<ActorCombatSnapshot> mActors;
    };

    struct MeleeCombatEvent
    {
        PlayerId attackerPlayerId;
        ActorId targetActorId;
        CombatRevision attackerCombatRevision = CombatRevision::initial();
        CombatRevision targetCombatRevision = CombatRevision::initial();
        float damage = 0.f;
        MeleeDamageStat damagedStat = MeleeDamageStat::Health;
        bool hit = false;
        bool blocked = false;
        bool targetDied = false;
        friend constexpr bool operator==(MeleeCombatEvent, MeleeCombatEvent) noexcept = default;
    };

    class ReliableCombatEventBatch
    {
    public:
        static std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision,
            std::span<const MeleeCombatEvent> events);
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const MeleeCombatEvent> events() const noexcept { return mEvents; }
        friend bool operator==(const ReliableCombatEventBatch&, const ReliableCombatEventBatch&) noexcept = default;
    private:
        ReliableCombatEventBatch(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision revision, std::vector<MeleeCombatEvent> events) : mSession(session),
            mGeneration(generation), mTick(tick), mCanonicalRevision(revision), mEvents(std::move(events)) {}
        SessionId mSession; SessionGeneration mGeneration; ServerTick mTick; CanonicalRevision mCanonicalRevision;
        std::vector<MeleeCombatEvent> mEvents;
    };

    std::vector<std::byte> encodeClientMeleeAttackCommand(const ClientMeleeAttackCommand& value);
    std::vector<std::byte> encodeLatestWinsCombatSnapshot(const LatestWinsCombatSnapshot& value);
    std::vector<std::byte> encodeReliableCombatEventBatch(const ReliableCombatEventBatch& value);
    std::variant<ClientMeleeAttackCommand, CombatReplicationDecodeError> decodeClientMeleeAttackCommand(
        std::span<const std::byte> payload);
    std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> decodeLatestWinsCombatSnapshot(
        std::span<const std::byte> payload);
    std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> decodeReliableCombatEventBatch(
        std::span<const std::byte> payload);
}

#endif
