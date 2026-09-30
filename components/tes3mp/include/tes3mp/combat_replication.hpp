#ifndef TES3MP_COMBAT_REPLICATION_HPP
#define TES3MP_COMBAT_REPLICATION_HPP

#include "command_primitives.hpp"
#include "direct_magic.hpp"
#include "magic_use.hpp"
#include "melee_combat.hpp"
#include "session_types.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace TES3MP
{
    // Engine-independent recipe for one actor at the enclosing committed tick.
    // kind: 1 player, 2 placed actor. Action identities are scoped to life.
    // phase: idle/wind-up/release/follow/complete (0..4); rate is section progress/sec.
    // bodyState: upright/knockout/knockdown/hit (1..4); body frames are at 30 Hz.
    struct ActorPresentationSnapshot
    {
        std::uint64_t id = 0, life = 1, action = 0, bodyAction = 0;
        std::uint8_t kind = 0, phase = 0, direction = 0, bodyState = 1, hitGroup = 0;
        float strength = 0, completion = 0, rate = 0, bodyFrame = 0;
        std::uint16_t bodyStop = 0, loopStart = 0, loopStop = 0;
        std::string group;
        bool dead = false;
        std::uint64_t cast = 0;
        std::uint8_t castPhase = 0, castRange = 0;
        std::uint16_t castElapsed = 0, castRelease = 0, castStop = 0;
        // Invisibility, Chameleon, Light, NightEye, DetectAnimal,
        // DetectEnchantment, DetectKey; aggregate magnitudes after source suppression.
        // The final entry carries committed Charm into stock dialogue rules.
        std::array<float, 8> visibility{};
        // WaterBreathing, SwiftSwim, WaterWalking, Burden, Feather, Jump,
        // Levitate, SlowFall; committed magnitudes for inherited OpenMW movement.
        std::array<float, 8> movement{};
        bool movementOwned = false;
        // Distinct stock MGEF indices with active ContinuousVfx on this life.
        std::vector<std::uint16_t> visualEffects;
        friend bool operator==(const ActorPresentationSnapshot&, const ActorPresentationSnapshot&) = default;
    };
    inline constexpr std::size_t MaximumCombatSnapshotActors = 248;
    inline constexpr std::size_t MaximumCombatSnapshotPlayers = 255;
    inline constexpr std::size_t MaximumCombatEventsPerBatch = 256;
    inline constexpr std::size_t ReplicatedCombatSkillCount = 19;
    inline constexpr std::size_t MaximumReplicatedActiveMagicEffects = 64;
    inline constexpr std::size_t MaximumReplicatedMagicEffectEvents = MaximumCombatEventsPerBatch;
    inline constexpr std::size_t MaximumReplicatedPhysicalProjectiles = 8;
    inline constexpr std::size_t MaximumReplicatedMagicProjectiles = 8;
    inline constexpr std::size_t MaximumReplicatedMagicImpactCues = 32;

    // A durable release receipt also carries its last committed flight/terminal
    // position. Identity includes the caster life so respawn cannot revive a shot.
    struct PhysicalProjectileSnapshot
    {
        std::uint8_t casterKind = 1, terminal = 0;
        std::uint64_t caster = 0, casterLife = 0, command = 0, releaseTick = 0;
        std::string record;
        std::array<float, 3> position{}, velocity{};
        friend bool operator==(const PhysicalProjectileSnapshot&, const PhysicalProjectileSnapshot&) = default;
    };

    // Source is the authored spell or enchantment record. Clients resolve its
    // target-range visual data locally; flight position and contact stay server-owned.
    struct MagicProjectileSnapshot
    {
        std::uint8_t casterKind = 1, sourceKind = 0;
        std::uint64_t caster = 0, casterLife = 0, command = 0;
        std::string record;
        std::array<float, 3> position{}, velocity{};
        friend bool operator==(const MagicProjectileSnapshot&, const MagicProjectileSnapshot&) = default;
    };

    struct MagicImpactCue
    {
        std::uint8_t casterKind = 1, sourceKind = 0;
        std::uint64_t caster = 0, casterLife = 0, command = 0;
        std::string record;
        std::array<float, 3> position{};
        std::uint8_t range = 2; // ESM Self/Touch/Target hit: 0..2; committed cast: 3.
        friend bool operator==(const MagicImpactCue&, const MagicImpactCue&) = default;
    };

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
        // World-space point on the camera ray. The server derives flight from
        // its committed player position and owns all contacts.
        std::optional<std::array<float, 3>> aimPoint;
        friend constexpr bool operator==(ClientMeleeAttackCommand, ClientMeleeAttackCommand) noexcept = default;
    };

    // 0: legacy/local presentation, 1: upright, 2: fatigue knockout, 3: hit knockdown.
    // Frame is the committed 30 Hz offset from the bound clip's start key.
    struct KnockoutSnapshot
    {
        std::uint8_t state = 0;
        std::uint16_t frame = 0;
        bool paralyzed = false;
        friend constexpr bool operator==(KnockoutSnapshot, KnockoutSnapshot) noexcept = default;
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
        std::uint64_t castId = 0;
        std::uint8_t castPhase = 0, castRange = 0;
        std::uint16_t castElapsed = 0, castRelease = 0, castStop = 0;
        KnockoutSnapshot knockout;
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
        KnockoutSnapshot knockout;
        friend constexpr bool operator==(PlayerCombatSnapshot, PlayerCombatSnapshot) noexcept = default;
        friend constexpr auto operator<=>(const PlayerCombatSnapshot& lhs, const PlayerCombatSnapshot& rhs) noexcept
        {
            return lhs.playerId <=> rhs.playerId;
        }
    };

    // Complete presentation state, including terminal states, for self and visible peers.
    // Phase: 0 idle, 1 wind-up, 2 release, 3 follow-through, 4 complete.
    struct PlayerSwingSnapshot
    {
        PlayerId playerId;
        std::uint64_t command = 0, source = 0, targetLife = 0;
        std::uint8_t direction = 0, phase = 0, interruption = 0;
        float strength = 0, completion = 0;
        std::string group;
        friend bool operator==(const PlayerSwingSnapshot&, const PlayerSwingSnapshot&) = default;
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
            std::span<const ActiveMagicEffectSnapshot> activeEffects = {},
            std::span<const PlayerSwingSnapshot> swings = {}, KnockoutSnapshot selfKnockout = {},
            std::span<const ActorPresentationSnapshot> presentation = {},
            std::span<const PhysicalProjectileSnapshot> projectiles = {},
            std::span<const MagicProjectileSnapshot> magicProjectiles = {});
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
        KnockoutSnapshot selfKnockout() const noexcept { return mSelfKnockout; }
        std::span<const ActorCombatSnapshot> actors() const noexcept { return mActors; }
        std::span<const CombatSkillSnapshot> selfSkills() const noexcept { return mSkills; }
        std::span<const PlayerCombatSnapshot> players() const noexcept { return mPlayers; }
        std::span<const ActiveMagicEffectSnapshot> activeEffects() const noexcept { return mActiveEffects; }
        std::span<const PlayerSwingSnapshot> swings() const noexcept { return mSwings; }
        std::span<const ActorPresentationSnapshot> presentation() const noexcept { return mPresentation; }
        std::span<const PhysicalProjectileSnapshot> projectiles() const noexcept { return mProjectiles; }
        std::span<const MagicProjectileSnapshot> magicProjectiles() const noexcept { return mMagicProjectiles; }
        friend bool operator==(const LatestWinsCombatSnapshot&, const LatestWinsCombatSnapshot&) noexcept = default;

    private:
        LatestWinsCombatSnapshot(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, PlayerId self, CombatRevision selfRevision, float selfHealth,
            float selfMaximumHealth, float selfFatigue, float selfMaximumFatigue, float selfMagicka,
            float selfMaximumMagicka, bool selfDead, std::vector<ActorCombatSnapshot> actors,
            std::vector<CombatSkillSnapshot> skills, std::vector<PlayerCombatSnapshot> players,
            std::vector<ActiveMagicEffectSnapshot> activeEffects, std::vector<PlayerSwingSnapshot> swings,
            KnockoutSnapshot selfKnockout, std::vector<ActorPresentationSnapshot> presentation,
            std::vector<PhysicalProjectileSnapshot> projectiles,
            std::vector<MagicProjectileSnapshot> magicProjectiles)
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
            , mSwings(std::move(swings))
            , mSelfKnockout(selfKnockout)
            , mPresentation(std::move(presentation))
            , mProjectiles(std::move(projectiles))
            , mMagicProjectiles(std::move(magicProjectiles))
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
        std::vector<PlayerSwingSnapshot> mSwings;
        KnockoutSnapshot mSelfKnockout;
        std::vector<ActorPresentationSnapshot> mPresentation;
        std::vector<PhysicalProjectileSnapshot> mProjectiles;
        std::vector<MagicProjectileSnapshot> mMagicProjectiles;
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
        std::variant<PlayerId, ActorId> caster;
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
        std::uint64_t casterLife = 1;
        std::uint64_t casterId() const noexcept
        { return std::visit([](auto id) { return id.value(); }, caster); }
        bool actorCaster() const noexcept { return std::holds_alternative<ActorId>(caster); }
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
            std::span<const MagicEffectCombatEvent> magicEffectEvents = {},
            std::span<const MagicImpactCue> magicImpactCues = {});
        SessionId targetSessionId() const noexcept { return mSession; }
        SessionGeneration targetSessionGeneration() const noexcept { return mGeneration; }
        ServerTick serverTick() const noexcept { return mTick; }
        CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const MeleeCombatEvent> events() const noexcept { return mEvents; }
        std::span<const ActorMeleeCombatEvent> actorEvents() const noexcept { return mActorEvents; }
        std::span<const MagicUseCombatEvent> magicEvents() const noexcept { return mMagicEvents; }
        std::span<const MagicEffectCombatEvent> magicEffectEvents() const noexcept { return mMagicEffectEvents; }
        std::span<const MagicImpactCue> magicImpactCues() const noexcept { return mMagicImpactCues; }
        friend bool operator==(const ReliableCombatEventBatch&, const ReliableCombatEventBatch&) noexcept = default;

    private:
        ReliableCombatEventBatch(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision revision, std::vector<MeleeCombatEvent> events,
            std::vector<ActorMeleeCombatEvent> actorEvents, std::vector<MagicUseCombatEvent> magicEvents,
            std::vector<MagicEffectCombatEvent> magicEffectEvents,
            std::vector<MagicImpactCue> magicImpactCues)
            : mSession(session)
            , mGeneration(generation)
            , mTick(tick)
            , mCanonicalRevision(revision)
            , mEvents(std::move(events))
            , mActorEvents(std::move(actorEvents))
            , mMagicEvents(std::move(magicEvents))
            , mMagicEffectEvents(std::move(magicEffectEvents))
            , mMagicImpactCues(std::move(magicImpactCues))
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
        std::vector<MagicImpactCue> mMagicImpactCues;
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
