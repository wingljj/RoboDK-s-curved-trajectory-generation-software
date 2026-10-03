#include "export/CsvExporter.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::istringstream input(line);
    for (std::string value; std::getline(input, value, ',');) result.push_back(value);
    return result;
}
sps::SurfacePath path() {
    sps::SurfacePath result;
    const sps::MotionType motionTypes[] = {sps::MotionType::Approach, sps::MotionType::Process,
        sps::MotionType::Transfer, sps::MotionType::Process, sps::MotionType::Depart};
    for (int segmentIndex = 0; segmentIndex < 5; ++segmentIndex) {
        sps::PathSegment segment;
        segment.segmentId = segmentIndex;
        segment.layerId = segmentIndex;
        segment.type = motionTypes[segmentIndex];
        for (int i = 0; i < 2; ++i) {
            sps::PathPoint point;
            point.surfacePoint = gp_Pnt(i + 0.25, segmentIndex + 0.5, 0);
            point.tcpPoint = gp_Pnt(i + 0.25, segmentIndex + 0.5, 2);
            point.faceId = segmentIndex + 1;
            point.segmentId = segmentIndex;
            point.layerId = segmentIndex;
            point.pointId = i;
            point.orientation = gp_Quaternion(0, 0, 0, 1);
            segment.points.push_back(point);
        }
        result.segments.push_back(segment);
    }
    return result;
}
void fullCsv() {
    const auto source = path();
    std::ostringstream output;
    sps::CsvExporter().write(output, source);
    std::istringstream input(output.str());
    std::string line;
    require(static_cast<bool>(std::getline(input, line)), "CSV must have a header");
    const auto header = fields(line);
    require(header.size() == 20 && header[0] == "segment" && header[11] == "qw" && header[12] == "face_id" && header[14] == "motion_type",
            "CSV must preserve pose, Face ID, and explicit motion type schema");
    std::size_t rows = 0;
    const char* motionNames[] = {"Approach", "Process", "Transfer", "Process", "Depart"};
    while (std::getline(input, line)) {
        require(!line.empty(), "full CSV must contain no blank rows between path segments");
        const auto row = fields(line);
        require(row.size() == header.size(), "CSV row must match header width");
        require(std::abs(std::stod(row[4]) - 2) < 1e-12 && std::abs(std::stod(row[17])) < 1e-12,
                "CSV must retain separate TCP and contact positions");
        const std::size_t segmentIndex = rows / 2;
        require(segmentIndex < source.segments.size(), "CSV must not create extra rows");
        require(std::stoi(row[0]) == static_cast<int>(segmentIndex) &&
                std::stoi(row[1]) == static_cast<int>(rows % 2) &&
                std::stoi(row[12]) == static_cast<int>(segmentIndex + 1),
                "CSV must retain target order, segment IDs, point IDs, and runtime Face IDs");
        require(row[14] == motionNames[segmentIndex], "CSV must retain non-process move types");
        ++rows;
    }
    require(rows == source.pointCount(), "full CSV must retain every path target");
}
void simplifiedCsv() {
    const auto source = path();
    std::ostringstream output;
    sps::CsvExporter(true).write(output, source);
    require(output.str().find("\n\n") == std::string::npos,
            "simplified CSV must contain no blank rows between path segments");
    std::istringstream input(output.str());
    std::string line;
    std::getline(input, line);
    require(line == "X,Y,Z,Nx,Ny,Nz", "simplified CSV header");
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        require(!line.empty(), "simplified CSV must contain no blank rows between path segments");
        const auto row = fields(line);
        require(row.size() == 6, "simplified CSV requires exactly six numeric fields");
        require(rows < source.pointCount(), "simplified CSV must not create extra rows");
        const auto& point = source.segments[rows / 2].points[rows % 2];
        require(std::abs(std::stod(row[0]) - point.tcpPoint.X()) < 1e-12 &&
                std::abs(std::stod(row[1]) - point.tcpPoint.Y()) < 1e-12 &&
                std::abs(std::stod(row[2]) - point.tcpPoint.Z()) < 1e-12,
                "simplified CSV must keep approach, process, transfer, and depart targets in order");
        ++rows;
    }
    require(rows == source.pointCount(), "simplified CSV must export a continuous target list including connecting moves");
}
void rejectedExport() {
    bool rejected = false;
    try { std::ostringstream output; sps::CsvExporter().write(output, {}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "empty export must report an error");
    auto invalid = path();
    invalid.segments.front().points.front().tcpPoint.SetX(std::numeric_limits<double>::quiet_NaN());
    rejected = false;
    try { std::ostringstream output; sps::CsvExporter().write(output, invalid); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "nonfinite export must report an error");
    rejected = false;
    try { std::ostringstream output; output.setstate(std::ios::badbit); sps::CsvExporter().write(output, path()); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "failed output stream must report an error");
}
}

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"full", fullCsv}, {"simplified", simplifiedCsv}, {"errors", rejectedExport}
    };
    int failed = 0;
    for (const auto& test : cases) {
        if (!filter.empty() && filter != test.first) continue;
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
