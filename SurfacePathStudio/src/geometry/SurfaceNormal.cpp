#include "SurfaceNormal.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sps {
namespace {
bool inside(const TopoDS_Face& face, const gp_Pnt& point, double u, double v,
            double linearTolerance) {
    // UV has arbitrary units. Only a parameter-space numerical tolerance is
    // passed to the classifier; near-boundary acceptance is checked in 3D mm.
    BRepClass_FaceClassifier classifier(face, gp_Pnt2d(u, v), Precision::PConfusion(),
                                        Standard_True);
    if (classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON) return true;
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(), face);
    return distance.IsDone() && distance.NbSolution() > 0 && distance.Value() <= linearTolerance;
}
}

gp_Dir orientedNormal(const TopoDS_Face& face, double u, double v,
                      const GeometryTolerance&) {
    BRepAdaptor_Surface surface(face, Standard_True);
    BRepLProp_SLProps properties(surface, u, v, 2, Precision::Confusion());
    if (!properties.IsNormalDefined()) throw std::runtime_error("Surface normal undefined");
    gp_Dir normal = properties.Normal();
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    return normal;
}

bool evaluateSurfaceUV(const TopoDS_Face& face, double u, double v,
                       const GeometryTolerance& tolerance,
                       SurfaceEvaluation& result) {
    if (face.IsNull() || !std::isfinite(u) || !std::isfinite(v)) return false;
    try {
        BRepAdaptor_Surface surface(face, Standard_True);
        const gp_Pnt point = surface.Value(u, v);
        const double linear = std::max(tolerance.linear, BRep_Tool::Tolerance(face));
        if (!inside(face, point, u, v, linear)) return false;
        result.point = point;
        result.u = u;
        result.v = v;
        result.normal = orientedNormal(face, u, v, tolerance);
        return true;
    } catch (const Standard_Failure&) {
        return false;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool evaluateSurfacePoint(const TopoDS_Face& face, const gp_Pnt& point,
                          const GeometryTolerance& tolerance,
                          SurfaceEvaluation& result) {
    if (face.IsNull()) return false;
    try {
        double uMin, uMax, vMin, vMax;
        BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
        const double linear = std::max(tolerance.linear, BRep_Tool::Tolerance(face));
        // Surface(face) applies TopLoc_Location. Both input and reconstructed
        // points are in CAD world coordinates, including rotated assemblies.
        GeomAPI_ProjectPointOnSurf projection(point, BRep_Tool::Surface(face),
                                              uMin, uMax, vMin, vMax, Precision::Confusion());
        if (!projection.IsDone() || projection.NbPoints() == 0) return false;
        double bestDistance = linear;
        bool found = false;
        for (int index = 1; index <= projection.NbPoints(); ++index) {
            const double distance = projection.Distance(index);
            if (!std::isfinite(distance) || distance > bestDistance) continue;
            double u, v;
            projection.Parameters(index, u, v);
            SurfaceEvaluation candidate;
            if (!evaluateSurfaceUV(face, u, v, tolerance, candidate)) continue;
            bestDistance = distance;
            result = candidate;
            found = true;
        }
        return found;
    } catch (const Standard_Failure&) {
        return false;
    }
}
} // namespace sps
