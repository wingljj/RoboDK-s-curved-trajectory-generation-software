#pragma once

#include "path/PathTypes.h"
#include <TopTools_IndexedMapOfShape.hxx>
#include <string>
#include <vector>

namespace sps {

struct FaceMetadata {
    int id = -1;
    std::string type;
    double area = 0.0;
};

class FaceRepository {
public:
    void reset(const TopoDS_Shape& shape);
    int count() const { return m_faces.Extent(); }
    const TopoDS_Face& face(int id) const;
    int idFor(const TopoDS_Face& face) const;
    const FaceMetadata& metadata(int id) const;
    SelectedSurfaceRegion region(const std::vector<int>& ids) const;
private:
    TopTools_IndexedMapOfShape m_faces;
    std::vector<FaceMetadata> m_metadata;
};

} // namespace sps
