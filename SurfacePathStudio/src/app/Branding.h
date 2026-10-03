#pragma once
#include <QString>

namespace sps::branding {
inline QString productName() {
    return QStringLiteral("上海卫星装备研究所曲面轨迹生成软件");
}
inline QString organizationName() {
    return QStringLiteral("上海卫星装备研究所");
}
inline QString departmentName() {
    return QStringLiteral("总装中心");
}
inline QString logoPath() {
    return QStringLiteral(":/branding/casc-logo.jpg");
}
inline QString iconPath() {
    return QStringLiteral(":/branding/casc.ico");
}
} // namespace sps::branding
