#ifndef TES3MP_COMMAND_PRIMITIVES_HPP
#define TES3MP_COMMAND_PRIMITIVES_HPP

#include "spatial_types.hpp"

#include <compare>

namespace TES3MP
{
    class ClientCommandHeader
    {
    public:
        constexpr ClientCommandHeader(SessionId sessionId, SessionGeneration sessionGeneration,
            CommandSequence commandSequence, CommandId commandId, ServerTick observedServerTick) noexcept
            : mSessionId(sessionId)
            , mSessionGeneration(sessionGeneration)
            , mCommandSequence(commandSequence)
            , mCommandId(commandId)
            , mObservedServerTick(observedServerTick)
        {
        }

        constexpr SessionId sessionId() const noexcept { return mSessionId; }
        constexpr SessionGeneration sessionGeneration() const noexcept { return mSessionGeneration; }
        constexpr CommandSequence commandSequence() const noexcept { return mCommandSequence; }
        constexpr CommandId commandId() const noexcept { return mCommandId; }
        constexpr ServerTick observedServerTick() const noexcept { return mObservedServerTick; }

        friend constexpr bool operator==(ClientCommandHeader, ClientCommandHeader) noexcept = default;
        friend constexpr auto operator<=>(ClientCommandHeader, ClientCommandHeader) noexcept = default;

    private:
        SessionId mSessionId;
        SessionGeneration mSessionGeneration;
        CommandSequence mCommandSequence;
        CommandId mCommandId;
        ServerTick mObservedServerTick;
    };

    class EntityPrecondition
    {
    public:
        constexpr EntityPrecondition(
            EntityId entityId, EntityRevision expectedRevision, AuthorityEpoch expectedAuthorityEpoch) noexcept
            : mEntityId(entityId)
            , mExpectedRevision(expectedRevision)
            , mExpectedAuthorityEpoch(expectedAuthorityEpoch)
        {
        }

        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr EntityRevision expectedRevision() const noexcept { return mExpectedRevision; }
        constexpr AuthorityEpoch expectedAuthorityEpoch() const noexcept { return mExpectedAuthorityEpoch; }

        friend constexpr bool operator==(EntityPrecondition, EntityPrecondition) noexcept = default;
        friend constexpr auto operator<=>(EntityPrecondition, EntityPrecondition) noexcept = default;

    private:
        EntityId mEntityId;
        EntityRevision mExpectedRevision;
        AuthorityEpoch mExpectedAuthorityEpoch;
    };

    class WriterAdmissionStamp
    {
    public:
        constexpr WriterAdmissionStamp(ServerTick eligibleServerTick, IngressOrdinal ingressOrdinal) noexcept
            : mEligibleServerTick(eligibleServerTick)
            , mIngressOrdinal(ingressOrdinal)
        {
        }

        constexpr ServerTick eligibleServerTick() const noexcept { return mEligibleServerTick; }
        constexpr IngressOrdinal ingressOrdinal() const noexcept { return mIngressOrdinal; }

        friend constexpr bool operator==(WriterAdmissionStamp, WriterAdmissionStamp) noexcept = default;
        friend constexpr auto operator<=>(WriterAdmissionStamp, WriterAdmissionStamp) noexcept = default;

    private:
        ServerTick mEligibleServerTick;
        IngressOrdinal mIngressOrdinal;
    };

    class SpatialEntitySnapshot
    {
    public:
        constexpr SpatialEntitySnapshot(ServerTick serverTick, EntityId entityId, EntityRevision entityRevision,
            AuthorityEpoch authorityEpoch, Transform transform, LinearVelocity3 linearVelocity) noexcept
            : mServerTick(serverTick)
            , mEntityId(entityId)
            , mEntityRevision(entityRevision)
            , mAuthorityEpoch(authorityEpoch)
            , mTransform(transform)
            , mLinearVelocity(linearVelocity)
        {
        }

        constexpr ServerTick serverTick() const noexcept { return mServerTick; }
        constexpr EntityId entityId() const noexcept { return mEntityId; }
        constexpr EntityRevision entityRevision() const noexcept { return mEntityRevision; }
        constexpr AuthorityEpoch authorityEpoch() const noexcept { return mAuthorityEpoch; }
        constexpr const Transform& transform() const noexcept { return mTransform; }
        constexpr LinearVelocity3 linearVelocity() const noexcept { return mLinearVelocity; }

        friend constexpr bool operator==(const SpatialEntitySnapshot&, const SpatialEntitySnapshot&) noexcept
            = default;
        friend constexpr auto operator<=>(const SpatialEntitySnapshot&, const SpatialEntitySnapshot&) noexcept
            = default;

    private:
        ServerTick mServerTick;
        EntityId mEntityId;
        EntityRevision mEntityRevision;
        AuthorityEpoch mAuthorityEpoch;
        Transform mTransform;
        LinearVelocity3 mLinearVelocity;
    };
}

#endif
