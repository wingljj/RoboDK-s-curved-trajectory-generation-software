#include "CsvExporter.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <ostream>
#include <stdexcept>

namespace sps {
namespace {
const char* motionName(MotionType type) {
    switch (type) {
    case MotionType::Process: return "Process";
    case MotionType::Transfer: return "Transfer";
    case MotionType::Approach: return "Approach";
    case MotionType::Depart: return "Depart";
    }
    throw std::runtime_error("Unknown motion type");
}
bool finitePoint(const PathPoint& point) {
    return std::isfinite(point.tcpPoint.X()) && std::isfinite(point.tcpPoint.Y()) &&
           std::isfinite(point.tcpPoint.Z()) && std::isfinite(point.orientation.X()) &&
           std::isfinite(point.orientation.Y()) && std::isfinite(point.orientation.Z()) &&
           std::isfinite(point.orientation.W());
}
}
void CsvExporter::write(std::ostream& stream, const SurfacePath& path) const {
    if (path.pointCount() == 0) throw std::runtime_error("Path contains no valid points");
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15);
    if (simplified_) stream << "X,Y,Z,Nx,Ny,Nz\n";
    else stream << "segment,point,x,y,z,nx,ny,nz,qx,qy,qz,qw,face_id,layer_id,motion_type,contact_x,contact_y,contact_z,u,v\n";
    for (const auto& segment : path.segments) {
        for (const auto& point : segment.points) {
            if (!finitePoint(point)) throw std::runtime_error("Cannot export non-finite coordinates or orientation");
            if (!simplified_) stream << point.segmentId << ',' << point.pointId << ',';
            stream << point.tcpPoint.X() << ',' << point.tcpPoint.Y() << ',' << point.tcpPoint.Z() << ','
                   << point.surfaceNormal.X() << ',' << point.surfaceNormal.Y() << ',' << point.surfaceNormal.Z();
            if (!simplified_) {
                stream << ',' << point.orientation.X() << ',' << point.orientation.Y() << ','
                       << point.orientation.Z() << ',' << point.orientation.W() << ','
                       << point.faceId << ',' << point.layerId << ',' << motionName(segment.type) << ','
                       << point.surfacePoint.X() << ',' << point.surfacePoint.Y() << ',' << point.surfacePoint.Z() << ','
                       << point.u << ',' << point.v;
            }
            stream << '\n';
        }
    }
    if (!stream) throw std::runtime_error("CSV output failed");
}
} // namespace sps
