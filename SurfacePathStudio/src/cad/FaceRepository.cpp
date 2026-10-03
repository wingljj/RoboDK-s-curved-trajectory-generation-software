#include "FaceRepository.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <stdexcept>
#include <unordered_set>

namespace sps {
namespace {
std::string surfaceType(GeomAbs_SurfaceType type) {
    switch (type) {
    case GeomAbs_Plane: return "Plane";
    case GeomAbs_Cylinder: return "Cylinder";
    case GeomAbs_Cone: return "Cone";
    case GeomAbs_Sphere: return "Sphere";
    case GeomAbs_Torus: return "Torus";
    case GeomAbs_BezierSurface: return "Bezier Surface";
    case GeomAbs_BSplineSurface: return "BSpline Surface";
    case GeomAbs_SurfaceOfRevolution: return "Surface of Revolution";
    case GeomAbs_SurfaceOfExtrusion: return "Surface of Extrusion";
    case GeomAbs_OffsetSurface: return "Offset Surface";
    default: return "Other";
    }
}
}

void FaceRepository::reset(const TopoDS_Shape& shape) {
    m_faces.Clear();
    m_metadata.clear();
    if (shape.IsNull()) return;
    TopExp::MapShapes(shape, TopAbs_FACE, m_faces);
    m_metadata.reserve(count());
    for (int id = 1; id <= count(); ++id) {
        const auto& item = face(id);
        BRepAdaptor_Surface surface(item, true);
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(item, properties);
        m_metadata.push_back({id, surfaceType(surface.GetType()), properties.Mass()});
    }
}

const TopoDS_Face& FaceRepository::face(int id) const {
    if (id < 1 || id > count()) throw std::out_of_range("Invalid CAD Face ID");
    return TopoDS::Face(m_faces.FindKey(id));
}

int FaceRepository::idFor(const TopoDS_Face& item) const {
    if (item.IsNull()) return -1;
    const int id = m_faces.FindIndex(item);
    return id == 0 ? -1 : id;
}

const FaceMetadata& FaceRepository::metadata(int id) const {
    if (id < 1 || id > count()) throw std::out_of_range("Invalid CAD Face ID");
    return m_metadata[static_cast<std::size_t>(id - 1)];
}

SelectedSurfaceRegion FaceRepository::region(const std::vector<int>& ids) const {
    SelectedSurfaceRegion result;
    std::unordered_set<int> included;
    for (int id : ids) {
        if (included.insert(id).second) result.faces.push_back({id, face(id)});
    }
    return result;
}
} // namespace sps
