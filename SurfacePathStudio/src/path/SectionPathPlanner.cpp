#include "SectionPathPlanner.h"
#include "PathOrdering.h"
#include "geometry/CurveSampler.h"
#include "geometry/SurfaceNormal.h"
#include "geometry/SurfaceUtils.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRep_Tool.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sps {
namespace {
class SectionProgress final : public Message_ProgressIndicator {
public:
    explicit SectionProgress(CancelCheck cancelled) : cancelled_(std::move(cancelled)) {}
protected:
    Standard_Boolean UserBreak() override { return cancelled_ && cancelled_(); }
    void Show(const Message_ProgressScope&, Standard_Boolean) override {}
private:
    CancelCheck cancelled_;
};

void validate(const PathParameters& p) {
    const auto finite = [](double value) { return std::isfinite(value); };
    if (!finite(p.stepOver) || p.stepOver <= 0) throw std::runtime_error("Step over must be positive and finite");
    if (!finite(p.pointDistance) || p.pointDistance <= 0) throw std::runtime_error("Point distance must be positive and finite");
    if (!finite(p.chordTolerance) || p.chordTolerance <= 0) throw std::runtime_error("Chord tolerance must be positive and finite");
    if (!finite(p.normalAngleToleranceDeg) || p.normalAngleToleranceDeg <= 0 || p.normalAngleToleranceDeg > 180)
        throw std::runtime_error("Normal angle tolerance must be in (0, 180] degrees");
    if (!finite(p.boundaryMargin) || p.boundaryMargin < 0) throw std::runtime_error("Boundary margin must be nonnegative and finite");
    if (!finite(p.extensionDistance) || p.extensionDistance < 0) throw std::runtime_error("Extension distance must be nonnegative and finite");
    if (!finite(p.machiningAngleDeg) || !finite(p.leadAngleDeg) || !finite(p.tiltAngleDeg) || !finite(p.zOffset))
        throw std::runtime_error("Angles and tool offset must be finite");
    if (!finite(p.tolerance.linear) || p.tolerance.linear <= 0 || !finite(p.tolerance.angular) || p.tolerance.angular <= 0 ||
        !finite(p.tolerance.join) || p.tolerance.join < p.tolerance.linear)
        throw std::runtime_error("Invalid geometry tolerances");
    if (p.maxPoints < 2 || p.maxLayers < 1) throw std::runtime_error("Invalid path resource limits");
    if (p.extensionMode == ExtensionMode::SurfaceConstrained)
        throw std::runtime_error("Surface-constrained extension is not implemented");
    if (!finite(p.direction.X()) || !finite(p.direction.Y()) || !finite(p.direction.Z()))
        throw std::runtime_error("Invalid path direction");
    if (p.referenceNormal && (!finite(p.referenceNormal->X()) || !finite(p.referenceNormal->Y()) ||
                              !finite(p.referenceNormal->Z())))
        throw std::runtime_error("Invalid reference normal: coordinates must be finite");
}

const SelectedFace* findEdgeFace(BRepAlgoAPI_Section& section, const TopoDS_Edge& edge,
                                 const SelectedSurfaceRegion& region,
                                 const GeometryTolerance& tolerance) {
    TopoDS_Shape ancestor;
    if (section.HasAncestorFaceOn1(edge, ancestor))
        for (const auto& face : region.faces) if (ancestor.IsSame(face.face)) return &face;
    // Common blocks/original boundary edges have no section ancestry.
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter(), last = curve.LastParameter();
    for (const auto& face : region.faces) {
        SurfaceEvaluation evaluation;
        bool onFace = true;
        for (double fraction : {0.2, 0.5, 0.8}) {
            if (!evaluateSurfacePoint(face.face, curve.Value(first + fraction * (last - first)), tolerance, evaluation)) {
                onFace = false;
                break;
            }
        }
        if (onFace) return &face;
    }
    return nullptr;
}
}

SurfacePath SectionPathPlanner::generate(const SelectedSurfaceRegion& region,
                                        const PathParameters& parameters,
                                        CancelCheck cancelled,
                                        ProgressCallback progress) const {
    validate(parameters);
    checkCancelled(cancelled);
    if (progress) progress(0);
    try {
        const RegionGeometry geometry = analyzeRegion(region, parameters, cancelled);
        gp_Vec tangent = parameters.direction;
        const double scale = std::max({std::abs(tangent.X()), std::abs(tangent.Y()), std::abs(tangent.Z())});
        if (scale == 0) throw std::runtime_error("Invalid path direction: zero vector");
        tangent /= scale;
        const gp_Vec normal(geometry.normal);
        tangent -= normal * tangent.Dot(normal);
        if (tangent.Magnitude() <= parameters.tolerance.angular)
            throw std::runtime_error("Path direction is parallel to the reference normal. Choose another direction.");
        tangent.Normalize();
        constexpr double pi = 3.14159265358979323846;
        tangent.Rotate(gp_Ax1(geometry.center, geometry.normal),
                       std::remainder(parameters.machiningAngleDeg, 360.0) * pi / 180.0);
        gp_Vec binormal = normal.Crossed(tangent);
        binormal.Normalize();
        tangent = binormal.Crossed(normal);
        tangent.Normalize();

        double xMin, yMin, zMin, xMax, yMax, zMax;
        geometry.bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -minimum;
        for (double x : {xMin, xMax}) for (double y : {yMin, yMax}) for (double z : {zMin, zMax}) {
            const double offset = gp_Vec(geometry.center, gp_Pnt(x, y, z)).Dot(binormal);
            minimum = std::min(minimum, offset);
            maximum = std::max(maximum, offset);
        }
        const double layerCount = std::floor((maximum - minimum + parameters.tolerance.linear) / parameters.stepOver) + 1;
        if (!std::isfinite(layerCount) || layerCount > static_cast<double>(parameters.maxLayers) ||
            layerCount > static_cast<double>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Section layer limit exceeded; increase step over");
        SurfacePath path;
        path.referenceCenter = geometry.center;
        path.referenceNormal = geometry.normal;
        path.referenceTangent = gp_Dir(tangent);
        path.referenceBinormal = gp_Dir(binormal);
        for (int index = 0; index < static_cast<int>(layerCount); ++index)
            path.sectionOffsets.push_back(parameters.startFromMaxB
                ? maximum - index * parameters.stepOver : minimum + index * parameters.stepOver);

        CurveSampler sampler;
        std::size_t totalPoints = 0;
        for (std::size_t layer = 0; layer < path.sectionOffsets.size(); ++layer) {
            checkCancelled(cancelled);
            const gp_Pnt planePoint = geometry.center.Translated(binormal * path.sectionOffsets[layer]);
            // Intersect actual trimmed B-Rep Faces; never read triangulation.
            BRepAlgoAPI_Section section(geometry.compound, gp_Pln(planePoint, gp_Dir(binormal)), Standard_False);
            section.SetNonDestructive(Standard_True);
            section.Approximation(Standard_True);
            section.ComputePCurveOn1(Standard_True);
            Handle(Message_ProgressIndicator) indicator = new SectionProgress(cancelled);
            section.Build(indicator->Start());
            checkCancelled(cancelled);
            if (!section.IsDone()) throw std::runtime_error("OCCT could not build the B-Rep section");
            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(section.Shape(), TopAbs_EDGE, edges);
            std::vector<PathSegment> segments;
            for (int index = 1; index <= edges.Extent(); ++index) {
                checkCancelled(cancelled);
                const TopoDS_Edge edge = TopoDS::Edge(edges(index));
                if (BRep_Tool::Degenerated(edge)) continue;
                const SelectedFace* face = findEdgeFace(section, edge, region, parameters.tolerance);
                if (!face) throw std::runtime_error("Cannot identify section edge's trimmed source Face");
                auto points = sampler.sample(edge, *face, parameters, cancelled);
                PathSegment current;
                current.layerId = static_cast<int>(layer);
                const auto flush = [&]() {
                    if (current.points.size() >= 2) {
                        totalPoints += current.points.size();
                        if (totalPoints > parameters.maxPoints) throw std::runtime_error("Path point limit exceeded");
                        segments.push_back(std::move(current));
                    }
                    current = PathSegment{};
                    current.layerId = static_cast<int>(layer);
                };
                for (auto& point : points) {
                    checkCancelled(cancelled);
                    if (parameters.boundaryMargin > 0 && !geometry.boundaryEdges.empty() &&
                        distanceToShape(point.surfacePoint, geometry.boundary) < parameters.boundaryMargin) {
                        flush();
                        continue;
                    }
                    current.points.push_back(std::move(point));
                }
                flush();
            }
            const bool negative = parameters.firstRowNegative != (parameters.zigZag && (layer % 2 == 1));
            orderLayer(segments, region, gp_Dir(tangent), negative, parameters);
            for (auto& segment : segments) path.segments.push_back(std::move(segment));
            if (progress) progress(static_cast<int>((layer + 1) * 95 / path.sectionOffsets.size()));
        }
        if (path.segments.empty()) throw std::runtime_error("No intersection curves found or path contains no valid points");
        finalizePath(path, parameters, cancelled);
        if (parameters.extensionMode == ExtensionMode::Tangent && parameters.extensionDistance > 0)
            path.warnings.push_back("Tangent extensions are off-surface Approach/Depart moves; no collision or clearance checks are performed.");
        if (progress) progress(100);
        return path;
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("OCCT geometry error: ") + error.GetMessageString());
    }
}
} // namespace sps
