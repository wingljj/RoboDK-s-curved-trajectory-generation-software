#pragma once

#include "path/PathTypes.h"
#include <TopoDS_Edge.hxx>

namespace sps {
class CurveSampler {
public:
    std::vector<PathPoint> sample(const TopoDS_Edge& edge,
                                  const SelectedFace& face,
                                  const PathParameters& parameters,
                                  CancelCheck cancelled = {}) const;
};
} // namespace sps
