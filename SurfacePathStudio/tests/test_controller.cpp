#include "app/PathGenerationController.h"
#include "examples/ExampleModels.h"
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void runUntil(const std::function<bool()>& finished, int timeoutMilliseconds = 20000) {
    if (finished()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    bool timedOut = false;
    poll.setInterval(1);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (finished()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
    poll.start();
    deadline.start(timeoutMilliseconds);
    loop.exec();
    require(!timedOut && finished(), "background controller event-loop timeout");
}
sps::SelectedSurfaceRegion selectedRegion(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    sps::SelectedSurfaceRegion result;
    for (int i = 1; i <= faces.Extent(); ++i) result.faces.push_back({i, TopoDS::Face(faces(i))});
    return result;
}
sps::examples::Fixture model(bool heavy) {
    for (auto fixture : sps::examples::fixtures()) {
        if (fixture.fileStem != (heavy ? "bspline_trimmed" : "plane")) continue;
        if (heavy) {
            fixture.parameters.stepOver = 0.75;
            fixture.parameters.pointDistance = 0.75;
        }
        return fixture;
    }
    throw std::runtime_error("controller test fixture is missing");
}
struct Signals {
    std::atomic<int> ready{0}, errors{0}, progress{0}, busyStarts{0}, busyStops{0};
    std::atomic<bool> allOnMainThread{true}, validProgress{true};
    std::atomic<std::size_t> points{0};
    std::mutex errorMutex;
    QString lastError;

    void checkThread() {
        if (QThread::currentThread() != QCoreApplication::instance()->thread()) allOnMainThread.store(false);
    }
};
void observe(sps::PathGenerationController& controller, Signals& observed) {
    // DirectConnection intentionally observes the emitting thread. A queued
    // receiver would hide an incorrect signal emitted by the geometry worker.
    auto* app = QCoreApplication::instance();
    QObject::connect(&controller, &sps::PathGenerationController::pathReady, app,
        [&observed](const sps::SurfacePath& path) {
            observed.checkThread();
            observed.points.store(path.pointCount());
            ++observed.ready;
        }, Qt::DirectConnection);
    QObject::connect(&controller, &sps::PathGenerationController::failed, app,
        [&observed](const QString& error) {
            observed.checkThread();
            { std::lock_guard<std::mutex> lock(observed.errorMutex); observed.lastError = error; }
            ++observed.errors;
        }, Qt::DirectConnection);
    QObject::connect(&controller, &sps::PathGenerationController::progressChanged, app,
        [&observed](int percentage) {
            observed.checkThread();
            if (percentage < 0 || percentage > 100) observed.validProgress.store(false);
            ++observed.progress;
        }, Qt::DirectConnection);
    QObject::connect(&controller, &sps::PathGenerationController::busyChanged, app,
        [&observed](bool busy) {
            observed.checkThread();
            if (busy) ++observed.busyStarts;
            else ++observed.busyStops;
        }, Qt::DirectConnection);
}
void asyncCompletion(bool heavy) {
    Signals observed;
    sps::PathGenerationController controller;
    observe(controller, observed);
    const auto fixture = model(heavy);
    int heartbeatDuringJob = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, QCoreApplication::instance(), [&] {
        if (controller.busy() && observed.ready.load() == 0 && observed.errors.load() == 0) ++heartbeatDuringJob;
    });
    heartbeat.start();
    controller.start(selectedRegion(fixture.shape), fixture.parameters);
    require(observed.ready.load() == 0 && observed.errors.load() == 0,
            "start must return before delivering a completion signal");
    runUntil([&] { return observed.ready.load() + observed.errors.load() > 0; });
    heartbeat.stop();
    require(observed.ready.load() == 1 && observed.errors.load() == 0 && observed.points.load() > 0,
            "background generation must deliver one successful CAD path");
    require(observed.allOnMainThread.load(), "controller signals must be emitted on the application main thread");
    require(observed.progress.load() > 0 && observed.validProgress.load(), "background generation must report valid main-thread progress");
    require(observed.busyStarts.load() == 1 && observed.busyStops.load() == 1 && !controller.busy(),
            "controller must bracket a job with one busy start and finish");
    if (heavy) require(heartbeatDuringJob >= 3, "Qt timers must keep running while B-Rep generation is in progress");
}
void cancellation() {
    Signals observed;
    sps::PathGenerationController controller;
    observe(controller, observed);
    auto fixture = model(true);
    fixture.parameters.stepOver = 0.25;
    fixture.parameters.pointDistance = 0.25;
    bool requested = false;
    QTimer cancelWhenRunning;
    cancelWhenRunning.setInterval(1);
    QObject::connect(&cancelWhenRunning, &QTimer::timeout, QCoreApplication::instance(), [&] {
        if (!requested && controller.busy()) { requested = true; controller.cancel(); cancelWhenRunning.stop(); }
    });
    controller.start(selectedRegion(fixture.shape), fixture.parameters);
    cancelWhenRunning.start();
    runUntil([&] { return observed.ready.load() + observed.errors.load() > 0; });
    require(requested && observed.ready.load() == 0 && observed.errors.load() == 1,
            "a cancelled job must deliver an explicit error rather than a saved path");
    { std::lock_guard<std::mutex> lock(observed.errorMutex);
      require(observed.lastError.contains(QStringLiteral("cancel"), Qt::CaseInsensitive), "cancelled job must report cancellation"); }
    require(observed.allOnMainThread.load() && !controller.busy(), "cancel completion must return to the main thread and clear busy");
}
void busyRejection() {
    Signals observed;
    sps::PathGenerationController controller;
    observe(controller, observed);
    const auto fixture = model(true);
    controller.start(selectedRegion(fixture.shape), fixture.parameters);
    require(controller.busy(), "a new nontrivial generation job must be busy");
    bool rejected = false;
    try { controller.start(selectedRegion(fixture.shape), fixture.parameters); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && observed.busyStarts.load() == 1, "a duplicate start must be rejected before creating a second job");
    controller.cancel();
    runUntil([&] { return observed.ready.load() + observed.errors.load() > 0; });
    require(observed.errors.load() == 1 && observed.ready.load() == 0 && !controller.busy(),
            "rejecting duplicate start must leave the original job cancellable");
}
void safeDestruction() {
    Signals observed;
    auto controller = std::make_unique<sps::PathGenerationController>();
    observe(*controller, observed);
    QPointer<sps::PathGenerationController> guard(controller.get());
    auto fixture = model(true);
    fixture.parameters.stepOver = 0.25;
    fixture.parameters.pointDistance = 0.25;
    controller->start(selectedRegion(fixture.shape), fixture.parameters);
    require(controller->busy(), "destruction test needs a running worker");
    QElapsedTimer destructionTime;
    destructionTime.start();
    controller.reset();
    require(guard.isNull(), "controller destruction must complete QObject lifetime");
    require(destructionTime.elapsed() < 5000, "destructor must cancel and join its worker promptly");
    bool eventsDrained = false;
    QTimer::singleShot(0, QCoreApplication::instance(), [&] { eventsDrained = true; });
    runUntil([&] { return eventsDrained && QThreadPool::globalInstance()->activeThreadCount() == 0; });
    require(observed.ready.load() == 0 && observed.errors.load() == 0,
            "destroyed controller must not emit a stale completion or cancellation callback");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"async", [] { asyncCompletion(false); }}, {"responsive", [] { asyncCompletion(true); }},
        {"cancel", cancellation}, {"busy", busyRejection}, {"destruction", safeDestruction}
    };
    int failed = 0;
    for (const auto& test : cases) {
        if (!filter.empty() && filter != test.first) continue;
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
