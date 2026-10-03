#include "MainWindow.h"
#include "PathGenerationController.h"
#include "cad/CadDocument.h"
#include "cad/StepImporter.h"
#include "export/CsvExporter.h"
#include "geometry/SurfaceNormal.h"
#include "ui/ModelTreeWidget.h"
#include "ui/PathParameterPanel.h"
#include "viewer/OcctViewWidget.h"
#include "viewer/PathPresentation.h"
#include "viewer/SelectionManager.h"
#ifdef SURFACEPATH_EXAMPLES_AVAILABLE
#include "examples/ExampleModels.h"
#endif
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <QJsonArray>
#include <AIS_ListOfInteractive.hxx>
#include <SelectMgr_Selection.hxx>
#include <TColStd_ListOfInteger.hxx>
#include <Image_PixMap.hxx>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QAction>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
#include <stdexcept>
#include <cstring>

namespace sps {
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
gp_Pnt faceMidpoint(const TopoDS_Face& face) {
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    return BRepAdaptor_Surface(face).Value((u0+u1)/2,(v0+v1)/2);
}
void clickPoint(OcctViewWidget* viewer,const gp_Pnt& point,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    int x,y;viewer->view()->Convert(point.X(),point.Y(),point.Z(),x,y);
    const QPointF local(x/viewer->devicePixelRatioF(),y/viewer->devicePixelRatioF());
    const QPointF window(viewer->mapTo(viewer->window(),local.toPoint()));
    const QPointF global(viewer->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress,local,window,global,Qt::LeftButton,Qt::LeftButton,modifiers);
    QMouseEvent release(QEvent::MouseButtonRelease,local,window,global,Qt::LeftButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(viewer,&press);
    QApplication::sendEvent(viewer,&release);
}
void writeText(const QString& path,const std::string& contents) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly),"Smoke output cannot be opened");
    require(file.write(contents.data(),static_cast<qint64>(contents.size()))==static_cast<qint64>(contents.size()),"Smoke output incomplete");
    require(file.commit(),"Smoke output commit failed");
}
bool dragStep(QWidget* target,const QList<QUrl>& files,bool finish=true) {
    QMimeData mime;mime.setUrls(files);
    QDragEnterEvent enter(QPoint(100,100),Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&enter);
    if(!enter.isAccepted())return false;
    QDragMoveEvent move(QPoint(105,105),Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&move);
    if(!move.isAccepted())return false;
    if(!finish)return true;
    QDropEvent drop(QPointF(105,105),Qt::CopyAction|Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target,&drop);
    return drop.isAccepted()&&drop.dropAction()==Qt::CopyAction;
}
}
void MainWindow::capturePreview(const QString& fileName) {
    viewer_->view()->Redraw();
    int width=0,height=0;
    viewer_->view()->Window()->Size(width,height);
    Image_PixMap pixels;
    require(pixels.InitZero(Image_Format_RGBA,width,height),"OCCT native render allocation failed");
    require(viewer_->view()->ToPixMap(pixels,width,height,Graphic3d_BT_RGBA),"OCCT native viewport render failed");
    require(pixels.Format()==Image_Format_RGBA,"Unexpected OCCT pixel format");
    QImage cad(width,height,QImage::Format_RGBA8888);
    for(int row=0;row<height;++row)std::memcpy(cad.scanLine(row),pixels.Row(row),static_cast<std::size_t>(width)*4);
    // Native WA_PaintOnScreen children are not included in QWidget::grab().
    // Compose the rendered OCCT viewport at its actual widget location.
    auto screenshot=grab();
    {
        QPainter painter(&screenshot);
        painter.drawImage(QRect(viewer_->mapTo(this,QPoint(0,0)),viewer_->size()),cad);
    }
    require(screenshot.save(fileName),"Desktop preview screenshot could not be saved");
}
void MainWindow::runSmokeTest(const QString& outputDirectory) {
    smokeDirectory_=QDir(outputDirectory).absolutePath();
    QDir().mkpath(smokeDirectory_);
    smokeReport_=QJsonObject{{"passed",false},{"qtVersion",qVersion()},{"uiHeartbeats",0}};
    QTimer::singleShot(30000,this,[this]{if(!smokeDirectory_.isEmpty())smokeFailure("Desktop smoke test timed out");});
    try {
        const auto productName=QStringLiteral("上海卫星装备研究所曲面轨迹生成软件");
        require(QApplication::applicationDisplayName()==productName,"Application display name is not the requested product name");
        require(windowTitle().contains(productName),"Startup title does not contain the product name");
        require(QCoreApplication::applicationVersion()==QStringLiteral(SPS_APP_VERSION),"Application version is missing");
        require(!windowIcon().isNull(),"China Aerospace window icon is missing");
        require(!QPixmap(QStringLiteral(":/branding/casc-logo.jpg")).isNull(),"Embedded China Aerospace logo is missing");
        auto* aboutAction=findChild<QAction*>("actionAbout");
        require(aboutAction!=nullptr,"Help About action is missing");
        QString aboutError;
        bool aboutVerified=false;
        QTimer::singleShot(150,this,[this,productName,&aboutError,&aboutVerified] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            try {
                require(dialog&&dialog->objectName()=="aboutDialog","About action did not open its modal dialog");
                QString contents;
                for(const auto* label:dialog->findChildren<QLabel*>())contents+=label->text()+QLatin1Char('\n');
                require(contents.contains(productName),"About dialog product name is missing");
                require(contents.contains(QStringLiteral("上海卫星装备研究所"))&&contents.contains(QStringLiteral("总装中心")),"About dialog research organization or department is missing");
                require(contents.contains(QStringLiteral("CSV"))&&contents.contains(QStringLiteral("TCP"))&&contents.contains(QStringLiteral("STEP")),"About dialog feature description is missing");
                require(contents.contains(QCoreApplication::applicationVersion()),"About dialog version is missing");
                auto* logo=dialog->findChild<QLabel*>("aboutLogo");
                require(logo&&logo->isVisible()&&!logo->property("pixmap").value<QPixmap>().isNull(),"About dialog logo is not displayed");
                require(dialog->grab().save(smokeDirectory_+"/about-dialog.png"),"About screenshot could not be saved");
                aboutVerified=true;
            } catch(const std::exception& error) {aboutError=QString::fromUtf8(error.what());}
            if(dialog)dialog->accept();
        });
        aboutAction->trigger();
        if(!aboutError.isEmpty())throw std::runtime_error(aboutError.toStdString());
        require(aboutVerified,"About dialog verification did not run");
        smokeReport_["productBranding"]=true;
        smokeReport_["aboutDialog"]=true;
        smokeReport_["applicationVersion"]=QCoreApplication::applicationVersion();
        smokeReport_["productName"]=productName;
#ifdef SURFACEPATH_EXAMPLES_AVAILABLE
        const auto fixtures=examples::fixtures();
        const auto sample=std::find_if(fixtures.begin(),fixtures.end(),[](const auto& fixture){return fixture.fileStem=="adjacent_bspline";});
        require(sample!=fixtures.end(),"Adjacent BSpline fixture is missing");
        STEPControl_Writer writer;
        const QString input=smokeDirectory_+"/adjacent_bspline.step";
        require(writer.Transfer(sample->shape,STEPControl_AsIs)==IFSelect_RetDone,"Example STEP transfer failed");
        require(writer.Write(input.toUtf8().constData())==IFSelect_RetDone,"Example STEP write failed");
        const QString droppedFile=smokeDirectory_+QStringLiteral("/拖入曲面.STP");
        if(QFile::exists(droppedFile))require(QFile::remove(droppedFile),"Old smoke fixture cannot be replaced");
        require(QFile::copy(input,droppedFile),"Unicode drop fixture cannot be prepared");
        require(dragStep(viewer_,{QUrl::fromLocalFile(droppedFile)}),"Dragging a local STP into the CAD viewport was not accepted");
        require(document_&&document_->sourcePath==droppedFile,"Dropped STP was not imported through the regular file loader");
        require(windowTitle().contains(productName)&&windowTitle().contains(QFileInfo(droppedFile).fileName()),"Imported model title lost the product identity");
        require(dragStep(tree_->viewport(),{QUrl::fromLocalFile(input)}),"Dropping STEP into the project tree failed");
        require(dragStep(parameters_,{QUrl::fromLocalFile(droppedFile)}),"Dropping STP into the parameter panel failed");
        const auto imported=document_;
        require(!dragStep(viewer_,{QUrl("https://example.com/model.stp")}),"Remote drag must not be accepted as a local STEP");
        require(!dragStep(viewer_,{QUrl::fromLocalFile(input),QUrl::fromLocalFile(droppedFile)}),"Multiple dropped models should not replace each other silently");
        require(!dragStep(viewer_,{QUrl::fromLocalFile(smokeDirectory_)}),"Dropped directories must be rejected");
        const QString textFile=smokeDirectory_+"/unsupported.txt";
        writeText(textFile,"not a CAD file");
        require(!dragStep(viewer_,{QUrl::fromLocalFile(textFile)}),"Unsupported file drag must be rejected");
        require(document_==imported,"Rejected drag changed the current CAD document");
        smokeReport_["stepDragDrop"]=true;
        smokeReport_["invalidDropsRejected"]=true;
        require(findChild<QAction*>("actionShowNormals")->isChecked(),"Surface normals must be visible by default");
        require(findChild<QAction*>("actionShowTcpFrames")->isChecked(),"TCP frames must be visible by default");
        require(findChild<QAction*>("actionShowContacts")->isChecked(),"Machining targets must be visible by default");
        smokeReport_["defaultPathDiagnostics"]=true;
        require(document_->valid && document_->faces.count()==2,"Imported example must contain two real CAD Faces");
        smokeReport_["stepImport"]=true;
        smokeReport_["faceCount"]=document_->faces.count();
        QTimer::singleShot(400,this,[this] {
            try {
                const gp_Pnt first=faceMidpoint(document_->faces.face(1));
                const gp_Pnt second=faceMidpoint(document_->faces.face(2));
                viewer_->setInteractionMode(InteractionMode::SelectFace);
                clickPoint(viewer_,first);
                int screenX=0,screenY=0;
                viewer_->view()->Convert(first.X(),first.Y(),first.Z(),screenX,screenY);
                const auto pickedOwner=Handle(StdSelect_BRepOwner)::DownCast(viewer_->context()->DetectedOwner());
                smokeReport_["firstPickDiagnostics"]=QJsonObject{
                    {"screen",QJsonArray{screenX,screenY}},
                    {"widget",QJsonArray{viewer_->width(),viewer_->height()}},
                    {"devicePixelRatio",viewer_->devicePixelRatioF()},
                    {"pickedCount",viewer_->context()->MainSelector()->NbPicked()},
                    {"ownerHasShape",!pickedOwner.IsNull()&&pickedOwner->HasShape()},
                    {"shapeType",pickedOwner.IsNull()||!pickedOwner->HasShape()?-1:static_cast<int>(pickedOwner->Shape().ShapeType())},
                    {"faceId",pickedOwner.IsNull()||!pickedOwner->HasShape()||pickedOwner->Shape().ShapeType()!=TopAbs_FACE?-1:document_->faces.idFor(TopoDS::Face(pickedOwner->Shape()))}};
                AIS_ListOfInteractive displayed;
                viewer_->context()->DisplayedObjects(displayed);
                QJsonArray objects;
                for(AIS_ListOfInteractive::Iterator item(displayed);item.More();item.Next()) {
                    TColStd_ListOfInteger modes;
                    viewer_->context()->ActivatedModes(item.Value(),modes);
                    QJsonArray activeModes;
                    for(TColStd_ListOfInteger::Iterator mode(modes);mode.More();mode.Next())activeModes.append(mode.Value());
                    objects.append(QJsonObject{{"type",item.Value()->DynamicType()->Name()},
                        {"modes",activeModes},
                        {"state",item.Value()->HasSelection(4)?static_cast<int>(item.Value()->Selection(4)->GetSelectionState()):-1},
                        {"faceSensitiveEntities",item.Value()->HasSelection(4)?item.Value()->Selection(4)->Entities().Size():-1}});
                }
                smokeReport_["displayedObjects"]=objects;
                if(selection_->selectedFaceIds()!=std::vector<int>{1}) {
                    int nativeWidth=0,nativeHeight=0;
                    viewer_->view()->Window()->Size(nativeWidth,nativeHeight);
                    smokeReport_["nativeWindow"]=QJsonArray{nativeWidth,nativeHeight};
                    QJsonArray hits;
                    for(int py=40;py<nativeHeight;py+=80)for(int px=40;px<nativeWidth;px+=80) {
                        viewer_->context()->MoveTo(px,py,viewer_->view(),false);
                        if(viewer_->context()->MainSelector()->NbPicked()>0&&hits.size()<12)hits.append(QJsonArray{px,py});
                    }
                    smokeReport_["gridHits"]=hits;
                    viewer_->context()->MainSelector()->Pick(gp_Ax1(first.Translated(gp_Vec(0,0,10)),gp_Dir(0,0,-1)),viewer_->view());
                    smokeReport_["axisPickCount"]=viewer_->context()->MainSelector()->NbPicked();
                    capturePreview(smokeDirectory_+"/imported-model.png");
                }
                require(selection_->selectedFaceIds()==std::vector<int>{1},"Native mouse selection did not return Face 1");
                clickPoint(viewer_,second,Qt::ControlModifier);
                require(selection_->selectedFaceIds().size()==2,"Ctrl mouse click did not select two CAD Faces");
                clickPoint(viewer_,first,Qt::ControlModifier);
                require(selection_->selectedFaceIds()==std::vector<int>{2},"Ctrl click did not remove the picked Face");
                clickPoint(viewer_,first,Qt::ControlModifier);
                require(selection_->selectedFaceIds().size()==2,"Ctrl click did not re-add the picked Face");
                smokeReport_["mouseFaceSelection"]=true;
                smokeReport_["ctrlMultiSelection"]=true;
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
                QApplication::sendEvent(viewer_,&escape);
                require(selection_->selectedFaceIds().empty(),"Esc did not clear selection");
                undo_->undo();
                require(selection_->selectedFaceIds().size()==2,"Undo did not restore Face selection");
                smokeReport_["escapeAndSelectionUndo"]=true;
                bool edgePicked=false;
                const auto edgeConnection=connect(viewer_,&OcctViewWidget::edgePicked,this,[&](const TopoDS_Edge&){edgePicked=true;});
                for(TopExp_Explorer edge(document_->faces.face(1),TopAbs_EDGE);edge.More()&&!edgePicked;edge.Next()) {
                    const auto curveEdge=TopoDS::Edge(edge.Current());
                    if(BRep_Tool::Degenerated(curveEdge))continue;
                    BRepAdaptor_Curve curve(curveEdge);
                    viewer_->setInteractionMode(InteractionMode::SelectEdge);
                    clickPoint(viewer_,curve.Value((curve.FirstParameter()+curve.LastParameter())/2));
                }
                disconnect(edgeConnection);
                require(edgePicked,"Native CAD Edge picking failed");
                require(parameters_->parameters().direction.Magnitude()>0,"Edge direction was not transferred to parameters");
                smokeReport_["cadEdgeDirection"]=true;
                bool pointsPicked=false;
                const auto pointConnection=connect(viewer_,&OcctViewWidget::directionPointsPicked,this,[&](const gp_Pnt& a,const gp_Pnt& b){pointsPicked=a.Distance(b)>1;});
                viewer_->setInteractionMode(InteractionMode::PickDirectionPoint1);
                clickPoint(viewer_,first);clickPoint(viewer_,second);
                disconnect(pointConnection);
                require(pointsPicked,"Two picked points were not returned on the CAD surface");
                smokeReport_["twoPointDirection"]=true;
                selection_->set({1});selection_->selectTangentFaces(5);
                require(selection_->selectedFaceIds().size()==2,"Tangent Face selection failed across a shared CAD Edge");
                smokeReport_["tangentFaces"]=true;
                PathParameters parameters;
                parameters.machiningAngleDeg=30;parameters.toolAlongNormal=false;
                parameters_->setParameters(parameters);
                invalidatePreview();
                auto* heartbeat=new QTimer(this);
                heartbeat->setInterval(20);
                connect(heartbeat,&QTimer::timeout,this,[this]{if(generation_->busy())smokeReport_["uiHeartbeats"]=smokeReport_["uiHeartbeats"].toInt()+1;});
                heartbeat->start();
                capturePreview(smokeDirectory_+"/face-selection.png");
                requestPreview();
                require(generation_->busy(),"Drop-while-busy test needs an active path computation");
                const auto activeDocument=document_;
                require(!dragStep(viewer_,{QUrl::fromLocalFile(smokeDirectory_+"/adjacent_bspline.step")}),"File drop must not replace geometry during path computation");
                require(document_==activeDocument,"Busy drop changed the worker's CAD document");
                smokeReport_["busyDropsRejected"]=true;
            } catch(const std::exception& error) {smokeFailure(QString::fromUtf8(error.what()));}
        });
#else
        throw std::runtime_error("The desktop smoke test requires example model support");
#endif
    } catch(const std::exception& error) {smokeFailure(QString::fromUtf8(error.what()));}
}
void MainWindow::finishSmokeTest() {
    try {
        require(preview_.has_value() && preview_->pointCount()>100,"No sampled preview path was produced");
        const SurfacePath path=*preview_;
        require(path.sectionOffsets.size()>2,"Parallel section plane generation failed");
        double maxError=0;
        std::set<int> faces;
        for(const auto& segment:path.segments)for(const auto& point:segment.points) {
            if(segment.type!=MotionType::Process)continue;
            SurfaceEvaluation evaluation;
            require(evaluateSurfacePoint(document_->faces.face(point.faceId),point.surfacePoint,GeometryTolerance{},evaluation),"Process contact lies outside its trimmed B-Rep Face");
            maxError=std::max(maxError,point.surfacePoint.Distance(evaluation.point));
            require(std::abs(point.toolX.Dot(point.toolY))<1e-8 && std::abs(point.toolZ.Dot(point.toolY))<1e-8,"TCP frame is not orthonormal");
            require(point.toolX.Crossed(point.toolY).Dot(point.toolZ)>0.999999,"TCP frame is not right handed");
            require(point.toolZ.Dot(point.surfaceNormal)<-0.99999,"Against Surface Normal did not orient Tool Z");
            faces.insert(point.faceId);
        }
        require(faces.size()==2,"Path did not retain both imported Face IDs");
        require(smokeReport_["uiHeartbeats"].toInt()>0,"GUI event loop did not remain responsive");
        smokeReport_["surfacePath"]=true;
        smokeReport_["pointCount"]=static_cast<int>(path.pointCount());
        smokeReport_["segmentCount"]=static_cast<int>(path.segments.size());
        smokeReport_["maxSurfaceErrorMm"]=maxError;
        smokeReport_["tcpFrames"]=true;
        capturePreview(smokeDirectory_+"/surface-path-preview.png");
        findChild<QAction*>("actionGeneratePath")->trigger();
        require(paths_.size()==1 && !preview_,"Generate did not save preview into the project");
        AIS_ListOfInteractive generatedObjects;
        viewer_->context()->DisplayedObjects(generatedObjects);
        int generatedDiagnostics=0;
        for(AIS_ListOfInteractive::Iterator item(generatedObjects);item.More();item.Next())
            if(Handle(AIS_Shape)::DownCast(item.Value()).IsNull())++generatedDiagnostics;
        require(generatedDiagnostics>=6,"Generate must display saved machining targets, normals and TCP frames");
        smokeReport_["generatedTrajectoryAndNormals"]=true;
        const int id=activePathId_;
        deleteRecord(id);require(paths_.empty(),"Delete path failed");
        undo_->undo();require(paths_.size()==1,"Undo deleted path failed");
        undo_->redo();require(paths_.empty(),"Redo deleted path failed");
        undo_->undo();require(paths_.size()==1,"Second undo deleted path failed");
        smokeReport_["projectTreeAndUndo"]=true;
        const auto record=paths_.at(id);
        bool foundPathItem=false;
        for(QTreeWidgetItemIterator item(tree_);*item;++item) {
            if((*item)->text(0)==record->name) {
                foundPathItem=true;
                AIS_ListOfInteractive beforeHide;
                viewer_->context()->DisplayedObjects(beforeHide);
                (*item)->setCheckState(0,Qt::Unchecked);
                require(!record->visible,"Tree Hide did not update saved path visibility");
                AIS_ListOfInteractive afterHide;
                viewer_->context()->DisplayedObjects(afterHide);
                require(beforeHide.Extent()-afterHide.Extent()==7,"Hiding a saved path must erase its Process/Transfer, contacts, normals and TCP axes together");
                (*item)->setCheckState(0,Qt::Checked);
                require(record->visible,"Tree Show did not update saved path visibility");
                AIS_ListOfInteractive afterShow;
                viewer_->context()->DisplayedObjects(afterShow);
                require(afterShow.Extent()==beforeHide.Extent(),"Showing a saved path must restore its trajectory and diagnostics");
                tree_->setCurrentItem(*item);
                break;
            }
        }
        require(foundPathItem,"Generated path item is missing from the project tree");
        smokeReport_["pathVisibility"]=true;
        std::ostringstream full,simple;
        CsvExporter(false).write(full,record->path);CsvExporter(true).write(simple,record->path);
        writeText(smokeDirectory_+"/surface-path.csv",full.str());
        writeText(smokeDirectory_+"/surface-path-xyz-normal.csv",simple.str());
        require(full.str().find("face_id,layer_id,motion_type")!=std::string::npos,"Full CSV has no topology/motion metadata");
        require(full.str().find("\n\n")==std::string::npos&&simple.str().find("\n\n")==std::string::npos,"CSV must not contain blank separator rows");
        require(full.str().find(",Transfer,")!=std::string::npos,"CSV must retain links between machining rows as Transfer moves");
        smokeReport_["continuousCsv"]=true;
        smokeReport_["csvExport"]=true;
        capturePreview(smokeDirectory_+"/surface-path-generated.png");
        const auto previousDocument=document_;
        require(dragStep(viewer_,{QUrl::fromLocalFile(smokeDirectory_+QStringLiteral("/拖入曲面.STP"))}),"Dropping the next STEP after generation failed");
        require(document_!=previousDocument&&paths_.empty()&&selection_->selectedFaceIds().empty(),"A newly dropped model must clear the previous machining project");
        smokeReport_["dropReplacesProject"]=true;
        smokeReport_["passed"]=true;
        writeText(smokeDirectory_+"/desktop-smoke.json",QJsonDocument(smokeReport_).toJson().toStdString());
        smokeDirectory_.clear();
        QApplication::exit(0);
    } catch(const std::exception& error) {smokeFailure(QString::fromUtf8(error.what()));}
}
void MainWindow::smokeFailure(const QString& error) {
    smokeReport_["passed"]=false;smokeReport_["error"]=error;
    QFile file(smokeDirectory_+"/desktop-smoke.json");
    if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(smokeReport_).toJson());
    smokeDirectory_.clear();
    QApplication::exit(1);
}
} // namespace sps
