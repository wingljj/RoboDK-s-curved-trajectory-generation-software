#pragma once
#include "path/PathTypes.h"
#include <QObject>
#include <QFutureWatcher>
#include <QString>
#include <atomic>
#include <memory>

namespace sps {
struct GenerationResult {
    SurfacePath path;
    QString error;
};
class PathGenerationController : public QObject {
    Q_OBJECT
public:
    explicit PathGenerationController(QObject* parent = nullptr);
    ~PathGenerationController() override;
    bool busy() const { return watcher_.isRunning(); }
    void start(SelectedSurfaceRegion region, PathParameters parameters);
    void cancel();
signals:
    void pathReady(const sps::SurfacePath& path);
    void failed(const QString& error);
    void progressChanged(int percent);
    void busyChanged(bool busy);
private:
    QFutureWatcher<GenerationResult> watcher_;
    std::shared_ptr<std::atomic_bool> cancelled_;
};
} // namespace sps
