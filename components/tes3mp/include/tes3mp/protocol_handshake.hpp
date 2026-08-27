#ifndef TES3MP_PROTOCOL_HANDSHAKE_HPP
#define TES3MP_PROTOCOL_HANDSHAKE_HPP

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace TES3MP
{
    inline constexpr std::size_t MaximumOptionalCapabilityCount = 32;
    inline constexpr std::size_t MaximumRequiredCapabilityCount = 32;
    inline constexpr std::size_t MaximumNegotiatedCapabilityCount
        = MaximumOptionalCapabilityCount + MaximumRequiredCapabilityCount;

    class CapabilityId
    {
    public:
        static constexpr std::optional<CapabilityId> fromValue(std::uint32_t value) noexcept
        {
            if (value == 0)
                return std::nullopt;
            return CapabilityId(value);
        }

        constexpr std::uint32_t value() const noexcept { return mValue; }

        friend constexpr bool operator==(CapabilityId, CapabilityId) noexcept = default;
        friend constexpr auto operator<=>(CapabilityId, CapabilityId) noexcept = default;

    private:
        constexpr explicit CapabilityId(std::uint32_t value) noexcept
            : mValue(value)
        {
        }

        std::uint32_t mValue;
    };

    struct ProtocolVersion
    {
        std::uint16_t major;
        std::uint16_t minor;

        friend constexpr bool operator==(ProtocolVersion, ProtocolVersion) noexcept = default;
    };

    enum class HandshakeErrorStage : std::uint8_t
    {
        SizePrefix,
        Identifier,
        Verification,
        SemanticValidation,
    };

    enum class HandshakeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        InvalidVersionRange,
        TooManyCapabilities,
        ZeroCapability,
        CapabilitiesNotStrictlySorted,
        CapabilityInBothSets,
        UnknownRejectionReason,
        MissingRejectionCapability,
        UnexpectedRejectionCapability,
    };

    struct HandshakeError
    {
        HandshakeErrorStage stage;
        HandshakeErrorCode code;
        std::size_t observed = 0;
        std::size_t limit = 0;
        std::uint32_t capability = 0;

        friend constexpr bool operator==(HandshakeError, HandshakeError) noexcept = default;
    };

    class ProtocolVersionRange
    {
    public:
        static std::variant<ProtocolVersionRange, HandshakeError> create(
            std::uint16_t major, std::uint16_t minimumMinor, std::uint16_t maximumMinor) noexcept;

        constexpr std::uint16_t major() const noexcept { return mMajor; }
        constexpr std::uint16_t minimumMinor() const noexcept { return mMinimumMinor; }
        constexpr std::uint16_t maximumMinor() const noexcept { return mMaximumMinor; }

        friend constexpr bool operator==(ProtocolVersionRange, ProtocolVersionRange) noexcept = default;

    private:
        constexpr ProtocolVersionRange(
            std::uint16_t major, std::uint16_t minimumMinor, std::uint16_t maximumMinor) noexcept
            : mMajor(major)
            , mMinimumMinor(minimumMinor)
            , mMaximumMinor(maximumMinor)
        {
        }

        std::uint16_t mMajor;
        std::uint16_t mMinimumMinor;
        std::uint16_t mMaximumMinor;
    };

    class CapabilityOffer
    {
    public:
        static std::variant<CapabilityOffer, HandshakeError> create(ProtocolVersionRange versions,
            std::span<const CapabilityId> optionalCapabilities, std::span<const CapabilityId> requiredCapabilities);

        const ProtocolVersionRange& versions() const noexcept { return mVersions; }
        std::span<const CapabilityId> optionalCapabilities() const noexcept { return mOptionalCapabilities; }
        std::span<const CapabilityId> requiredCapabilities() const noexcept { return mRequiredCapabilities; }

    private:
        friend class ClientHello;

        CapabilityOffer(ProtocolVersionRange versions, std::vector<CapabilityId> optionalCapabilities,
            std::vector<CapabilityId> requiredCapabilities)
            : mVersions(versions)
            , mOptionalCapabilities(std::move(optionalCapabilities))
            , mRequiredCapabilities(std::move(requiredCapabilities))
        {
        }

        ProtocolVersionRange mVersions;
        std::vector<CapabilityId> mOptionalCapabilities;
        std::vector<CapabilityId> mRequiredCapabilities;
    };

    class ClientHello
    {
    public:
        static ClientHello fromOffer(CapabilityOffer offer) { return ClientHello(std::move(offer)); }

        const ProtocolVersionRange& versions() const noexcept { return mOffer.versions(); }
        std::span<const CapabilityId> optionalCapabilities() const noexcept { return mOffer.optionalCapabilities(); }
        std::span<const CapabilityId> requiredCapabilities() const noexcept { return mOffer.requiredCapabilities(); }

    private:
        friend std::variant<ClientHello, HandshakeError> decodeClientHello(std::span<const std::byte> payload);

        explicit ClientHello(CapabilityOffer offer)
            : mOffer(std::move(offer))
        {
        }

        CapabilityOffer mOffer;
    };

    class ServerHello
    {
    public:
        ProtocolVersion selectedVersion() const noexcept { return mSelectedVersion; }
        std::span<const CapabilityId> negotiatedCapabilities() const noexcept { return mNegotiatedCapabilities; }

    private:
        friend std::variant<ServerHello, HandshakeError> decodeServerHello(std::span<const std::byte> payload);
        friend std::variant<ServerHello, class SessionRejected> negotiateClientHello(
            const ClientHello& client, const CapabilityOffer& server);

        ServerHello(ProtocolVersion selectedVersion, std::vector<CapabilityId> negotiatedCapabilities)
            : mSelectedVersion(selectedVersion)
            , mNegotiatedCapabilities(std::move(negotiatedCapabilities))
        {
        }

        ProtocolVersion mSelectedVersion;
        std::vector<CapabilityId> mNegotiatedCapabilities;
    };

    enum class SessionRejectionReason : std::uint8_t
    {
        ProtocolMajorMismatch = 1,
        NoCompatibleMinor = 2,
        UnsupportedRequiredCapability = 3,
    };

    class SessionRejected
    {
    public:
        SessionRejectionReason reason() const noexcept { return mReason; }
        const ProtocolVersionRange& serverVersions() const noexcept { return mServerVersions; }
        std::optional<CapabilityId> unsupportedCapability() const noexcept { return mUnsupportedCapability; }

    private:
        friend std::variant<SessionRejected, HandshakeError> decodeSessionRejected(std::span<const std::byte> payload);
        friend std::variant<ServerHello, SessionRejected> negotiateClientHello(
            const ClientHello& client, const CapabilityOffer& server);

        SessionRejected(SessionRejectionReason reason, ProtocolVersionRange serverVersions,
            std::optional<CapabilityId> unsupportedCapability) noexcept
            : mReason(reason)
            , mServerVersions(serverVersions)
            , mUnsupportedCapability(unsupportedCapability)
        {
        }

        SessionRejectionReason mReason;
        ProtocolVersionRange mServerVersions;
        std::optional<CapabilityId> mUnsupportedCapability;
    };

    enum class InitialPeerProtocol : std::uint8_t
    {
        NeedMoreBytes,
        Vnext,
        LegacyOrUnknown,
    };

    using ClientHelloDecodeResult = std::variant<ClientHello, HandshakeError>;
    using ServerHelloDecodeResult = std::variant<ServerHello, HandshakeError>;
    using SessionRejectedDecodeResult = std::variant<SessionRejected, HandshakeError>;
    using NegotiationResult = std::variant<ServerHello, SessionRejected>;

    std::vector<std::byte> encodeClientHello(const ClientHello& value);
    std::vector<std::byte> encodeServerHello(const ServerHello& value);
    std::vector<std::byte> encodeSessionRejected(const SessionRejected& value);

    ClientHelloDecodeResult decodeClientHello(std::span<const std::byte> payload);
    ServerHelloDecodeResult decodeServerHello(std::span<const std::byte> payload);
    SessionRejectedDecodeResult decodeSessionRejected(std::span<const std::byte> payload);

    NegotiationResult negotiateClientHello(const ClientHello& client, const CapabilityOffer& server);
    InitialPeerProtocol classifyInitialPeerProtocol(std::span<const std::byte> bytes) noexcept;
}

#endif
