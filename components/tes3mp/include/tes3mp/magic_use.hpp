#ifndef TES3MP_MAGIC_USE_HPP
#define TES3MP_MAGIC_USE_HPP

#include "command_primitives.hpp"
#include "session_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace TES3MP
{
    enum class MagicUseSourceKind : std::uint8_t
    {
        Spell = 0,
        EnchantedItem = 1,
    };

    enum class MagicUseTargetKind : std::uint8_t
    {
        Self = 0,
        Player = 1,
        Actor = 2,
    };

    enum class MagicUseDecodeErrorCode : std::uint8_t
    {
        PayloadTooSmall,
        PayloadTooLarge,
        PayloadLengthMismatch,
        InvalidIdentifier,
        VerificationFailed,
        MissingHeader,
        InvalidStrongValue,
        InvalidSourceKind,
        InvalidTargetKind,
        InvalidSource,
        InvalidTarget,
    };

    struct MagicUseDecodeError
    {
        MagicUseDecodeErrorCode code = MagicUseDecodeErrorCode::VerificationFailed;
        std::size_t observed = 0;
        std::size_t limit = 0;

        friend constexpr bool operator==(MagicUseDecodeError, MagicUseDecodeError) noexcept = default;
    };

    struct ClientMagicUseCommand
    {
        SessionId sessionId;
        SessionGeneration sessionGeneration;
        CommandSequence commandSequence;
        CommandId commandId;
        CanonicalRevision observedCanonicalRevision;
        MagicUseSourceKind sourceKind = MagicUseSourceKind::Spell;
        std::uint64_t sourceId = 0;
        MagicUseTargetKind targetKind = MagicUseTargetKind::Self;
        std::uint64_t targetId = 0;
        ServerTick sourceServerTick = ServerTick::initial();
        CombatRevision expectedCasterRevision = CombatRevision::initial();
        CombatRevision expectedTargetRevision = CombatRevision::initial();
        InventoryRevision expectedInventoryRevision = InventoryRevision::initial();

        friend constexpr bool operator==(ClientMagicUseCommand, ClientMagicUseCommand) noexcept = default;
    };

    std::vector<std::byte> encodeClientMagicUseCommand(const ClientMagicUseCommand& value);
    std::variant<ClientMagicUseCommand, MagicUseDecodeError> decodeClientMagicUseCommand(
        std::span<const std::byte> payload);
}

#endif
