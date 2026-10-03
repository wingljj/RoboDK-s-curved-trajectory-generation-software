#pragma once

#include "PathPlanner.h"

namespace sps {
class SectionPathPlanner final : public PathPlanner {
public:
    SurfacePath generate(const SelectedSurfaceRegion& region,
                         const PathParameters& parameters,
                         CancelCheck cancelled = {},
                         ProgressCallback progress = {}) const override;
};
} // namespace sps
