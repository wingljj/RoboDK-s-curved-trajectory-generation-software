#include "cad/StepImporter.h"
#include "examples/ExampleModels.h"
#include "path/SectionPathPlanner.h"
#include "export/CsvExporter.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Controller.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <Quantity_Color.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <set>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void expectNear(double actual, double expected, double tolerance, const std::string& message) {
    require(std::abs(actual - expected) <= tolerance,
            message + " (actual " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}
void writeStep(const TopoDS_Shape& shape, const QString& fileName, const char* unit = "MM") {
    // StepData_StepModel captures the write unit in its constructor. Select
    // the target unit before constructing the writer's model (OCCT 7.6).
    STEPControl_Controller::Init();
    Interface_Static::SetCVal("write.step.unit", unit);
    STEPControl_Writer writer;
    require(writer.Transfer(shape, STEPControl_AsIs) == IFSelect_RetDone, "fixture STEP transfer failed");
    require(writer.Write(fileName.toUtf8().constData()) == IFSelect_RetDone, "fixture STEP write failed");
    Interface_Static::SetCVal("write.step.unit", "MM");
}
void planeRoundtrip(const char* unit) {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary STEP directory must exist");
    const auto fixtures = sps::examples::fixtures();
    const QString filename = directory.filePath(QStringLiteral("plane.step"));
    writeStep(fixtures.front().shape, filename, unit);
    const auto document = sps::StepImporter::load(filename);
    require(document && !document->shape.IsNull() && !document->xcaf.IsNull(), "STEP must import real B-Rep and XCAF document");
    require(document->faces.count() == 1, "plane STEP must preserve one CAD Face");
    require(document->valid, "roundtrip plane must retain valid CAD topology");
    require(!document->roots.empty(), "STEP must retain project shape hierarchy");
    require(!document->unitNames.empty(), "STEP source length units must be reported");
    expectNear(document->faces.metadata(1).area, 8000, 1e-2, "100 x 80 mm imported plane area");
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(document->shape, bounds, false, false);
    bounds.SetGap(0);
    double x0, y0, z0, x1, y1, z1;
    bounds.Get(x0, y0, z0, x1, y1, z1);
    expectNear(x1 - x0, 100, 1e-3, "STEP X dimension must normalize to millimetres");
    expectNear(y1 - y0, 80, 1e-3, "STEP Y dimension must normalize to millimetres");
    const QString unicodeFilename = directory.filePath(QString::fromUtf8("曲面模型.step"));
    require(QFile::copy(filename, unicodeFilename), "Unicode STEP copy must succeed");
    const auto unicodeDocument = sps::StepImporter::load(unicodeFilename);
    require(unicodeDocument->faces.count() == 1, "Unicode filename must import without losing CAD Face");
}
void allExampleRoundtrips() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary STEP directory must exist");
    for (const auto& fixture : sps::examples::fixtures()) {
        const QString filename = directory.filePath(QString::fromStdString(fixture.fileStem + ".step"));
        writeStep(fixture.shape, filename);
        const auto document = sps::StepImporter::load(filename);
        require(document->valid, fixture.fileStem + " STEP roundtrip must remain valid");
        require(document->faces.count() == (fixture.fileStem == "adjacent_bspline" ? 2 : 1),
                fixture.fileStem + " STEP roundtrip must preserve CAD Face topology");
        for (int i = 1; i <= document->faces.count(); ++i)
            require(document->faces.metadata(i).area > 0, "imported CAD face must have positive analytic area");
    }
}
void invalidFiles() {
    bool rejected = false;
    try { sps::StepImporter::load(QStringLiteral("file_that_does_not_exist.step")); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "missing STEP must report an error");
    QTemporaryDir directory;
    const auto filename = directory.filePath(QStringLiteral("malformed.step"));
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly), "malformed STEP file must open");
    file.write("This is not a STEP file.");
    file.close();
    rejected = false;
    try { sps::StepImporter::load(filename); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "malformed STEP must report an error");
}
void assemblyLocationsAndColors() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary assembly STEP directory must exist");
    Handle(TDocStd_Document) source;
    XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", source);
    XCAFDoc_DocumentTool::SetLengthUnit(source, 1.0, UnitsMethods_LengthUnit_Millimeter);
    const auto shapes = XCAFDoc_DocumentTool::ShapeTool(source->Main());
    const auto colors = XCAFDoc_DocumentTool::ColorTool(source->Main());
    const auto plane = sps::examples::fixtures().front().shape;
    const auto part = shapes->AddShape(plane, false);
    TDataStd_Name::Set(part, TCollection_ExtendedString("Reference plane"));
    colors->SetColor(part, Quantity_Color(0.15, 0.45, 0.85, Quantity_TOC_RGB), XCAFDoc_ColorSurf);
    const auto nested = shapes->NewShape();
    TDataStd_Name::Set(nested, TCollection_ExtendedString("Nested subassembly"));
    gp_Trsf inner;
    inner.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0.4);
    inner.SetTranslationPart(gp_Vec(10, 20, 30));
    const auto nestedPart = shapes->AddComponent(nested, part, TopLoc_Location(inner));
    TDataStd_Name::Set(nestedPart, TCollection_ExtendedString("Rotated plane"));
    const auto root = shapes->NewShape();
    TDataStd_Name::Set(root, TCollection_ExtendedString("Test nested assembly"));
    gp_Trsf outer;
    outer.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), 0.7);
    outer.SetTranslationPart(gp_Vec(135, -42, 77));
    const auto nestedInstance = shapes->AddComponent(root, nested, TopLoc_Location(outer));
    TDataStd_Name::Set(nestedInstance, TCollection_ExtendedString("Located subassembly"));
    gp_Trsf secondLocation;
    secondLocation.SetTranslation(gp_Vec(-200, 30, 10));
    const auto secondInstance = shapes->AddComponent(root, part, TopLoc_Location(secondLocation));
    TDataStd_Name::Set(secondInstance, TCollection_ExtendedString("Second plane instance"));
    shapes->UpdateAssemblies();
    STEPControl_Controller::Init();
    Interface_Static::SetCVal("write.step.unit", "MM");
    STEPCAFControl_Writer writer;
    writer.SetColorMode(true);
    writer.SetNameMode(true);
    require(writer.Transfer(source, STEPControl_AsIs), "assembly STEP transfer failed");
    const QString filename = directory.filePath(QStringLiteral("assembly.step"));
    require(writer.Write(filename.toUtf8().constData()) == IFSelect_RetDone, "assembly STEP write failed");
    const auto imported = sps::StepImporter::load(filename);
    require(imported->valid && imported->faces.count() == 2, "assembly must retain two located valid plane instances");
    require(imported->roots.size() == 1 && imported->roots.front().children.size() == 2,
            "assembly hierarchy must retain one root and two components");
    require(imported->roots.front().name == QStringLiteral("Test nested assembly"), "root assembly name must survive STEP");
    BRep_Builder builder;
    TopoDS_Compound expected;
    builder.MakeCompound(expected);
    builder.Add(expected, plane.Moved(TopLoc_Location(outer * inner)));
    builder.Add(expected, plane.Moved(TopLoc_Location(secondLocation)));
    Bnd_Box importedBounds, expectedBounds;
    BRepBndLib::AddOptimal(imported->shape, importedBounds, false, false);
    BRepBndLib::AddOptimal(expected, expectedBounds, false, false);
    importedBounds.SetGap(0);
    expectedBounds.SetGap(0);
    double actual[6], desired[6];
    importedBounds.Get(actual[0], actual[1], actual[2], actual[3], actual[4], actual[5]);
    expectedBounds.Get(desired[0], desired[1], desired[2], desired[3], desired[4], desired[5]);
    for (int i = 0; i < 6; ++i) expectNear(actual[i], desired[i], 1e-3, "nested assembly world placement");
    for (int i = 1; i <= imported->faces.count(); ++i) {
        require(imported->faces.idFor(imported->faces.face(i)) == i, "Face repository must identify located instance face");
        expectNear(imported->faces.metadata(i).area, 8000, 1e-2, "instance area must remain unchanged by rigid placement");
    }
    int leaves = 0;
    std::function<void(const sps::CadNode&)> visit = [&](const sps::CadNode& node) {
        if (!node.children.empty()) { for (const auto& child : node.children) visit(child); return; }
        ++leaves;
        require(!node.name.isEmpty(), "component names must survive STEP");
        for (TopExp_Explorer explorer(node.shape, TopAbs_FACE); explorer.More(); explorer.Next())
            require(imported->faces.idFor(TopoDS::Face(explorer.Current())) > 0,
                    "tree leaf and viewer world-located geometry must resolve to the same runtime Face");
    };
    for (const auto& node : imported->roots) visit(node);
    require(leaves == 2, "nested assembly must retain two plane leaves");
    // STEPCAF may attach a surface color to a sub-Face label. Check the
    // effective presentation style, including inherited instance placement.
    std::set<int> coloredFaces;
    for (const auto& node : imported->roots) {
        XCAFPrs_IndexedDataMapOfShapeStyle styles;
        XCAFPrs::CollectStyleSettings(node.label, TopLoc_Location(), styles);
        for (int i = 1; i <= styles.Extent(); ++i) {
            const auto& style = styles.FindFromIndex(i);
            if (!style.IsSetColorSurf()) continue;
            const auto& color = style.GetColorSurf();
            expectNear(color.Red(), 0.15, 1e-4, "STEP surface red component");
            expectNear(color.Green(), 0.45, 1e-4, "STEP surface green component");
            expectNear(color.Blue(), 0.85, 1e-4, "STEP surface blue component");
            for (TopExp_Explorer explorer(styles.FindKey(i), TopAbs_FACE); explorer.More(); explorer.Next())
                coloredFaces.insert(imported->faces.idFor(TopoDS::Face(explorer.Current())));
        }
    }
    require(coloredFaces.size() == 2 && coloredFaces.count(0) == 0,
            "both located instances must retain effective surface color");
}
void importedExamplesGeneratePaths() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary pipeline STEP directory must exist");
    for (const auto& fixture : sps::examples::fixtures()) {
        const QString filename = directory.filePath(QString::fromStdString(fixture.fileStem + ".step"));
        writeStep(fixture.shape, filename);
        const auto document = sps::StepImporter::load(filename);
        std::vector<int> ids;
        for (int i = 1; i <= document->faces.count(); ++i) ids.push_back(i);
        const auto selected = document->faces.region(ids);
        const auto path = sps::SectionPathPlanner().generate(selected, fixture.parameters);
        require(path.pointCount() > 0, fixture.fileStem + " imported B-Rep must generate contact paths");
        for (const auto& segment : path.segments) if (segment.type == sps::MotionType::Process) for (const auto& point : segment.points) {
            const auto& face = document->faces.face(point.faceId);
            require(BRepAdaptor_Surface(face).Value(point.u, point.v).Distance(point.surfacePoint) <= 2e-3,
                    "imported path contact must evaluate to its true CAD surface");
            BRepClass_FaceClassifier classifier(face, gp_Pnt2d(point.u, point.v), 2e-4);
            require(classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON,
                    "imported path contact must remain in trimmed B-Rep Face");
            expectNear(point.orientation.Norm(), 1, 1e-6, "imported path must have valid TCP quaternion");
        }
        std::ostringstream csv;
        sps::CsvExporter().write(csv, path);
        require(csv.str().find("face_id,layer_id,motion_type") != std::string::npos,
                "imported generated path must export full target data");
    }
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"millimetres", [] { planeRoundtrip("MM"); }},
        {"inches", [] { planeRoundtrip("INCH"); }},
        {"examples", allExampleRoundtrips}, {"invalid_files", invalidFiles},
        {"assembly", assemblyLocationsAndColors}, {"pipeline", importedExamplesGeneratePaths}
    };
    int failed = 0;
    for (const auto& test : cases) {
        if (!filter.empty() && filter != test.first) continue;
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
        catch (...) { ++failed; std::cerr << "FAIL " << test.first << ": unexpected CAD exception\n"; }
    }
    return failed == 0 ? 0 : 1;
}
