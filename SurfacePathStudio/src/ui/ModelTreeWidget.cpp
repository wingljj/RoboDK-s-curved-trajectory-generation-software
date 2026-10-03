#include "ModelTreeWidget.h"
#include "cad/CadDocument.h"
#include <QMenu>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QVariant>

namespace sps {
namespace {
enum ItemKind { Folder, Cad, CurrentRegion, SavedRegion, Face, Path };
constexpr int KindRole = Qt::UserRole;
constexpr int IdRole = Qt::UserRole + 1;
constexpr int FacesRole = Qt::UserRole + 2;
QVariantList faceList(const std::vector<int>& ids) {
    QVariantList values;
    for (int id : ids) values.push_back(id);
    return values;
}
std::vector<int> idsFrom(const QVariant& value) {
    std::vector<int> ids;
    for (const auto& item : value.toList()) ids.push_back(item.toInt());
    return ids;
}
void populateNode(QTreeWidgetItem* parent, const CadNode& node) {
    auto* item = new QTreeWidgetItem(parent, {node.name});
    item->setData(0,KindRole,Folder);
    for (const auto& child : node.children) populateNode(item,child);
}
}
ModelTreeWidget::ModelTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setObjectName("projectTree");
    setMinimumWidth(210);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setDocument({});
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const int kind = item->data(0,KindRole).toInt();
        if (kind == Path) emit pathSelected(item->data(0,IdRole).toInt());
        else if (kind == Face) emit regionSelected({item->data(0,IdRole).toInt()});
        else if (kind == CurrentRegion) emit regionSelected(selectionIds_);
        else if (kind == SavedRegion) emit regionSelected(idsFrom(item->data(0,FacesRole)));
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        const int kind = item->data(0,KindRole).toInt();
        if (kind == Path) emit pathVisibilityChanged(item->data(0,IdRole).toInt(), item->checkState(0)==Qt::Checked);
        if (kind == Cad) emit cadVisibilityChanged(item->checkState(0)==Qt::Checked);
    });
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = itemAt(position);
        if (!item || item->data(0,KindRole).toInt()!=Path) return;
        const int id = item->data(0,IdRole).toInt();
        QMenu menu(this);
        menu.addAction(tr("重命名"), this, [this,id]{emit renamePathRequested(id);});
        menu.addAction(tr("重新生成"), this, [this,id]{emit regeneratePathRequested(id);});
        menu.addAction(item->checkState(0)==Qt::Checked ? tr("隐藏") : tr("显示"), this, [item]{
            item->setCheckState(0,item->checkState(0)==Qt::Checked ? Qt::Unchecked : Qt::Checked);
        });
        menu.addSeparator();
        menu.addAction(tr("删除"), this, [this,id]{emit deletePathRequested(id);});
        menu.exec(viewport()->mapToGlobal(position));
    });
}
void ModelTreeWidget::setDocument(const std::shared_ptr<CadDocument>& document) {
    QSignalBlocker blocker(this);
    clear();
    selectionIds_.clear();
    auto* project = new QTreeWidgetItem(this,{tr("项目")});
    cad_ = new QTreeWidgetItem(project,{"CAD"});
    cad_->setData(0,KindRole,Cad);
    cad_->setCheckState(0,Qt::Checked);
    if (document) {
        auto* file = new QTreeWidgetItem(cad_,{QFileInfo(document->sourcePath).fileName()});
        for (const auto& root : document->roots) populateNode(file,root);
        auto* faces = new QTreeWidgetItem(file,{tr("CAD Faces (%1)").arg(document->faces.count())});
        for(int id=1; id<=document->faces.count(); ++id) {
            const auto& metadata = document->faces.metadata(id);
            auto* item = new QTreeWidgetItem(faces,{tr("Face %1 · %2").arg(id).arg(QString::fromStdString(metadata.type))});
            item->setData(0,KindRole,Face);
            item->setData(0,IdRole,id);
            item->setToolTip(0,tr("面积: %1 mm²").arg(metadata.area,0,'f',2));
        }
    }
    selections_ = new QTreeWidgetItem(project,{tr("曲面区域")});
    currentSelection_ = new QTreeWidgetItem(selections_,{tr("当前加工区域 (0)")});
    currentSelection_->setData(0,KindRole,CurrentRegion);
    paths_ = new QTreeWidgetItem(project,{tr("路径")});
    project->setExpanded(true);
    cad_->setExpanded(true);
    selections_->setExpanded(true);
    paths_->setExpanded(true);
}
void ModelTreeWidget::setCurrentSelection(const std::vector<int>& ids) {
    QSignalBlocker blocker(this);
    selectionIds_=ids;
    currentSelection_->setText(0,tr("当前加工区域 (%1)").arg(ids.size()));
    while(currentSelection_->childCount()) delete currentSelection_->takeChild(0);
    for(int id:ids) {
        auto* item=new QTreeWidgetItem(currentSelection_,{QString("Face %1").arg(id)});
        item->setData(0,KindRole,Face);
        item->setData(0,IdRole,id);
    }
    currentSelection_->setExpanded(true);
}
void ModelTreeWidget::addPath(int id,const QString& name,const std::vector<int>& faceIds) {
    QSignalBlocker blocker(this);
    auto* item = new QTreeWidgetItem(paths_,{name});
    item->setData(0,KindRole,Path);
    item->setData(0,IdRole,id);
    item->setCheckState(0,Qt::Checked);
    auto* region = new QTreeWidgetItem(selections_,{QString("SurfaceRegion_%1").arg(id)});
    region->setData(0,KindRole,SavedRegion);
    region->setData(0,IdRole,id);
    region->setData(0,FacesRole,faceList(faceIds));
    for(int faceId:faceIds) {
        auto* face=new QTreeWidgetItem(region,{QString("Face %1").arg(faceId)});
        face->setData(0,KindRole,Face);
        face->setData(0,IdRole,faceId);
    }
    paths_->setExpanded(true);
    setCurrentItem(item);
}
void ModelTreeWidget::removePath(int id) {
    QSignalBlocker blocker(this);
    for(auto* parent:{paths_,selections_}) {
        for(int i=parent->childCount()-1;i>=0;--i) {
            auto* item=parent->child(i);
            if(item->data(0,IdRole).isValid() && item->data(0,IdRole).toInt()==id) delete parent->takeChild(i);
        }
    }
}
void ModelTreeWidget::renamePath(int id,const QString& name) {
    QSignalBlocker blocker(this);
    for(int i=0;i<paths_->childCount();++i) if(paths_->child(i)->data(0,IdRole).toInt()==id) paths_->child(i)->setText(0,name);
}
void ModelTreeWidget::setPathVisible(int id,bool visible) {
    QSignalBlocker blocker(this);
    for(int i=0;i<paths_->childCount();++i) if(paths_->child(i)->data(0,IdRole).toInt()==id) paths_->child(i)->setCheckState(0,visible?Qt::Checked:Qt::Unchecked);
}
} // namespace sps
