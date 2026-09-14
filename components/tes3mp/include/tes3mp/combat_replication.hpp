#ifndef TES3MP_COMBAT_REPLICATION_HPP
#define TES3MP_COMBAT_REPLICATION_HPP

#include "command_primitives.hpp"
#include "direct_magic.hpp"
#include "magic_use.hpp"
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
    inline constexpr std::size_t MaximumCombatSnapshotPlayers = 255;
    inline constexpr std::size_t MaximumCombatEventsPerBatch = 256;
    inline constexpr std::size_t ReplicatedCombatSkillCount = 19;
    inline constexpr std::size_t MaximumReplicatedActiveMagicEffects = 64;
    inline constexpr std::size_t MaximumReplicatedMagicEffectEvents = MaximumCombatEventsPerBatch;

    enum class ReplicatedCombatSkill : std::uint8_t
    {
        Block = 0,
        ShortBlade = 1,
        LongBlade = 2,
        BluntWeapon = 3,
        Axe = 4,
        Spear = 5,
        HandToHand = 6,
        LightArmor = 7,
        MediumArmor = 8,
        HeavyArmor = 9,
        Unarmored = 10,
        Security = 11,
        Alteration = 12,
        Conjuration = 13,
        Destruction = 14,
        Illusion = 15,
        Mysticism = 16,
        Restoration = 17,
        Enchant = 18,
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
        InvalidMagicKind,
        InvalidMagicEffect,
    };

    struct CombatReplicationDecodeError
    {
        CombatReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;
        friend constexpr bool operator==(CombatReplicationDecodeError, CombatReplicationDecodeError) noexcept = default;
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
        float magicka = 0.f;
        float maximumMagicka = 0.f;
        bool dead = false;
        friend constexpr bool operator==(ActorCombatSnapshot, ActorCombatSnapshot) noexcept = default;
        friend constexpr auto operator<=>(const ActorCombatSnapshot& lhs, const ActorCombatSnapshot& rhs) noexcept
        {
            return lhs.actorId <=> rhs.actorId;
        }
    };

    struct PlayerCombatSnapshot
    {
        PlayerId playerId;
        CombatRevision combatRevision = CombatRevision::initial();
        float health = 0.f;
        float maximumHealth = 0.f;
        float fatigue = 0.f;
        float maximumFatigue = 0.f;
        float magicka = 0.f;
        float maximumMagicka = 0.f;
        bool dead = false;
        friend constexpr bool operator==(PlayerCombatSnapshot, PlayerCombatSnapshot) noexcept = default;
        friend constexpr auto operator<=>(const PlayerCombatSnapshot& lhs, const PlayerCombatSnapshot& rhs) noexcept
        {
            return lhs.playerId <=> rhs.playerId;
        }
    };

    struct CombatSkillSnapshot
    {
        ReplicatedCombatSkill skill = ReplicatedCombatSkill::Block;
        float value = 0.f;
        float progress = 0.f;
        friend constexpr bool operator==(CombatSkillSnapshot, CombatSkillSnapshot) noexcept = default;
    };

    struct ActiveMagicEffectSnapshot
    {
        ActiveMagicEffectId instanceId;
        PlayerId casterPlayerId;
        MagicUseSourceKind sourceKind = MagicUseSourceKind::Spell;
        std::uint64_t sourceId = 0;
        MagicUseTargetKind targetKind = MagicUseTargetKind::Player;
        std::uint64_t targetId = 0;
        DirectMagicEffectKind effectKind = DirectMagicEffectKind::DamageHealth;
        float magnitudePerSecond = 0.f;
        ServerTick startTick = ServerTick::initial();
        ServerTick endTick = ServerTick::initial();
        friend constexpr bool operator==(ActiveMagicEffectSnapshot, ActiveMagicEffectSnapshot) noexcept = default;
    };

    class LatestWinsCombatSnapshot
    {
    public:
        static std::variant<LatestWinsCombatSnapshot, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision, PlayerId self,
            CombatRevision selfRevision, float selfHealth, float selfMaximumHealth, float selfFatigue,
            float selfMaximumFatigue, float selfMagicka, float selfMaximumMagicka, bool selfDead,
            std::span<const ActorCombatSnapshot> actors, std::span<const CombatSkillSnapshot> skills,
            std::span<const PlayerCombatSnapshot> players = {},
            std::span<const ActiveMagicEffectSnapshot> activeEffects = {});
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
        std::span<const PlayerCombatSnapshot> players() const noexcept { return mPlayers; }
        std::span<const ActiveMagicEffectSnapshot> activeEffects() const noexcept { return mActiveEffects; }
        friend bool operator==(const LatestWinsCombatSnapshot&, const LatestWinsCombatSnapshot&) noexcept = default;

    private:
        LatestWinsCombatSnapshot(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, PlayerId self, CombatRevision selfRevision, float selfHealth,
            float selfMaximumHealth, float selfFatigue, float selfMaximumFatigue, float selfMagicka,
            float selfMaximumMagicka, bool selfDead, std::vector<ActorCombatSnapshot> actors,
            std::vector<CombatSkillSnapshot> skills, std::vector<PlayerCombatSnapshot> players,
            std::vector<ActiveMagicEffectSnapshot> activeEffects)
            : mSession(session)
            , mGeneration(generation)
            , mTick(tick)
            , mCanonicalRevision(canonicalRevision)
            , mSelf(self)
            , mSelfRevision(selfRevision)
            , mSelfFatigue(selfFatigue)
            , mSelfMaximumFatigue(selfMaximumFatigue)
            , mSelfHealth(selfHealth)
            , mSelfMaximumHealth(selfMaximumHealth)
            , mSelfMagicka(selfMagicka)
            , mSelfMaximumMagicka(selfMaximumMagicka)
            , mSelfDead(selfDead)
            , mActors(std::move(actors))
            , mSkills(std::move(skills))
            , mPlayers(std::move(players))
            , mActiveEffects(std::move(activeEffects))
        {
        }
        SessionId mSession;
        SessionGeneration mGeneration;
        ServerTick mTick;
        CanonicalRevision mCanonicalRevision;
        PlayerId mSelf;
        CombatRevision mSelfRevision;
        float mSelfFatigue;
        float mSelfMaximumFatigue;
        float mSelfHealth;
        float mSelfMaximumHealth;
        float mSelfMagicka;
        float mSelfMaximumMagicka;
        bool mSelfDead;
        std::vector<ActorCombatSnapshot> mActors;
        std::vector<CombatSkillSnapshot> mSkills;
        std::vector<PlayerCombatSnapshot> mPlayers;
        std::vector<ActiveMagicEffectSnapshot> mActiveEffects;
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

    struct MagicUseCombatEvent
    {
        PlayerId casterPlayerId;
        MagicUseSourceKind sourceKind = MagicUseSourceKind::Spell;
        std::uint64_t sourceId = 0;
        MagicUseTargetKind targetKind = MagicUseTargetKind::Self;
        std::uint64_t targetId = 0;
        CombatRevision casterCombatRevision = CombatRevision::initial();
        CombatRevision targetCombatRevision = CombatRevision::initial();
        bool castSucceeded = false;
        float selfHealthDelta = 0.f;
        float selfFatigueDelta = 0.f;
        float selfMagickaDelta = 0.f;
        float targetHealthDelta = 0.f;
        float targetFatigueDelta = 0.f;
        float targetMagickaDelta = 0.f;
        bool targetDied = false;
        friend constexpr bool operator==(MagicUseCombatEvent, MagicUseCombatEvent) noexcept = default;
    };

    enum class MagicEffectCombatEventKind : std::uint8_t
    {
        Started = 0,
        Updated = 1,
        Ended = 2,
    };

    enum class MagicEffectCombatEndReason : std::uint8_t
    {
        None = 0,
        Expired = 1,
        Dispelled = 2,
        Replaced = 3,
        TargetDied = 4,
    };

    struct MagicEffectCombatEvent
    {
        ActiveMagicEffectId instanceId;
        MagicEffectCombatEventKind eventKind = MagicEffectCombatEventKind::Started;
        MagicEffectCombatEndReason endReason = MagicEffectCombatEndReason::None;
        MagicUseTargetKind targetKind = MagicUseTargetKind::Player;
        std::uint64_t targetId = 0;
        DirectMagicEffectKind effectKind = DirectMagicEffectKind::DamageHealth;
        float magnitudePerSecond = 0.f;
        float appliedDelta = 0.f;
        ServerTick startTick = ServerTick::initial();
        ServerTick endTick = ServerTick::initial();
        CombatRevision targetCombatRevision = CombatRevision::initial();
        friend constexpr bool operator==(MagicEffectCombatEvent, MagicEffectCombatEvent) noexcept = default;
    };

    class ReliableCombatEventBatch
    {
    public:
        static std::variant<ReliableCombatEventBatch, CombatReplicationDecodeError> create(SessionId session,
            SessionGeneration generation, ServerTick tick, CanonicalRevision canonicalRevision,
            std::span<const MeleeCombatEvent> events, std::span<const ActorMeleeCombatEvent> actorEvents = {},
            std::span<const MagicUseCombatEvent> magicEvents = {},
            std::span<const MagicEffectCombatEvent> magicEffectEvents = {});
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const MeleeCombatEvent> events() const noexcept { return mEvents; }
        std::span<const ActorMeleeCombatEvent> actorEvents() const noexcept { return mActorEvents; }
        std::span<const MagicUseCombatEvent> magicEvents() const noexcept { return mMagicEvents; }
        std::span<const MagicEffectCombatEvent> magicEffectEvents() const noexcept { return mMagicEffectEvents; }
        friend bool operator==(const ReliableCombatEventBatch&, const ReliableCombatEventBatch&) noexcept = default;

    private:
        ReliableCombatEventBatch(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision revision, std::vector<MeleeCombatEvent> events,
            std::vector<ActorMeleeCombatEvent> actorEvents, std::vector<MagicUseCombatEvent> magicEvents,
            std::vector<MagicEffectCombatEvent> magicEffectEvents)
            : mSession(session)
            , mGeneration(generation)
            , mTick(tick)
            , mCanonicalRevision(revision)
            , mEvents(std::move(events))
            , mActorEvents(std::move(actorEvents))
            , mMagicEvents(std::move(magicEvents))
            , mMagicEffectEvents(std::move(magicEffectEvents))
        {
        }
        SessionId mSession;
        SessionGeneration mGeneration;
        ServerTick mTick;
        CanonicalRevision mCanonicalRevision;
        std::vector<MeleeCombatEvent> mEvents;
        std::vector<ActorMeleeCombatEvent> mActorEvents;
        std::vector<MagicUseCombatEvent> mMagicEvents;
        std::vector<MagicEffectCombatEvent> mMagicEffectEvents;
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
