#include <tes3mp/dialogue_choice_protocol.hpp>

#include <tes3mp/protocol_frame.hpp>

#include "generated/client_dialogue_choice_command_generated.h"
#include "generated/reliable_dialogue_choice_result_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace TES3MP
{
    namespace
    {
        constexpr std::size_t SizePrefixBytes = sizeof(flatbuffers::uoffset_t);
        constexpr std::size_t MinimumBufferBytes = SizePrefixBytes + sizeof(flatbuffers::uoffset_t) + 4;

        DialogueChoiceProtocolError error(DialogueChoiceProtocolErrorCode code, std::size_t observed = 0,
            std::size_t limit = 0) noexcept
        {
            return { code, observed, limit };
        }

        std::optional<DialogueChoiceProtocolError> prefix(std::span<const std::byte> payload) noexcept
        {
            if (payload.size() < MinimumBufferBytes)
                return error(DialogueChoiceProtocolErrorCode::PayloadTooSmall, payload.size(), MinimumBufferBytes);
            if (payload.size() > ReliableOperationMaximumPayloadBytes)
                return error(DialogueChoiceProtocolErrorCode::PayloadTooLarge, payload.size(),
                    ReliableOperationMaximumPayloadBytes);
            const auto declared = flatbuffers::GetSizePrefixedBufferLength(
                reinterpret_cast<const std::uint8_t*>(payload.data()));
            if (declared != payload.size())
                return error(DialogueChoiceProtocolErrorCode::PayloadLengthMismatch, payload.size(), declared);
            return std::nullopt;
        }

        flatbuffers::Verifier verifier(std::span<const std::byte> payload)
        {
            flatbuffers::Verifier::Options options;
            options.max_depth = 4;
            options.max_tables = 2;
            options.max_size = ReliableOperationMaximumPayloadBytes + 1;
            return flatbuffers::Verifier(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size(), options);
        }

        std::vector<std::byte> take(flatbuffers::FlatBufferBuilder& builder)
        {
            const auto* first = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
            return { first, first + builder.GetSize() };
        }

        template <class T>
        std::optional<T> strong(std::uint64_t value) noexcept
        {
            return T::fromValue(value);
        }
    }

    std::vector<std::byte> encodeClientDialogueChoiceCommand(const ClientDialogueChoiceCommand& value)
    {
        namespace S = Protocol::Schema::DialogueChoiceCommand;
        flatbuffers::FlatBufferBuilder builder;
        const auto root = S::CreateClientDialogueChoiceCommand(builder, value.sessionId.value(),
            value.sessionGeneration.value(), value.commandSequence.value(), value.commandId.value(),
            value.observedCanonicalRevision.value(), value.choiceId.value());
        S::FinishSizePrefixedClientDialogueChoiceCommandBuffer(builder, root);
        return take(builder);
    }

    ClientDialogueChoiceCommandDecodeResult decodeClientDialogueChoiceCommand(std::span<const std::byte> payload)
    {
        namespace S = Protocol::Schema::DialogueChoiceCommand;
        if (const auto invalid = prefix(payload))
            return *invalid;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!S::SizePrefixedClientDialogueChoiceCommandBufferHasIdentifier(bytes))
            return error(DialogueChoiceProtocolErrorCode::InvalidIdentifier);
        auto checked = verifier(payload);
        if (!S::VerifySizePrefixedClientDialogueChoiceCommandBuffer(checked))
            return error(DialogueChoiceProtocolErrorCode::VerificationFailed);
        const auto* value = S::GetSizePrefixedClientDialogueChoiceCommand(bytes);
        const auto session = strong<SessionId>(value->session_id());
        const auto generation = strong<SessionGeneration>(value->session_generation());
        const auto sequence = strong<CommandSequence>(value->command_sequence());
        const auto commandId = strong<CommandId>(value->command_id());
        const auto revision = CanonicalRevision::fromValue(value->observed_canonical_revision());
        const auto choice = strong<DialogueChoiceId>(value->choice_id());
        if (!session || !generation || !sequence || !commandId || !revision || !choice)
            return error(DialogueChoiceProtocolErrorCode::InvalidStrongValue);
        return ClientDialogueChoiceCommand{ *session, *generation, *sequence, *commandId, *revision, *choice };
    }

    std::vector<std::byte> encodeReliableDialogueChoiceResult(const ReliableDialogueChoiceResult& value)
    {
        namespace S = Protocol::Schema::DialogueChoiceResult;
        flatbuffers::FlatBufferBuilder builder;
        const auto root = S::CreateReliableDialogueChoiceResult(builder, value.targetSessionId.value(),
            value.targetSessionGeneration.value(), value.commandSequence.value(), value.commandId.value(),
            value.choiceId.value(), static_cast<S::Disposition>(value.disposition), value.duplicate,
            value.committedCanonicalRevision.value());
        S::FinishSizePrefixedReliableDialogueChoiceResultBuffer(builder, root);
        return take(builder);
    }

    ReliableDialogueChoiceResultDecodeResult decodeReliableDialogueChoiceResult(std::span<const std::byte> payload)
    {
        namespace S = Protocol::Schema::DialogueChoiceResult;
        if (const auto invalid = prefix(payload))
            return *invalid;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!S::SizePrefixedReliableDialogueChoiceResultBufferHasIdentifier(bytes))
            return error(DialogueChoiceProtocolErrorCode::InvalidIdentifier);
        auto checked = verifier(payload);
        if (!S::VerifySizePrefixedReliableDialogueChoiceResultBuffer(checked))
            return error(DialogueChoiceProtocolErrorCode::VerificationFailed);
        const auto* value = S::GetSizePrefixedReliableDialogueChoiceResult(bytes);
        const auto session = strong<SessionId>(value->target_session_id());
        const auto generation = strong<SessionGeneration>(value->target_session_generation());
        const auto sequence = strong<CommandSequence>(value->command_sequence());
        const auto commandId = strong<CommandId>(value->command_id());
        const auto choice = strong<DialogueChoiceId>(value->choice_id());
        const auto revision = CanonicalRevision::fromValue(value->committed_canonical_revision());
        if (!session || !generation || !sequence || !commandId || !choice || !revision)
            return error(DialogueChoiceProtocolErrorCode::InvalidStrongValue);
        if (value->disposition() < S::Disposition::Committed || value->disposition() > S::Disposition::Rejected)
            return error(DialogueChoiceProtocolErrorCode::UnknownDisposition,
                static_cast<std::size_t>(value->disposition()));
        return ReliableDialogueChoiceResult{ *session, *generation, *sequence, *commandId, *choice,
            static_cast<DialogueChoiceDisposition>(value->disposition()), value->duplicate(), *revision };
    }
}
