#include "path/SectionPathPlanner.h"
#include "path/PathOrdering.h"
#include "examples/ExampleModels.h"
#include "geometry/SurfaceNormal.h"
#include "geometry/SurfaceUtils.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void expectNear(double actual, double expected, double tolerance, const std::string& message) {
    require(std::abs(actual - expected) <= tolerance,
            message + " (actual " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}
TopoDS_Face plane() {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0, 100, 0, 80).Face();
}
TopoDS_Face hole() {
    BRepBuilderAPI_MakeFace face(plane());
    TopoDS_Wire inner = BRepBuilderAPI_MakeWire(
        BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(50, 40, 0), gp_Dir(0, 0, 1)), 12))).Wire();
    inner.Reverse();
    face.Add(inner);
    require(BRepCheck_Analyzer(face.Face()).IsValid(), "hole fixture must be a valid B-Rep face");
    return face.Face();
}
sps::SelectedSurfaceRegion region(const TopoDS_Face& face) {
    return {{{1, face}}};
}
sps::SelectedSurfaceRegion region(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    sps::SelectedSurfaceRegion result;
    for (int i = 1; i <= faces.Extent(); ++i) result.faces.push_back({i, TopoDS::Face(faces(i))});
    return result;
}
const sps::examples::Fixture& fixture(const std::string& name) {
    static const auto fixtures = sps::examples::fixtures();
    for (const auto& value : fixtures) if (value.fileStem == name) return value;
    throw std::runtime_error("Unknown fixture: " + name);
}
template<class Function>
void requireThrows(Function operation, const std::string& message) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, message);
}
void verifyContactPoints(const sps::SurfacePath& path, const sps::SelectedSurfaceRegion& selected) {
    require(path.pointCount() > 0, "selected CAD region must produce path points");
    for (const auto& segment : path.segments) {
        if (segment.type != sps::MotionType::Process) continue;
        require(segment.points.size() >= 2, "process segments require two or more contact points");
        for (const auto& point : segment.points) {
            const sps::SelectedFace* owner = nullptr;
            for (const auto& face : selected.faces) if (face.id == point.faceId) owner = &face;
            require(owner != nullptr, "contact point must retain its selected Face ID");
            BRepAdaptor_Surface surface(owner->face);
            require(surface.Value(point.u, point.v).Distance(point.surfacePoint) <= 2e-3,
                    "contact point must evaluate to its true CAD surface UV");
            BRepClass_FaceClassifier classifier(owner->face, gp_Pnt2d(point.u, point.v), 2e-4);
            require(classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON,
                    "contact UV must remain inside the selected trimmed Face");
            require(std::isfinite(point.surfacePoint.X()) && std::isfinite(point.surfacePoint.Y()) && std::isfinite(point.surfacePoint.Z()),
                    "path cannot contain a nonfinite contact point");
        }
    }
}
void planePath() {
    const TopoDS_Face face = plane();
    sps::SectionPathPlanner planner;
    sps::PathParameters parameters;
    const auto path = planner.generate(region(face), parameters);
    require(path.pointCount() > 0, "100 x 80 plane must produce path points");
    std::set<int> layers;
    for (const auto& segment : path.segments) {
        if (segment.type != sps::MotionType::Process) continue;
        require(segment.points.size() >= 2, "each process segment needs at least two points");
        layers.insert(segment.layerId);
        for (const auto& point : segment.points) {
            expectNear(point.surfacePoint.Z(), 0, 1e-5, "plane contact Z");
            expectNear(point.surfaceNormal.Dot(gp_Dir(0, 0, 1)), 1, 1e-7, "plane oriented normal");
            require(point.faceId == 1, "runtime face ID must be retained");
            BRepClass_FaceClassifier classifier(face, gp_Pnt2d(point.u, point.v), 1e-5);
            require(classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON,
                    "plane contact UV must lie in trimmed face");
        }
    }
    require(layers.size() == 9, "80 mm span with 10 mm stepover must produce nine nonempty layers");
    expectNear(path.totalProcessLength, 900, 1e-3, "plane process length");
}
void reversedNormal() {
    TopoDS_Face face = plane();
    face.Reverse();
    sps::SectionPathPlanner planner;
    const auto path = planner.generate(region(face), sps::PathParameters{});
    require(path.pointCount() > 0, "reversed face must produce path points");
    for (const auto& segment : path.segments) for (const auto& point : segment.points) {
        expectNear(point.surfaceNormal.Dot(gp_Dir(0, 0, -1)), 1, 1e-7, "reversed topology must flip surface normal");
    }
}
void holeClipping() {
    const TopoDS_Face face = hole();
    sps::SectionPathPlanner planner;
    const auto path = planner.generate(region(face), sps::PathParameters{});
    require(path.pointCount() > 0, "face with hole must produce path points");
    std::size_t centerSegments = 0;
    for (const auto& segment : path.segments) {
        if (segment.type != sps::MotionType::Process) continue;
        if (!segment.points.empty() && std::abs(segment.points.front().surfacePoint.Y() - 40) < 1e-4) ++centerSegments;
        for (std::size_t i = 0; i < segment.points.size(); ++i) {
            const auto& p = segment.points[i].surfacePoint;
            require(std::hypot(p.X() - 50, p.Y() - 40) >= 12 - 1e-4, "process point must remain outside circular hole");
            if (i > 0) {
                const auto& previous = segment.points[i - 1].surfacePoint;
                const gp_Pnt midpoint((p.X() + previous.X()) / 2, (p.Y() + previous.Y()) / 2, 0);
                require(std::hypot(midpoint.X() - 50, midpoint.Y() - 40) >= 12 - 1e-4,
                        "process segment must not bridge across the hole");
            }
        }
    }
    require(centerSegments == 2, "section through circular hole must remain two separate process segments");
}
void cylinderPath(bool full) {
    const auto& model = fixture(full ? "cylinder_full" : "cylinder");
    const auto selected = region(model.shape);
    const auto path = sps::SectionPathPlanner().generate(selected, model.parameters);
    verifyContactPoints(path, selected);
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) {
        for (const auto& point : segment.points) {
            const auto& p = point.surfacePoint;
            expectNear(std::hypot(p.X(), p.Y()), 30, 2e-3, "analytic cylinder radius");
            expectNear(point.surfaceNormal.Dot(gp_Dir(p.X(), p.Y(), 0)), 1, 1e-6, "analytic radial cylinder normal");
            require(p.Z() >= -1e-4 && p.Z() <= 80 + 1e-4, "cylinder height trim must be respected");
        }
    }
    if (full) {
        auto automatic = model.parameters;
        automatic.referenceNormal.reset();
        requireThrows([&] { sps::SectionPathPlanner().generate(selected, automatic); },
                      "full cylinder must request an explicit reference normal when mean normals cancel");
    }
}
void spherePath() {
    const auto& model = fixture("sphere_trimmed");
    const auto selected = region(model.shape);
    const auto path = sps::SectionPathPlanner().generate(selected, model.parameters);
    verifyContactPoints(path, selected);
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
        const auto& p = point.surfacePoint;
        expectNear(p.Distance(gp_Pnt(0, 0, 0)), 60, 2e-3, "analytic trimmed sphere radius");
        expectNear(point.surfaceNormal.Dot(gp_Dir(p.X(), p.Y(), p.Z())), 1, 1e-6, "analytic radial sphere normal");
        require(std::abs(std::atan2(p.Y(), p.X())) <= 0.75 + 1e-4 && std::abs(std::asin(p.Z() / 60)) <= 0.55 + 1e-4,
                "spherical parameter trim must be respected");
    }
}
void bsplinePath() {
    const auto& model = fixture("bspline_trimmed");
    const auto selected = region(model.shape);
    const auto path = sps::SectionPathPlanner().generate(selected, model.parameters);
    verifyContactPoints(path, selected);
    double minZ = std::numeric_limits<double>::infinity(), maxZ = -minZ;
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
        minZ = std::min(minZ, point.surfacePoint.Z());
        maxZ = std::max(maxZ, point.surfacePoint.Z());
    }
    require(maxZ - minZ > 5, "freeform path must follow the CAD surface undulations");
}
void multiFacePath() {
    const auto& model = fixture("adjacent_bspline");
    const auto selected = region(model.shape);
    const auto path = sps::SectionPathPlanner().generate(selected, model.parameters);
    verifyContactPoints(path, selected);
    bool crossedSeam = false;
    std::set<int> touchedFaces;
    for (const auto& segment : path.segments) {
        if (segment.type != sps::MotionType::Process) continue;
        std::set<int> segmentFaces;
        for (std::size_t i = 0; i < segment.points.size(); ++i) {
            const auto& point = segment.points[i];
            touchedFaces.insert(point.faceId);
            segmentFaces.insert(point.faceId);
            if (i > 0) {
                require(point.surfacePoint.Distance(segment.points[i - 1].surfacePoint) <= 2.01,
                        "connected face process samples must preserve requested maximum spacing");
                require(point.surfaceNormal.Dot(segment.points[i - 1].surfaceNormal) > 0.98,
                        "smooth BSpline seam must retain normal continuity");
            }
        }
        if (segmentFaces.size() == 2) crossedSeam = true;
    }
    require(touchedFaces.size() == 2, "multiple selection must generate contact points on both selected faces");
    require(crossedSeam, "smooth faces sharing a sewn edge must join into a continuous process segment");
}
void arbitraryTransform() {
    gp_Trsf transform;
    transform.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), 0.7);
    transform.SetTranslationPart(gp_Vec(135, -42, 77));
    const auto original = plane();
    const auto moved = TopoDS::Face(BRepBuilderAPI_Transform(original, transform, true).Shape());
    sps::PathParameters parameters;
    parameters.direction = gp_Vec(1, 0, 0).Transformed(transform);
    parameters.referenceNormal = gp_Dir(0, 0, 1).Transformed(transform);
    const auto path = sps::SectionPathPlanner().generate(region(moved), parameters);
    verifyContactPoints(path, region(moved));
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
        const auto local = point.surfacePoint.Transformed(transform.Inverted());
        expectNear(local.Z(), 0, 1e-4, "arbitrarily rotated and translated plane contact");
        expectNear(point.surfaceNormal.Dot(*parameters.referenceNormal), 1, 1e-7, "rigidly transformed normal");
        require(local.X() >= -1e-3 && local.X() <= 100 + 1e-3 && local.Y() >= -1e-3 && local.Y() <= 80 + 1e-3,
                "arbitrarily placed trim must be respected");
    }
}
void invalidParameters() {
    sps::SectionPathPlanner planner;
    const auto selected = region(plane());
    requireThrows([&] { planner.generate({}, {}); }, "empty face selection must be rejected");
    requireThrows([&] { planner.generate(region(TopoDS_Face()), {}); }, "null face must be rejected");
    const std::vector<std::function<void(sps::PathParameters&)>> invalid = {
        [](auto& p) { p.stepOver = 0; }, [](auto& p) { p.pointDistance = -1; },
        [](auto& p) { p.stepOver = std::numeric_limits<double>::quiet_NaN(); },
        [](auto& p) { p.pointDistance = std::numeric_limits<double>::infinity(); },
        [](auto& p) { p.direction = gp_Vec(0, 0, 0); }, [](auto& p) { p.direction = gp_Vec(0, 0, 1); },
        [](auto& p) { p.boundaryMargin = -1; }, [](auto& p) { p.chordTolerance = 0; },
        [](auto& p) { p.normalAngleToleranceDeg = 0; },
        [](auto& p) { p.referenceNormal = gp_Dir(std::numeric_limits<double>::quiet_NaN(), 0, 1); },
        [](auto& p) { p.extensionMode = sps::ExtensionMode::SurfaceConstrained; p.extensionDistance = 3; }
    };
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        sps::PathParameters parameters;
        invalid[i](parameters);
        requireThrows([&] { planner.generate(selected, parameters); }, "invalid parameters case " + std::to_string(i) + " must be rejected");
    }
}
void cancellation() {
    sps::SectionPathPlanner planner;
    requireThrows([&] { planner.generate(region(plane()), {}, [] { return true; }); }, "already cancelled generation must stop");
    int calls = 0;
    requireThrows([&] { planner.generate(region(plane()), {}, [&] { return ++calls > 3; }); }, "generation must check cancellation during work");
    require(calls > 3, "cancellation must be checked more than once");
}
void poseOffset() {
    sps::PathParameters parameters;
    parameters.toolAlongNormal = false;
    parameters.zOffset = 2;
    parameters.leadAngleDeg = 10;
    parameters.tiltAngleDeg = 5;
    const auto& model = fixture("bspline_trimmed");
    const auto path = sps::SectionPathPlanner().generate(region(model.shape), parameters);
    verifyContactPoints(path, region(model.shape));
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) {
        for (std::size_t i = 0; i < segment.points.size(); ++i) {
            const auto& p = segment.points[i];
            const gp_Pnt expected = p.surfacePoint.Translated(gp_Vec(p.surfaceNormal) * 2);
            require(p.tcpPoint.Distance(expected) < 1e-6, "TCP offset must preserve contact and follow surface normal");
            expectNear(p.toolX.Dot(p.toolY), 0, 1e-7, "TCP X/Y orthogonality");
            expectNear(p.toolY.Dot(p.toolZ), 0, 1e-7, "TCP Y/Z orthogonality");
            expectNear(p.toolZ.Dot(p.toolX), 0, 1e-7, "TCP Z/X orthogonality");
            expectNear(p.toolX.Crossed(p.toolY).Dot(p.toolZ), 1, 1e-7, "TCP basis must be right handed");
            expectNear(p.orientation.Norm(), 1, 1e-7, "unit quaternion");
            require((p.orientation * gp_Vec(0, 0, 1) - gp_Vec(p.toolZ)).Magnitude() < 1e-6,
                    "quaternion must encode tool Z orientation");
            if (i > 0) {
                const auto& previous = segment.points[i - 1].orientation;
                const double dot = p.orientation.X() * previous.X() + p.orientation.Y() * previous.Y()
                                 + p.orientation.Z() * previous.Z() + p.orientation.W() * previous.W();
                require(dot >= 0, "successive quaternions must retain the same hemisphere");
            }
        }
    }
    parameters.leadAngleDeg = parameters.tiltAngleDeg = 0;
    const auto flat = sps::SectionPathPlanner().generate(region(plane()), parameters);
    for (const auto& segment : flat.segments) if (segment.type == sps::MotionType::Process) for (const auto& p : segment.points)
        expectNear(p.toolZ.Dot(p.surfaceNormal), -1, 1e-7, "against surface normal tool Z");
}
void boundaryMargin() {
    sps::PathParameters parameters;
    parameters.boundaryMargin = 5;
    const auto selected = region(hole());
    const auto path = sps::SectionPathPlanner().generate(selected, parameters);
    verifyContactPoints(path, selected);
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
        const auto& p = point.surfacePoint;
        require(p.X() >= 5 - 1e-3 && p.X() <= 95 + 1e-3 && p.Y() >= 5 - 1e-3 && p.Y() <= 75 + 1e-3,
                "boundary margin must exclude samples near outer edges");
        require(std::hypot(p.X() - 50, p.Y() - 40) >= 17 - 1e-3, "boundary margin must include circular hole edge");
    }
    const auto& model = fixture("adjacent_bspline");
    const auto joined = sps::SectionPathPlanner().generate(region(model.shape), parameters);
    bool crosses = false;
    for (const auto& segment : joined.segments) if (segment.type == sps::MotionType::Process) {
        std::set<int> ids;
        for (const auto& p : segment.points) ids.insert(p.faceId);
        crosses = crosses || ids.size() == 2;
    }
    require(crosses, "selected-region margin must not remove internal shared seam");
}
void patterns() {
    sps::PathParameters parameters;
    const auto path = sps::SectionPathPlanner().generate(region(plane()), parameters);
    require(path.pointCount() > 0, "zigzag test requires generated process points");
    int lastLayer = -1, previousSign = 0;
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) {
        const int sign = segment.points.back().surfacePoint.X() > segment.points.front().surfacePoint.X() ? 1 : -1;
        if (lastLayer >= 0 && lastLayer != segment.layerId) require(sign == -previousSign, "zigzag must alternate process direction per row");
        lastLayer = segment.layerId;
        previousSign = sign;
    }
    parameters.zigZag = false;
    parameters.startFromMaxB = true;
    parameters.firstRowNegative = true;
    const auto oneWay = sps::SectionPathPlanner().generate(region(plane()), parameters);
    require(oneWay.pointCount() > 0, "one way test requires generated process points");
    double previousY = std::numeric_limits<double>::infinity();
    for (const auto& segment : oneWay.segments) if (segment.type == sps::MotionType::Process) {
        require(segment.points.back().surfacePoint.X() < segment.points.front().surfacePoint.X(), "one way negative T must apply to each layer");
        require(segment.points.front().surfacePoint.Y() <= previousY + 1e-4, "start from maximum B must order descending sections");
        previousY = segment.points.front().surfacePoint.Y();
    }
}
void safeLimits() {
    sps::PathParameters parameters;
    parameters.maxPoints = 5;
    requireThrows([&] { sps::SectionPathPlanner().generate(region(plane()), parameters); }, "point cap must stop excessive output");
    parameters.maxPoints = 1000000;
    parameters.maxLayers = 2;
    requireThrows([&] { sps::SectionPathPlanner().generate(region(plane()), parameters); }, "layer cap must stop excessive section count");
}
void tangentExtension() {
    sps::PathParameters parameters;
    parameters.extensionMode = sps::ExtensionMode::Tangent;
    parameters.extensionDistance = 5;
    const auto path = sps::SectionPathPlanner().generate(region(plane()), parameters);
    bool approach = false, depart = false;
    for (const auto& segment : path.segments) {
        approach = approach || segment.type == sps::MotionType::Approach;
        depart = depart || segment.type == sps::MotionType::Depart;
    }
    require(approach && depart, "tangent extension must create explicit approach/depart moves");
    verifyContactPoints(path, region(plane()));
}
void adaptiveSampling() {
    const auto& model = fixture("bspline_trimmed");
    const auto selected = region(model.shape);
    sps::PathParameters coarse;
    coarse.pointDistance = 20;
    coarse.chordTolerance = 10;
    coarse.normalAngleToleranceDeg = 180;
    const auto sparse = sps::SectionPathPlanner().generate(selected, coarse);
    auto tight = coarse;
    tight.chordTolerance = 0.02;
    tight.normalAngleToleranceDeg = 1;
    const auto refined = sps::SectionPathPlanner().generate(selected, tight);
    verifyContactPoints(refined, selected);
    require(refined.pointCount() > sparse.pointCount(), "tighter chord and normal limits must refine curved CAD sampling");
    constexpr double degrees = 180 / 3.14159265358979323846;
    for (const auto& segment : refined.segments) if (segment.type == sps::MotionType::Process) {
        for (std::size_t i = 1; i < segment.points.size(); ++i) {
            const auto& a = segment.points[i - 1];
            const auto& b = segment.points[i];
            const double angle = std::acos(std::clamp(a.surfaceNormal.Dot(b.surfaceNormal), -1.0, 1.0)) * degrees;
            require(angle <= 1.01, "adaptive samples must meet maximum adjacent normal angle");
            const gp_Pnt midpoint((a.surfacePoint.X() + b.surfacePoint.X()) / 2,
                                  (a.surfacePoint.Y() + b.surfacePoint.Y()) / 2,
                                  (a.surfacePoint.Z() + b.surfacePoint.Z()) / 2);
            require(sps::distanceToShape(midpoint, model.shape) <= 0.021,
                    "refined straight interpolation must remain within chord tolerance of true CAD surface");
        }
    }
}
void disconnectedIslands() {
    const gp_Pln support(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    const auto left = BRepBuilderAPI_MakeFace(support, 0, 50, 0, 80).Face();
    const auto right = BRepBuilderAPI_MakeFace(support, 50.02, 100, 0, 80).Face();
    BRep_Builder builder;
    TopoDS_Compound shape;
    builder.MakeCompound(shape);
    builder.Add(shape, left);
    builder.Add(shape, right);
    const auto selected = region(shape);
    const auto path = sps::SectionPathPlanner().generate(selected, {});
    verifyContactPoints(path, selected);
    std::set<int> faces;
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) {
        std::set<int> segmentFaces;
        for (const auto& point : segment.points) { faces.insert(point.faceId); segmentFaces.insert(point.faceId); }
        require(segmentFaces.size() == 1, "close faces without shared topology must remain separate process segments");
    }
    require(faces.size() == 2, "both disconnected islands must generate paths");
}
void startFromMaximum() {
    const auto face = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0, 100, 0, 83).Face();
    sps::PathParameters parameters;
    parameters.startFromMaxB = true;
    const auto path = sps::SectionPathPlanner().generate(region(face), parameters);
    require(path.pointCount() > 0, "maximum-side test requires path points");
    std::vector<double> rows;
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process)
        rows.push_back(segment.points.front().surfacePoint.Y());
    require(rows.size() >= 2, "maximum-side test requires multiple layers");
    expectNear(rows[0], 83, 1e-4, "first section must anchor to actual maximum selected B extent");
    expectNear(rows[1], 73, 1e-4, "subsequent maximum-side sections must keep 10 mm stepover");
}
void extremeAngles() {
    sps::PathParameters extreme;
    extreme.leadAngleDeg = 1e308;
    extreme.tiltAngleDeg = -1e308;
    auto equivalent = extreme;
    equivalent.leadAngleDeg = std::remainder(extreme.leadAngleDeg, 360.0);
    equivalent.tiltAngleDeg = std::remainder(extreme.tiltAngleDeg, 360.0);
    const auto large = sps::SectionPathPlanner().generate(region(plane()), extreme);
    const auto reduced = sps::SectionPathPlanner().generate(region(plane()), equivalent);
    require(large.pointCount() > 0 && large.pointCount() == reduced.pointCount(), "finite large angles must preserve valid path targets");
    require(large.segments.size() == reduced.segments.size(), "large angles must not change process segmentation");
    for (std::size_t i = 0; i < large.segments.size(); ++i) {
        for (std::size_t j = 0; j < large.segments[i].points.size(); ++j) {
            const auto& actual = large.segments[i].points[j];
            const auto& desired = reduced.segments[i].points[j];
            require(std::isfinite(actual.orientation.Norm()), "finite lead and tilt angles must not create NaN quaternions");
            expectNear(actual.orientation.Norm(), 1, 1e-7, "extreme-angle quaternion must remain normalized");
            require((gp_Vec(actual.toolZ) - gp_Vec(desired.toolZ)).Magnitude() < 1e-6,
                    "angle rotation must equal its finite 360-degree remainder");
            expectNear(actual.toolX.Crossed(actual.toolY).Dot(actual.toolZ), 1, 1e-7, "extreme-angle TCP must remain right handed");
        }
    }
}
void topologyNormalsOnClosedCurve() {
    const auto& model = fixture("cylinder_full");
    sps::PathParameters parameters;
    parameters.referenceNormal = gp_Dir(1, 0, 0);
    parameters.direction = gp_Vec(0, 1, 0); // section planes are horizontal circles
    parameters.pointDistance = 1000;
    parameters.chordTolerance = 100;
    parameters.normalAngleToleranceDeg = 180;
    parameters.zOffset = 2;
    const auto selected = region(model.shape);
    const auto path = sps::SectionPathPlanner().generate(selected, parameters);
    verifyContactPoints(path, selected);
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
        const auto& p = point.surfacePoint;
        const gp_Dir radial(p.X(), p.Y(), 0);
        expectNear(point.surfaceNormal.Dot(radial), 1, 1e-6,
                   "closed-curve coarse sampling must preserve topology-correct radial normals");
        const auto expected = p.Translated(gp_Vec(radial) * 2);
        require(point.tcpPoint.Distance(expected) < 1e-5,
                "large geometric normal change must not move TCP offset inward");
    }
}
void topologyNormalsAtFaceSeam() {
    const auto& model = fixture("adjacent_bspline");
    auto selected = region(model.shape);
    require(selected.faces.size() == 2, "opposite-orientation test needs two sewn faces");
    selected.faces[1].face.Reverse();
    sps::PathParameters parameters;
    parameters.referenceNormal = gp_Dir(0, 0, 1);
    parameters.normalAngleToleranceDeg = 180;
    const auto path = sps::SectionPathPlanner().generate(selected, parameters);
    verifyContactPoints(path, selected);
    std::set<int> touchedFaces;
    for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) {
        std::set<int> segmentFaces;
        for (const auto& point : segment.points) {
            touchedFaces.insert(point.faceId);
            segmentFaces.insert(point.faceId);
            const auto& face = selected.faces[static_cast<std::size_t>(point.faceId - 1)].face;
            BRepAdaptor_Surface surface(face);
            gp_Pnt position;
            gp_Vec du, dv;
            surface.D1(point.u, point.v, position, du, dv);
            gp_Dir expected(du.Crossed(dv));
            if (face.Orientation() == TopAbs_REVERSED) expected.Reverse();
            expectNear(point.surfaceNormal.Dot(expected), 1, 1e-6,
                       "merged-face pose calculation must preserve each Face's topology-correct normal");
        }
        require(segmentFaces.size() == 1, "opposite topological Face normals must remain separate process segments");
    }
    require(touchedFaces.size() == 2, "both orientations must still generate their own valid paths");
}
void seamSamplingAngle() {
    // A planar patch and a cylinder meet at a real shared edge with a 2-degree
    // normal change. Each source sample meets a 3-degree limit; dropping the
    // cylinder's seam sample would create an unapproved 4-degree transition.
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const gp_Dir seamNormal(std::sin(2 * radians), 0, std::cos(2 * radians));
    constexpr double radius = 30;
    Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(
        gp_Ax3(gp_Pnt(-radius * seamNormal.X(), 0, -radius * seamNormal.Z()),
               gp_Dir(0, 1, 0), seamNormal), radius);
    BRepBuilderAPI_Sewing sewing;
    sewing.Add(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), -10, 0, 0, 10).Face());
    sewing.Add(BRepBuilderAPI_MakeFace(cylinder, 0, 0.1, 0, 10, 1e-7).Face());
    sewing.Perform();
    const auto selected = region(sewing.SewedShape());
    require(selected.faces.size() == 2, "seam regression requires two real CAD faces");
    const sps::SelectedFace* planar = nullptr;
    const sps::SelectedFace* curved = nullptr;
    for (const auto& face : selected.faces) {
        if (BRepAdaptor_Surface(face.face).GetType() == GeomAbs_Plane) planar = &face;
        else curved = &face;
    }
    require(planar && curved, "seam fixture must contain a plane and a cylinder");
    const auto sample = [](const sps::SelectedFace& face, const gp_Pnt& position) {
        sps::SurfaceEvaluation evaluation;
        require(sps::evaluateSurfacePoint(face.face, position, {}, evaluation), "seam sample must be on its CAD face");
        sps::PathPoint point;
        point.faceId = face.id;
        point.surfacePoint = evaluation.point;
        point.surfaceNormal = evaluation.normal;
        point.u = evaluation.u;
        point.v = evaluation.v;
        gp_Pnt unused;
        gp_Vec du, dv;
        BRepAdaptor_Surface(face.face).D1(point.u, point.v, unused, du, dv);
        point.tangent = gp_Dir(du);
        return point;
    };
    sps::PathSegment left, right;
    left.points = {sample(*planar, gp_Pnt(-1, 5, 0)), sample(*planar, gp_Pnt(0, 5, 0))};
    right.points = {sample(*curved, cylinder->Value(0, 5)),
                    sample(*curved, cylinder->Value(2 * radians, 5)),
                    sample(*curved, cylinder->Value(4 * radians, 5))};
    std::vector<sps::PathSegment> segments{left, right};
    sps::PathParameters parameters;
    parameters.normalAngleToleranceDeg = 3;
    sps::orderLayer(segments, selected, gp_Dir(1, 0, 0), false, parameters);
    require(segments.size() == 1, "a smooth shared CAD edge should join process sections");
    for (std::size_t i = 1; i < segments.front().points.size(); ++i) {
        const auto& a = segments.front().points[i - 1];
        const auto& b = segments.front().points[i];
        require(a.surfaceNormal.Dot(b.surfaceNormal) >= std::cos(3.001 * radians),
                "seam deduplication must preserve the adjacent CAD normal-angle bound");
        require(a.tangent.Dot(b.tangent) >= std::cos(3.001 * radians),
                "seam deduplication must preserve the adjacent tangent-angle bound");
    }
}
void continuousTrajectory() {
    for(bool extension:{false,true}) {
        sps::PathParameters parameters;
        if(extension) {parameters.extensionMode=sps::ExtensionMode::Tangent;parameters.extensionDistance=5;}
        const auto path=sps::SectionPathPlanner().generate(region(plane()),parameters);
        int transfers=0,processes=0;
        std::size_t processPoints=0;
        gp_Quaternion previousOrientation;
        bool hasPrevious=false;
        for(std::size_t i=0;i<path.segments.size();++i) {
            const auto& segment=path.segments[i];
            if(segment.type==sps::MotionType::Process){++processes;processPoints+=segment.points.size();}
            for(const auto& point:segment.points) {
                if(hasPrevious) {
                    const auto& q=point.orientation;
                    require(q.X()*previousOrientation.X()+q.Y()*previousOrientation.Y()+q.Z()*previousOrientation.Z()+q.W()*previousOrientation.W()>=-1e-12,
                            "Quaternion signs must remain continuous across robot row changes");
                }
                previousOrientation=point.orientation;hasPrevious=true;
            }
            if(segment.type==sps::MotionType::Transfer) {
                ++transfers;
                require(i>0&&i+1<path.segments.size()&&segment.points.size()==2,"Transfer must link two machining legs");
                require(segment.points.front().tcpPoint.Distance(path.segments[i-1].points.back().tcpPoint)<1e-7,"Transfer must start at the previous robot TCP target");
                require(segment.points.back().tcpPoint.Distance(path.segments[i+1].points.front().tcpPoint)<1e-7,"Transfer must end at the next robot TCP target");
                if(extension) {
                    require(path.segments[i-1].type==sps::MotionType::Depart&&path.segments[i+1].type==sps::MotionType::Approach,
                            "Extended rows must link Depart to the next Approach");
                }
            }
            if(i>0)require(path.segments[i-1].points.back().tcpPoint.Distance(segment.points.front().tcpPoint)<1e-7,
                           "Robot trajectory must have no unconnected gap between ordered segments");
        }
        require(processes==9&&transfers==8,"Nine raster rows require eight explicit robot links");
        expectNear(path.totalProcessLength,900,1e-3,"Adding connections must preserve true on-surface machining length");
        if(!extension)expectNear(path.totalTransferLength,80,1e-3,"Eight 10-mm row changes must contribute transfer length");
        verifyContactPoints(path,region(plane()));
        if(!extension) {
            parameters.maxPoints=processPoints;
            requireThrows([&]{sps::SectionPathPlanner().generate(region(plane()),parameters);},"Point limits must include generated Transfer targets");
        }
    }
}
}

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"plane", planePath}, {"reversed", reversedNormal}, {"hole", holeClipping},
        {"cylinder", [] { cylinderPath(false); }}, {"cylinder_full", [] { cylinderPath(true); }},
        {"sphere_trimmed", spherePath}, {"bspline_trimmed", bsplinePath}, {"multiface", multiFacePath},
        {"transform", arbitraryTransform}, {"invalid_parameters", invalidParameters}, {"cancel", cancellation},
        {"pose_offset", poseOffset}, {"margin", boundaryMargin}, {"patterns", patterns},
        {"limits", safeLimits}, {"extension", tangentExtension}, {"adaptive", adaptiveSampling},
        {"disconnected", disconnectedIslands}, {"start_maximum", startFromMaximum}, {"extreme_angles", extremeAngles},
        {"topology_normals", topologyNormalsOnClosedCurve}, {"topology_seam", topologyNormalsAtFaceSeam},
        {"seam_sampling", seamSamplingAngle}, {"continuous_trajectory", continuousTrajectory}
    };
    int failed = 0;
    for (const auto& test : cases) {
        if (!filter.empty() && filter != test.first) continue;
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
        catch (...) { ++failed; std::cerr << "FAIL " << test.first << ": unexpected CAD exception\n"; }
    }
    return failed == 0 ? 0 : 1;
}
