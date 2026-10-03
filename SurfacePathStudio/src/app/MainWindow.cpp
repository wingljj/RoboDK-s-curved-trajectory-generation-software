#include "MainWindow.h"
#include "AboutDialog.h"
#include "Branding.h"
#include "PathGenerationController.h"
#include "cad/CadDocument.h"
#include "cad/StepImporter.h"
#include "export/CsvExporter.h"
#include "ui/ModelTreeWidget.h"
#include "ui/PathParameterPanel.h"
#include "viewer/OcctViewWidget.h"
#include "viewer/PathPresentation.h"
#include "viewer/SelectionManager.h"
#ifdef SURFACEPATH_EXAMPLES_AVAILABLE
#include "examples/ExampleModels.h"
#include <STEPControl_Controller.hxx>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include <Interface_Static.hxx>
#endif
#include <BRepAdaptor_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QIcon>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sps {
namespace {
class FunctionalCommand final : public QUndoCommand {
public:
    FunctionalCommand(const QString& text, std::function<void()> redo,
                      std::function<void()> undo)
        : QUndoCommand(text), redo_(std::move(redo)), undo_(std::move(undo)) {}
    void redo() override { redo_(); }
    void undo() override { undo_(); }
private:
    std::function<void()> redo_, undo_;
};
class WaitCursor final {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};
std::vector<int> regionIds(const SelectedSurfaceRegion& region) {
    std::vector<int> ids;
    for (const auto& face : region.faces) ids.push_back(face.id);
    return ids;
}
QString pathSummary(const SurfacePath& path) {
    QString summary = QObject::tr("%1 个分段，%2 个点\n加工长度 %3 mm，转移长度 %4 mm")
        .arg(static_cast<qulonglong>(path.segments.size()))
        .arg(static_cast<qulonglong>(path.pointCount()))
        .arg(path.totalProcessLength, 0, 'f', 2).arg(path.totalTransferLength, 0, 'f', 2);
    if (!path.warnings.empty()) {
        for (const auto& warning : path.warnings) summary += QStringLiteral("\n") + QString::fromStdString(warning);
    }
    return summary;
}
bool isCancellation(const QString& error) {
    return error.contains(QStringLiteral("cancel"), Qt::CaseInsensitive) || error.contains(QStringLiteral("取消"));
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(branding::productName());
    setWindowIcon(QIcon(branding::iconPath()));
    resize(1440, 900);
    viewer_ = new OcctViewWidget(this);
    viewer_->setObjectName("cadViewer");
    setCentralWidget(viewer_);
    selection_ = new SelectionManager(this);
    generation_ = new PathGenerationController(this);
    undo_ = new QUndoStack(this);
    tree_ = new ModelTreeWidget(this);
    parameters_ = new PathParameterPanel(this);
    auto* projectDock = new QDockWidget(tr("项目"), this);
    projectDock->setObjectName("projectDock");
    projectDock->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    auto* parameterDock = new QDockWidget(tr("路径参数"), this);
    parameterDock->setObjectName("parameterDock");
    parameterDock->setWidget(parameters_);
    addDockWidget(Qt::RightDockWidgetArea, parameterDock);
    setAcceptDrops(true);
    for(QWidget* target:{static_cast<QWidget*>(viewer_),static_cast<QWidget*>(tree_),
                         static_cast<QWidget*>(tree_->viewport()),static_cast<QWidget*>(parameters_),
                         static_cast<QWidget*>(projectDock),static_cast<QWidget*>(parameterDock)})target->setAcceptDrops(true);
    // Native viewport and dock children can be the actual Qt drop target.
    // Scope the application filter to this window's widgets so text editors
    // cannot consume a file URL instead of opening the CAD model.
    qApp->installEventFilter(this);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setMaximumWidth(180);
    progress_->setVisible(false);
    statusBar()->addPermanentWidget(progress_);
    previewPresentation_ = std::make_shared<PathPresentation>(viewer_->context());
    buildActions();
    viewer_->setInteractionMode(InteractionMode::SelectFace);

    connect(selection_, &SelectionManager::changed, this, &MainWindow::updateSelection);
    connect(viewer_, &OcctViewWidget::faceClicked, this, [this](int id, bool additive) {
        auto ids = additive ? selection_->selectedFaceIds() : std::vector<int>{};
        const auto item = std::find(ids.begin(), ids.end(), id);
        if (item == ids.end()) ids.push_back(id);
        else if (additive) ids.erase(item);
        changeSelection(ids);
    });
    connect(viewer_, &OcctViewWidget::clearSelectionRequested, this, [this] { changeSelection({}); });
    connect(viewer_, &OcctViewWidget::hoveredFace, this, [this](int id) {
        if (generation_->busy()) return;
        if (document_ && id >= 1 && id <= document_->faces.count()) {
            const auto& data = document_->faces.metadata(id);
            statusBar()->showMessage(tr("Face %1 · %2 · 面积 %3 mm²")
                .arg(id).arg(QString::fromStdString(data.type)).arg(data.area, 0, 'f', 2));
        } else {
            statusBar()->showMessage(tr("已选 %1 个 CAD Face，总面积 %2 mm²")
                .arg(static_cast<qulonglong>(selection_->selectedFaceIds().size()))
                .arg(selection_->totalArea(), 0, 'f', 2));
        }
    });
    connect(viewer_, &OcctViewWidget::edgePicked, this, [this](const TopoDS_Edge& edge) {
        try {
            BRepAdaptor_Curve curve(edge);
            const double first = curve.FirstParameter(), last = curve.LastParameter();
            if (!std::isfinite(first) || !std::isfinite(last)) throw std::runtime_error("Edge has an unbounded parameter range");
            gp_Pnt midpoint;
            gp_Vec tangent;
            curve.D1(first + 0.5 * (last - first), midpoint, tangent);
            if (tangent.SquareMagnitude() <= Precision::SquareConfusion()) tangent = gp_Vec(curve.Value(first), curve.Value(last));
            if (tangent.SquareMagnitude() <= Precision::SquareConfusion()) throw std::runtime_error("Selected Edge has no usable direction");
            if (edge.Orientation() == TopAbs_REVERSED) tangent.Reverse();
            tangent.Normalize();
            parameters_->setEdgeDirection(tangent);
            viewer_->setInteractionMode(InteractionMode::SelectFace);
            findChild<QAction*>("actionSelectFaces")->setChecked(true);
            statusBar()->showMessage(tr("已使用 Edge 中点切向作为路径方向"), 5000);
        } catch (const Standard_Failure& error) {
            QMessageBox::warning(this, tr("方向选择失败"), QString::fromUtf8(error.GetMessageString()));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("方向选择失败"), QString::fromUtf8(error.what()));
        }
    });
    connect(viewer_, &OcctViewWidget::directionPointsPicked, this, [this](const gp_Pnt& first, const gp_Pnt& second) {
        gp_Vec direction(first, second);
        if (direction.SquareMagnitude() <= Precision::SquareConfusion()) {
            statusBar()->showMessage(tr("两个点过于接近，请重新拾取"), 5000);
            viewer_->setInteractionMode(InteractionMode::PickDirectionPoint1);
            return;
        }
        direction.Normalize();
        parameters_->setPickedDirection(direction);
        viewer_->setInteractionMode(InteractionMode::SelectFace);
        findChild<QAction*>("actionSelectFaces")->setChecked(true);
        statusBar()->showMessage(tr("已使用两点向量作为路径方向"), 5000);
    });
    connect(tree_, &ModelTreeWidget::regionSelected, this, &MainWindow::changeSelection);
    connect(tree_, &ModelTreeWidget::pathSelected, this, [this](int id) {
        const auto found = paths_.find(id);
        if (found == paths_.end()) return;
        invalidatePreview();
        parameters_->setParameters(found->second->parameters);
        changeSelection(regionIds(found->second->region));
        activePathId_ = id;
        for (const auto& entry : paths_) entry.second->presentation->setHighlighted(entry.first == id);
        parameters_->setSummary(pathSummary(found->second->path));
    });
    connect(tree_, &ModelTreeWidget::pathVisibilityChanged, this, [this](int id, bool visible) {
        const auto found = paths_.find(id);
        if (found == paths_.end()) return;
        found->second->visible = visible;
        applyDisplayOptions(*found->second->presentation);
        viewer_->update();
    });
    connect(tree_, &ModelTreeWidget::cadVisibilityChanged, this, [this](bool visible) {
        viewer_->setModelVisible(visible);
        auto* action = findChild<QAction*>("actionShowCad");
        QSignalBlocker blocker(action);
        action->setChecked(visible);
    });
    connect(tree_, &ModelTreeWidget::deletePathRequested, this, &MainWindow::deleteRecord);
    connect(tree_, &ModelTreeWidget::renamePathRequested, this, &MainWindow::renameRecord);
    connect(tree_, &ModelTreeWidget::regeneratePathRequested, this, &MainWindow::regenerateRecord);
    connect(parameters_, &PathParameterPanel::parametersChanged, this, &MainWindow::invalidatePreview);
    connect(parameters_, &PathParameterPanel::previewRequested, this, [this] { requestPreview(); });
    connect(parameters_, &PathParameterPanel::generateRequested, this, [this] {
        if (preview_ && activeRevision_ == revision_) acceptPreview();
        else requestPreview(true);
    });
    connect(parameters_, &PathParameterPanel::cancelRequested, generation_, &PathGenerationController::cancel);
    connect(parameters_, &PathParameterPanel::pickEdgeRequested, this, [this] {
        findChild<QAction*>("actionSelectEdges")->trigger();
    });
    connect(parameters_, &PathParameterPanel::pickPointsRequested, this, [this] {
        findChild<QAction*>("actionDirectionPoints")->trigger();
    });
    connect(generation_, &PathGenerationController::progressChanged, progress_, &QProgressBar::setValue);
    connect(generation_, &PathGenerationController::busyChanged, this, [this](bool busy) {
        parameters_->setBusy(busy);
        progress_->setVisible(busy);
        if (busy) progress_->setValue(0);
        openAction_->setEnabled(!busy);
        generateAction_->setEnabled(!busy);
        findChild<QAction*>("actionPreviewPath")->setEnabled(!busy);
        findChild<QAction*>("actionCancelGeneration")->setEnabled(busy);
        if (busy) statusBar()->showMessage(tr("正在计算曲面截线和姿态…"));
    });
    connect(generation_, &PathGenerationController::pathReady, this, [this](const SurfacePath& path) {
        if (activeRevision_ != revision_) {
            saveAfterGeneration_ = false;
            replacementId_ = -1;
            statusBar()->showMessage(tr("参数或加工区域已变化，已丢弃过期计算结果"), 5000);
            return;
        }
        preview_ = path;
        previewPresentation_->show(path);
        applyDisplayOptions(*previewPresentation_);
        parameters_->setSummary(pathSummary(path));
        statusBar()->showMessage(tr("路径预览完成：%1 个点").arg(static_cast<qulonglong>(path.pointCount())), 5000);
        viewer_->update();
        if (saveAfterGeneration_) acceptPreview();
        if (!smokeDirectory_.isEmpty()) QTimer::singleShot(200, this, &MainWindow::finishSmokeTest);
    });
    connect(generation_, &PathGenerationController::failed, this, [this](const QString& error) {
        saveAfterGeneration_ = false;
        replacementId_ = -1;
        if (!smokeDirectory_.isEmpty()) { smokeFailure(error); return; }
        if (activeRevision_ != revision_ || isCancellation(error)) {
            statusBar()->showMessage(tr("路径计算已取消"), 5000);
            return;
        }
        parameters_->setSummary(tr("路径生成失败：%1").arg(error));
        statusBar()->showMessage(tr("路径生成失败"), 5000);
        QMessageBox::warning(this, tr("路径生成失败"), error);
    });
    parameters_->setSelectionInfo(0, 0);
    statusBar()->showMessage(tr("导入 STEP 后选择 CAD Face；按 Ctrl 可多选。"));
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    delete generation_;
    generation_ = nullptr;
    undo_->clear();
    paths_.clear();
    previewPresentation_.reset();
}

void MainWindow::buildActions() {
    auto* file = menuBar()->addMenu(tr("文件"));
    auto* edit = menuBar()->addMenu(tr("编辑"));
    auto* view = menuBar()->addMenu(tr("视图"));
    auto* select = menuBar()->addMenu(tr("选择"));
    auto* path = menuBar()->addMenu(tr("路径"));
    auto* help = menuBar()->addMenu(tr("帮助"));
    auto* toolbar = addToolBar(tr("常用操作"));
    toolbar->setObjectName("mainToolBar");
    toolbar->setMovable(false);
    auto action = [this](QMenu* menu, const QString& text, const char* name, std::function<void()> callback) {
        auto* result = new QAction(text, this);
        result->setObjectName(QString::fromLatin1(name));
        menu->addAction(result);
        connect(result, &QAction::triggered, this, [callback](bool) { callback(); });
        return result;
    };
    openAction_ = action(file, tr("导入 STEP…"), "actionOpenStep", [this] {
        const auto name = QFileDialog::getOpenFileName(this, tr("导入 STEP 模型"), {}, tr("STEP 文件 (*.step *.stp);;所有文件 (*)"));
        if (!name.isEmpty()) openStepFile(name);
    });
    openAction_->setShortcut(QKeySequence::Open);
    toolbar->addAction(openAction_);
    action(file, tr("导出完整 CSV…"), "actionExportFullCsv", [this] { exportPath(false); });
    action(file, tr("导出简化 CSV…"), "actionExportSimpleCsv", [this] { exportPath(true); });
#ifdef SURFACEPATH_EXAMPLES_AVAILABLE
    auto* exampleMenu = file->addMenu(tr("示例模型"));
    const auto fixtures = examples::fixtures();
    const QStringList labels{tr("平面"), tr("圆柱片面"), tr("整圆柱面"), tr("修剪球面"), tr("修剪自由曲面"), tr("相连双曲面"), tr("带孔平面")};
    for (int index = 0; index < static_cast<int>(fixtures.size()); ++index)
        action(exampleMenu, index < labels.size() ? labels[index] : QString::fromStdString(fixtures[index].name),
               qPrintable(QString("actionExample%1").arg(index)), [this, index] { loadExample(index); });
#endif
    file->addSeparator();
    action(file, tr("退出"), "actionExit", [this] { close(); })->setShortcut(QKeySequence::Quit);
    auto* undoAction = undo_->createUndoAction(this, tr("撤销"));
    undoAction->setShortcut(QKeySequence::Undo);
    auto* redoAction = undo_->createRedoAction(this, tr("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    edit->addAction(undoAction); edit->addAction(redoAction);
    toolbar->addSeparator(); toolbar->addAction(undoAction); toolbar->addAction(redoAction);
    action(edit, tr("删除当前路径"), "actionDeletePath", [this] { deleteRecord(activePathId_); })->setShortcut(QKeySequence::Delete);
    action(edit, tr("重命名当前路径…"), "actionRenamePath", [this] { renameRecord(activePathId_); });
    action(edit, tr("重新生成当前路径"), "actionRegeneratePath", [this] { regenerateRecord(activePathId_); });
    toolbar->addSeparator();
    auto* fit = action(view, tr("适合窗口"), "actionFitAll", [this] { viewer_->fitAll(); });
    fit->setShortcut(QKeySequence(Qt::Key_F)); toolbar->addAction(fit);
    action(view, tr("重置视图"), "actionResetView", [this] { viewer_->resetView(); });
    action(view, tr("轴测视图"), "actionIsometric", [this] { viewer_->orientation(V3d_XposYnegZpos); });
    action(view, tr("俯视图"), "actionTop", [this] { viewer_->orientation(V3d_Zpos); });
    action(view, tr("前视图"), "actionFront", [this] { viewer_->orientation(V3d_Yneg); });
    action(view, tr("右视图"), "actionRight", [this] { viewer_->orientation(V3d_Xpos); });
    auto* shaded = new QAction(tr("着色显示"), this);
    shaded->setObjectName("actionShaded"); shaded->setCheckable(true); shaded->setChecked(true); view->addAction(shaded);
    connect(shaded, &QAction::toggled, viewer_, &OcctViewWidget::shaded);
    auto* cad = new QAction(tr("显示 CAD"), this);
    cad->setObjectName("actionShowCad"); cad->setCheckable(true); cad->setChecked(true); view->addAction(cad);
    connect(cad, &QAction::toggled, this, [this](bool visible) {
        viewer_->setModelVisible(visible);
        QSignalBlocker blocker(tree_);
        if (auto* project = tree_->topLevelItem(0)) if (auto* item = project->child(0)) item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    });
    auto* faceIds = new QAction(tr("显示 Face ID"), this);
    faceIds->setObjectName("actionShowFaceIds"); faceIds->setCheckable(true); view->addAction(faceIds);
    connect(faceIds, &QAction::toggled, viewer_, &OcctViewWidget::setFaceIdsVisible);
    view->addSeparator();
    auto displayAction = [this, view](const QString& text, const char* name, bool* flag) {
        auto* item = new QAction(text, this);
        item->setObjectName(QString::fromLatin1(name)); item->setCheckable(true); item->setChecked(*flag); view->addAction(item);
        connect(item, &QAction::toggled, this, [this, flag](bool enabled) {
            *flag = enabled;
            if (previewPresentation_) applyDisplayOptions(*previewPresentation_);
            for (const auto& entry : paths_) applyDisplayOptions(*entry.second->presentation);
            viewer_->update();
        });
    };
    displayAction(tr("显示路径"), "actionShowPath", &showPath_);
    displayAction(tr("显示接触点"), "actionShowContacts", &showContacts_);
    displayAction(tr("显示曲面法向"), "actionShowNormals", &showNormals_);
    displayAction(tr("显示 TCP 坐标系"), "actionShowTcpFrames", &showFrames_);
    displayAction(tr("显示参考坐标系"), "actionShowReferenceFrame", &showReference_);
    displayAction(tr("显示截平面"), "actionShowSectionPlanes", &showPlanes_);
    displayAction(tr("显示层号"), "actionShowLayerNumbers", &showLayerNumbers_);
    view->addSeparator();
    for (auto* dock : findChildren<QDockWidget*>()) view->addAction(dock->toggleViewAction());
    auto* modes = new QActionGroup(this);
    modes->setExclusive(true);
    auto modeAction = [this, modes, select, toolbar](const QString& text, const char* name, InteractionMode mode) {
        auto* item = new QAction(text, this);
        item->setObjectName(QString::fromLatin1(name)); item->setCheckable(true);
        modes->addAction(item); select->addAction(item); toolbar->addAction(item);
        connect(item, &QAction::triggered, this, [this, mode] { viewer_->setInteractionMode(mode); });
        return item;
    };
    modeAction(tr("选面"), "actionSelectFaces", InteractionMode::SelectFace)->setChecked(true);
    modeAction(tr("选方向边"), "actionSelectEdges", InteractionMode::SelectEdge);
    modeAction(tr("两点方向"), "actionDirectionPoints", InteractionMode::PickDirectionPoint1);
    modeAction(tr("旋转浏览"), "actionNavigate", InteractionMode::Navigate);
    select->addSeparator();
    action(select, tr("清除面选择"), "actionClearSelection", [this] { changeSelection({}); });
    action(select, tr("选择相切相邻面"), "actionSelectTangentFaces", [this] {
        const auto before = selection_->selectedFaceIds();
        std::vector<int> after;
        {
            QSignalBlocker blocker(selection_);
            selection_->selectTangentFaces();
            after = selection_->selectedFaceIds();
            selection_->set(before);
        }
        changeSelection(after);
    });
    toolbar->addSeparator();
    auto* preview = action(path, tr("预览路径"), "actionPreviewPath", [this] { requestPreview(); });
    toolbar->addAction(preview);
    generateAction_ = action(path, tr("生成并保存路径"), "actionGeneratePath", [this] {
        if (preview_ && activeRevision_ == revision_) acceptPreview();
        else requestPreview(true);
    });
    toolbar->addAction(generateAction_);
    auto* cancel = action(path, tr("取消计算"), "actionCancelGeneration", [this] { generation_->cancel(); });
    cancel->setEnabled(false);
    action(help, tr("关于…"), "actionAbout", [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });
}

void MainWindow::openStepFile(const QString& file) {
    if (generation_->busy()) { statusBar()->showMessage(tr("请等待或取消当前路径计算"), 5000); return; }
    try {
        WaitCursor cursor;
        statusBar()->showMessage(tr("正在导入 STEP：%1").arg(QFileInfo(file).fileName()));
        setDocument(StepImporter::load(file));
    } catch (const Standard_Failure& error) {
        const auto text = QString::fromUtf8(error.GetMessageString());
        if (!smokeDirectory_.isEmpty()) smokeFailure(text);
        else QMessageBox::warning(this, tr("STEP 导入失败"), text);
    } catch (const std::exception& error) {
        const auto text = QString::fromUtf8(error.what());
        if (!smokeDirectory_.isEmpty()) smokeFailure(text);
        else QMessageBox::warning(this, tr("STEP 导入失败"), text);
    }
}

QString MainWindow::droppedStepFile(const QMimeData* mime, Qt::DropActions actions) const {
    if(!mime||!mime->hasUrls()||!(actions&Qt::CopyAction)||!generation_||generation_->busy()||QApplication::activeModalWidget())return {};
    const auto files=mime->urls();
    if(files.size()!=1||!files.front().isLocalFile())return {};
    const QFileInfo file(files.front().toLocalFile());
    if(!file.isFile()||!file.isReadable())return {};
    const QString extension=file.suffix();
    if(extension.compare(QStringLiteral("stp"),Qt::CaseInsensitive)!=0&&
       extension.compare(QStringLiteral("step"),Qt::CaseInsensitive)!=0)return {};
    return file.absoluteFilePath();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if(droppedStepFile(event->mimeData(),event->possibleActions()).isEmpty()){event->ignore();return;}
    event->setDropAction(Qt::CopyAction);event->accept();
}
void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if(droppedStepFile(event->mimeData(),event->possibleActions()).isEmpty()){event->ignore();return;}
    event->setDropAction(Qt::CopyAction);event->accept();
}
void MainWindow::dropEvent(QDropEvent* event) {
    const QString file=droppedStepFile(event->mimeData(),event->possibleActions());
    if(file.isEmpty()){event->ignore();return;}
    event->setDropAction(Qt::CopyAction);event->accept();
    openStepFile(file);
}
bool MainWindow::eventFilter(QObject* watched,QEvent* event) {
    const auto type=event->type();
    if(type!=QEvent::DragEnter&&type!=QEvent::DragMove&&type!=QEvent::Drop)return QMainWindow::eventFilter(watched,event);
    const auto widget=qobject_cast<QWidget*>(watched);
    if(!widget||(widget!=this&&!isAncestorOf(widget)))return QMainWindow::eventFilter(watched,event);
    // Ordinary text drags retain the target widget's existing behavior.
    const auto drop=static_cast<QDropEvent*>(event);
    if(!drop->mimeData()||!drop->mimeData()->hasUrls())return QMainWindow::eventFilter(watched,event);
    if(type==QEvent::DragEnter)dragEnterEvent(static_cast<QDragEnterEvent*>(event));
    else if(type==QEvent::DragMove)dragMoveEvent(static_cast<QDragMoveEvent*>(event));
    else dropEvent(drop);
    return true;
}

void MainWindow::setDocument(std::shared_ptr<CadDocument> document) {
    invalidatePreview();
    undo_->clear();
    paths_.clear();
    document_ = std::move(document);
    activePathId_ = -1; nextPathId_ = 1;
    tree_->setDocument(document_);
    viewer_->setModelVisible(true);
    viewer_->displayDocument(document_);
    viewer_->setInteractionMode(InteractionMode::SelectFace);
    findChild<QAction*>("actionSelectFaces")->setChecked(true);
    {
        QSignalBlocker blocker(findChild<QAction*>("actionShowCad"));
        findChild<QAction*>("actionShowCad")->setChecked(true);
    }
    parameters_->resetDirectionSources();
    selection_->setDocument(document_);
    if (document_) {
        setWindowTitle(tr("%1 — %2").arg(QFileInfo(document_->sourcePath).fileName(), branding::productName()));
        statusBar()->showMessage(tr("STEP 已导入 · %1 · %2 个 CAD Face · 内部单位 mm，请选择加工区域")
            .arg(document_->valid ? tr("拓扑有效") : tr("拓扑检查未通过"))
            .arg(document_->faces.count()));
    }
}

void MainWindow::changeSelection(const std::vector<int>& ids) {
    std::vector<int> valid;
    if (document_) for (int id : ids) {
        if (id >= 1 && id <= document_->faces.count() && std::find(valid.begin(), valid.end(), id) == valid.end()) valid.push_back(id);
    }
    const auto before = selection_->selectedFaceIds();
    if (before == valid) return;
    undo_->push(new FunctionalCommand(tr("选择加工曲面"),
        [this, valid] { selection_->set(valid); }, [this, before] { selection_->set(before); }));
}

void MainWindow::updateSelection() {
    const auto& ids = selection_->selectedFaceIds();
    viewer_->setSelectedFaceIds(ids);
    tree_->setCurrentSelection(ids);
    parameters_->setSelectionInfo(static_cast<int>(ids.size()), selection_->totalArea());
    invalidatePreview();
    statusBar()->showMessage(tr("已选 %1 个 CAD Face，总面积 %2 mm²")
        .arg(static_cast<qulonglong>(ids.size())).arg(selection_->totalArea(), 0, 'f', 2));
}

void MainWindow::invalidatePreview() {
    ++revision_;
    if (generation_ && generation_->busy()) generation_->cancel();
    preview_.reset();
    if (previewPresentation_) previewPresentation_->clear();
    saveAfterGeneration_ = false;
    replacementId_ = -1;
    parameters_->setSummary(tr("参数或加工区域已更新，请重新预览。"));
    viewer_->update();
}

void MainWindow::requestPreview(bool saveAfter, int replacementId) {
    if (generation_->busy()) { statusBar()->showMessage(tr("路径正在计算，请等待或取消"), 5000); return; }
    if (!document_) { statusBar()->showMessage(tr("请先导入 STEP 模型"), 5000); return; }
    try {
        const auto parameters = parameters_->parameters();
        SelectedSurfaceRegion region;
        if (replacementId >= 0) {
            const auto old = paths_.find(replacementId);
            if (old == paths_.end()) return;
            region = old->second->region;
        } else {
            region = document_->faces.region(selection_->selectedFaceIds());
        }
        if (region.faces.empty()) { statusBar()->showMessage(tr("请至少选择一个 CAD Face 作为加工区域"), 5000); return; }
        invalidatePreview();
        activeRegion_ = std::move(region);
        activeParameters_ = parameters;
        activeRevision_ = revision_;
        saveAfterGeneration_ = saveAfter;
        replacementId_ = replacementId;
        parameters_->setSummary(tr("正在生成真实曲面截线…"));
        generation_->start(activeRegion_, activeParameters_);
    } catch (const Standard_Failure& error) {
        QMessageBox::warning(this, tr("无法预览"), QString::fromUtf8(error.GetMessageString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("无法预览"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::acceptPreview() {
    if (!preview_ || activeRevision_ != revision_) return;
    auto record = std::make_shared<PathRecord>();
    record->path = *preview_;
    record->region = activeRegion_;
    record->parameters = activeParameters_;
    record->presentation = std::make_shared<PathPresentation>(viewer_->context());
    const auto previous = paths_.find(replacementId_);
    if (replacementId_ >= 0 && previous != paths_.end()) {
        const auto old = previous->second;
        record->id = old->id; record->name = old->name; record->visible = old->visible;
        undo_->push(new FunctionalCommand(tr("重新生成路径"),
            [this, record] { removeRecord(record->id); addRecord(record); },
            [this, old] { removeRecord(old->id); addRecord(old); }));
    } else {
        record->id = nextPathId_++;
        record->name = QString("SurfacePath_%1").arg(record->id);
        undo_->push(new FunctionalCommand(tr("生成路径"),
            [this, record] { addRecord(record); }, [this, record] { removeRecord(record->id); }));
    }
    preview_.reset();
    previewPresentation_->clear();
    saveAfterGeneration_ = false; replacementId_ = -1;
    parameters_->setSummary(pathSummary(record->path));
    statusBar()->showMessage(tr("已保存路径：%1").arg(record->name), 5000);
    viewer_->update();
}

void MainWindow::addRecord(const std::shared_ptr<PathRecord>& record) {
    if (paths_.count(record->id)) removeRecord(record->id);
    if (!record->presentation) record->presentation = std::make_shared<PathPresentation>(viewer_->context());
    record->presentation->show(record->path);
    paths_[record->id] = record;
    activePathId_ = record->id;
    tree_->addPath(record->id, record->name, regionIds(record->region));
    tree_->setPathVisible(record->id, record->visible);
    for (const auto& entry : paths_) entry.second->presentation->setHighlighted(entry.first == activePathId_);
    applyDisplayOptions(*record->presentation);
    parameters_->setSummary(pathSummary(record->path));
    viewer_->update();
}

void MainWindow::removeRecord(int id) {
    const auto found = paths_.find(id);
    if (found == paths_.end()) return;
    if (generation_->busy() && replacementId_ == id) invalidatePreview();
    found->second->presentation->clear();
    paths_.erase(found);
    tree_->removePath(id);
    if (activePathId_ == id) activePathId_ = -1;
    viewer_->update();
}

void MainWindow::deleteRecord(int id) {
    const auto found = paths_.find(id);
    if (found == paths_.end()) return;
    const auto record = found->second;
    undo_->push(new FunctionalCommand(tr("删除路径"),
        [this, id] { removeRecord(id); }, [this, record] { addRecord(record); }));
}

void MainWindow::renameRecord(int id) {
    const auto found = paths_.find(id);
    if (found == paths_.end()) return;
    const auto record = found->second;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("重命名路径"), tr("路径名称"),
        QLineEdit::Normal, record->name, &accepted).trimmed();
    if (!accepted || name.isEmpty() || name == record->name) return;
    const auto oldName = record->name;
    undo_->push(new FunctionalCommand(tr("重命名路径"),
        [this, record, name] { record->name = name; tree_->renamePath(record->id, name); },
        [this, record, oldName] { record->name = oldName; tree_->renamePath(record->id, oldName); }));
}

void MainWindow::regenerateRecord(int id) {
    if (!paths_.count(id)) return;
    requestPreview(true, id);
}

void MainWindow::exportPath(bool simplified) {
    const SurfacePath* source = nullptr;
    std::optional<SurfacePath> previewSnapshot;
    std::shared_ptr<PathRecord> record;
    QString name = QStringLiteral("SurfacePath");
    const auto found = paths_.find(activePathId_);
    if (found != paths_.end()) { record = found->second; source = &record->path; name = record->name; }
    else if (preview_) { previewSnapshot = *preview_; source = &*previewSnapshot; }
    if (!source) { statusBar()->showMessage(tr("请先生成或预览路径"), 5000); return; }
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    const auto file = QFileDialog::getSaveFileName(this, simplified ? tr("导出简化 CSV") : tr("导出完整 CSV"),
        name + (simplified ? QStringLiteral("_simple.csv") : QStringLiteral(".csv")), tr("CSV 文件 (*.csv)"));
    if (file.isEmpty()) return;
    try {
        std::ostringstream stream;
        CsvExporter(simplified).write(stream, *source);
        const auto csvData = QByteArray::fromStdString(stream.str());
        QSaveFile output(file);
        if (!output.open(QIODevice::WriteOnly) || output.write(csvData) != csvData.size() || !output.commit())
            throw std::runtime_error(output.errorString().toStdString());
        statusBar()->showMessage(tr("CSV 已导出：%1").arg(file), 5000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("CSV 导出失败"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::applyDisplayOptions(PathPresentation& presentation) const {
    bool visible = true;
    for (const auto& entry : paths_) if (entry.second->presentation.get() == &presentation) {
        visible = entry.second->visible; break;
    }
    presentation.setVisible(visible);
    presentation.setPathVisible(showPath_);
    presentation.setContactPointsVisible(showContacts_);
    presentation.setNormalsVisible(showNormals_);
    presentation.setFramesVisible(showFrames_);
    presentation.setReferenceFrameVisible(showReference_);
    presentation.setSectionPlanesVisible(showPlanes_);
    presentation.setLayerNumbersVisible(showLayerNumbers_);
}

void MainWindow::loadExample(int index) {
#ifdef SURFACEPATH_EXAMPLES_AVAILABLE
    if (generation_->busy()) { statusBar()->showMessage(tr("请等待或取消当前路径计算"), 5000); return; }
    try {
        WaitCursor cursor;
        const auto fixtures = examples::fixtures();
        if (index < 0 || index >= static_cast<int>(fixtures.size())) return;
        const auto& fixture = fixtures[index];
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/SurfacePathStudio-examples");
        if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create example directory");
        const auto file = QDir(directory).filePath(QString::fromStdString(fixture.fileStem) + QStringLiteral(".step"));
        STEPControl_Controller::Init();
        Interface_Static::SetCVal("write.step.unit", "MM");
        STEPControl_Writer writer;
        writer.Model()->SetLocalLengthUnit(1.0);
        if (writer.Transfer(fixture.shape, STEPControl_AsIs) != IFSelect_RetDone ||
            writer.Write(QFile::encodeName(file).constData()) != IFSelect_RetDone)
            throw std::runtime_error("Cannot write example STEP file");
        setDocument(StepImporter::load(file));
        parameters_->setParameters(fixture.parameters);
        invalidatePreview();
        statusBar()->showMessage(tr("示例已载入：%1。请选择 CAD Face 后预览。")
            .arg(QString::fromStdString(fixture.name)), 8000);
    } catch (const Standard_Failure& error) {
        QMessageBox::warning(this, tr("示例载入失败"), QString::fromUtf8(error.GetMessageString()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("示例载入失败"), QString::fromUtf8(error.what()));
    }
#else
    Q_UNUSED(index);
#endif
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (generation_ && generation_->busy()) {
        generation_->cancel();
        event->ignore();
        statusBar()->showMessage(tr("正在取消计算，结束后可关闭窗口。"), 8000);
        return;
    }
    event->accept();
}
} // namespace sps
