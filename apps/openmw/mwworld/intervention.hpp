#ifndef OPENMW_MWWORLD_INTERVENTION_HPP
#define OPENMW_MWWORLD_INTERVENTION_HPP
#include "worldmodel.hpp"
#include "cellstore.hpp"
#include <components/esm/util.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
namespace MWWorld
{
    inline ConstPtr closestExteriorMarker(WorldModel& model, const osg::Vec3f& worldPos, const ESM::RefId& id)
    {
        const ESM::ExteriorCellLocation posIndex = ESM::positionToExteriorCellLocation(worldPos.x(), worldPos.y());

        // Potential optimization: don't scan the entire world for markers and actually do the Todd spiral
        std::vector<Ptr> markers;
        model.getExteriorPtrs(id, markers);

        struct MarkerInfo
        {
            Ptr mPtr;
            int mColumn, mRow; // Local coordinates in the valid marker grid
        };
        std::vector<MarkerInfo> validMarkers;
        validMarkers.reserve(markers.size());

        // The idea is to collect all markers that belong to the smallest possible square grid around worldPos
        // They are grouped with their position on that grid's edge where the origin is the SW corner
        int minGridSize = std::numeric_limits<int>::max();
        for (const Ptr& marker : markers)
        {
            const osg::Vec3f markerPos = marker.getRefData().getPosition().asVec3();
            const ESM::ExteriorCellLocation index = ESM::positionToExteriorCellLocation(markerPos.x(), markerPos.y());

            const int deltaX = index.mX - posIndex.mX;
            const int deltaY = index.mY - posIndex.mY;
            const int gridSize = std::max(std::abs(deltaX), std::abs(deltaY)) * 2;
            if (gridSize == 0)
                return marker;

            if (gridSize <= minGridSize)
            {
                if (gridSize < minGridSize)
                {
                    validMarkers.clear();
                    minGridSize = gridSize;
                }
                validMarkers.push_back({ marker, gridSize / 2 + deltaX, gridSize / 2 + deltaY });
            }
        }

        ConstPtr closestMarker;
        if (validMarkers.empty())
            return closestMarker;
        if (validMarkers.size() == 1)
            return validMarkers[0].mPtr;

        // All the markers are on the edge of the grid
        // Break ties by picking the earliest marker on SW -> SE -> NE -> NW -> SW path
        int earliestDistance = std::numeric_limits<int>::max();
        for (const MarkerInfo& marker : validMarkers)
        {
            int distance = 0;
            if (marker.mRow == 0) // South edge (plus SW and SE corners)
                distance = marker.mColumn;
            else if (marker.mColumn == minGridSize) // East edge and NE corner
                distance = minGridSize + marker.mRow;
            else if (marker.mRow == minGridSize) // North edge and NW corner
                distance = minGridSize * 3 - marker.mColumn;
            else // West edge
                distance = minGridSize * 4 - marker.mRow;
            if (distance < earliestDistance)
            {
                closestMarker = marker.mPtr;
                earliestDistance = distance;
            }
        }
        return closestMarker;
    }
    // Stock cell breadth-first search and exterior grid ordering, with the
    // initiating player's exterior position supplied by the caller.
    inline ConstPtr closestInterventionMarker(WorldModel& model, ESM::RefId startCell,
        const osg::Vec3f& exteriorPosition, const ESM::RefId& id, size_t cellBudget = SIZE_MAX)
    {
        if (model.getCell(startCell).isExterior()) return closestExteriorMarker(model, exteriorPosition, id);
        // Search for a 'nearest' marker, counting each cell between the starting
        // cell and the exterior as a distance of 1.  If an exterior is found, jump
        // to the nearest exterior marker, without further interior searching.
        std::set<ESM::RefId> checkedCells;
        std::set<ESM::RefId> currentCells;
        std::set<ESM::RefId> nextCells;
        MWWorld::ConstPtr closestMarker;

        nextCells.insert(startCell);
        while (!nextCells.empty())
        {
            currentCells.clear();
            std::swap(currentCells, nextCells);
            for (const auto& cell : currentCells)
            {
                MWWorld::CellStore& next = model.getCell(cell);
                if (checkedCells.size() >= cellBudget) throw std::invalid_argument("Intervention cell search budget exhausted");
                checkedCells.insert(cell);

                closestMarker = next.searchConst(id);
                if (!closestMarker.isEmpty())
                {
                    return closestMarker;
                }

                // Check if any door in the cell leads to an exterior directly
                for (const MWWorld::LiveCellRef<ESM::Door>& ref : next.getReadOnlyDoors().mList)
                {
                    if (!ref.mRef.getTeleport())
                        continue;

                    if (ref.mRef.getDestCell().is<ESM::ESM3ExteriorCellRefId>())
                    {
                        osg::Vec3f worldPos = ref.mRef.getDoorDest().asVec3();
                        return closestExteriorMarker(model, worldPos, id);
                    }
                    else
                    {
                        const auto& dest = ref.mRef.getDestCell();
                        if (!checkedCells.contains(dest) && !currentCells.contains(dest))
                            nextCells.insert(dest);
                    }
                }
            }
        }
        return MWWorld::Ptr();
    }
}
#endif
