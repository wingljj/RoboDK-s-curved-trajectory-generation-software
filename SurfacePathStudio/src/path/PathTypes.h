#pragma once

#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sps {

struct GeometryTolerance {
    double linear = 1e-4;       // millimetres
    double angular = 1e-6;     // radians
    double join = 0.05;        // millimetres; topology is also required
};

enum class MotionType { Process, Transfer, Approach, Depart };
enum class ExtensionMode { None, Tangent, SurfaceConstrained };

struct SelectedFace {
    int id = -1;
    TopoDS_Face face;
};

struct SelectedSurfaceRegion {
    std::vector<SelectedFace> faces;
};

struct PathParameters {
    gp_Vec direction{1.0, 0.0, 0.0};
    std::optional<gp_Dir> referenceNormal;
    double machiningAngleDeg = 0.0;
    double stepOver = 10.0;
    double pointDistance = 2.0;
    double boundaryMargin = 0.0;
    bool toolAlongNormal = true;
    double leadAngleDeg = 0.0;
    double tiltAngleDeg = 0.0;
    double zOffset = 0.0;
    bool startFromMaxB = false;
    bool firstRowNegative = false;
    bool zigZag = true;
    ExtensionMode extensionMode = ExtensionMode::None;
    double extensionDistance = 0.0;
    double chordTolerance = 0.1;
    double normalAngleToleranceDeg = 3.0;
    GeometryTolerance tolerance;
    std::size_t maxPoints = 1000000;
    std::size_t maxLayers = 10000;
};

struct PathPoint {
    gp_Pnt surfacePoint;
    gp_Pnt tcpPoint;
    gp_Dir tangent{1.0, 0.0, 0.0};
    gp_Dir surfaceNormal{0.0, 0.0, 1.0};
    gp_Dir toolX{1.0, 0.0, 0.0};
    gp_Dir toolY{0.0, 1.0, 0.0};
    gp_Dir toolZ{0.0, 0.0, 1.0};
    gp_Quaternion orientation;
    double u = 0.0;
    double v = 0.0;
    int faceId = -1;
    int layerId = -1;
    int segmentId = -1;
    int pointId = -1;
};

struct PathSegment {
    int layerId = -1;
    int segmentId = -1;
    MotionType type = MotionType::Process;
    std::vector<PathPoint> points;
};

struct SurfacePath {
    std::vector<PathSegment> segments;
    double totalProcessLength = 0.0;
    double totalTransferLength = 0.0;
    gp_Pnt referenceCenter;
    gp_Dir referenceNormal{0.0, 0.0, 1.0};
    gp_Dir referenceTangent{1.0, 0.0, 0.0};
    gp_Dir referenceBinormal{0.0, 1.0, 0.0};
    std::vector<double> sectionOffsets;
    std::vector<std::string> warnings;

    std::size_t pointCount() const {
        std::size_t count = 0;
        for (const auto& segment : segments) count += segment.points.size();
        return count;
    }
};

using CancelCheck = std::function<bool()>;
using ProgressCallback = std::function<void(int)>;

} // namespace sps
