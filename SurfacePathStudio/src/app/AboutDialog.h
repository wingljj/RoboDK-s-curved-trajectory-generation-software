#pragma once
#include <QDialog>

namespace sps {
class AboutDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
} // namespace sps
