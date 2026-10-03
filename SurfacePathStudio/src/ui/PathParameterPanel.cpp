#include "PathParameterPanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>

namespace sps {
QDoubleSpinBox* PathParameterPanel::number(const QString& name, double value,
                                          double minimum, double maximum, const QString& suffix) {
    auto* input = new QDoubleSpinBox(this);
    input->setObjectName(name);
    input->setDecimals(4);
    input->setRange(minimum, maximum);
    input->setValue(value);
    input->setSuffix(suffix);
    input->setKeyboardTracking(false);
    numbers_[name] = input;
    connect(input, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PathParameterPanel::parametersChanged);
    return input;
}
PathParameterPanel::PathParameterPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    selection_ = new QLabel(tr("尚未选择曲面"), this);
    selection_->setWordWrap(true);
    outer->addWidget(selection_);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    fields_ = new QWidget(scroll);
    auto* fieldsLayout = new QVBoxLayout(fields_);
    auto group = [&](const QString& title) {
        auto* box = new QGroupBox(title, fields_);
        auto* form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fieldsLayout->addWidget(box);
        return form;
    };
    auto combo = [&](const QStringList& choices) {
        auto* input = new QComboBox(this);
        input->addItems(choices);
        connect(input, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PathParameterPanel::parametersChanged);
        return input;
    };
    auto* directionForm = group(tr("策略与方向"));
    directionForm->addRow(tr("策略"), new QLabel(tr("平行 B-Rep 截面")));
    direction_ = combo({"Global X", "Global Y", "Global Z", tr("所选 CAD Edge"), tr("拾取两点"), tr("自定义向量")});
    direction_->setObjectName("directionMode");
    directionForm->addRow(tr("路径方向"), direction_);
    auto* picks = new QWidget(this);
    auto* picksLayout = new QHBoxLayout(picks);
    picksLayout->setContentsMargins(0,0,0,0);
    auto* edge = new QPushButton(tr("选择 Edge"), picks);
    auto* points = new QPushButton(tr("拾取两点"), picks);
    picksLayout->addWidget(edge);
    picksLayout->addWidget(points);
    connect(edge, &QPushButton::clicked, this, &PathParameterPanel::pickEdgeRequested);
    connect(points, &QPushButton::clicked, this, &PathParameterPanel::pickPointsRequested);
    directionForm->addRow(picks);
    directionForm->addRow(tr("向量 X"), number("directionX", 1, -1e6, 1e6));
    directionForm->addRow(tr("向量 Y"), number("directionY", 0, -1e6, 1e6));
    directionForm->addRow(tr("向量 Z"), number("directionZ", 0, -1e6, 1e6));
    directionForm->addRow(tr("加工角度"), number("machiningAngle", 0, -360, 360, "°"));
    manualNormal_ = new QCheckBox(tr("自定义参考法向"), this);
    directionForm->addRow(manualNormal_);
    connect(manualNormal_, &QCheckBox::toggled, this, &PathParameterPanel::parametersChanged);
    directionForm->addRow(tr("参考法向 X"), number("normalX", 0, -1e6, 1e6));
    directionForm->addRow(tr("参考法向 Y"), number("normalY", 0, -1e6, 1e6));
    directionForm->addRow(tr("参考法向 Z"), number("normalZ", 1, -1e6, 1e6));
    auto* pathForm = group(tr("路径参数 · mm"));
    pathForm->addRow(tr("截面间距"), number("stepOver", 10, 0.001, 1e6, " mm"));
    pathForm->addRow(tr("采样点间距"), number("pointDistance", 2, 0.001, 1e6, " mm"));
    pathForm->addRow(tr("边界余量"), number("boundaryMargin", 0, 0, 1e5, " mm"));
    pattern_ = combo({tr("Zig-Zag 蛇形"), tr("One-Way 单向")});
    pathForm->addRow(tr("排列方式"), pattern_);
    start_ = combo({"Min B", "Max B"});
    pathForm->addRow(tr("起始侧"), start_);
    first_ = combo({"Positive T", "Negative T"});
    pathForm->addRow(tr("首行方向"), first_);
    auto* poseForm = group(tr("TCP 姿态"));
    normal_ = combo({tr("沿曲面法向"), tr("反向曲面法向")});
    poseForm->addRow(tr("Tool Z"), normal_);
    poseForm->addRow(tr("Lead · 局部 Y"), number("leadAngle", 0, -85, 85, "°"));
    poseForm->addRow(tr("Tilt · 局部 X"), number("tiltAngle", 0, -85, 85, "°"));
    poseForm->addRow(tr("Z Offset"), number("zOffset", 0, -1e5, 1e5, " mm"));
    auto* accuracyForm = group(tr("精度与延长"));
    accuracyForm->addRow(tr("弦误差"), number("chordTolerance", 0.1, 0.0001, 100, " mm"));
    accuracyForm->addRow(tr("法向角度误差"), number("normalAngleTolerance", 3, 0.01, 90, "°"));
    extension_ = combo({tr("None 无延长"), tr("Tangent 切向延长"), tr("曲面约束延长 · 未实现")});
    if (auto* model = qobject_cast<QStandardItemModel*>(extension_->model())) model->item(2)->setEnabled(false);
    accuracyForm->addRow(tr("延长方式"), extension_);
    accuracyForm->addRow(tr("延长距离"), number("extensionDistance", 0, 0, 1e5, " mm"));
    fieldsLayout->addStretch();
    scroll->setWidget(fields_);
    outer->addWidget(scroll, 1);
    summary_ = new QLabel(tr("设置参数后点击预览。"), this);
    summary_->setWordWrap(true);
    outer->addWidget(summary_);
    auto* buttons = new QHBoxLayout;
    preview_ = new QPushButton(tr("预览"), this);
    generate_ = new QPushButton(tr("生成"), this);
    cancel_ = new QPushButton(tr("取消"), this);
    preview_->setObjectName("previewButton");
    generate_->setObjectName("generateButton");
    cancel_->setObjectName("cancelButton");
    buttons->addWidget(preview_);
    buttons->addWidget(generate_);
    buttons->addWidget(cancel_);
    cancel_->setEnabled(false);
    outer->addLayout(buttons);
    connect(preview_, &QPushButton::clicked, this, &PathParameterPanel::previewRequested);
    connect(generate_, &QPushButton::clicked, this, &PathParameterPanel::generateRequested);
    connect(cancel_, &QPushButton::clicked, this, &PathParameterPanel::cancelRequested);
    setMinimumWidth(300);
}
PathParameters PathParameterPanel::parameters() const {
    const auto value = [&](const char* key) { return numbers_.at(QString::fromLatin1(key))->value(); };
    PathParameters p;
    switch (direction_->currentIndex()) {
    case 0: p.direction = gp_Vec(1,0,0); break;
    case 1: p.direction = gp_Vec(0,1,0); break;
    case 2: p.direction = gp_Vec(0,0,1); break;
    case 3:
        if (!edgeDirection_) throw std::runtime_error("Please select a CAD Edge to define path direction");
        p.direction = *edgeDirection_; break;
    case 4:
        if (!pickedDirection_) throw std::runtime_error("Please pick two surface points to define path direction");
        p.direction = *pickedDirection_; break;
    default: p.direction = gp_Vec(value("directionX"), value("directionY"), value("directionZ")); break;
    }
    if (manualNormal_->isChecked()) {
        gp_Vec normal(value("normalX"), value("normalY"), value("normalZ"));
        if (normal.SquareMagnitude() <= p.tolerance.angular * p.tolerance.angular) throw std::runtime_error("Invalid reference normal");
        p.referenceNormal = gp_Dir(normal);
    }
    p.machiningAngleDeg = value("machiningAngle");
    p.stepOver = value("stepOver");
    p.pointDistance = value("pointDistance");
    p.boundaryMargin = value("boundaryMargin");
    p.toolAlongNormal = normal_->currentIndex() == 0;
    p.leadAngleDeg = value("leadAngle");
    p.tiltAngleDeg = value("tiltAngle");
    p.zOffset = value("zOffset");
    p.startFromMaxB = start_->currentIndex() == 1;
    p.firstRowNegative = first_->currentIndex() == 1;
    p.zigZag = pattern_->currentIndex() == 0;
    p.extensionMode = extension_->currentIndex() == 1 ? ExtensionMode::Tangent : ExtensionMode::None;
    p.extensionDistance = value("extensionDistance");
    p.chordTolerance = value("chordTolerance");
    p.normalAngleToleranceDeg = value("normalAngleTolerance");
    return p;
}
void PathParameterPanel::setEdgeDirection(const gp_Vec& direction) {
    edgeDirection_ = direction;
    direction_->setCurrentIndex(3);
    emit parametersChanged();
}
void PathParameterPanel::resetDirectionSources() {
    edgeDirection_.reset();
    pickedDirection_.reset();
    direction_->setCurrentIndex(0);
}
void PathParameterPanel::setParameters(const PathParameters& p) {
    QSignalBlocker blocker(this);
    const auto set = [&](const char* key,double value) {numbers_.at(QString::fromLatin1(key))->setValue(value);};
    direction_->setCurrentIndex(5);
    set("directionX",p.direction.X()); set("directionY",p.direction.Y()); set("directionZ",p.direction.Z());
    manualNormal_->setChecked(p.referenceNormal.has_value());
    if(p.referenceNormal) {set("normalX",p.referenceNormal->X());set("normalY",p.referenceNormal->Y());set("normalZ",p.referenceNormal->Z());}
    set("machiningAngle",p.machiningAngleDeg); set("stepOver",p.stepOver); set("pointDistance",p.pointDistance);
    set("boundaryMargin",p.boundaryMargin); set("leadAngle",p.leadAngleDeg); set("tiltAngle",p.tiltAngleDeg);
    set("zOffset",p.zOffset); set("chordTolerance",p.chordTolerance); set("normalAngleTolerance",p.normalAngleToleranceDeg);
    set("extensionDistance",p.extensionDistance);
    normal_->setCurrentIndex(p.toolAlongNormal?0:1); pattern_->setCurrentIndex(p.zigZag?0:1);
    start_->setCurrentIndex(p.startFromMaxB?1:0); first_->setCurrentIndex(p.firstRowNegative?1:0);
    extension_->setCurrentIndex(p.extensionMode==ExtensionMode::Tangent?1:0);
}
void PathParameterPanel::setPickedDirection(const gp_Vec& direction) {
    pickedDirection_ = direction;
    direction_->setCurrentIndex(4);
    emit parametersChanged();
}
void PathParameterPanel::setBusy(bool busy) {
    fields_->setEnabled(!busy);
    preview_->setEnabled(!busy);
    generate_->setEnabled(!busy);
    cancel_->setEnabled(busy);
}
void PathParameterPanel::setSelectionInfo(int count, double area) {
    selection_->setText(tr("已选 %1 个 CAD Face\n总面积 %2 mm²").arg(count).arg(area,0,'f',2));
}
void PathParameterPanel::setSummary(const QString& text) { summary_->setText(text); }
} // namespace sps
