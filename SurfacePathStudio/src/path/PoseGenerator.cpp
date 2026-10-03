#include "PoseGenerator.h"

#include <gp_Ax1.hxx>
#include <gp_Mat.hxx>
#include <Precision.hxx>
#include <cmath>
#include <stdexcept>

namespace sps {
void generatePoses(PathSegment& segment, const PathParameters& parameters) {
    constexpr double pi = 3.14159265358979323846;
    const double lead = std::remainder(parameters.leadAngleDeg, 360.0) * (pi / 180.0);
    const double tilt = std::remainder(parameters.tiltAngleDeg, 360.0) * (pi / 180.0);
    bool havePrevious = false;
    gp_Dir previousNormal(0, 0, 1);
    gp_Quaternion previousOrientation;
    for (auto& point : segment.points) {
        // Topology-correct normals are computed first; this only stabilizes a
        // sequence if the source model has a locally inconsistent normal sign.
        if (havePrevious && point.surfaceNormal.Dot(previousNormal) < 0) point.surfaceNormal.Reverse();
        gp_Vec z(point.surfaceNormal);
        if (!parameters.toolAlongNormal) z.Reverse();
        gp_Vec x(point.tangent);
        x -= z * x.Dot(z);
        if (x.Magnitude() <= Precision::Confusion())
            throw std::runtime_error("Path tangent is parallel to the surface normal");
        x.Normalize();
        gp_Vec y = z.Crossed(x);
        y.Normalize();
        x = y.Crossed(z);
        x.Normalize();
        // Intrinsic order: lead about local Y, then tilt about the resulting X.
        const gp_Ax1 leadAxis(gp_Pnt(0, 0, 0), gp_Dir(y));
        x.Rotate(leadAxis, lead);
        z.Rotate(leadAxis, lead);
        const gp_Ax1 tiltAxis(gp_Pnt(0, 0, 0), gp_Dir(x));
        y.Rotate(tiltAxis, tilt);
        z.Rotate(tiltAxis, tilt);
        z.Normalize();
        x -= z * x.Dot(z);
        x.Normalize();
        y = z.Crossed(x);
        y.Normalize();
        x = y.Crossed(z);
        x.Normalize();
        point.toolX = gp_Dir(x);
        point.toolY = gp_Dir(y);
        point.toolZ = gp_Dir(z);
        point.tcpPoint = point.surfacePoint.Translated(gp_Vec(point.surfaceNormal) * parameters.zOffset);
        // Columns are tool axes expressed in CAD/world coordinates: local -> world.
        gp_Mat rotation(x.XYZ(), y.XYZ(), z.XYZ());
        point.orientation = gp_Quaternion(rotation);
        point.orientation.Normalize();
        if (havePrevious && point.orientation.Dot(previousOrientation) < 0)
            point.orientation.Set(-point.orientation.X(), -point.orientation.Y(),
                                  -point.orientation.Z(), -point.orientation.W());
        previousNormal = point.surfaceNormal;
        previousOrientation = point.orientation;
        havePrevious = true;
    }
}
} // namespace sps
