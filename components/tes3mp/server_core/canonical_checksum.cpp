#include <tes3mp/canonical_checksum.hpp>

#include <type_traits>
#include <utility>

namespace
{
    constexpr std::uint64_t Crc64EcmaPolynomial = 0x42F0E1EBA9EA3693ULL;

    constexpr std::uint64_t updateCrc64Ecma(std::uint64_t checksum, std::uint8_t byte) noexcept
    {
        checksum ^= static_cast<std::uint64_t>(byte) << 56;
        for (unsigned bit = 0; bit < 8; ++bit)
            checksum = checksum & 0x8000000000000000ULL ? (checksum << 1) ^ Crc64EcmaPolynomial : checksum << 1;
        return checksum;
    }

    class CanonicalByteWriter
    {
    public:
        template <class Value>
        void fixed(Value value)
        {
            using Unsigned = std::make_unsigned_t<Value>;
            const Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(Value); ++index)
                byte(static_cast<std::uint8_t>(bits >> (index * 8)));
        }

        void byte(std::uint8_t value) { mBytes.push_back(value); }
        std::vector<std::uint8_t> take() { return std::move(mBytes); }

    private:
        std::vector<std::uint8_t> mBytes;
    };

    class CanonicalChecksumWriter
    {
    public:
        template <class Value>
        void fixed(Value value) noexcept
        {
            using Unsigned = std::make_unsigned_t<Value>;
            const Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(Value); ++index)
                byte(static_cast<std::uint8_t>(bits >> (index * 8)));
        }

        void byte(std::uint8_t value) noexcept { mChecksum = updateCrc64Ecma(mChecksum, value); }
        constexpr TES3MP::CanonicalChecksum checksum() const noexcept { return TES3MP::CanonicalChecksum(mChecksum); }

    private:
        std::uint64_t mChecksum = 0;
    };

    template <class Writer>
    void encodeCell(Writer& writer, const TES3MP::CellId& cell)
    {
        writer.byte(static_cast<std::uint8_t>(cell.kind()));
        if (const auto* interior = cell.asInterior())
        {
            writer.fixed(interior->cellSpace().value());
            return;
        }
        const auto& exterior = *cell.asExterior();
        writer.fixed(exterior.worldspace().value());
        writer.fixed(exterior.gridX());
        writer.fixed(exterior.gridY());
    }

    template <class Writer>
    void encodeCanonicalStateV1(Writer& writer, TES3MP::CanonicalStateVersion stateVersion,
        TES3MP::ServerTick checkpointTick, const TES3MP::CanonicalServerState& state)
    {
        using namespace TES3MP;
        writer.byte('T');
        writer.byte('3');
        writer.byte('C');
        writer.byte('S');
        writer.fixed(CanonicalStateEncodingVersion);
        writer.fixed(CanonicalChecksumAlgorithmVersion);
        writer.fixed(CanonicalRulesVersion);
        writer.fixed(stateVersion.value());
        writer.fixed(checkpointTick.value());

        writer.fixed(static_cast<std::uint32_t>(state.players().size()));
        for (const CanonicalPlayerEntityState& player : state.players())
        {
            writer.fixed(player.playerId().value());
            writer.fixed(player.entityId().value());
            encodeCell(writer, player.transform().cell());
            const Position3 position = player.transform().position();
            writer.fixed(position.x());
            writer.fixed(position.y());
            writer.fixed(position.z());
            const Orientation3 orientation = player.transform().orientation();
            writer.fixed(orientation.x().value());
            writer.fixed(orientation.y().value());
            writer.fixed(orientation.z().value());
            const LinearVelocity3 velocity = player.linearVelocity();
            writer.fixed(velocity.x());
            writer.fixed(velocity.y());
            writer.fixed(velocity.z());
            writer.fixed(player.entityRevision().value());
            writer.fixed(player.authorityEpoch().value());
            writer.fixed(player.lastSpatialChangeTick().value());
        }

        writer.fixed(static_cast<std::uint32_t>(state.activeSessions().size()));
        for (const CanonicalSessionProgress& session : state.activeSessions())
        {
            writer.fixed(session.sessionId().value());
            writer.fixed(session.sessionGeneration().value());
            writer.fixed(session.playerId().value());
            writer.fixed(session.entityId().value());
            const auto acknowledgement = session.highestContiguousFinalizedCommand();
            writer.byte(acknowledgement ? 1 : 0);
            if (acknowledgement)
                writer.fixed(acknowledgement->value());
            writer.fixed(static_cast<std::uint32_t>(session.finalizedCommandHistory().size()));
            for (const FinalizedCommandRecord record : session.finalizedCommandHistory())
            {
                writer.fixed(record.commandSequence().value());
                writer.fixed(record.commandId().value());
                writer.byte(static_cast<std::uint8_t>(record.disposition()));
            }
        }

        writer.fixed(std::uint32_t{ 0 });
    }
}

namespace TES3MP
{
    std::vector<std::uint8_t> canonicalStateBytesV1(
        CanonicalStateVersion stateVersion, ServerTick checkpointTick, const CanonicalServerState& state)
    {
        CanonicalByteWriter writer;
        encodeCanonicalStateV1(writer, stateVersion, checkpointTick, state);
        return writer.take();
    }

    CanonicalChecksum crc64Ecma182(std::span<const std::uint8_t> bytes) noexcept
    {
        CanonicalChecksumWriter writer;
        for (const std::uint8_t byte : bytes)
            writer.byte(byte);
        return writer.checksum();
    }

    CanonicalChecksum canonicalStateChecksumV1(
        CanonicalStateVersion stateVersion, ServerTick checkpointTick, const CanonicalServerState& state) noexcept
    {
        CanonicalChecksumWriter writer;
        encodeCanonicalStateV1(writer, stateVersion, checkpointTick, state);
        return writer.checksum();
    }
}
