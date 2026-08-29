#include <tes3mp/authentication.hpp>
#include <tes3mp/protocol_frame.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    using namespace TES3MP;

    static_assert(!std::is_copy_constructible_v<AuthenticationMaterial>);
    static_assert(!std::is_copy_constructible_v<ResumeToken>);
    static_assert(!std::is_copy_constructible_v<AuthenticationRequest>);
    static_assert(!std::is_copy_constructible_v<AuthenticationAcceptedMessage>);

    std::vector<std::byte> bytes(std::size_t size, unsigned seed)
    {
        std::vector<std::byte> result(size);
        for (std::size_t index = 0; index < size; ++index)
            result[index] = static_cast<std::byte>((seed + index * 29u) & 0xffu);
        return result;
    }

    AuthenticationMaterial material(std::span<const std::byte> value)
    {
        return std::move(*AuthenticationMaterial::create(value));
    }

    ResumeToken token(unsigned seed)
    {
        const auto value = bytes(ResumeTokenBytes, seed);
        return std::move(*ResumeToken::create(value));
    }

    bool values_round_trip_without_exposing_secret_views()
    {
        const auto password = bytes(MaximumAuthenticationMaterialBytes, 17);
        const auto joinBytes = encodeAuthenticationRequest(AuthenticationRequest::join(material(password)));
        auto decodedJoin = decodeAuthenticationRequest(joinBytes);
        auto* join = std::get_if<AuthenticationRequest>(&decodedJoin);
        if (join == nullptr || join->kind() != AuthenticationCredentialKind::JoinPassword
            || join->materialSize() != MaximumAuthenticationMaterialBytes
            || encodeAuthenticationRequest(*join) != joinBytes)
            return false;

        const auto resumeBytes = encodeAuthenticationRequest(AuthenticationRequest::resume(token(31)));
        auto decodedResume = decodeAuthenticationRequest(resumeBytes);
        auto* resume = std::get_if<AuthenticationRequest>(&decodedResume);
        if (resume == nullptr || resume->kind() != AuthenticationCredentialKind::ResumeToken
            || resume->materialSize() != ResumeTokenBytes || encodeAuthenticationRequest(*resume) != resumeBytes)
            return false;

        auto accepted = AuthenticationAcceptedMessage::create(token(47), MaximumResumeTokenLifetimeMilliseconds);
        const auto acceptedBytes = encodeAuthenticationAccepted(*accepted);
        auto decodedAccepted = decodeAuthenticationAccepted(acceptedBytes);
        auto* acceptedValue = std::get_if<AuthenticationAcceptedMessage>(&decodedAccepted);
        if (acceptedValue == nullptr || acceptedValue->lifetimeMilliseconds() != MaximumResumeTokenLifetimeMilliseconds
            || encodeAuthenticationAccepted(*acceptedValue) != acceptedBytes)
            return false;

        for (const auto reason :
            { AuthenticationPublicRejection::Denied, AuthenticationPublicRejection::TemporarilyUnavailable })
        {
            const auto encoded = encodeAuthenticationRejected({ reason });
            const auto decoded = decodeAuthenticationRejected(encoded);
            const auto* rejected = std::get_if<AuthenticationRejectedMessage>(&decoded);
            if (rejected == nullptr || rejected->reason != reason)
                return false;
        }
        return true;
    }

    bool exact_bounds_are_enforced()
    {
        const auto empty = AuthenticationMaterial::create({});
        const auto maximum = bytes(MaximumAuthenticationMaterialBytes, 1);
        const auto oversized = bytes(MaximumAuthenticationMaterialBytes + 1, 1);
        const auto shortToken = bytes(ResumeTokenBytes - 1, 1);
        const auto exactToken = bytes(ResumeTokenBytes, 1);
        const auto longToken = bytes(ResumeTokenBytes + 1, 1);
        if (!empty || !AuthenticationMaterial::create(maximum) || AuthenticationMaterial::create(oversized)
            || ResumeToken::create(shortToken) || !ResumeToken::create(exactToken) || ResumeToken::create(longToken))
            return false;

        auto minimumToken = ResumeToken::create(exactToken);
        auto belowMinimum = AuthenticationAcceptedMessage::create(
            std::move(*minimumToken), MinimumResumeTokenLifetimeMilliseconds - 1);
        auto maximumToken = ResumeToken::create(exactToken);
        auto aboveMaximum = AuthenticationAcceptedMessage::create(
            std::move(*maximumToken), MaximumResumeTokenLifetimeMilliseconds + 1);
        auto validToken = ResumeToken::create(exactToken);
        auto valid
            = AuthenticationAcceptedMessage::create(std::move(*validToken), MinimumResumeTokenLifetimeMilliseconds);
        return !belowMinimum && !aboveMaximum && valid.has_value();
    }

    template <class Decode>
    bool rejects_all_truncations_identifier_and_trailing(const std::vector<std::byte>& encoded, Decode&& decode)
    {
        for (std::size_t size = 0; size < encoded.size(); ++size)
        {
            if (!std::holds_alternative<AuthenticationCodecError>(decode(std::span(encoded).first(size))))
                return false;
        }
        auto identifier = encoded;
        identifier[8] ^= std::byte{ 0xff };
        if (!std::holds_alternative<AuthenticationCodecError>(decode(identifier)))
            return false;
        auto trailing = encoded;
        trailing.push_back(std::byte{ 0 });
        return std::holds_alternative<AuthenticationCodecError>(decode(trailing));
    }

    bool malformed_payloads_fail_closed()
    {
        const auto request = encodeAuthenticationRequest(AuthenticationRequest::resume(token(3)));
        auto acceptedValue = AuthenticationAcceptedMessage::create(token(5), MinimumResumeTokenLifetimeMilliseconds);
        const auto accepted = encodeAuthenticationAccepted(*acceptedValue);
        const auto rejected = encodeAuthenticationRejected({ AuthenticationPublicRejection::Denied });
        const std::vector<std::byte> oversized(SessionControlMaximumPayloadBytes + 1);
        return rejects_all_truncations_identifier_and_trailing(
                   request, [](auto value) { return decodeAuthenticationRequest(value); })
            && rejects_all_truncations_identifier_and_trailing(
                accepted, [](auto value) { return decodeAuthenticationAccepted(value); })
            && rejects_all_truncations_identifier_and_trailing(
                rejected, [](auto value) { return decodeAuthenticationRejected(value); })
            && std::holds_alternative<AuthenticationCodecError>(decodeAuthenticationRequest(oversized))
            && std::holds_alternative<AuthenticationCodecError>(decodeAuthenticationAccepted(oversized))
            && std::holds_alternative<AuthenticationCodecError>(decodeAuthenticationRejected(oversized));
    }

    bool every_single_bit_mutation_rejects_or_normalizes()
    {
        const auto request = encodeAuthenticationRequest(AuthenticationRequest::resume(token(13)));
        for (std::size_t index = 0; index < request.size(); ++index)
        {
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                auto mutated = request;
                mutated[index] ^= static_cast<std::byte>(1u << bit);
                auto decoded = decodeAuthenticationRequest(mutated);
                if (auto* value = std::get_if<AuthenticationRequest>(&decoded))
                {
                    auto normalized = decodeAuthenticationRequest(encodeAuthenticationRequest(*value));
                    if (!std::holds_alternative<AuthenticationRequest>(normalized))
                        return false;
                }
            }
        }
        return true;
    }

    bool writeFile(const std::filesystem::path& path, std::span<const std::byte> value)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
        return stream.good();
    }

    bool writeCorpus(const std::filesystem::path& directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
        auto accepted = AuthenticationAcceptedMessage::create(token(23), MinimumResumeTokenLifetimeMilliseconds);
        auto empty = AuthenticationMaterial::create({});
        return writeFile(directory / "valid-join-password",
                   encodeAuthenticationRequest(AuthenticationRequest::join(std::move(*empty))))
            && writeFile(
                directory / "valid-resume-token", encodeAuthenticationRequest(AuthenticationRequest::resume(token(19))))
            && writeFile(directory / "valid-authentication-accepted", encodeAuthenticationAccepted(*accepted))
            && writeFile(directory / "valid-authentication-rejected",
                encodeAuthenticationRejected({ AuthenticationPublicRejection::Denied }));
    }

    std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::nullopt;
        std::vector<std::byte> value(static_cast<std::size_t>(size));
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
        if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            return std::nullopt;
        return value;
    }

    bool verifyCorpus(const std::filesystem::path& directory)
    {
        const auto join = readFile(directory / "valid-join-password");
        const auto resume = readFile(directory / "valid-resume-token");
        const auto accepted = readFile(directory / "valid-authentication-accepted");
        const auto rejected = readFile(directory / "valid-authentication-rejected");
        return join && std::holds_alternative<AuthenticationRequest>(decodeAuthenticationRequest(*join)) && resume
            && std::holds_alternative<AuthenticationRequest>(decodeAuthenticationRequest(*resume)) && accepted
            && std::holds_alternative<AuthenticationAcceptedMessage>(decodeAuthenticationAccepted(*accepted))
            && rejected
            && std::holds_alternative<AuthenticationRejectedMessage>(decodeAuthenticationRejected(*rejected));
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-corpus")
        return writeCorpus(argv[2]) ? 0 : 1;
    if (argc == 3 && std::string_view(argv[1]) == "--verify-corpus")
        return verifyCorpus(argv[2]) ? 0 : 1;

    return values_round_trip_without_exposing_secret_views() && exact_bounds_are_enforced()
            && malformed_payloads_fail_closed() && every_single_bit_mutation_rejects_or_normalizes()
        ? 0
        : 1;
}
