#pragma once
#include <QObject>
#include <memory>
#include <vector>

namespace sps {
struct CadDocument;
class SelectionManager : public QObject {
    Q_OBJECT
public:
    explicit SelectionManager(QObject* parent = nullptr);
    void setDocument(std::shared_ptr<CadDocument> document);
    const std::vector<int>& selectedFaceIds() const { return m_ids; }
    void set(const std::vector<int>& ids);
    void toggle(int id, bool additive = true);
    void clear();
    void selectTangentFaces(double toleranceDegrees = 5.0);
    double totalArea() const;
signals:
    void changed();
private:
    std::shared_ptr<CadDocument> m_document;
    std::vector<int> m_ids;
};
} // namespace sps
