#include "RecentUiSmokeTest.h"
#include "ProjectController.h"
#include "RecentProjectsController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
int runRecentUiSmokeTest(QGuiApplication &app, QObject *root,
                         ProjectController &project,
                         RecentProjectsController &recent,
                         const QString &source, const QString &directory) {
  QDir().mkpath(directory);
  const QUrl first =
      QUrl::fromLocalFile(QDir(directory).filePath("first.spproj"));
  const QUrl second =
      QUrl::fromLocalFile(QDir(directory).filePath("second.spproj"));
  ProjectController target;
  auto panel = root->findChild<QObject *>("recentProjectsPanel");
  auto unsaved = root->findChild<QObject *>("unsavedEditorDialog");
  if (!panel || !unsaved ||
      !project.newRecording(QUrl::fromLocalFile(source), 3) ||
      !project.saveAs(first) ||
      !target.newRecording(QUrl::fromLocalFile(source), 3) ||
      !target.split(1) || !target.saveAs(second) ||
      !recent.remember(second, "Generated second"))
    return 2;
  project.setCanvasOption("padding", 170);
  QTimer timer;
  QElapsedTimer elapsed;
  elapsed.start();
  int stage = 0;
  bool done = false;
  QJsonObject result;
  auto finish = [&](QString error) {
    if (done)
      return;
    done = true;
    timer.stop();
    result["success"] = error.isEmpty();
    result["error"] = error;
    if (auto window = qobject_cast<QQuickWindow *>(root))
      window->grabWindow().save(QDir(directory).filePath("recent-editor.png"));
    QFile file(QDir(directory).filePath("recent-ui-result.json"));
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(result).toJson()) < 0)
      error = "Report write failed";
    app.exit(error.isEmpty() ? 0 : 1);
  };
  QMetaObject::invokeMethod(panel, "open");
  QObject::connect(&timer, &QTimer::timeout, &app, [&] {
    if (elapsed.elapsed() > 10000) {
      finish("Recent UI timeout");
      return;
    }
    if (stage == 0) {
      if (!panel->property("opened").toBool())
        return;
      result["panelShown"] = true;
      QMetaObject::invokeMethod(panel, "close");
      if (!QMetaObject::invokeMethod(panel, "openRequested",
                                     Q_ARG(QUrl, second))) {
        finish("Recent open signal unavailable");
        return;
      }
      stage = 1;
    } else if (stage == 1) {
      if (!unsaved->property("opened").toBool())
        return;
      const bool guarded =
          project.dirty() && project.projectLocation() == first;
      result["unsavedGuard"] = guarded;
      if (!guarded) {
        finish("Recent open discarded dirty project");
        return;
      }
      QMetaObject::invokeMethod(unsaved, "reject");
      stage = 2;
    } else if (stage == 2) {
      if (unsaved->property("visible").toBool())
        return;
      result["cancelPreservesEdits"] =
          project.dirty() && project.projectLocation() == first;
      project.discardChanges();
      QMetaObject::invokeMethod(panel, "openRequested", Q_ARG(QUrl, second));
      stage = 3;
    } else {
      if (project.projectLocation() != second)
        return;
      const bool loaded = project.clips().size() == 2 && !project.dirty() &&
                          project.error().isEmpty();
      const bool forgotten =
          recent.removeEntry(second) &&
          QFile::exists(QDir(second.toLocalFile()).filePath("project.json"));
      result["selectedProjectOpened"] = loaded;
      result["forgetPreservesFiles"] = forgotten;
      finish(loaded && forgotten
                 ? QString{}
                 : QStringLiteral("Recent open or forget failed"));
    }
  });
  timer.start(100);
  return app.exec();
}
