#pragma once
#include "path/PathTypes.h"
#include <QWidget>
#include <map>

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
namespace sps {
class PathParameterPanel : public QWidget {
    Q_OBJECT
public:
    explicit PathParameterPanel(QWidget* parent = nullptr);
    PathParameters parameters() const;
    void setParameters(const PathParameters& parameters);
    void resetDirectionSources();
    void setEdgeDirection(const gp_Vec& direction);
    void setPickedDirection(const gp_Vec& direction);
    void setBusy(bool busy);
    void setSelectionInfo(int count, double area);
    void setSummary(const QString& summary);
signals:
    void previewRequested();
    void generateRequested();
    void cancelRequested();
    void pickEdgeRequested();
    void pickPointsRequested();
    void parametersChanged();
private:
    QDoubleSpinBox* number(const QString& name, double value, double minimum,
                           double maximum, const QString& suffix = {});
    std::map<QString, QDoubleSpinBox*> numbers_;
    QComboBox* direction_ = nullptr;
    QComboBox* normal_ = nullptr;
    QComboBox* pattern_ = nullptr;
    QComboBox* start_ = nullptr;
    QComboBox* first_ = nullptr;
    QComboBox* extension_ = nullptr;
    QCheckBox* manualNormal_ = nullptr;
    QPushButton* preview_ = nullptr;
    QPushButton* generate_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QLabel* selection_ = nullptr;
    QLabel* summary_ = nullptr;
    QWidget* fields_ = nullptr;
    std::optional<gp_Vec> edgeDirection_;
    std::optional<gp_Vec> pickedDirection_;
};
} // namespace sps
