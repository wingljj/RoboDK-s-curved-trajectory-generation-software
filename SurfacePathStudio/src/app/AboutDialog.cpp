#include "AboutDialog.h"
#include "Branding.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVariant>
#include <QStringList>
#include <QVBoxLayout>

namespace sps {
namespace {
QLabel* sectionHeading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "sectionHeading");
    QFont font = label->font();
    font.setPointSizeF(11.0);
    font.setBold(true);
    label->setFont(font);
    return label;
}
} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(branding::productName() + tr(" — 关于"));
    setWindowIcon(QIcon(branding::iconPath()));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumWidth(680);
    resize(740, 610);
    setStyleSheet(QStringLiteral(
        "QDialog#aboutDialog { background: #f5f8fc; color: #18324d; }"
        "QFrame#aboutHeader { background: #103b67; border-radius: 12px; }"
        "QLabel#aboutLogo { background: white; border-radius: 9px; }"
        "QLabel#aboutProductName { color: white; }"
        "QLabel#aboutSubtitle { color: #d4e5f5; }"
        "QLabel[role=sectionHeading] { color: #103b67; }"
        "QLabel#aboutDescription { color: #465c72; }"
        "QFrame#aboutFeatures { background: #e9f0f8; border-radius: 8px; }"
        "QLabel[role=feature] { color: #244969; }"
        "QLabel[role=metadataCaption] { color: #66798b; }"
        "QLabel[role=metadataValue] { color: #183d61; }"
        "QFrame#aboutDivider { color: #d9e3ee; }"
        "QPushButton#aboutCloseButton { background: #155a96; color: white;"
        " border: 1px solid #155a96; border-radius: 6px; min-width: 84px; padding: 8px 18px; }"
        "QPushButton#aboutCloseButton:hover { background: #1d6bad; }"
        "QPushButton#aboutCloseButton:pressed { background: #103b67; }"
        "QPushButton#aboutCloseButton:focus { border: 2px solid #6ca9dd; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(18);

    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("aboutHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 20, 24, 20);
    headerLayout->setSpacing(22);

    auto* logo = new QLabel(header);
    logo->setObjectName(QStringLiteral("aboutLogo"));
    logo->setAccessibleName(tr("中国航天标识"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(136, 136);
    const QPixmap originalLogo(branding::logoPath());
    if (!originalLogo.isNull()) {
        const qreal scale = devicePixelRatioF();
        auto displayedLogo = originalLogo.scaled(
            QSize(qRound(120 * scale), qRound(120 * scale)),
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        displayedLogo.setDevicePixelRatio(scale);
        logo->setPixmap(displayedLogo);
    } else {
        logo->setText(tr("中国航天"));
    }
    headerLayout->addWidget(logo);

    auto* headingLayout = new QVBoxLayout;
    headingLayout->setSpacing(12);
    headingLayout->addStretch();
    auto* product = new QLabel(branding::productName(), header);
    product->setObjectName(QStringLiteral("aboutProductName"));
    product->setWordWrap(true);
    QFont productFont = product->font();
    productFont.setPointSizeF(18.0);
    productFont.setBold(true);
    product->setFont(productFont);
    headingLayout->addWidget(product);
    auto* subtitle = new QLabel(tr("曲面轨迹生成与姿态可视化"), header);
    subtitle->setObjectName(QStringLiteral("aboutSubtitle"));
    subtitle->setWordWrap(true);
    headingLayout->addWidget(subtitle);
    headingLayout->addStretch();
    headerLayout->addLayout(headingLayout, 1);
    layout->addWidget(header);

    auto* introduction = new QVBoxLayout;
    introduction->setSpacing(8);
    introduction->addWidget(sectionHeading(tr("软件介绍"), this));
    auto* description = new QLabel(tr(
        "面向曲面工艺的轨迹准备，支持 STEP/STP 模型导入与多 CAD Face 加工区域选择，"
        "基于真实 B-Rep 曲面生成路径，并提供曲面法向、TCP 姿态可视化和连续 CSV 导出。"), this);
    description->setObjectName(QStringLiteral("aboutDescription"));
    description->setWordWrap(true);
    description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    introduction->addWidget(description);
    layout->addLayout(introduction);

    auto* featureSection = new QVBoxLayout;
    featureSection->setSpacing(8);
    featureSection->addWidget(sectionHeading(tr("主要功能"), this));
    auto* features = new QFrame(this);
    features->setObjectName(QStringLiteral("aboutFeatures"));
    auto* featureLayout = new QGridLayout(features);
    featureLayout->setContentsMargins(16, 14, 16, 14);
    featureLayout->setHorizontalSpacing(24);
    featureLayout->setVerticalSpacing(12);
    const QStringList featureTexts{
        tr("STEP/STP 模型导入"), tr("多 CAD Face 选择"),
        tr("真实 B-Rep 曲面路径"), tr("法向与 TCP 姿态可视化"),
        tr("参数调整、预览与生成"), tr("连续 CSV 导出")};
    for (int index = 0; index < featureTexts.size(); ++index) {
        auto* label = new QLabel(QStringLiteral("•  ") + featureTexts[index], features);
        label->setProperty("role", "feature");
        label->setWordWrap(true);
        featureLayout->addWidget(label, index / 2, index % 2);
    }
    featureLayout->setColumnStretch(0, 1);
    featureLayout->setColumnStretch(1, 1);
    featureSection->addWidget(features);
    layout->addLayout(featureSection);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("aboutDivider"));
    divider->setFrameShape(QFrame::HLine);
    layout->addWidget(divider);

    auto* metadata = new QGridLayout;
    metadata->setHorizontalSpacing(20);
    metadata->setVerticalSpacing(8);
    const auto addMetadata = [this, metadata](int row, const QString& caption,
                                              const QString& value, const char* name) {
        auto* captionLabel = new QLabel(caption, this);
        captionLabel->setProperty("role", "metadataCaption");
        auto* valueLabel = new QLabel(value, this);
        valueLabel->setObjectName(QString::fromLatin1(name));
        valueLabel->setProperty("role", "metadataValue");
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        metadata->addWidget(captionLabel, row, 0);
        metadata->addWidget(valueLabel, row, 1);
    };
    addMetadata(0, tr("研发单位"), branding::organizationName(), "aboutOrganization");
    addMetadata(1, tr("研发部门"), branding::departmentName(), "aboutDepartment");
    addMetadata(2, tr("软件版本"), QCoreApplication::applicationVersion(), "aboutVersion");
    metadata->setColumnStretch(1, 1);
    layout->addLayout(metadata);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* close = buttons->button(QDialogButtonBox::Close);
    close->setObjectName(QStringLiteral("aboutCloseButton"));
    close->setText(tr("关闭"));
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
} // namespace sps
