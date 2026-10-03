#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include "path/PathTypes.h"
#include <map>
#include <memory>

class QAction;
class QProgressBar;
class QUndoStack;
class QCloseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

namespace sps {
class OcctViewWidget;
class SelectionManager;
class PathPresentation;
class PathParameterPanel;
class ModelTreeWidget;
class PathGenerationController;
struct CadDocument;
struct PathRecord {
    int id = 0;
    QString name;
    SurfacePath path;
    SelectedSurfaceRegion region;
    PathParameters parameters;
    std::shared_ptr<PathPresentation> presentation;
    bool visible = true;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openStepFile(const QString& file);
    void runSmokeTest(const QString& outputDirectory);
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QString droppedStepFile(const QMimeData* mime, Qt::DropActions actions) const;
    void buildActions();
    void setDocument(std::shared_ptr<CadDocument> document);
    void changeSelection(const std::vector<int>& ids);
    void updateSelection();
    void invalidatePreview();
    void requestPreview(bool saveAfter = false, int replacementId = -1);
    void acceptPreview();
    void addRecord(const std::shared_ptr<PathRecord>& record);
    void removeRecord(int id);
    void deleteRecord(int id);
    void renameRecord(int id);
    void regenerateRecord(int id);
    void exportPath(bool simplified);
    void applyDisplayOptions(PathPresentation& presentation) const;
    void loadExample(int index);
    void finishSmokeTest();
    void smokeFailure(const QString& error);
    void capturePreview(const QString& fileName);
    OcctViewWidget* viewer_ = nullptr;
    SelectionManager* selection_ = nullptr;
    PathParameterPanel* parameters_ = nullptr;
    ModelTreeWidget* tree_ = nullptr;
    PathGenerationController* generation_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QUndoStack* undo_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* generateAction_ = nullptr;
    std::shared_ptr<CadDocument> document_;
    std::shared_ptr<PathPresentation> previewPresentation_;
    std::optional<SurfacePath> preview_;
    SelectedSurfaceRegion activeRegion_;
    PathParameters activeParameters_;
    std::map<int,std::shared_ptr<PathRecord>> paths_;
    int nextPathId_ = 1;
    int activePathId_ = -1;
    int replacementId_ = -1;
    unsigned revision_ = 0;
    unsigned activeRevision_ = 0;
    bool saveAfterGeneration_ = false;
    bool showPath_ = true;
    bool showContacts_ = true;
    bool showNormals_ = true;
    bool showFrames_ = true;
    bool showReference_ = false;
    bool showPlanes_ = false;
    bool showLayerNumbers_ = false;
    QString smokeDirectory_;
    QJsonObject smokeReport_;
};
} // namespace sps
