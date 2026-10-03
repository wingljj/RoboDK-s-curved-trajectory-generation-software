#include "PathOrdering.h"
#include "PoseGenerator.h"
#include "geometry/SurfaceUtils.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sps {
namespace {
const TopoDS_Face* faceForId(const SelectedSurfaceRegion& region, int id) {
    for (const auto& selected : region.faces) if (selected.id == id) return &selected.face;
    return nullptr;
}
void reverseSegment(PathSegment& segment) {
    std::reverse(segment.points.begin(), segment.points.end());
    for (auto& point : segment.points) point.tangent.Reverse();
}
double coordinate(const gp_Pnt& point, const gp_Dir& tangent) {
    return point.XYZ().Dot(tangent.XYZ());
}
bool canJoin(const PathSegment& a, const PathSegment& b,
             const SelectedSurfaceRegion& region, const PathParameters& parameters) {
    if (a.points.empty() || b.points.empty()) return false;
    const auto& end = a.points.back();
    const auto& start = b.points.front();
    if (end.surfacePoint.Distance(start.surfacePoint) > parameters.tolerance.join) return false;
    // Keep sharp corners and disconnected islands as separate process segments.
    constexpr double pi = 3.14159265358979323846;
    // Match sampling's normal hemisphere bound. Opposite topological Face
    // normals must remain separate instead of being sign-flipped after joining.
    constexpr double normalHemisphereLimitDeg = 89.0;
    const double smoothAngle = std::min(parameters.normalAngleToleranceDeg, normalHemisphereLimitDeg) * pi / 180.0;
    if (end.surfaceNormal.Dot(start.surfaceNormal) < std::cos(smoothAngle) ||
        end.tangent.Dot(start.tangent) < std::cos(smoothAngle)) return false;
    const auto* first = faceForId(region, end.faceId);
    const auto* second = faceForId(region, start.faceId);
    return first && second && facesShareBoundaryAt(*first, *second, end.surfacePoint,
                                                   start.surfacePoint, parameters.tolerance);
}
}

void orderLayer(std::vector<PathSegment>& segments,
                const SelectedSurfaceRegion& region,
                const gp_Dir& tangent, bool negative,
                const PathParameters& parameters) {
    for (auto& segment : segments) {
        if (segment.points.size() < 2) continue;
        const double forward = coordinate(segment.points.back().surfacePoint, tangent) -
                               coordinate(segment.points.front().surfacePoint, tangent);
        const double direction = std::abs(forward) <= parameters.tolerance.linear
            ? segment.points.front().tangent.Dot(tangent) : forward;
        if ((negative && direction > 0) || (!negative && direction < 0)) reverseSegment(segment);
    }
    bool joined = true;
    while (joined) {
        joined = false;
        for (std::size_t i = 0; i < segments.size() && !joined; ++i) {
            if (segments[i].points.front().surfacePoint.Distance(segments[i].points.back().surfacePoint) <=
                parameters.tolerance.linear) continue;
            for (std::size_t j = 0; j < segments.size(); ++j) {
                if (i == j || !canJoin(segments[i], segments[j], region, parameters)) continue;
                auto& destination = segments[i].points;
                auto& source = segments[j].points;
                const bool samePoint = destination.back().surfacePoint.Distance(source.front().surfacePoint) <=
                                       parameters.tolerance.linear;
                bool removeDuplicate = samePoint;
                if (removeDuplicate && source.size() > 1) {
                    // The seam has two authoritative CAD normals. Removing one
                    // must not combine two permitted angular changes into a
                    // larger, unsampled transition to the next source point.
                    constexpr double pi = 3.14159265358979323846;
                    const double cosine = std::cos(std::min(parameters.normalAngleToleranceDeg, 89.0) * pi / 180.0);
                    const auto& previous = destination.back();
                    const auto& next = source[1];
                    removeDuplicate = previous.surfaceNormal.Dot(next.surfaceNormal) >= cosine &&
                                      previous.tangent.Dot(next.tangent) >= cosine;
                }
                destination.insert(destination.end(), source.begin() + (removeDuplicate ? 1 : 0), source.end());
                segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(j));
                joined = true;
                break;
            }
        }
    }
    const auto sortCoordinate = [&](const PathSegment& segment) {
        double value = coordinate(segment.points.front().surfacePoint, tangent);
        for (const auto& point : segment.points)
            value = negative ? std::max(value, coordinate(point.surfacePoint, tangent))
                             : std::min(value, coordinate(point.surfacePoint, tangent));
        return value;
    };
    std::sort(segments.begin(), segments.end(), [&](const auto& a, const auto& b) {
        return negative ? sortCoordinate(a) > sortCoordinate(b) : sortCoordinate(a) < sortCoordinate(b);
    });
}

void finalizePath(SurfacePath& path, const PathParameters& parameters,
                  CancelCheck cancelled) {
    std::vector<PathSegment> finalSegments;
    for (auto& segment : path.segments) {
        checkCancelled(cancelled);
        if (segment.points.size() < 2) continue;
        generatePoses(segment, parameters);
        const bool extension = parameters.extensionMode == ExtensionMode::Tangent &&
                               parameters.extensionDistance > 0;
        if (extension) {
            const PathPoint endpoint = segment.points.front();
            PathPoint external = endpoint;
            // Contact stays at the process endpoint; only TCP extrapolates.
            external.tcpPoint.Translate(gp_Vec(endpoint.tangent) * -parameters.extensionDistance);
            PathSegment approach;
            approach.layerId = segment.layerId;
            approach.type = MotionType::Approach;
            approach.points = {external, endpoint};
            finalSegments.push_back(std::move(approach));
        }
        const PathPoint endpoint = segment.points.back();
        finalSegments.push_back(std::move(segment));
        if (extension) {
            PathPoint external = endpoint;
            external.tcpPoint.Translate(gp_Vec(endpoint.tangent) * parameters.extensionDistance);
            PathSegment depart;
            depart.layerId = finalSegments.back().layerId;
            depart.type = MotionType::Depart;
            depart.points = {endpoint, external};
            finalSegments.push_back(std::move(depart));
        }
    }
    // Keep exact trimmed machining segments distinct, while connecting their
    // ordered TCP targets with explicit straight Transfer moves. Extensions
    // are already included, so these links join Depart to the next Approach.
    std::vector<PathSegment> connected;
    for(auto& segment:finalSegments) {
        checkCancelled(cancelled);
        if(!connected.empty()&&connected.back().points.back().tcpPoint.Distance(segment.points.front().tcpPoint)>parameters.tolerance.linear) {
            PathSegment transfer;
            transfer.type=MotionType::Transfer;
            transfer.layerId=segment.layerId;
            transfer.points={connected.back().points.back(),segment.points.front()};
            const gp_Dir direction(gp_Vec(transfer.points.front().tcpPoint,transfer.points.back().tcpPoint));
            for(auto& point:transfer.points)point.tangent=direction;
            connected.push_back(std::move(transfer));
        }
        connected.push_back(std::move(segment));
    }
    path.segments = std::move(connected);
    path.totalProcessLength = 0;
    path.totalTransferLength = 0;
    std::size_t count = 0;
    gp_Quaternion previousOrientation;
    bool hasPreviousOrientation=false;
    for (std::size_t index = 0; index < path.segments.size(); ++index) {
        auto& segment = path.segments[index];
        segment.segmentId = static_cast<int>(index);
        for (std::size_t pointIndex = 0; pointIndex < segment.points.size(); ++pointIndex) {
            auto& point = segment.points[pointIndex];
            if(hasPreviousOrientation) {
                const auto& q=point.orientation;
                const double dot=q.X()*previousOrientation.X()+q.Y()*previousOrientation.Y()+
                                 q.Z()*previousOrientation.Z()+q.W()*previousOrientation.W();
                if(dot<0)point.orientation=gp_Quaternion(-q.X(),-q.Y(),-q.Z(),-q.W());
            }
            previousOrientation=point.orientation;
            hasPreviousOrientation=true;
            point.layerId = segment.layerId;
            point.segmentId = segment.segmentId;
            point.pointId = static_cast<int>(pointIndex);
            if (pointIndex > 0) {
                const auto& previous = segment.points[pointIndex - 1];
                if (segment.type == MotionType::Process)
                    path.totalProcessLength += previous.surfacePoint.Distance(point.surfacePoint);
                else path.totalTransferLength += previous.tcpPoint.Distance(point.tcpPoint);
            }
        }
        count += segment.points.size();
        if (count > parameters.maxPoints) throw std::runtime_error("Path point limit exceeded");
    }
    if (path.segments.empty()) throw std::runtime_error("Path contains no valid points");
}
} // namespace sps
