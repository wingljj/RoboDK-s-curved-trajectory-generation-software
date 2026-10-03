#include "PathGenerationController.h"
#include "path/SectionPathPlanner.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QMetaObject>
#include <Standard_Failure.hxx>
#include <stdexcept>

namespace sps {
PathGenerationController::PathGenerationController(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<GenerationResult>::finished, this, [this] {
        emit busyChanged(false);
        auto result = watcher_.result();
        if (!result.error.isEmpty()) emit failed(result.error);
        else emit pathReady(result.path);
    });
}
PathGenerationController::~PathGenerationController() { cancel(); watcher_.waitForFinished(); }
void PathGenerationController::cancel() { if(cancelled_) cancelled_->store(true); }
void PathGenerationController::start(SelectedSurfaceRegion region, PathParameters parameters) {
    if (busy()) throw std::runtime_error("A path generation job is already running");
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = cancelled_;
    emit busyChanged(true);
    watcher_.setFuture(QtConcurrent::run([this, region=std::move(region), parameters, cancellation] {
        GenerationResult result;
        try {
            result.path = SectionPathPlanner().generate(region, parameters,
                [cancellation] { return cancellation->load(); },
                [this](int percentage) {
                    QMetaObject::invokeMethod(this, [this,percentage] { emit progressChanged(percentage); }, Qt::QueuedConnection);
                });
        } catch (const Standard_Failure& error) {
            result.error=QString::fromUtf8(error.GetMessageString());
        } catch (const std::exception& error) {
            result.error=QString::fromUtf8(error.what());
        } catch (...) { result.error=QStringLiteral("Unexpected geometry error"); }
        return result;
    }));
}
} // namespace sps
