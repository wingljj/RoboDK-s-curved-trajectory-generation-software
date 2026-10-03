#pragma once

#include "FaceRepository.h"
#include <QString>
#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <vector>

namespace sps {
struct CadNode {
    QString name;
    TDF_Label label;
    TopoDS_Shape shape;
    std::vector<CadNode> children;
};

struct CadDocument {
    TopoDS_Shape shape;
    Handle(TDocStd_Document) xcaf;
    QString sourcePath;
    FaceRepository faces;
    std::vector<CadNode> roots;
    std::vector<std::string> unitNames;
    bool valid = false;
};
} // namespace sps
