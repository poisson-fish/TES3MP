#ifndef TES3MP_ACTOR_REPLICATION_HPP
#define TES3MP_ACTOR_REPLICATION_HPP

#include "actor_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumActorInterestMembers = 248;

    enum class ActorReplicationDecodeErrorCode : std::uint8_t
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
        InvalidCellKind,
        InvalidInteriorGrid,
        InvalidActivity,
    };

    struct ActorReplicationDecodeError
    {
        ActorReplicationDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::size_t index = 0;

        friend constexpr bool operator==(ActorReplicationDecodeError, ActorReplicationDecodeError) noexcept = default;
    };

    struct ActorInterestMember
    {
        ActorId actorId;
        EntityId entityId;
        ActorPrototypeId prototypeId;

        friend constexpr bool operator==(ActorInterestMember, ActorInterestMember) noexcept = default;
        friend constexpr auto operator<=>(ActorInterestMember, ActorInterestMember) noexcept = default;
    };

    class ReliableActorInterestBaseline
    {
    public:
        static std::variant<ReliableActorInterestBaseline, ActorReplicationDecodeError> create(
            SessionId targetSessionId, SessionGeneration targetSessionGeneration, ServerTick serverTick,
            CanonicalRevision canonicalRevision, std::span<const ActorInterestMember> members);

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        std::span<const ActorInterestMember> members() const noexcept { return mMembers; }

        friend bool operator==(const ReliableActorInterestBaseline&, const ReliableActorInterestBaseline&) noexcept
            = default;

    private:
        ReliableActorInterestBaseline(SessionId session, SessionGeneration generation, ServerTick tick,
            CanonicalRevision canonicalRevision, std::vector<ActorInterestMember> members)
            : mTargetSessionId(session), mTargetSessionGeneration(generation), mServerTick(tick),
              mCanonicalRevision(canonicalRevision), mMembers(std::move(members)) {}

        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        ServerTick mServerTick;
        CanonicalRevision mCanonicalRevision;
        std::vector<ActorInterestMember> mMembers;
    };

    class ActorSpatialSnapshot
    {
    public:
        constexpr ActorSpatialSnapshot(ServerTick serverTick, ActorId actorId, EntityId entityId,
            ActorPrototypeId prototypeId, EntityRevision entityRevision, AuthorityEpoch authorityEpoch,
            Transform transform, LinearVelocity3 linearVelocity, ActorActivity activity) noexcept
            : mServerTick(serverTick), mActorId(actorId), mEntityId(entityId), mPrototypeId(prototypeId),
              mEntityRevision(entityRevision), mAuthorityEpoch(authorityEpoch), mTransform(transform),
              mLinearVelocity(linearVelocity), mActivity(activity) {}

        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr ActorId actorId() const noexcept { return mActorId; }
        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr ActorPrototypeId prototypeId() const noexcept { return mPrototypeId; }
        constexpr EntityRevision entityRevision() const noexcept { return mEntityRevision; }
        constexpr AuthorityEpoch authorityEpoch() const noexcept { return mAuthorityEpoch; }
        constexpr const Transform& transform() const noexcept { return mTransform; }
        constexpr LinearVelocity3 linearVelocity() const noexcept { return mLinearVelocity; }
        constexpr ActorActivity activity() const noexcept { return mActivity; }

        friend constexpr bool operator==(const ActorSpatialSnapshot&, const ActorSpatialSnapshot&) noexcept = default;

    private:
        ServerTick mServerTick;
        ActorId mActorId;
        EntityId mEntityId;
        ActorPrototypeId mPrototypeId;
        EntityRevision mEntityRevision;
        AuthorityEpoch mAuthorityEpoch;
        Transform mTransform;
        LinearVelocity3 mLinearVelocity;
        ActorActivity mActivity;
    };

    class ActorWorldView
    {
    public:
        static std::variant<ActorWorldView, ActorReplicationDecodeError> create(
            std::span<const ActorSpatialSnapshot> entries);
        std::span<const ActorSpatialSnapshot> entries() const noexcept { return mEntries; }
        friend bool operator==(const ActorWorldView&, const ActorWorldView&) noexcept = default;

    private:
        explicit ActorWorldView(std::vector<ActorSpatialSnapshot> entries) : mEntries(std::move(entries)) {}
        std::vector<ActorSpatialSnapshot> mEntries;
    };

    class LatestWinsActorSnapshot
    {
    public:
        LatestWinsActorSnapshot(SessionId targetSessionId, SessionGeneration targetSessionGeneration,
            ServerTick serverTick, CanonicalRevision canonicalRevision, ActorWorldView view)
            : mTargetSessionId(targetSessionId), mTargetSessionGeneration(targetSessionGeneration),
              mServerTick(serverTick), mCanonicalRevision(canonicalRevision), mView(std::move(view)) {}

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr CanonicalRevision canonicalRevision() const noexcept { return mCanonicalRevision; }
        const ActorWorldView& view() const noexcept { return mView; }
        friend bool operator==(const LatestWinsActorSnapshot&, const LatestWinsActorSnapshot&) noexcept = default;

    private:
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        ServerTick mServerTick;
        CanonicalRevision mCanonicalRevision;
        ActorWorldView mView;
    };

    using ReliableActorInterestBaselineDecodeResult
        = std::variant<ReliableActorInterestBaseline, ActorReplicationDecodeError>;
    using LatestWinsActorSnapshotDecodeResult = std::variant<LatestWinsActorSnapshot, ActorReplicationDecodeError>;

    std::vector<std::byte> encodeReliableActorInterestBaseline(const ReliableActorInterestBaseline& value);
    std::vector<std::byte> encodeLatestWinsActorSnapshot(const LatestWinsActorSnapshot& value);
    ReliableActorInterestBaselineDecodeResult decodeReliableActorInterestBaseline(std::span<const std::byte> payload);
    LatestWinsActorSnapshotDecodeResult decodeLatestWinsActorSnapshot(std::span<const std::byte> payload);
}

#endif
