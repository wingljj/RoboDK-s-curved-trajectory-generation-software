#include "OcctViewWidget.h"
#include "cad/CadDocument.h"

#include <AIS_DisplayMode.hxx>
#include <AIS_Point.hxx>
#include <AIS_TextLabel.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <Standard_Failure.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <XCAFPrs_AISObject.hxx>
#ifdef _WIN32
#include <WNT_Window.hxx>
#endif
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <cmath>

namespace sps {
namespace {
QPointF mousePosition(QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}
Aspect_Drawable nativeHandle(WId id) {
#ifdef _WIN32
    return reinterpret_cast<Aspect_Drawable>(id);
#else
    return static_cast<Aspect_Drawable>(id);
#endif
}
}

OcctViewWidget::OcctViewWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    Handle(Aspect_DisplayConnection) connection = new Aspect_DisplayConnection;
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(connection);
    m_viewer = new V3d_Viewer(driver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();
    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetAutoActivateSelection(false);
    m_view = m_viewer->CreateView();
    m_view->SetImmediateUpdate(false);
    m_view->SetBgGradientColors(Quantity_Color(0.12, 0.16, 0.21, Quantity_TOC_RGB),
                               Quantity_Color(0.25, 0.30, 0.36, Quantity_TOC_RGB),
                               Aspect_GFM_VER, false);
    m_view->SetProj(V3d_XposYnegZpos);
}

OcctViewWidget::~OcctViewWidget() {
    if (!m_context.IsNull()) m_context->RemoveAll(false);
    if (!m_view.IsNull()) m_view->Remove();
}

void OcctViewWidget::initializeView() {
    if (!m_view->Window().IsNull() && m_view->Window()->NativeHandle() == nativeHandle(winId())) return;
#ifdef _WIN32
    Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
    Handle(Aspect_NeutralWindow) window = new Aspect_NeutralWindow;
    window->SetNativeHandle(nativeHandle(winId()));
    window->SetSize(qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
#endif
    m_view->SetWindow(window);
    m_view->MustBeResized();
}

void OcctViewWidget::showEvent(QShowEvent*) { initializeView(); update(); }
void OcctViewWidget::paintEvent(QPaintEvent*) { initializeView(); m_view->Redraw(); }
void OcctViewWidget::resizeEvent(QResizeEvent*) {
    if (!m_view->Window().IsNull()) {
#ifndef _WIN32
        const auto window = Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
        if (!window.IsNull()) window->SetSize(qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF()));
#endif
        m_view->MustBeResized();
    }
    update();
}

void OcctViewWidget::clearDocument() {
    for (auto& item : m_highlights) m_context->Remove(item, false);
    for (auto& item : m_models) m_context->Remove(item, false);
    for (auto& item : m_faceLabels) m_context->Remove(item, false);
    for (auto& item : m_directionMarkers) m_context->Remove(item, false);
    if (!m_edgeHighlight.IsNull()) m_context->Remove(m_edgeHighlight, false);
    m_highlights.clear();
    m_models.clear();
    m_faceLabels.clear();
    m_directionMarkers.clear();
    m_edgeHighlight.Nullify();
    m_document.reset();
    m_context->ClearSelected(false);
    update();
}

void OcctViewWidget::displayDocument(std::shared_ptr<CadDocument> document) {
    clearDocument();
    m_document = std::move(document);
    if (!m_document) return;
    for (const auto& root : m_document->roots) {
        Handle(XCAFPrs_AISObject) item = new XCAFPrs_AISObject(root.label);
        m_context->Display(item, m_shaded ? AIS_Shaded : AIS_WireFrame, -1, false);
        if (!m_modelVisible) m_context->Erase(item, false);
        m_models.push_back(item);
    }
    setInteractionMode(m_mode);
    setFaceIdsVisible(m_faceIdsVisible);
    fitAll();
    // Docks and project-tree contents can change the viewport during import.
    QTimer::singleShot(0, this, &OcctViewWidget::fitAll);
}

void OcctViewWidget::fitAll() {
    m_view->MustBeResized();
    m_view->FitAll(0.08, false);
    m_view->ZFitAll();
    update();
}
void OcctViewWidget::resetView() { m_view->SetProj(V3d_XposYnegZpos); fitAll(); }
void OcctViewWidget::orientation(V3d_TypeOfOrientation direction) { m_view->SetProj(direction); update(); }

void OcctViewWidget::setModelVisible(bool visible) {
    m_modelVisible = visible;
    for (auto& item : m_models) {
        if (visible) m_context->Display(item, false);
        else m_context->Erase(item, false);
    }
    for (auto& item : m_highlights) {
        if (visible) m_context->Display(item, false);
        else m_context->Erase(item, false);
    }
    for (auto& item : m_faceLabels) {
        if (visible && m_faceIdsVisible) m_context->Display(item, 0, -1, false);
        else m_context->Erase(item, false);
    }
    if (!m_edgeHighlight.IsNull()) {
        if (visible) m_context->Display(m_edgeHighlight, AIS_WireFrame, -1, false);
        else m_context->Erase(m_edgeHighlight, false);
    }
    setInteractionMode(m_mode);
    update();
}
void OcctViewWidget::setFaceIdsVisible(bool visible) {
    m_faceIdsVisible = visible;
    if (visible && m_document && m_faceLabels.empty()) {
        for (int id = 1; id <= m_document->faces.count(); ++id) {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(m_document->faces.face(id), properties);
            Handle(AIS_TextLabel) label = new AIS_TextLabel;
            label->SetText(TCollection_ExtendedString(("Face " + std::to_string(id)).c_str()));
            label->SetPosition(properties.CentreOfMass());
            label->SetColor(Quantity_NOC_YELLOW);
            label->SetHeight(12.0);
            m_faceLabels.push_back(label);
        }
    }
    for (auto& label : m_faceLabels) {
        if (visible && m_modelVisible) m_context->Display(label, 0, -1, false);
        else if (m_context->IsDisplayed(label)) m_context->Erase(label, false);
    }
    update();
}
void OcctViewWidget::shaded(bool enabled) {
    m_shaded = enabled;
    for (auto& item : m_models) m_context->SetDisplayMode(item, enabled ? AIS_Shaded : AIS_WireFrame, false);
    update();
}

void OcctViewWidget::setInteractionMode(InteractionMode mode) {
    if (mode == InteractionMode::PickDirectionPoint1) {
        for (auto& item : m_directionMarkers) m_context->Remove(item, false);
        m_directionMarkers.clear();
    }
    m_mode = mode;
    for (auto& item : m_models) {
        m_context->Deactivate(item);
        if (!m_modelVisible || mode == InteractionMode::Navigate) continue;
        // Display(..., selectionMode=-1) deliberately excludes the model from
        // auto selection. OCCT 7.6 requires loading it into the selector before
        // explicitly activating a Face/Edge mode.
        m_context->Load(item, -1);
        m_context->Activate(item, AIS_Shape::SelectionMode(mode == InteractionMode::SelectEdge ? TopAbs_EDGE : TopAbs_FACE));
    }
    m_context->ClearDetected(false);
    setCursor(mode == InteractionMode::Navigate ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void OcctViewWidget::setSelectedFaceIds(const std::vector<int>& ids) {
    for (auto& item : m_highlights) m_context->Remove(item, false);
    m_highlights.clear();
    if (!m_document) return;
    for (int id : ids) {
        Handle(AIS_Shape) item = new AIS_Shape(m_document->faces.face(id));
        // Reuse the displayed model's tessellation. Remeshing a single Face at
        // a different deflection would make its translucent overlay intersect
        // the root model's cached GPU triangles.
        item->Attributes()->SetAutoTriangulation(false);
        item->SetColor(Quantity_NOC_CYAN1);
        item->SetTransparency(0.25);
        item->SetPolygonOffsets(Aspect_POM_Fill, -1.0, -1.0);
        m_context->Display(item, AIS_Shaded, -1, false);
        if (!m_modelVisible) m_context->Erase(item, false);
        m_highlights.push_back(item);
    }
    update();
}

QPoint OcctViewWidget::pixelPoint(const QPointF& position) const {
    return QPoint(qRound(position.x() * devicePixelRatioF()), qRound(position.y() * devicePixelRatioF()));
}
void OcctViewWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    m_pressPosition = m_lastPosition = pixelPoint(mousePosition(event));
    if (event->button() == Qt::RightButton || (event->button() == Qt::LeftButton && m_mode == InteractionMode::Navigate))
        m_view->StartRotation(m_lastPosition.x(), m_lastPosition.y());
    event->accept();
}
void OcctViewWidget::mouseMoveEvent(QMouseEvent* event) {
    const auto position = pixelPoint(mousePosition(event));
    if ((event->buttons() & Qt::MiddleButton) != 0) {
        m_view->Pan(position.x() - m_lastPosition.x(), m_lastPosition.y() - position.y());
    } else if ((event->buttons() & Qt::RightButton) != 0 ||
              ((event->buttons() & Qt::LeftButton) != 0 && m_mode == InteractionMode::Navigate)) {
        m_view->Rotation(position.x(), position.y());
    } else if (event->buttons() == Qt::NoButton && m_mode != InteractionMode::Navigate) {
        m_context->MoveTo(position.x(), position.y(), m_view, false);
        int id = -1;
        if (m_document) {
            const auto owner = Handle(StdSelect_BRepOwner)::DownCast(m_context->DetectedOwner());
            if (!owner.IsNull() && owner->HasShape() && owner->Shape().ShapeType() == TopAbs_FACE) {
                auto shape = owner->Shape();
                shape.Location(owner->Location() * shape.Location());
                id = m_document->faces.idFor(TopoDS::Face(shape));
            }
        }
        emit hoveredFace(id);
    }
    m_lastPosition = position;
    update();
    event->accept();
}
void OcctViewWidget::mouseReleaseEvent(QMouseEvent* event) {
    const auto position = pixelPoint(mousePosition(event));
    if (event->button() != Qt::LeftButton || m_mode == InteractionMode::Navigate ||
        (position - m_pressPosition).manhattanLength() > qRound(4.0 * devicePixelRatioF())) return;
    m_context->MoveTo(position.x(), position.y(), m_view, false);
    const auto owner = Handle(StdSelect_BRepOwner)::DownCast(m_context->DetectedOwner());
    if (owner.IsNull() || !owner->HasShape()) return;
    auto shape = owner->Shape();
    shape.Location(owner->Location() * shape.Location());
    if (m_mode == InteractionMode::SelectFace && m_document && shape.ShapeType() == TopAbs_FACE) {
        const int id = m_document->faces.idFor(TopoDS::Face(shape));
        if (id > 0) emit faceClicked(id, (event->modifiers() & Qt::ControlModifier) != 0);
    } else if (m_mode == InteractionMode::SelectEdge && shape.ShapeType() == TopAbs_EDGE) {
        if (!m_edgeHighlight.IsNull()) m_context->Remove(m_edgeHighlight, false);
        m_edgeHighlight = new AIS_Shape(shape);
        m_edgeHighlight->SetColor(Quantity_NOC_YELLOW);
        m_edgeHighlight->SetWidth(3.0);
        m_context->Display(m_edgeHighlight, AIS_WireFrame, -1, false);
        emit edgePicked(TopoDS::Edge(shape));
    } else if (m_mode == InteractionMode::PickDirectionPoint1 || m_mode == InteractionMode::PickDirectionPoint2) {
        if (m_context->MainSelector()->NbPicked() > 0) {
            const auto point = m_context->MainSelector()->PickedPoint(1);
            Handle(AIS_Point) marker = new AIS_Point(new Geom_CartesianPoint(point));
            marker->SetColor(Quantity_NOC_YELLOW);
            marker->SetMarker(Aspect_TOM_O_PLUS);
            m_context->Display(marker, 0, -1, false);
            m_directionMarkers.push_back(marker);
            if (m_mode == InteractionMode::PickDirectionPoint1) {
                m_firstDirectionPoint = point;
                setInteractionMode(InteractionMode::PickDirectionPoint2);
            } else {
                emit directionPointsPicked(m_firstDirectionPoint, point);
                setInteractionMode(InteractionMode::SelectFace);
            }
        }
    }
    update();
    event->accept();
}
void OcctViewWidget::wheelEvent(QWheelEvent* event) {
    const double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    m_view->SetScale(m_view->Scale() * factor);
    update();
    event->accept();
}
void OcctViewWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (!m_edgeHighlight.IsNull()) m_context->Remove(m_edgeHighlight, false);
        m_edgeHighlight.Nullify();
        for (auto& item : m_directionMarkers) m_context->Remove(item, false);
        m_directionMarkers.clear();
        if (m_mode == InteractionMode::PickDirectionPoint1 || m_mode == InteractionMode::PickDirectionPoint2)
            setInteractionMode(InteractionMode::SelectFace);
        emit clearSelectionRequested();
        update();
        event->accept();
    }
    else if (event->key() == Qt::Key_F) { fitAll(); event->accept(); }
    else QWidget::keyPressEvent(event);
}
} // namespace sps
