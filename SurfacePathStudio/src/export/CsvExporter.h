#pragma once
#include "RobotPathExporter.h"

namespace sps {
class CsvExporter final : public RobotPathExporter {
public:
    explicit CsvExporter(bool simplified = false) : simplified_(simplified) {}
    void write(std::ostream& stream, const SurfacePath& path) const override;
private:
    bool simplified_;
};
} // namespace sps
