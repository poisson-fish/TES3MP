#ifndef TES3MP_SERVER_CONTENT_COLLISION_HPP
#define TES3MP_SERVER_CONTENT_COLLISION_HPP

#include <tes3mp/content_identity.hpp>
#include <tes3mp/movement_kernel.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

namespace TES3MP::ServerApp
{
    inline constexpr std::size_t MaximumCollisionContentBytes = 2 * 1024 * 1024;
    inline constexpr std::size_t MaximumCollisionBoxes = 32 * 1024;
    inline constexpr std::int64_t MaximumCollisionCoordinate = 2'147'483'647;

    enum class ContentCollisionError
    {
        Unavailable,
        TooLarge,
        Malformed,
        ManifestMismatch,
        IncompleteCells,
    };

    class ContentCollisionProvider final : public ServerCollisionQuery
    {
    public:
        static std::variant<std::unique_ptr<ContentCollisionProvider>, ContentCollisionError> load(
            const std::filesystem::path& path, const ContentManifest& manifest) noexcept;

        bool canOccupy(const CellId& cell, Position3 position) const noexcept;
        std::optional<ServerCollisionResult> resolve(const ServerCollisionRequest& request) noexcept override;

    private:
        struct CollisionBox
        {
            CellId cell;
            Position3 minimum;
            Position3 maximum;

            friend constexpr bool operator==(const CollisionBox&, const CollisionBox&) noexcept = default;
            friend constexpr auto operator<=>(const CollisionBox&, const CollisionBox&) noexcept = default;
        };

        ContentCollisionProvider(ContentManifestId manifest, std::vector<CellId> cells,
            std::vector<CollisionBox> boxes) noexcept
            : mManifest(manifest), mCells(std::move(cells)), mBoxes(std::move(boxes))
        {
        }

        ContentManifestId mManifest;
        std::vector<CellId> mCells;
        std::vector<CollisionBox> mBoxes;
    };
}

#endif
