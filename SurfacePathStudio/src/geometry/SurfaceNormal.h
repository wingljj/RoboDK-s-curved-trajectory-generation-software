#pragma once

#include "path/PathTypes.h"

namespace sps {
struct SurfaceEvaluation {
    gp_Pnt point;
    gp_Dir normal{0.0, 0.0, 1.0};
    double u = 0.0;
    double v = 0.0;
};

gp_Dir orientedNormal(const TopoDS_Face& face, double u, double v,
                      const GeometryTolerance& tolerance = {});
bool evaluateSurfacePoint(const TopoDS_Face& face, const gp_Pnt& point,
                          const GeometryTolerance& tolerance,
                          SurfaceEvaluation& result);
bool evaluateSurfaceUV(const TopoDS_Face& face, double u, double v,
                       const GeometryTolerance& tolerance,
                       SurfaceEvaluation& result);
} // namespace sps
