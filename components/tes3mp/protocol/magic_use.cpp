#include <tes3mp/magic_use.hpp>

#include "generated/client_magic_use_command_generated.h"

#include <tes3mp/protocol_frame.hpp>

#include <flatbuffers/flatbuffers.h>

#include <cstring>
#include <type_traits>

namespace
{
    namespace Schema = TES3MP::Protocol::Schema::MagicUseCommand;
    using Code = TES3MP::MagicUseDecodeErrorCode;

    TES3MP::MagicUseDecodeError error(Code code, std::size_t observed = 0, std::size_t limit = 0)
    {
        return { code, observed, limit };
    }

    template <class Strong>
    std::optional<Strong> strong(std::uint64_t raw)
    {
        return Strong::fromValue(raw);
    }

    std::optional<TES3MP::MagicUseDecodeError> prefix(std::span<const std::byte> payload, std::size_t maximum)
    {
        if (payload.size() < sizeof(flatbuffers::uoffset_t))
            return error(Code::PayloadTooSmall, payload.size(), sizeof(flatbuffers::uoffset_t));
        if (payload.size() > maximum)
            return error(Code::PayloadTooLarge, payload.size(), maximum);
        flatbuffers::uoffset_t declared = 0;
        std::memcpy(&declared, payload.data(), sizeof(declared));
        declared = flatbuffers::EndianScalar(declared);
        if (static_cast<std::size_t>(declared) + sizeof(declared) != payload.size())
            return error(Code::PayloadLengthMismatch, payload.size(), declared + sizeof(declared));
        return std::nullopt;
    }
}

namespace TES3MP
{
    std::vector<std::byte> encodeClientMagicUseCommand(const ClientMagicUseCommand& input)
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto header
            = Schema::CreateClientCommandHeader(builder, input.sessionId.value(), input.sessionGeneration.value(),
                input.commandSequence.value(), input.commandId.value(), input.observedCanonicalRevision.value());
        const auto root = Schema::CreateClientMagicUseCommand(builder, header,
            static_cast<Schema::MagicUseSourceKind>(input.sourceKind), input.sourceId,
            static_cast<Schema::MagicUseTargetKind>(input.targetKind), input.targetId, input.sourceServerTick.value(),
            input.expectedCasterRevision.value(), input.expectedTargetRevision.value(),
            input.expectedInventoryRevision.value());
        Schema::FinishSizePrefixedClientMagicUseCommandBuffer(builder, root);
        std::vector<std::byte> result(builder.GetSize());
        std::memcpy(result.data(), builder.GetBufferPointer(), builder.GetSize());
        return result;
    }

    std::variant<ClientMagicUseCommand, MagicUseDecodeError> decodeClientMagicUseCommand(
        std::span<const std::byte> payload)
    {
        if (auto failure = prefix(payload, ReliableOperationMaximumPayloadBytes))
            return *failure;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        if (!Schema::SizePrefixedClientMagicUseCommandBufferHasIdentifier(bytes))
            return error(Code::InvalidIdentifier);
        flatbuffers::Verifier verifier(bytes, payload.size(), 64, 1024);
        if (!Schema::VerifySizePrefixedClientMagicUseCommandBuffer(verifier))
            return error(Code::VerificationFailed);
        const auto* root = Schema::GetSizePrefixedClientMagicUseCommand(bytes);
        if (!root->header())
            return error(Code::MissingHeader);
        const auto session = strong<SessionId>(root->header()->session_id());
        const auto generation = strong<SessionGeneration>(root->header()->session_generation());
        const auto sequence = strong<CommandSequence>(root->header()->command_sequence());
        const auto command = strong<CommandId>(root->header()->command_id());
        const auto canonical = strong<CanonicalRevision>(root->header()->observed_canonical_revision());
        const auto tick = strong<ServerTick>(root->source_server_tick());
        const auto casterRevision = strong<CombatRevision>(root->expected_caster_revision());
        const auto targetRevision = strong<CombatRevision>(root->expected_target_revision());
        const auto inventoryRevision = strong<InventoryRevision>(root->expected_inventory_revision());
        if (!session || !generation || !sequence || !command || !canonical || !tick || !casterRevision
            || !targetRevision || !inventoryRevision)
            return error(Code::InvalidStrongValue);
        if (static_cast<std::uint8_t>(root->source_kind())
            > static_cast<std::uint8_t>(MagicUseSourceKind::EnchantedItem))
            return error(Code::InvalidSourceKind, static_cast<std::size_t>(root->source_kind()));
        if (static_cast<std::uint8_t>(root->target_kind()) > static_cast<std::uint8_t>(MagicUseTargetKind::Actor))
            return error(Code::InvalidTargetKind, static_cast<std::size_t>(root->target_kind()));
        if (root->source_id() == 0)
            return error(Code::InvalidSource);
        const auto targetKind = static_cast<MagicUseTargetKind>(root->target_kind());
        if ((targetKind == MagicUseTargetKind::Self) != (root->target_id() == 0))
            return error(Code::InvalidTarget);
        return ClientMagicUseCommand{ *session, *generation, *sequence, *command, *canonical,
            static_cast<MagicUseSourceKind>(root->source_kind()), root->source_id(), targetKind, root->target_id(),
            *tick, *casterRevision, *targetRevision, *inventoryRevision };
    }
}
