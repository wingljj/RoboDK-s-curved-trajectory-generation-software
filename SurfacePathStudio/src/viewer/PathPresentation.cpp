#include "PathPresentation.h"
#include <AIS_TextLabel.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <TCollection_ExtendedString.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace sps {
namespace {
class ArrayPresentation : public AIS_InteractiveObject {
public:
    ArrayPresentation(const Handle(Graphic3d_ArrayOfPrimitives)& primitive, const Quantity_Color& color,
                      double width, bool points = false, bool highlightColor = true)
        : m_array(primitive), m_color(color), m_width(width), m_points(points), m_defaultColor(color),
          m_defaultWidth(width), m_highlightColor(highlightColor) {
        // Path and pose diagnostics stay readable when contact targets coincide
        // with display triangles, including a Tool Z directed into the surface.
        SetZLayer(Graphic3d_ZLayerId_Topmost);
    }
    void highlight(bool value) {
        m_color = value && m_highlightColor ? Quantity_Color(Quantity_NOC_YELLOW) : m_defaultColor;
        m_width = value ? m_defaultWidth + 1.5 : m_defaultWidth;
        SetToUpdate();
    }
protected:
    void Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& presentation,
                 const Standard_Integer) override {
        const auto group = presentation->NewGroup();
        if (m_points) group->SetGroupPrimitivesAspect(new Graphic3d_AspectMarker3d(Aspect_TOM_POINT, m_color, m_width));
        else {
            Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(m_color, Aspect_TOL_SOLID, m_width);
            group->SetGroupPrimitivesAspect(aspect->Aspect());
        }
        group->AddPrimitiveArray(m_array);
    }
    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}
private:
    Handle(Graphic3d_ArrayOfPrimitives) m_array;
    Quantity_Color m_color;
    double m_width;
    bool m_points;
    Quantity_Color m_defaultColor;
    double m_defaultWidth;
    bool m_highlightColor;
};

gp_Pnt moved(const gp_Pnt& point, const gp_Dir& direction, double length) {
    return point.Translated(gp_Vec(direction) * length);
}
void addSegment(const Handle(Graphic3d_ArrayOfSegments)& array, const gp_Pnt& a, const gp_Pnt& b) {
    array->AddVertex(a);
    array->AddVertex(b);
}
}

PathPresentation::PathPresentation(const Handle(AIS_InteractiveContext)& context) : m_context(context) {}
PathPresentation::~PathPresentation() { clear(); }
void PathPresentation::clear() {
    for (auto* collection : {&m_paths, &m_contacts, &m_normals, &m_frames, &m_reference, &m_planes, &m_layers}) {
        for (auto& item : *collection) m_context->Remove(item, false);
        collection->clear();
    }
}

void PathPresentation::show(const SurfacePath& path) {
    clear();
    if (path.pointCount() == 0) { m_context->UpdateCurrentViewer(); return; }
    // One primitive collection per motion type preserves segment breaks without constructing display-only B-Rep edges.
    for (MotionType type : {MotionType::Process, MotionType::Transfer, MotionType::Approach, MotionType::Depart}) {
        int vertices = 0, bounds = 0;
        for (const auto& segment : path.segments) if (segment.type == type && segment.points.size() > 1) {
            vertices += static_cast<int>(segment.points.size()); ++bounds;
        }
        if (vertices == 0) continue;
        Handle(Graphic3d_ArrayOfPolylines) lines = new Graphic3d_ArrayOfPolylines(vertices, bounds);
        for (const auto& segment : path.segments) if (segment.type == type && segment.points.size() > 1) {
            lines->AddBound(static_cast<int>(segment.points.size()));
            for (const auto& point : segment.points) lines->AddVertex(point.tcpPoint);
        }
        const auto color = type == MotionType::Process ? Quantity_NOC_ORANGE
                         : type == MotionType::Transfer ? Quantity_NOC_SKYBLUE
                         : type == MotionType::Approach ? Quantity_NOC_GREEN3 : Quantity_NOC_MAGENTA1;
        m_paths.push_back(new ArrayPresentation(lines, color, type == MotionType::Process ? 2.5 : 1.8,
                                               false, type == MotionType::Process));
    }
    std::size_t processPointCount = 0, processSegmentCount = 0;
    for (const auto& segment : path.segments) if (segment.type == MotionType::Process) {
        processPointCount += segment.points.size();
        ++processSegmentCount;
    }
    if (processPointCount > 0) {
        Handle(Graphic3d_ArrayOfPoints) contacts = new Graphic3d_ArrayOfPoints(static_cast<int>(processPointCount));
        for (const auto& segment : path.segments) if (segment.type == MotionType::Process)
            for (const auto& point : segment.points) contacts->AddVertex(point.surfacePoint);
        m_contacts.push_back(new ArrayPresentation(contacts, Quantity_NOC_YELLOW, 2.0, true));
    }
    const std::size_t stride = std::max<std::size_t>(10, (processPointCount + 299) / 300);
    const int frameCount = static_cast<int>(processPointCount / stride + processSegmentCount + 1);
    std::array<Handle(Graphic3d_ArrayOfSegments), 3> axes;
    for (auto& axis : axes) axis = new Graphic3d_ArrayOfSegments(frameCount * 2);
    Handle(Graphic3d_ArrayOfSegments) normals = new Graphic3d_ArrayOfSegments(frameCount * 2);
    const double frameSize = 4.0;
    std::size_t counter = 0;
    std::set<int> labeled;
    for (const auto& segment : path.segments) {
        if (segment.type != MotionType::Process) continue;
        if (!segment.points.empty() && labeled.size() < 200 && labeled.insert(segment.layerId).second) {
            Handle(AIS_TextLabel) text = new AIS_TextLabel;
            text->SetText(TCollection_ExtendedString(("Layer " + std::to_string(segment.layerId)).c_str()));
            text->SetPosition(segment.points.front().tcpPoint);
            text->SetColor(Quantity_NOC_WHITE);
            text->SetHeight(12.0);
            m_layers.push_back(text);
        }
        for (std::size_t i = 0; i < segment.points.size(); ++i, ++counter) {
            if (counter % stride != 0 && i != 0) continue;
            const auto& point = segment.points[i];
            const std::array<gp_Dir, 3> frame{point.toolX, point.toolY, point.toolZ};
            for (std::size_t axis = 0; axis < frame.size(); ++axis)
                addSegment(axes[axis], point.tcpPoint, moved(point.tcpPoint, frame[axis], frameSize));
            addSegment(normals, point.surfacePoint, moved(point.surfacePoint, point.surfaceNormal, frameSize));
        }
    }
    const std::array<Quantity_NameOfColor, 3> colors{Quantity_NOC_RED, Quantity_NOC_GREEN, Quantity_NOC_BLUE1};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) if (axes[axis]->VertexNumber() > 0)
        m_frames.push_back(new ArrayPresentation(axes[axis], colors[axis], 1.6));
    if (normals->VertexNumber() > 0) m_normals.push_back(new ArrayPresentation(normals, Quantity_NOC_CYAN1, 1.3));
    const std::array<gp_Dir, 3> reference{path.referenceTangent, path.referenceBinormal, path.referenceNormal};
    for (std::size_t axis = 0; axis < reference.size(); ++axis) {
        Handle(Graphic3d_ArrayOfSegments) line = new Graphic3d_ArrayOfSegments(2);
        addSegment(line, path.referenceCenter, moved(path.referenceCenter, reference[axis], 15.0));
        m_reference.push_back(new ArrayPresentation(line, colors[axis], 3.0));
    }
    double tExtent = 10.0, nExtent = 10.0;
    for (const auto& segment : path.segments) if (segment.type == MotionType::Process) for (const auto& point : segment.points) {
        const auto offset = gp_Vec(path.referenceCenter, point.surfacePoint);
        tExtent = std::max(tExtent, std::abs(offset.Dot(gp_Vec(path.referenceTangent))) + 5.0);
        nExtent = std::max(nExtent, std::abs(offset.Dot(gp_Vec(path.referenceNormal))) + 5.0);
    }
    const std::size_t planeStride = std::max<std::size_t>(1, (path.sectionOffsets.size() + 99) / 100);
    const int planeCount = static_cast<int>((path.sectionOffsets.size() + planeStride - 1) / planeStride);
    if (planeCount > 0) {
        Handle(Graphic3d_ArrayOfPolylines) planes = new Graphic3d_ArrayOfPolylines(planeCount * 5, planeCount);
        for (std::size_t i = 0; i < path.sectionOffsets.size(); i += planeStride) {
            const auto center = moved(path.referenceCenter, path.referenceBinormal, path.sectionOffsets[i]);
            planes->AddBound(5);
            for (const auto& corner : std::array<std::array<double, 2>, 5>{{{{-1,-1}},{{1,-1}},{{1,1}},{{-1,1}},{{-1,-1}}}}) {
                auto point = center.Translated(gp_Vec(path.referenceTangent) * (corner[0] * tExtent));
                point.Translate(gp_Vec(path.referenceNormal) * (corner[1] * nExtent));
                planes->AddVertex(point);
            }
        }
        m_planes.push_back(new ArrayPresentation(planes, Quantity_NOC_GRAY50, 1.0));
    }
    for (const auto& item : m_paths)
        static_cast<ArrayPresentation*>(item.get())->highlight(m_highlighted);
    applyVisibility();
}

void PathPresentation::applyVisibility() {
    const std::array<std::pair<std::vector<Handle(AIS_InteractiveObject)>*, bool>, 7> collections{{
        {&m_paths, m_showPath}, {&m_contacts, m_showContacts}, {&m_normals, m_showNormals},
        {&m_frames, m_showFrames}, {&m_reference, m_showReference}, {&m_planes, m_showPlanes}, {&m_layers, m_showLayers}}};
    for (const auto& entry : collections) for (auto& item : *entry.first) {
        if (m_visible && entry.second) m_context->Display(item, 0, -1, false);
        else if (m_context->IsDisplayed(item)) m_context->Erase(item, false);
    }
    m_context->UpdateCurrentViewer();
}
void PathPresentation::setVisible(bool value) { m_visible = value; applyVisibility(); }
void PathPresentation::setPathVisible(bool value) { m_showPath = value; applyVisibility(); }
void PathPresentation::setContactPointsVisible(bool value) { m_showContacts = value; applyVisibility(); }
void PathPresentation::setNormalsVisible(bool value) { m_showNormals = value; applyVisibility(); }
void PathPresentation::setFramesVisible(bool value) { m_showFrames = value; applyVisibility(); }
void PathPresentation::setReferenceFrameVisible(bool value) { m_showReference = value; applyVisibility(); }
void PathPresentation::setSectionPlanesVisible(bool value) { m_showPlanes = value; applyVisibility(); }
void PathPresentation::setLayerNumbersVisible(bool value) { m_showLayers = value; applyVisibility(); }
void PathPresentation::setHighlighted(bool value) {
    m_highlighted = value;
    for (auto& item : m_paths) {
        static_cast<ArrayPresentation*>(item.get())->highlight(value);
        m_context->Redisplay(item, false);
    }
    m_context->UpdateCurrentViewer();
}
} // namespace sps
