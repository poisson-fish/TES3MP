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
    inline constexpr std::size_t ReplicatedCombatSkillCount = 7;

    enum class ReplicatedCombatSkill : std::uint8_t
    {
        Block = 0,
        ShortBlade = 1,
        LongBlade = 2,
        BluntWeapon = 3,
        Axe = 4,
        Spear = 5,
        HandToHand = 6,
    };

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
        InvalidSkill,
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
        float maximumHealth = 0.f;
        float fatigue = 0.f;
        float maximumFatigue = 0.f;
        bool dead = false;
        friend constexpr bool operator==(ActorCombatSnapshot, ActorCombatSnapshot) noexcept = default;
        friend constexpr auto operator<=>(const ActorCombatSnapshot& lhs, const ActorCombatSnapshot& rhs) noexcept
        { return lhs.actorId <=> rhs.actorId; }
    };

    struct CombatSkillSnapshot
    {
        ReplicatedCombatSkill skill = ReplicatedCombatSkill::Block;
        float value = 0.f;
        float progress = 0.f;
        friend constexpr bool operator==(CombatSkillSnapshot, CombatSkillSnapshot) noexcept = default;
    };

    class LatestWinsCombatSnapshot
    {
    public:
        static std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision, PlayerId self,
            CombatRevision selfRevision, float selfHealth, float selfMaximumHealth, float selfFatigue,
            float selfMaximumFatigue, float selfMagicka, float selfMaximumMagicka, bool selfDead,
            std::span<const ActorCombatSnapshot> actors, std::span<const CombatSkillSnapshot> skills);
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        PlayerId selfPlayerId() const noexcept { return mSelf; }
        CombatRevision selfCombatRevision() const noexcept { return mSelfRevision; }
        float selfHealth() const noexcept { return mSelfHealth; }
        float selfMaximumHealth() const noexcept { return mSelfMaximumHealth; }
        float selfFatigue() const noexcept { return mSelfFatigue; }
        float selfMaximumFatigue() const noexcept { return mSelfMaximumFatigue; }
        float selfMagicka() const noexcept { return mSelfMagicka; }
        float selfMaximumMagicka() const noexcept { return mSelfMaximumMagicka; }
        bool selfDead() const noexcept { return mSelfDead; }
        std::span<const ActorCombatSnapshot> actors() const noexcept { return mActors; }
        std::span<const CombatSkillSnapshot> selfSkills() const noexcept { return mSkills; }
        friend bool operator==(const LatestWinsCombatSnapshot&, const LatestWinsCombatSnapshot&) noexcept = default;
    private:
        LatestWinsCombatSnapshot(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, PlayerId self, CombatRevision selfRevision, float selfHealth,
            float selfMaximumHealth, float selfFatigue, float selfMaximumFatigue, float selfMagicka,
            float selfMaximumMagicka, bool selfDead, std::vector<ActorCombatSnapshot> actors,
            std::vector<CombatSkillSnapshot> skills) : mSession(session), mGeneration(generation), mTick(tick),
            mCanonicalRevision(canonicalRevision), mSelf(self), mSelfRevision(selfRevision), mSelfFatigue(selfFatigue),
            mSelfMaximumFatigue(selfMaximumFatigue), mSelfHealth(selfHealth),
            mSelfMaximumHealth(selfMaximumHealth), mSelfMagicka(selfMagicka),
            mSelfMaximumMagicka(selfMaximumMagicka), mSelfDead(selfDead), mActors(std::move(actors)),
            mSkills(std::move(skills)) {}
        SessionId mSession; SessionGeneration mGeneration; ServerTick mTick; CanonicalRevision mCanonicalRevision;
        PlayerId mSelf; CombatRevision mSelfRevision; float mSelfFatigue; float mSelfMaximumFatigue;
        float mSelfHealth; float mSelfMaximumHealth;
        float mSelfMagicka; float mSelfMaximumMagicka; bool mSelfDead;
        std::vector<ActorCombatSnapshot> mActors;
        std::vector<CombatSkillSnapshot> mSkills;
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

    struct ActorMeleeCombatEvent
    {
        ActorId attackerActorId;
        PlayerId targetPlayerId;
        CombatRevision attackerCombatRevision = CombatRevision::initial();
        CombatRevision targetCombatRevision = CombatRevision::initial();
        float damage = 0.f;
        MeleeDamageStat damagedStat = MeleeDamageStat::Health;
        bool hit = false;
        bool blocked = false;
        bool targetDied = false;
        friend constexpr bool operator==(ActorMeleeCombatEvent, ActorMeleeCombatEvent) noexcept = default;
    };

    class ReliableCombatEventBatch
    {
    public:
        static std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision,
            std::span<const MeleeCombatEvent> events,
            std::span<const ActorMeleeCombatEvent> actorEvents = {});
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const MeleeCombatEvent> events() const noexcept { return mEvents; }
        std::span<const ActorMeleeCombatEvent> actorEvents() const noexcept { return mActorEvents; }
        friend bool operator==(const ReliableCombatEventBatch&, const ReliableCombatEventBatch&) noexcept = default;
    private:
        ReliableCombatEventBatch(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision revision, std::vector<MeleeCombatEvent> events,
            std::vector<ActorMeleeCombatEvent> actorEvents) : mSession(session),
            mGeneration(generation), mTick(tick), mCanonicalRevision(revision), mEvents(std::move(events)),
            mActorEvents(std::move(actorEvents)) {}
        SessionId mSession; SessionGeneration mGeneration; ServerTick mTick; CanonicalRevision mCanonicalRevision;
        std::vector<MeleeCombatEvent> mEvents;
        std::vector<ActorMeleeCombatEvent> mActorEvents;
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
