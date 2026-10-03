#pragma once
#include "path/PathTypes.h"
#include <iosfwd>

namespace sps {
class RobotPathExporter {
public:
    virtual ~RobotPathExporter() = default;
    virtual void write(std::ostream& stream, const SurfacePath& path) const = 0;
};
} // namespace sps
