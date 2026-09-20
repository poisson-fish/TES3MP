#include "settings.hpp"

#include <components/misc/constants.hpp>
#include <components/settings/values.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace DetourNavigator
{
    namespace
    {
        struct NavMeshLimits
        {
            int mMaxTiles;
            int mMaxPolys;
        };

        template <class T>
        unsigned long getMinValuableBitsNumber(const T value)
        {
            unsigned long power = 0;
            while (power < sizeof(T) * 8 && (static_cast<T>(1) << power) < value)
                ++power;
            return power;
        }

        NavMeshLimits getNavMeshTileLimits(const DetourSettings& settings)
        {
            // Max tiles and max polys affect how the tile IDs are caculated.
            // There are 22 bits available for identifying a tile and a polygon.
            constexpr int polysAndTilesBits = 22;
            const unsigned long polysBits = getMinValuableBitsNumber(settings.mMaxPolys);

            if (polysBits >= polysAndTilesBits)
                throw std::invalid_argument("Too many polygons per tile: " + std::to_string(settings.mMaxPolys));

            const unsigned long tilesBits = polysAndTilesBits - polysBits;

            return NavMeshLimits{
                .mMaxTiles = static_cast<int>(1 << tilesBits),
                .mMaxPolys = static_cast<int>(1 << polysBits),
            };
        }

        RecastSettings makeRecastSettings(const ::Settings::NavigatorCategory& category, Debug::Level maxLogLevel)
        {
            RecastSettings result;

            result.mBorderSize = category.mBorderSize;
            result.mCellHeight = category.mCellHeight;
            result.mCellSize = category.mCellSize;
            result.mDetailSampleDist = category.mDetailSampleDist;
            result.mDetailSampleMaxError = category.mDetailSampleMaxError;
            result.mMaxClimb = Constants::sStepSizeUp;
            result.mMaxSimplificationError = category.mMaxSimplificationError;
            result.mMaxSlope = Constants::sMaxSlope;
            result.mRecastScaleFactor = category.mRecastScaleFactor;
            result.mSwimHeightScale = 0;
            result.mMaxEdgeLen = category.mMaxEdgeLen;
            result.mMaxVertsPerPoly = category.mMaxVertsPerPoly;
            result.mRegionMergeArea = category.mRegionMergeArea;
            result.mRegionMinArea = category.mRegionMinArea;
            result.mTileSize = category.mTileSize;
            result.mMaxLogLevel = maxLogLevel;

            return result;
        }

        DetourSettings makeDetourSettings(const ::Settings::NavigatorCategory& category)
        {
            DetourSettings result;

            result.mMaxNavMeshQueryNodes = category.mMaxNavMeshQueryNodes;
            result.mMaxPolys = category.mMaxPolygonsPerTile;
            result.mMaxPolygonPathSize = category.mMaxPolygonPathSize;
            result.mMaxSmoothPathSize = category.mMaxSmoothPathSize;

            return result;
        }
    }

    Settings makeSettingsFromSettingsManager(Debug::Level maxLogLevel)
    {
        return makeSettings(::Settings::navigator(), maxLogLevel);
    }

    Settings makeSettings(const ::Settings::NavigatorCategory& category, Debug::Level maxLogLevel)
    {
        Settings result;

        result.mRecast = makeRecastSettings(category, maxLogLevel);
        result.mDetour = makeDetourSettings(category);

        const NavMeshLimits limits = getNavMeshTileLimits(result.mDetour);

        result.mDetour.mMaxPolys = limits.mMaxPolys;

        result.mMaxTilesNumber = std::min(limits.mMaxTiles, category.mMaxTilesNumber.get());
        result.mWaitUntilMinDistanceToPlayer = category.mWaitUntilMinDistanceToPlayer;
        result.mAsyncNavMeshUpdaterThreads = category.mAsyncNavMeshUpdaterThreads;
        result.mMaxNavMeshTilesCacheSize = category.mMaxNavMeshTilesCacheSize;
        result.mEnableWriteRecastMeshToFile = category.mEnableWriteRecastMeshToFile;
        result.mEnableWriteNavMeshToFile = category.mEnableWriteNavMeshToFile;
        result.mRecastMeshPathPrefix = category.mRecastMeshPathPrefix;
        result.mNavMeshPathPrefix = category.mNavMeshPathPrefix;
        result.mEnableRecastMeshFileNameRevision = category.mEnableRecastMeshFileNameRevision;
        result.mEnableNavMeshFileNameRevision = category.mEnableNavMeshFileNameRevision;
        result.mMinUpdateInterval = std::chrono::milliseconds(category.mMinUpdateIntervalMs);
        result.mEnableNavMeshDiskCache = category.mEnableNavMeshDiskCache;
        result.mWriteToNavMeshDb = category.mWriteToNavmeshdb;
        result.mMaxDbFileSize = category.mMaxNavmeshdbFileSize;

        if (result.mMaxTilesNumber < category.mMaxTilesNumber.get())
            Log(Debug::Warning)
                << "Navigator max tiles number is adjusted due to limitation on number of bits for tile identifier: "
                << result.mMaxTilesNumber;

        return result;
    }
}
