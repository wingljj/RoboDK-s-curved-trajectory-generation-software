#pragma once
#include <QTreeWidget>
#include <memory>
#include <vector>

namespace sps {
struct CadDocument;
class ModelTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit ModelTreeWidget(QWidget* parent = nullptr);
    void setDocument(const std::shared_ptr<CadDocument>& document);
    void setCurrentSelection(const std::vector<int>& ids);
    void addPath(int id, const QString& name, const std::vector<int>& faceIds);
    void removePath(int id);
    void renamePath(int id, const QString& name);
    void setPathVisible(int id, bool visible);
signals:
    void regionSelected(const std::vector<int>& ids);
    void pathSelected(int id);
    void pathVisibilityChanged(int id, bool visible);
    void cadVisibilityChanged(bool visible);
    void deletePathRequested(int id);
    void renamePathRequested(int id);
    void regeneratePathRequested(int id);
private:
    QTreeWidgetItem* cad_ = nullptr;
    QTreeWidgetItem* selections_ = nullptr;
    QTreeWidgetItem* currentSelection_ = nullptr;
    QTreeWidgetItem* paths_ = nullptr;
    std::vector<int> selectionIds_;
};
} // namespace sps
