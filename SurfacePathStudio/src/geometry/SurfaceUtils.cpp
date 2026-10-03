#include "SurfaceUtils.h"
#include "SurfaceNormal.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sps {
void checkCancelled(const CancelCheck& cancelled) {
    if (cancelled && cancelled()) throw std::runtime_error("Path generation cancelled");
}

double distanceToShape(const gp_Pnt& point, const TopoDS_Shape& shape) {
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(), shape);
    if (!distance.IsDone() || distance.NbSolution() == 0)
        throw std::runtime_error("Cannot compute 3D boundary distance");
    return distance.Value();
}

RegionGeometry analyzeRegion(const SelectedSurfaceRegion& region,
                             const PathParameters& parameters,
                             CancelCheck cancelled) {
    if (region.faces.empty()) throw std::runtime_error("No face selected");
    RegionGeometry result;
    BRep_Builder builder;
    builder.MakeCompound(result.compound);
    builder.MakeCompound(result.boundary);
    gp_XYZ weightedCenter(0, 0, 0);
    gp_Vec weightedNormal(0, 0, 0);
    double totalArea = 0;
    TopTools_IndexedMapOfShape uniqueFaces;
    for (const auto& selected : region.faces) {
        checkCancelled(cancelled);
        const auto& face = selected.face;
        if (face.IsNull() || !BRep_Tool::IsGeometric(face) || !BRepCheck_Analyzer(face).IsValid())
            throw std::runtime_error("Invalid selected geometry");
        if (uniqueFaces.Contains(face)) continue;
        uniqueFaces.Add(face);
        builder.Add(result.compound, face);
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        const double area = properties.Mass();
        if (!std::isfinite(area) || area <= parameters.tolerance.linear * parameters.tolerance.linear)
            throw std::runtime_error("Degenerated face");
        weightedCenter += properties.CentreOfMass().XYZ() * area;
        totalArea += area;

        if (parameters.referenceNormal) continue;
        double uMin, uMax, vMin, vMax;
        BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
        if (!std::isfinite(uMin) || !std::isfinite(uMax) || !std::isfinite(vMin) || !std::isfinite(vMax))
            throw std::runtime_error("Selected face has unbounded surface parameters");
        // Derivative-Jacobian quadrature approximates the oriented normal
        // integral. Face area supplies the inter-face weighting; no mesh is used.
        constexpr int quadratureSamples = 20;
        BRepAdaptor_Surface surface(face, Standard_True);
        gp_Vec normalIntegral(0, 0, 0);
        double jacobianIntegral = 0;
        for (int i = 0; i < quadratureSamples; ++i) {
            checkCancelled(cancelled);
            const double u = uMin + (uMax - uMin) * (i + 0.5) / quadratureSamples;
            for (int j = 0; j < quadratureSamples; ++j) {
                const double v = vMin + (vMax - vMin) * (j + 0.5) / quadratureSamples;
                SurfaceEvaluation evaluation;
                if (!evaluateSurfaceUV(face, u, v, parameters.tolerance, evaluation)) continue;
                gp_Pnt point;
                gp_Vec du, dv;
                surface.D1(u, v, point, du, dv);
                const double jacobian = du.Crossed(dv).Magnitude();
                if (!std::isfinite(jacobian) || jacobian <= Precision::Confusion()) continue;
                normalIntegral += gp_Vec(evaluation.normal) * jacobian;
                jacobianIntegral += jacobian;
            }
        }
        if (jacobianIntegral > 0) weightedNormal += normalIntegral * (area / jacobianIntegral);
        else {
            BRepExtrema_DistShapeShape nearest(
                BRepBuilderAPI_MakeVertex(properties.CentreOfMass()).Vertex(), face);
            SurfaceEvaluation evaluation;
            if (!nearest.IsDone() || nearest.NbSolution() == 0 ||
                !evaluateSurfacePoint(face, nearest.PointOnShape2(1), parameters.tolerance, evaluation))
                throw std::runtime_error("Surface normal undefined on selected region");
            weightedNormal += gp_Vec(evaluation.normal) * area;
        }
    }
    if (totalArea <= 0) throw std::runtime_error("Degenerated selected region");
    result.center = gp_Pnt(weightedCenter / totalArea);
    if (parameters.referenceNormal) result.normal = *parameters.referenceNormal;
    else {
        // A closed or substantially opposing region has no useful unique normal.
        constexpr double cancellationRatio = 1e-3;
        if (weightedNormal.Magnitude() <= totalArea * cancellationRatio)
            throw std::runtime_error("Unable to determine a unique reference normal. Please specify a reference direction.");
        result.normal = gp_Dir(weightedNormal);
    }
    BRepBndLib::AddOptimal(result.compound, result.bounds, Standard_False, Standard_False);
    result.bounds.SetGap(0.0);
    if (result.bounds.IsVoid() || result.bounds.IsOpen())
        throw std::runtime_error("Selected region has invalid or infinite bounds");

    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
    TopExp::MapShapesAndAncestors(result.compound, TopAbs_EDGE, TopAbs_FACE, adjacency);
    for (int index = 1; index <= adjacency.Extent(); ++index) {
        const TopoDS_Edge edge = TopoDS::Edge(adjacency.FindKey(index));
        if (BRep_Tool::Degenerated(edge)) continue;
        TopTools_IndexedMapOfShape owners;
        for (TopTools_ListIteratorOfListOfShape iterator(adjacency.FindFromIndex(index)); iterator.More(); iterator.Next())
            owners.Add(iterator.Value());
        if (owners.Extent() != 1) continue; // internal seam of the selected region
        if (BRep_Tool::IsClosed(edge, TopoDS::Face(owners(1)))) continue; // periodic seam
        result.boundaryEdges.push_back(edge);
        builder.Add(result.boundary, edge);
    }
    return result;
}

bool facesShareBoundaryAt(const TopoDS_Face& first, const TopoDS_Face& second,
                          const gp_Pnt& firstPoint, const gp_Pnt& secondPoint,
                          const GeometryTolerance& tolerance) {
    if (first.IsSame(second)) return firstPoint.Distance(secondPoint) <= tolerance.linear;
    TopTools_IndexedMapOfShape firstEdges, secondEdges;
    TopExp::MapShapes(first, TopAbs_EDGE, firstEdges);
    TopExp::MapShapes(second, TopAbs_EDGE, secondEdges);
    for (int index = 1; index <= firstEdges.Extent(); ++index) {
        const auto& edge = firstEdges(index);
        if (!secondEdges.Contains(edge)) continue;
        if (distanceToShape(firstPoint, edge) <= tolerance.join &&
            distanceToShape(secondPoint, edge) <= tolerance.join) return true;
    }
    return false;
}
} // namespace sps
