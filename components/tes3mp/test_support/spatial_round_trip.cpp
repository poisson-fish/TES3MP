#include <tes3mp/test_support/spatial_round_trip.hpp>

#include <bit>
#include <cstdint>
#include <type_traits>

namespace
{
    template <class Value>
        requires(std::is_unsigned_v<Value>)
    void appendUnsigned(std::vector<std::byte>& output, Value value)
    {
        for (std::size_t index = 0; index < sizeof(Value); ++index)
        {
            output.push_back(static_cast<std::byte>(value & static_cast<Value>(0xff)));
            value >>= 8;
        }
    }

    template <class Value>
        requires(std::is_signed_v<Value>)
    void appendSigned(std::vector<std::byte>& output, Value value)
    {
        appendUnsigned(output, std::bit_cast<std::make_unsigned_t<Value>>(value));
    }

    class Reader
    {
    public:
        explicit Reader(std::span<const std::byte> bytes)
            : mBytes(bytes)
        {
        }

        template <class Value>
            requires(std::is_unsigned_v<Value>)
        std::optional<Value> readUnsigned()
        {
            if (mBytes.size() - mOffset < sizeof(Value))
                return std::nullopt;

            Value result = 0;
            for (std::size_t index = 0; index < sizeof(Value); ++index)
            {
                result |= static_cast<Value>(std::to_integer<unsigned char>(mBytes[mOffset + index]))
                    << (index * 8);
            }
            mOffset += sizeof(Value);
            return result;
        }

        template <class Value>
            requires(std::is_signed_v<Value>)
        std::optional<Value> readSigned()
        {
            const auto bits = readUnsigned<std::make_unsigned_t<Value>>();
            if (!bits)
                return std::nullopt;
            return std::bit_cast<Value>(*bits);
        }

        bool finished() const noexcept { return mOffset == mBytes.size(); }

    private:
        std::span<const std::byte> mBytes;
        std::size_t mOffset = 0;
    };
}

namespace TES3MP::TestSupport
{
    std::vector<std::byte> encodeSpatialEntitySnapshot(const SpatialEntitySnapshot& snapshot)
    {
        std::vector<std::byte> output;
        output.reserve(109);

        appendUnsigned(output, snapshot.serverTick().value());
        appendUnsigned(output, snapshot.entityId().value());
        appendUnsigned(output, snapshot.entityRevision().value());
        appendUnsigned(output, snapshot.authorityEpoch().value());

        const CellId& cell = snapshot.transform().cell();
        appendUnsigned(output, static_cast<std::uint8_t>(cell.kind()));
        if (const auto* interior = cell.asInterior())
        {
            appendUnsigned(output, interior->cellSpace().value());
        }
        else
        {
            const auto& exterior = *cell.asExterior();
            appendUnsigned(output, exterior.worldspace().value());
            appendSigned(output, exterior.gridX());
            appendSigned(output, exterior.gridY());
        }

        const Position3 position = snapshot.transform().position();
        appendSigned(output, position.x());
        appendSigned(output, position.y());
        appendSigned(output, position.z());

        const Orientation3 orientation = snapshot.transform().orientation();
        appendUnsigned(output, orientation.x().value());
        appendUnsigned(output, orientation.y().value());
        appendUnsigned(output, orientation.z().value());

        const LinearVelocity3 velocity = snapshot.linearVelocity();
        appendSigned(output, velocity.x());
        appendSigned(output, velocity.y());
        appendSigned(output, velocity.z());
        return output;
    }

    std::optional<SpatialEntitySnapshot> decodeSpatialEntitySnapshot(std::span<const std::byte> bytes)
    {
        Reader reader(bytes);
        const auto tickValue = reader.readUnsigned<std::uint64_t>();
        const auto entityValue = reader.readUnsigned<std::uint64_t>();
        const auto revisionValue = reader.readUnsigned<std::uint64_t>();
        const auto epochValue = reader.readUnsigned<std::uint64_t>();
        const auto cellKind = reader.readUnsigned<std::uint8_t>();
        const auto cellSpaceValue = reader.readUnsigned<std::uint64_t>();
        if (!tickValue || !entityValue || !revisionValue || !epochValue || !cellKind || !cellSpaceValue)
            return std::nullopt;

        const auto tick = ServerTick::fromValue(*tickValue);
        const auto entity = EntityId::fromValue(*entityValue);
        const auto revision = EntityRevision::fromValue(*revisionValue);
        const auto epoch = AuthorityEpoch::fromValue(*epochValue);
        const auto cellSpace = CellSpaceId::fromValue(*cellSpaceValue);
        if (!tick || !entity || !revision || !epoch || !cellSpace)
            return std::nullopt;

        CellId cell = CellId::interior(*cellSpace);
        if (*cellKind == static_cast<std::uint8_t>(CellId::Kind::Exterior))
        {
            const auto gridX = reader.readSigned<std::int32_t>();
            const auto gridY = reader.readSigned<std::int32_t>();
            if (!gridX || !gridY)
                return std::nullopt;
            cell = CellId::exterior(*cellSpace, *gridX, *gridY);
        }
        else if (*cellKind != static_cast<std::uint8_t>(CellId::Kind::Interior))
            return std::nullopt;

        const auto positionX = reader.readSigned<std::int64_t>();
        const auto positionY = reader.readSigned<std::int64_t>();
        const auto positionZ = reader.readSigned<std::int64_t>();
        const auto rotationX = reader.readUnsigned<std::uint32_t>();
        const auto rotationY = reader.readUnsigned<std::uint32_t>();
        const auto rotationZ = reader.readUnsigned<std::uint32_t>();
        const auto velocityX = reader.readSigned<std::int64_t>();
        const auto velocityY = reader.readSigned<std::int64_t>();
        const auto velocityZ = reader.readSigned<std::int64_t>();
        if (!positionX || !positionY || !positionZ || !rotationX || !rotationY || !rotationZ || !velocityX
            || !velocityY || !velocityZ || !reader.finished())
            return std::nullopt;

        return SpatialEntitySnapshot(*tick, *entity, *revision, *epoch,
            Transform(cell, Position3(*positionX, *positionY, *positionZ),
                Orientation3(Turn32::fromValue(*rotationX), Turn32::fromValue(*rotationY),
                    Turn32::fromValue(*rotationZ))),
            LinearVelocity3(*velocityX, *velocityY, *velocityZ));
    }
}
