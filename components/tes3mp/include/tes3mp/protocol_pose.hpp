#ifndef TES3MP_PROTOCOL_POSE_HPP
#define TES3MP_PROTOCOL_POSE_HPP

#include "spatial_types.hpp"
#include "value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::int64_t MaximumVrPoseOffsetQuanta = 1'048'576;

    class VrPoseOffset3
    {
    public:
        static constexpr std::optional<VrPoseOffset3> create(std::int64_t x, std::int64_t y, std::int64_t z) noexcept
        {
            if (!componentInRange(x) || !componentInRange(y) || !componentInRange(z))
                return std::nullopt;
            return VrPoseOffset3(x, y, z);
        }

        constexpr std::int64_t x() const noexcept { return mX; }
        constexpr std::int64_t y() const noexcept { return mY; }
        constexpr std::int64_t z() const noexcept { return mZ; }

        friend constexpr bool operator==(VrPoseOffset3, VrPoseOffset3) noexcept = default;

    private:
        static constexpr bool componentInRange(std::int64_t value) noexcept
        {
            return value >= -MaximumVrPoseOffsetQuanta && value <= MaximumVrPoseOffsetQuanta;
        }

        constexpr VrPoseOffset3(std::int64_t x, std::int64_t y, std::int64_t z) noexcept
            : mX(x)
            , mY(y)
            , mZ(z)
        {
        }

        std::int64_t mX;
        std::int64_t mY;
        std::int64_t mZ;
    };

    class VrTrackedTransform
    {
    public:
        constexpr VrTrackedTransform(VrPoseOffset3 offset, Orientation3 orientation) noexcept
            : mOffset(offset)
            , mOrientation(orientation)
        {
        }

        constexpr VrPoseOffset3 offset() const noexcept { return mOffset; }
        constexpr Orientation3 orientation() const noexcept { return mOrientation; }

        friend constexpr bool operator==(VrTrackedTransform, VrTrackedTransform) noexcept = default;

    private:
        VrPoseOffset3 mOffset;
        Orientation3 mOrientation;
    };

    class ClientVrPoseSample
    {
    public:
        constexpr ClientVrPoseSample(SessionId sourceSessionId, SessionGeneration sourceSessionGeneration,
            EntityId rootEntityId, AuthorityEpoch rootAuthorityEpoch, PoseSampleSequence sampleSequence,
            VrTrackedTransform head, std::optional<VrTrackedTransform> leftHand,
            std::optional<VrTrackedTransform> rightHand) noexcept
            : mSourceSessionId(sourceSessionId)
            , mSourceSessionGeneration(sourceSessionGeneration)
            , mRootEntityId(rootEntityId)
            , mRootAuthorityEpoch(rootAuthorityEpoch)
            , mSampleSequence(sampleSequence)
            , mHead(head)
            , mLeftHand(leftHand)
            , mRightHand(rightHand)
        {
        }

        constexpr SessionId sourceSessionId() const noexcept { return mSourceSessionId; }
        constexpr SessionGeneration sourceSessionGeneration() const noexcept { return mSourceSessionGeneration; }
        constexpr EntityId rootEntityId() const noexcept { return mRootEntityId; }
        constexpr AuthorityEpoch rootAuthorityEpoch() const noexcept { return mRootAuthorityEpoch; }
        constexpr PoseSampleSequence sampleSequence() const noexcept { return mSampleSequence; }
        constexpr VrTrackedTransform head() const noexcept { return mHead; }
        constexpr const std::optional<VrTrackedTransform>& leftHand() const noexcept { return mLeftHand; }
        constexpr const std::optional<VrTrackedTransform>& rightHand() const noexcept { return mRightHand; }

        friend constexpr bool operator==(const ClientVrPoseSample&, const ClientVrPoseSample&) noexcept = default;

    private:
        SessionId mSourceSessionId;
        SessionGeneration mSourceSessionGeneration;
        EntityId mRootEntityId;
        AuthorityEpoch mRootAuthorityEpoch;
        PoseSampleSequence mSampleSequence;
        VrTrackedTransform mHead;
        std::optional<VrTrackedTransform> mLeftHand;
        std::optional<VrTrackedTransform> mRightHand;
    };

    class ServerVrPoseSnapshot
    {
    public:
        constexpr ServerVrPoseSnapshot(SessionId targetSessionId, SessionGeneration targetSessionGeneration,
            PlayerId sourcePlayerId, SessionId sourceSessionId, SessionGeneration sourceSessionGeneration,
            EntityId rootEntityId, AuthorityEpoch rootAuthorityEpoch, PoseSampleSequence sampleSequence,
            VrTrackedTransform head, std::optional<VrTrackedTransform> leftHand,
            std::optional<VrTrackedTransform> rightHand) noexcept
            : mTargetSessionId(targetSessionId)
            , mTargetSessionGeneration(targetSessionGeneration)
            , mSourcePlayerId(sourcePlayerId)
            , mSourceSessionId(sourceSessionId)
            , mSourceSessionGeneration(sourceSessionGeneration)
            , mRootEntityId(rootEntityId)
            , mRootAuthorityEpoch(rootAuthorityEpoch)
            , mSampleSequence(sampleSequence)
            , mHead(head)
            , mLeftHand(leftHand)
            , mRightHand(rightHand)
        {
        }

        constexpr SessionId targetSessionId() const noexcept { return mTargetSessionId; }
        constexpr SessionGeneration targetSessionGeneration() const noexcept { return mTargetSessionGeneration; }
        constexpr PlayerId sourcePlayerId() const noexcept { return mSourcePlayerId; }
        constexpr SessionId sourceSessionId() const noexcept { return mSourceSessionId; }
        constexpr SessionGeneration sourceSessionGeneration() const noexcept { return mSourceSessionGeneration; }
        constexpr EntityId rootEntityId() const noexcept { return mRootEntityId; }
        constexpr AuthorityEpoch rootAuthorityEpoch() const noexcept { return mRootAuthorityEpoch; }
        constexpr PoseSampleSequence sampleSequence() const noexcept { return mSampleSequence; }
        constexpr VrTrackedTransform head() const noexcept { return mHead; }
        constexpr const std::optional<VrTrackedTransform>& leftHand() const noexcept { return mLeftHand; }
        constexpr const std::optional<VrTrackedTransform>& rightHand() const noexcept { return mRightHand; }

        friend constexpr bool operator==(const ServerVrPoseSnapshot&, const ServerVrPoseSnapshot&) noexcept = default;

    private:
        SessionId mTargetSessionId;
        SessionGeneration mTargetSessionGeneration;
        PlayerId mSourcePlayerId;
        SessionId mSourceSessionId;
        SessionGeneration mSourceSessionGeneration;
        EntityId mRootEntityId;
        AuthorityEpoch mRootAuthorityEpoch;
        PoseSampleSequence mSampleSequence;
        VrTrackedTransform mHead;
        std::optional<VrTrackedTransform> mLeftHand;
        std::optional<VrTrackedTransform> mRightHand;
    };

    enum class PoseDecodeErrorStage : std::uint8_t
    {
        SizePrefix,
        Identifier,
        Verification,
        SemanticValidation,
    };

    enum class PoseDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        InvalidStrongValue,
        MissingHead,
        MissingPosition,
        MissingOrientation,
        OffsetOutOfRange,
    };

    struct PoseDecodeError
    {
        PoseDecodeErrorStage stage;
        PoseDecodeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;

        friend constexpr bool operator==(PoseDecodeError, PoseDecodeError) noexcept = default;
    };

    enum class PoseSampleRecency : std::uint8_t
    {
        Newer,
        Duplicate,
        Stale,
        ConflictingDuplicate,
        SourceChanged,
    };

    using ClientVrPoseSampleDecodeResult = std::variant<ClientVrPoseSample, PoseDecodeError>;
    using ServerVrPoseSnapshotDecodeResult = std::variant<ServerVrPoseSnapshot, PoseDecodeError>;

    std::vector<std::byte> encodeClientVrPoseSample(const ClientVrPoseSample& value);
    std::vector<std::byte> encodeServerVrPoseSnapshot(const ServerVrPoseSnapshot& value);
    ClientVrPoseSampleDecodeResult decodeClientVrPoseSample(std::span<const std::byte> payload);
    ServerVrPoseSnapshotDecodeResult decodeServerVrPoseSnapshot(std::span<const std::byte> payload);

    PoseSampleRecency classifyPoseSample(const ClientVrPoseSample& previous, std::span<const std::byte> previousPayload,
        const ClientVrPoseSample& incoming, std::span<const std::byte> incomingPayload) noexcept;
    PoseSampleRecency classifyPoseSample(const ServerVrPoseSnapshot& previous,
        std::span<const std::byte> previousPayload, const ServerVrPoseSnapshot& incoming,
        std::span<const std::byte> incomingPayload) noexcept;
}

#endif
