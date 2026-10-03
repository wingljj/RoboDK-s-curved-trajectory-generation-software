#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_View.hxx>
#include <QWidget>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
#include <memory>
#include <vector>

namespace sps {
struct CadDocument;
enum class InteractionMode { Navigate, SelectFace, SelectEdge, PickDirectionPoint1, PickDirectionPoint2 };

class OcctViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit OcctViewWidget(QWidget* parent = nullptr);
    ~OcctViewWidget() override;
    Handle(AIS_InteractiveContext) context() const { return m_context; }
    Handle(V3d_View) view() const { return m_view; }
    void displayDocument(std::shared_ptr<CadDocument> document);
    void clearDocument();
    void fitAll();
    void resetView();
    void setInteractionMode(InteractionMode mode);
    InteractionMode interactionMode() const { return m_mode; }
    void setSelectedFaceIds(const std::vector<int>& ids);
    void orientation(V3d_TypeOfOrientation orientation);
    void setModelVisible(bool visible);
    void setFaceIdsVisible(bool visible);
    void shaded(bool enabled);
signals:
    void faceClicked(int id, bool additive);
    void edgePicked(const TopoDS_Edge& edge);
    void directionPointsPicked(const gp_Pnt& first, const gp_Pnt& second);
    void clearSelectionRequested();
    void hoveredFace(int id);
protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    void initializeView();
    QPoint pixelPoint(const QPointF& position) const;
    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;
    std::shared_ptr<CadDocument> m_document;
    std::vector<Handle(AIS_Shape)> m_models;
    std::vector<Handle(AIS_Shape)> m_highlights;
    std::vector<Handle(AIS_InteractiveObject)> m_faceLabels;
    std::vector<Handle(AIS_InteractiveObject)> m_directionMarkers;
    Handle(AIS_Shape) m_edgeHighlight;
    InteractionMode m_mode = InteractionMode::Navigate;
    QPoint m_lastPosition;
    QPoint m_pressPosition;
    gp_Pnt m_firstDirectionPoint;
    bool m_modelVisible = true;
    bool m_shaded = true;
    bool m_faceIdsVisible = false;
};
} // namespace sps
