#include "CadHealer.h"
#include <BRepCheck_Analyzer.hxx>

namespace sps {
bool CadHealer::isValid(const TopoDS_Shape& shape) {
    return !shape.IsNull() && BRepCheck_Analyzer(shape, true).IsValid();
}
} // namespace sps
