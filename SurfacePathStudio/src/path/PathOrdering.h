#pragma once

#include "PathTypes.h"

namespace sps {
void orderLayer(std::vector<PathSegment>& segments,
                const SelectedSurfaceRegion& region,
                const gp_Dir& tangent, bool negative,
                const PathParameters& parameters);
void finalizePath(SurfacePath& path, const PathParameters& parameters,
                  CancelCheck cancelled = {});
} // namespace sps
