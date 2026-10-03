#pragma once
#include <TopoDS_Shape.hxx>

namespace sps {
class CadHealer {
public:
    static bool isValid(const TopoDS_Shape& shape);
};
} // namespace sps
