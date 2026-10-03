#pragma once

#include "path/PathTypes.h"
#include <TopoDS_Shape.hxx>
#include <string>
#include <vector>

namespace sps::examples {
struct Fixture {
    std::string name;
    std::string fileStem;
    TopoDS_Shape shape;
    PathParameters parameters;
};

std::vector<Fixture> fixtures();
} // namespace sps::examples
