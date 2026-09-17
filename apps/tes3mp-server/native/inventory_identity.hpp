#ifndef TES3MP_NATIVE_INVENTORY_IDENTITY_H
#define TES3MP_NATIVE_INVENTORY_IDENTITY_H
#include <cstdint>
namespace TES3MP::Native
{
    struct InventoryInstanceId
    {
        std::uint32_t mIndex = 0;
        std::int32_t mContentFile = -1;
        bool operator==(const InventoryInstanceId&) const = default;
    };
}
#endif
