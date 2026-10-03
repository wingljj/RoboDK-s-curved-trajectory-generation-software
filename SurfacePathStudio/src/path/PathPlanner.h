#pragma once
#include "PathTypes.h"

namespace sps {
class PathPlanner {
public:
    virtual ~PathPlanner() = default;
    virtual SurfacePath generate(const SelectedSurfaceRegion& region,
                                 const PathParameters& parameters,
                                 CancelCheck cancelled = {},
                                 ProgressCallback progress = {}) const = 0;
};
// Future strategies remain separate implementations of PathPlanner.
class GeodesicOffsetPathPlanner;
class IsoParametricPathPlanner;
class ContourParallelPathPlanner;
} // namespace sps
