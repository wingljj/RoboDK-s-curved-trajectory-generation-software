#pragma once

#include "path/PathTypes.h"
#include <Bnd_Box.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>

namespace sps {
struct RegionGeometry {
    TopoDS_Compound compound;
    TopoDS_Compound boundary;
    std::vector<TopoDS_Edge> boundaryEdges;
    Bnd_Box bounds;
    gp_Pnt center;
    gp_Dir normal{0.0, 0.0, 1.0};
};

RegionGeometry analyzeRegion(const SelectedSurfaceRegion& region,
                             const PathParameters& parameters,
                             CancelCheck cancelled = {});
double distanceToShape(const gp_Pnt& point, const TopoDS_Shape& shape);
bool facesShareBoundaryAt(const TopoDS_Face& first, const TopoDS_Face& second,
                          const gp_Pnt& firstPoint, const gp_Pnt& secondPoint,
                          const GeometryTolerance& tolerance);
void checkCancelled(const CancelCheck& cancelled);
} // namespace sps
