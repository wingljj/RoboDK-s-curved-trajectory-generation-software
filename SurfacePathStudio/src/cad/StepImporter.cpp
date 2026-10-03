#include "StepImporter.h"
#include "CadHealer.h"

#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TopoDS_Compound.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <QFileInfo>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace sps {
namespace {
std::mutex importerMutex;

QString labelName(const TDF_Label& label) {
    Handle(TDataStd_Name) attr;
    if (!label.FindAttribute(TDataStd_Name::GetID(), attr)) return {};
    const auto& name = attr->Get();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(name.ToExtString()), name.Length());
#else
    return QString::fromUtf16(reinterpret_cast<const ushort*>(name.ToExtString()), name.Length());
#endif
}

CadNode makeNode(const TDF_Label& label, const TopLoc_Location& parentLocation, int depth = 0) {
    if (depth > 128) throw std::runtime_error("STEP assembly nesting exceeds 128 levels");
    CadNode node;
    node.label = label;
    node.name = labelName(label);
    node.shape = XCAFDoc_ShapeTool::GetShape(label);
    if (!node.shape.IsNull()) node.shape.Move(parentLocation);
    TDF_Label definition = label;
    if (XCAFDoc_ShapeTool::IsReference(label)) {
        XCAFDoc_ShapeTool::GetReferredShape(label, definition);
        if (node.name.isEmpty()) node.name = labelName(definition);
    }
    if (node.name.isEmpty()) node.name = QStringLiteral("CAD shape");
    if (XCAFDoc_ShapeTool::IsAssembly(definition)) {
        const auto location = parentLocation * XCAFDoc_ShapeTool::GetLocation(label);
        TDF_LabelSequence components;
        XCAFDoc_ShapeTool::GetComponents(definition, components);
        for (int i = 1; i <= components.Length(); ++i)
            node.children.push_back(makeNode(components.Value(i), location, depth + 1));
    }
    return node;
}
}

std::shared_ptr<CadDocument> StepImporter::load(const QString& fileName) {
    // Older transfer controllers have global caches; serialize STEP reads, not GUI rendering.
    std::lock_guard<std::mutex> lock(importerMutex);
    if (!QFileInfo::exists(fileName)) throw std::runtime_error("STEP load failed: file does not exist");
    try {
        auto document = std::make_shared<CadDocument>();
        document->sourcePath = QFileInfo(fileName).absoluteFilePath();
        // A detached document owns its data and can be released after replacement;
        // registering each import in the process-wide application would retain old models.
        document->xcaf = new TDocStd_Document("BinXCAF");
        XCAFDoc_DocumentTool::Set(document->xcaf->Main());
        // OCCT 7.6 STEPCAF transfer takes its target unit from the XCAF document.
        XCAFDoc_DocumentTool::SetLengthUnit(document->xcaf, 1.0, UnitsMethods_LengthUnit_Millimeter);
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(true);
        reader.SetPropsMode(true);
        reader.SetSHUOMode(true);
#ifdef _WIN32
        std::ifstream input(std::filesystem::path(fileName.toStdWString()), std::ios::binary);
#else
        std::ifstream input(std::filesystem::path(fileName.toUtf8().constData()), std::ios::binary);
#endif
        if (!input) throw std::runtime_error("STEP load failed: cannot open file");
        const auto utf8Path = document->sourcePath.toUtf8();
        if (reader.ChangeReader().ReadStream(utf8Path.constData(), input) != IFSelect_RetDone)
            throw std::runtime_error("STEP load failed: parser rejected the file");
        reader.ChangeReader().SetSystemLengthUnit(1.0);
        TColStd_SequenceOfAsciiString lengths, angles, solids;
        reader.ChangeReader().FileUnits(lengths, angles, solids);
        for (int i = 1; i <= lengths.Length(); ++i)
            document->unitNames.emplace_back(lengths.Value(i).ToCString());
        if (!reader.Transfer(document->xcaf))
            throw std::runtime_error("STEP load failed: no transferable B-Rep geometry");
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->xcaf->Main());
        TDF_LabelSequence freeShapes;
        shapeTool->GetFreeShapes(freeShapes);
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (int i = 1; i <= freeShapes.Length(); ++i) {
            auto root = makeNode(freeShapes.Value(i), TopLoc_Location());
            if (!root.shape.IsNull()) builder.Add(compound, root.shape);
            document->roots.push_back(std::move(root));
        }
        document->shape = compound;
        document->faces.reset(document->shape);
        if (document->faces.count() == 0)
            throw std::runtime_error("STEP load failed: model contains no B-Rep Faces");
        document->valid = CadHealer::isValid(document->shape);
        return document;
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("STEP load failed: ") + error.GetMessageString());
    }
}
} // namespace sps
