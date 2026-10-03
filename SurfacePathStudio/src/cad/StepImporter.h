#pragma once
#include "CadDocument.h"
#include <memory>

namespace sps {
class StepImporter {
public:
    static std::shared_ptr<CadDocument> load(const QString& fileName);
};
} // namespace sps
