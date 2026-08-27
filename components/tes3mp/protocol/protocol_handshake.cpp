#include <tes3mp/protocol_handshake.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/client_hello_generated.h"
#include "generated/server_hello_generated.h"
#include "generated/session_rejected_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <utility>

namespace
{
    using TES3MP::CapabilityId;
    using TES3MP::HandshakeError;
    using TES3MP::HandshakeErrorCode;
    using TES3MP::HandshakeErrorStage;

    constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
    constexpr std::size_t MinimumIdentifiedFlatBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;
    constexpr std::size_t MaximumVerifierDepth = 8;
    constexpr std::size_t MaximumVerifierTables = 8;

    constexpr HandshakeError error(HandshakeErrorStage stage, HandshakeErrorCode code, std::size_t observed = 0,
        std::size_t limit = 0, std::uint32_t capability = 0) noexcept
    {
        return HandshakeError{ stage, code, observed, limit, capability };
    }

    std::optional<HandshakeError> validatePayloadPrefix(std::span<const std::byte> payload) noexcept
    {
        if (payload.size() < MinimumIdentifiedFlatBufferBytes)
        {
            return error(HandshakeErrorStage::SizePrefix, HandshakeErrorCode::PayloadTooSmall, payload.size(),
                MinimumIdentifiedFlatBufferBytes);
        }
        if (payload.size() > TES3MP::SessionControlMaximumPayloadBytes)
        {
            return error(HandshakeErrorStage::SizePrefix, HandshakeErrorCode::PayloadTooLarge, payload.size(),
                TES3MP::SessionControlMaximumPayloadBytes);
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const std::size_t declared = flatbuffers::GetSizePrefixedBufferLength(bytes);
        if (declared != payload.size())
        {
            return error(
                HandshakeErrorStage::SizePrefix, HandshakeErrorCode::PayloadLengthMismatch, payload.size(), declared);
        }
        return std::nullopt;
    }

    flatbuffers::Verifier makeVerifier(std::span<const std::byte> payload)
    {
        flatbuffers::Verifier::Options options;
        options.max_depth = MaximumVerifierDepth;
        options.max_tables = MaximumVerifierTables;
        options.max_size = TES3MP::SessionControlMaximumPayloadBytes + 1;
        options.check_alignment = true;
        options.check_nested_flatbuffers = false;
        return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
    }

    template <class Vector>
    std::variant<std::vector<CapabilityId>, HandshakeError> decodeCapabilityVector(
        const Vector* values, std::size_t maximum)
    {
        const std::size_t size = values == nullptr ? 0 : values->size();
        if (size > maximum)
        {
            return error(
                HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::TooManyCapabilities, size, maximum);
        }

        std::vector<CapabilityId> result;
        result.reserve(size);
        std::uint32_t previous = 0;
        if (values != nullptr)
        {
            for (const std::uint32_t raw : *values)
            {
                const auto capability = CapabilityId::fromValue(raw);
                if (!capability)
                {
                    return error(
                        HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::ZeroCapability, 0, 0, raw);
                }
                if (raw <= previous)
                {
                    return error(HandshakeErrorStage::SemanticValidation,
                        HandshakeErrorCode::CapabilitiesNotStrictlySorted, raw, previous, raw);
                }
                result.push_back(*capability);
                previous = raw;
            }
        }
        return result;
    }

    std::optional<HandshakeError> validateDisjoint(
        std::span<const CapabilityId> optionalCapabilities, std::span<const CapabilityId> requiredCapabilities)
    {
        std::size_t optionalIndex = 0;
        std::size_t requiredIndex = 0;
        while (optionalIndex < optionalCapabilities.size() && requiredIndex < requiredCapabilities.size())
        {
            const auto optional = optionalCapabilities[optionalIndex];
            const auto required = requiredCapabilities[requiredIndex];
            if (optional == required)
            {
                return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::CapabilityInBothSets, 0, 0,
                    optional.value());
            }
            if (optional < required)
                ++optionalIndex;
            else
                ++requiredIndex;
        }
        return std::nullopt;
    }

    std::variant<TES3MP::CapabilityOffer, HandshakeError> makeOffer(std::uint16_t major, std::uint16_t minimumMinor,
        std::uint16_t maximumMinor, const flatbuffers::Vector<std::uint32_t>* optionalCapabilities,
        const flatbuffers::Vector<std::uint32_t>* requiredCapabilities)
    {
        auto versions = TES3MP::ProtocolVersionRange::create(major, minimumMinor, maximumMinor);
        if (const auto* rangeError = std::get_if<HandshakeError>(&versions))
            return *rangeError;

        auto optional = decodeCapabilityVector(optionalCapabilities, TES3MP::MaximumOptionalCapabilityCount);
        if (const auto* capabilityError = std::get_if<HandshakeError>(&optional))
            return *capabilityError;
        auto required = decodeCapabilityVector(requiredCapabilities, TES3MP::MaximumRequiredCapabilityCount);
        if (const auto* capabilityError = std::get_if<HandshakeError>(&required))
            return *capabilityError;

        return TES3MP::CapabilityOffer::create(std::get<TES3MP::ProtocolVersionRange>(versions),
            std::get<std::vector<CapabilityId>>(optional), std::get<std::vector<CapabilityId>>(required));
    }

    std::vector<std::uint32_t> rawCapabilities(std::span<const CapabilityId> capabilities)
    {
        std::vector<std::uint32_t> result;
        result.reserve(capabilities.size());
        for (const CapabilityId capability : capabilities)
            result.push_back(capability.value());
        return result;
    }

    std::vector<std::byte> takeBuffer(flatbuffers::FlatBufferBuilder& builder)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
        return std::vector<std::byte>(begin, begin + builder.GetSize());
    }

    std::vector<CapabilityId> supportedCapabilities(
        std::span<const CapabilityId> optionalCapabilities, std::span<const CapabilityId> requiredCapabilities)
    {
        std::vector<CapabilityId> result;
        result.reserve(optionalCapabilities.size() + requiredCapabilities.size());
        std::merge(optionalCapabilities.begin(), optionalCapabilities.end(), requiredCapabilities.begin(),
            requiredCapabilities.end(), std::back_inserter(result));
        return result;
    }

    std::optional<CapabilityId> firstMissing(
        std::span<const CapabilityId> required, std::span<const CapabilityId> supported)
    {
        for (const CapabilityId capability : required)
        {
            if (!std::binary_search(supported.begin(), supported.end(), capability))
                return capability;
        }
        return std::nullopt;
    }
}

namespace TES3MP
{
    std::variant<ProtocolVersionRange, HandshakeError> ProtocolVersionRange::create(
        std::uint16_t major, std::uint16_t minimumMinor, std::uint16_t maximumMinor) noexcept
    {
        if (minimumMinor > maximumMinor
            || static_cast<std::uint32_t>(maximumMinor) - static_cast<std::uint32_t>(minimumMinor) > 1)
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::InvalidVersionRange, minimumMinor,
                maximumMinor);
        }
        return ProtocolVersionRange(major, minimumMinor, maximumMinor);
    }

    std::variant<CapabilityOffer, HandshakeError> CapabilityOffer::create(ProtocolVersionRange versions,
        std::span<const CapabilityId> optionalCapabilities, std::span<const CapabilityId> requiredCapabilities)
    {
        if (optionalCapabilities.size() > MaximumOptionalCapabilityCount)
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::TooManyCapabilities,
                optionalCapabilities.size(), MaximumOptionalCapabilityCount);
        }
        if (requiredCapabilities.size() > MaximumRequiredCapabilityCount)
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::TooManyCapabilities,
                requiredCapabilities.size(), MaximumRequiredCapabilityCount);
        }
        if (!std::is_sorted(optionalCapabilities.begin(), optionalCapabilities.end())
            || std::adjacent_find(optionalCapabilities.begin(), optionalCapabilities.end())
                != optionalCapabilities.end()
            || !std::is_sorted(requiredCapabilities.begin(), requiredCapabilities.end())
            || std::adjacent_find(requiredCapabilities.begin(), requiredCapabilities.end())
                != requiredCapabilities.end())
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::CapabilitiesNotStrictlySorted);
        }
        if (const auto overlap = validateDisjoint(optionalCapabilities, requiredCapabilities))
            return *overlap;

        return CapabilityOffer(versions,
            std::vector<CapabilityId>(optionalCapabilities.begin(), optionalCapabilities.end()),
            std::vector<CapabilityId>(requiredCapabilities.begin(), requiredCapabilities.end()));
    }

    std::vector<std::byte> encodeClientHello(const ClientHello& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto optional = rawCapabilities(value.optionalCapabilities());
        const auto required = rawCapabilities(value.requiredCapabilities());
        const auto encodedOptional = builder.CreateVector(optional);
        const auto encodedRequired = builder.CreateVector(required);
        const auto root = Protocol::Schema::CreateClientHello(builder, value.versions().major(),
            value.versions().minimumMinor(), value.versions().maximumMinor(), encodedOptional, encodedRequired);
        Protocol::Schema::FinishSizePrefixedClientHelloBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeServerHello(const ServerHello& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto capabilities = rawCapabilities(value.negotiatedCapabilities());
        const auto root = Protocol::Schema::CreateServerHello(
            builder, value.selectedVersion().major, value.selectedVersion().minor, builder.CreateVector(capabilities));
        Protocol::Schema::FinishSizePrefixedServerHelloBuffer(builder, root);
        return takeBuffer(builder);
    }

    std::vector<std::byte> encodeSessionRejected(const SessionRejected& value)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto reason = static_cast<Protocol::Schema::SessionRejectionReason>(value.reason());
        const std::uint32_t unsupported = value.unsupportedCapability() ? value.unsupportedCapability()->value() : 0;
        const auto root = Protocol::Schema::CreateSessionRejected(builder, reason, value.serverVersions().major(),
            value.serverVersions().minimumMinor(), value.serverVersions().maximumMinor(), unsupported);
        Protocol::Schema::FinishSizePrefixedSessionRejectedBuffer(builder, root);
        return takeBuffer(builder);
    }

    ClientHelloDecodeResult decodeClientHello(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedClientHelloBufferHasIdentifier(bytes))
            return error(HandshakeErrorStage::Identifier, HandshakeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedClientHelloBuffer(verifier))
            return error(HandshakeErrorStage::Verification, HandshakeErrorCode::VerificationFailed);

        const auto* value = Protocol::Schema::GetSizePrefixedClientHello(bytes);
        auto offer = makeOffer(value->protocol_major(), value->minimum_minor(), value->maximum_minor(),
            value->optional_capabilities(), value->required_capabilities());
        if (const auto* decodeError = std::get_if<HandshakeError>(&offer))
            return *decodeError;
        return ClientHello::fromOffer(std::get<CapabilityOffer>(std::move(offer)));
    }

    ServerHelloDecodeResult decodeServerHello(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedServerHelloBufferHasIdentifier(bytes))
            return error(HandshakeErrorStage::Identifier, HandshakeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedServerHelloBuffer(verifier))
            return error(HandshakeErrorStage::Verification, HandshakeErrorCode::VerificationFailed);

        const auto* value = Protocol::Schema::GetSizePrefixedServerHello(bytes);
        auto capabilities = decodeCapabilityVector(value->negotiated_capabilities(), MaximumNegotiatedCapabilityCount);
        if (const auto* decodeError = std::get_if<HandshakeError>(&capabilities))
            return *decodeError;
        return ServerHello(ProtocolVersion{ value->protocol_major(), value->selected_minor() },
            std::get<std::vector<CapabilityId>>(std::move(capabilities)));
    }

    SessionRejectedDecodeResult decodeSessionRejected(std::span<const std::byte> payload)
    {
        if (const auto prefixError = validatePayloadPrefix(payload))
            return *prefixError;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Protocol::Schema::SizePrefixedSessionRejectedBufferHasIdentifier(bytes))
            return error(HandshakeErrorStage::Identifier, HandshakeErrorCode::InvalidIdentifier);
        auto verifier = makeVerifier(payload);
        if (!Protocol::Schema::VerifySizePrefixedSessionRejectedBuffer(verifier))
            return error(HandshakeErrorStage::Verification, HandshakeErrorCode::VerificationFailed);

        const auto* value = Protocol::Schema::GetSizePrefixedSessionRejected(bytes);
        auto versions = ProtocolVersionRange::create(
            value->server_protocol_major(), value->server_minimum_minor(), value->server_maximum_minor());
        if (const auto* decodeError = std::get_if<HandshakeError>(&versions))
            return *decodeError;

        SessionRejectionReason reason;
        switch (value->reason())
        {
            case Protocol::Schema::SessionRejectionReason::ProtocolMajorMismatch:
                reason = SessionRejectionReason::ProtocolMajorMismatch;
                break;
            case Protocol::Schema::SessionRejectionReason::NoCompatibleMinor:
                reason = SessionRejectionReason::NoCompatibleMinor;
                break;
            case Protocol::Schema::SessionRejectionReason::UnsupportedRequiredCapability:
                reason = SessionRejectionReason::UnsupportedRequiredCapability;
                break;
            default:
                return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::UnknownRejectionReason,
                    static_cast<std::size_t>(value->reason()));
        }

        const auto unsupported = CapabilityId::fromValue(value->unsupported_capability());
        if (reason == SessionRejectionReason::UnsupportedRequiredCapability && !unsupported)
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::MissingRejectionCapability);
        }
        if (reason != SessionRejectionReason::UnsupportedRequiredCapability && unsupported)
        {
            return error(HandshakeErrorStage::SemanticValidation, HandshakeErrorCode::UnexpectedRejectionCapability, 0,
                0, unsupported->value());
        }
        return SessionRejected(reason, std::get<ProtocolVersionRange>(versions), unsupported);
    }

    NegotiationResult negotiateClientHello(const ClientHello& client, const CapabilityOffer& server)
    {
        if (client.versions().major() != server.versions().major())
        {
            return SessionRejected(SessionRejectionReason::ProtocolMajorMismatch, server.versions(), std::nullopt);
        }

        const std::uint16_t lowestMinor = std::max(client.versions().minimumMinor(), server.versions().minimumMinor());
        const std::uint16_t highestMinor = std::min(client.versions().maximumMinor(), server.versions().maximumMinor());
        if (lowestMinor > highestMinor)
            return SessionRejected(SessionRejectionReason::NoCompatibleMinor, server.versions(), std::nullopt);

        const auto clientSupported
            = supportedCapabilities(client.optionalCapabilities(), client.requiredCapabilities());
        const auto serverSupported
            = supportedCapabilities(server.optionalCapabilities(), server.requiredCapabilities());
        const auto missingClientRequirement = firstMissing(client.requiredCapabilities(), serverSupported);
        const auto missingServerRequirement = firstMissing(server.requiredCapabilities(), clientSupported);
        std::optional<CapabilityId> missing;
        if (missingClientRequirement && missingServerRequirement)
            missing = std::min(*missingClientRequirement, *missingServerRequirement);
        else if (missingClientRequirement)
            missing = missingClientRequirement;
        else
            missing = missingServerRequirement;
        if (missing)
        {
            return SessionRejected(SessionRejectionReason::UnsupportedRequiredCapability, server.versions(), missing);
        }

        std::vector<CapabilityId> negotiated;
        negotiated.reserve(std::min(clientSupported.size(), serverSupported.size()));
        std::set_intersection(clientSupported.begin(), clientSupported.end(), serverSupported.begin(),
            serverSupported.end(), std::back_inserter(negotiated));
        return ServerHello(ProtocolVersion{ server.versions().major(), highestMinor }, std::move(negotiated));
    }

    InitialPeerProtocol classifyInitialPeerProtocol(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() < ProtocolFrameMagic.size())
            return InitialPeerProtocol::NeedMoreBytes;
        return std::equal(ProtocolFrameMagic.begin(), ProtocolFrameMagic.end(), bytes.begin())
            ? InitialPeerProtocol::Vnext
            : InitialPeerProtocol::LegacyOrUnknown;
    }
}
