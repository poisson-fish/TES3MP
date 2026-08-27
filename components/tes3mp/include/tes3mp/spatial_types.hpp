#ifndef TES3MP_SPATIAL_TYPES_HPP
#define TES3MP_SPATIAL_TYPES_HPP

#include "value_types.hpp"

#include <compare>
#include <cstdint>
#include <variant>

namespace TES3MP
{
    class InteriorCell
    {
    public:
        constexpr explicit InteriorCell(CellSpaceId cellSpace) noexcept
            : mCellSpace(cellSpace)
        {
        }

        constexpr CellSpaceId cellSpace() const noexcept { return mCellSpace; }

        friend constexpr bool operator==(InteriorCell, InteriorCell) noexcept = default;
        friend constexpr auto operator<=>(InteriorCell, InteriorCell) noexcept = default;

    private:
        CellSpaceId mCellSpace;
    };

    class ExteriorCell
    {
    public:
        constexpr ExteriorCell(CellSpaceId worldspace, std::int32_t gridX, std::int32_t gridY) noexcept
            : mWorldspace(worldspace)
            , mGridX(gridX)
            , mGridY(gridY)
        {
        }

        constexpr CellSpaceId worldspace() const noexcept { return mWorldspace; }
        constexpr std::int32_t gridX() const noexcept { return mGridX; }
        constexpr std::int32_t gridY() const noexcept { return mGridY; }

        friend constexpr bool operator==(ExteriorCell, ExteriorCell) noexcept = default;
        friend constexpr auto operator<=>(ExteriorCell, ExteriorCell) noexcept = default;

    private:
        CellSpaceId mWorldspace;
        std::int32_t mGridX;
        std::int32_t mGridY;
    };

    class CellId
    {
    public:
        enum class Kind : std::uint8_t
        {
            Interior = 0,
            Exterior = 1,
        };

        static constexpr CellId interior(CellSpaceId cellSpace) noexcept
        {
            return CellId(InteriorCell(cellSpace));
        }

        static constexpr CellId exterior(CellSpaceId worldspace, std::int32_t gridX, std::int32_t gridY) noexcept
        {
            return CellId(ExteriorCell(worldspace, gridX, gridY));
        }

        constexpr Kind kind() const noexcept
        {
            return std::holds_alternative<InteriorCell>(mValue) ? Kind::Interior : Kind::Exterior;
        }

        constexpr const InteriorCell* asInterior() const noexcept { return std::get_if<InteriorCell>(&mValue); }
        constexpr const ExteriorCell* asExterior() const noexcept { return std::get_if<ExteriorCell>(&mValue); }

        friend constexpr bool operator==(const CellId&, const CellId&) noexcept = default;
        friend constexpr auto operator<=>(const CellId&, const CellId&) noexcept = default;

    private:
        constexpr explicit CellId(InteriorCell value) noexcept
            : mValue(value)
        {
        }

        constexpr explicit CellId(ExteriorCell value) noexcept
            : mValue(value)
        {
        }

        std::variant<InteriorCell, ExteriorCell> mValue;
    };

    class Position3
    {
    public:
        constexpr Position3(std::int64_t x, std::int64_t y, std::int64_t z) noexcept
            : mX(x)
            , mY(y)
            , mZ(z)
        {
        }

        constexpr std::int64_t x() const noexcept { return mX; }
        constexpr std::int64_t y() const noexcept { return mY; }
        constexpr std::int64_t z() const noexcept { return mZ; }

        friend constexpr bool operator==(Position3, Position3) noexcept = default;
        friend constexpr auto operator<=>(Position3, Position3) noexcept = default;

    private:
        std::int64_t mX;
        std::int64_t mY;
        std::int64_t mZ;
    };

    class Turn32
    {
    public:
        using Value = std::uint32_t;

        static constexpr Turn32 fromValue(Value value) noexcept { return Turn32(value); }

        static constexpr Turn32 fromUnnormalized(std::uint64_t value) noexcept
        {
            return Turn32(static_cast<Value>(value));
        }

        constexpr Value value() const noexcept { return mValue; }

        friend constexpr bool operator==(Turn32, Turn32) noexcept = default;
        friend constexpr auto operator<=>(Turn32, Turn32) noexcept = default;

    private:
        constexpr explicit Turn32(Value value) noexcept
            : mValue(value)
        {
        }

        Value mValue;
    };

    class Orientation3
    {
    public:
        constexpr Orientation3(Turn32 x, Turn32 y, Turn32 z) noexcept
            : mX(x)
            , mY(y)
            , mZ(z)
        {
        }

        constexpr Turn32 x() const noexcept { return mX; }
        constexpr Turn32 y() const noexcept { return mY; }
        constexpr Turn32 z() const noexcept { return mZ; }

        friend constexpr bool operator==(Orientation3, Orientation3) noexcept = default;
        friend constexpr auto operator<=>(Orientation3, Orientation3) noexcept = default;

    private:
        Turn32 mX;
        Turn32 mY;
        Turn32 mZ;
    };

    class Transform
    {
    public:
        constexpr Transform(CellId cell, Position3 position, Orientation3 orientation) noexcept
            : mCell(cell)
            , mPosition(position)
            , mOrientation(orientation)
        {
        }

        constexpr const CellId& cell() const noexcept { return mCell; }
        constexpr Position3 position() const noexcept { return mPosition; }
        constexpr Orientation3 orientation() const noexcept { return mOrientation; }

        friend constexpr bool operator==(const Transform&, const Transform&) noexcept = default;
        friend constexpr auto operator<=>(const Transform&, const Transform&) noexcept = default;

    private:
        CellId mCell;
        Position3 mPosition;
        Orientation3 mOrientation;
    };

    class LinearVelocity3
    {
    public:
        constexpr LinearVelocity3(std::int64_t x, std::int64_t y, std::int64_t z) noexcept
            : mX(x)
            , mY(y)
            , mZ(z)
        {
        }

        constexpr std::int64_t x() const noexcept { return mX; }
        constexpr std::int64_t y() const noexcept { return mY; }
        constexpr std::int64_t z() const noexcept { return mZ; }

        friend constexpr bool operator==(LinearVelocity3, LinearVelocity3) noexcept = default;
        friend constexpr auto operator<=>(LinearVelocity3, LinearVelocity3) noexcept = default;

    private:
        std::int64_t mX;
        std::int64_t mY;
        std::int64_t mZ;
    };
}

#endif
