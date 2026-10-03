#include "examples/ExampleModels.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <iostream>
#include <set>
#include <stdexcept>

int main() {
    try {
        const auto fixtures = sps::examples::fixtures();
        if (fixtures.size() != 7) throw std::runtime_error("seven CAD fixtures are required, including full and partial cylinder");
        std::set<std::string> names;
        for (const auto& fixture : fixtures) {
            if (!names.insert(fixture.fileStem).second || fixture.name.empty()) throw std::runtime_error("fixtures need unique filenames and readable names");
            if (fixture.shape.IsNull() || !BRepCheck_Analyzer(fixture.shape).IsValid()) throw std::runtime_error(fixture.fileStem + " is not a valid B-Rep");
            int count = 0;
            for (TopExp_Explorer explorer(fixture.shape, TopAbs_FACE); explorer.More(); explorer.Next()) ++count;
            if (count != (fixture.fileStem == "adjacent_bspline" ? 2 : 1)) throw std::runtime_error(fixture.fileStem + " has unexpected face count");
            if (fixture.fileStem == "bspline_trimmed" || fixture.fileStem == "adjacent_bspline") {
                for (TopExp_Explorer explorer(fixture.shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
                    if (BRepAdaptor_Surface(TopoDS::Face(explorer.Current())).GetType() != GeomAbs_BSplineSurface)
                        throw std::runtime_error("freeform fixture must retain a real BSpline surface");
                }
            }
            if (fixture.fileStem == "adjacent_bspline") {
                TopTools_IndexedDataMapOfShapeListOfShape map;
                TopExp::MapShapesAndAncestors(fixture.shape, TopAbs_EDGE, TopAbs_FACE, map);
                bool shared = false;
                for (int i = 1; i <= map.Extent(); ++i) if (map.FindFromIndex(i).Extent() == 2) shared = true;
                if (!shared) throw std::runtime_error("adjacent surfaces must share a sewn topological edge");
            }
        }
        std::cout << "PASS seven valid analytic and freeform CAD fixtures\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL examples: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL examples: CAD exception\n";
        return 1;
    }
}
