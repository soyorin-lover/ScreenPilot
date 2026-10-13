#include "EditorShortcutSmokeTest.h"
#include "ProjectController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <cmath>
#include <memory>

int verifyEditorShortcuts(QGuiApplication &app, QObject *root,
                          ProjectController &project, const QString &sourcePath,
                          const QString &outputDirectory) {
  auto window = qobject_cast<QQuickWindow *>(root);
  auto editor = root->findChild<QQuickItem *>("editorView");
  if (!window || !editor || !qmlEngine(editor) ||
      !project.newRecording(QUrl::fromLocalFile(sourcePath), 8))
    return 2;
  auto settle = [&](int ms = 60) {
    QElapsedTimer elapsed;
    elapsed.start();
    do { app.processEvents(QEventLoop::AllEvents, 10); } while (elapsed.elapsed() < ms);
  };
  auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                 const QString &text = QString()) {
    QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
    QGuiApplication::sendEvent(window, &press);
    QGuiApplication::sendEvent(window, &release);
    settle();
  };
  auto seek = [&](double seconds) {
    QMetaObject::invokeMethod(editor, "seek", Q_ARG(QVariant, seconds));
    settle();
  };
  root->setProperty("editing", true);
  window->show();
  window->requestActivate();
  settle(500);
  editor->forceActiveFocus();
  seek(2);
  project.setExportOption("fps", 60);
  key(Qt::Key_Right);
  QJsonObject checks;
  checks["frameStep60"] = std::abs(editor->property("positionSeconds").toDouble() - (2 + 1.0 / 60)) < .0001;
  key(Qt::Key_Left, Qt::ShiftModifier);
  checks["fiveFrames"] = std::abs(editor->property("positionSeconds").toDouble() - (2 - 4.0 / 60)) < .0001;
  seek(2);
  key(Qt::Key_S, Qt::NoModifier, "s");
  checks["mainSplit"] = project.clips().size() == 2;
  key(Qt::Key_Z, Qt::ControlModifier);
  checks["undoSplit"] = project.clips().size() == 1;
  auto id = project.addManualFraming(1);
  project.updateManualFraming(id, {{"start", 1.0}, {"end", 3.0}});
  project.setSelectedManualFraming(id);
  seek(2);
  key(Qt::Key_S, Qt::NoModifier, "s");
  checks["manualSplit"] = project.manualFramingSegments().size() == 2;
  key(Qt::Key_Z, Qt::ControlModifier);
  checks["manualUndo"] = project.manualFramingSegments().size() == 1;
  project.setSelectedManualFraming(id);
  key(Qt::Key_C, Qt::ControlModifier);
  seek(4);
  key(Qt::Key_V, Qt::ControlModifier);
  checks["manualClipboard"] = project.hasManualClipboard() && project.manualFramingSegments().size() == 2;
  key(Qt::Key_Z, Qt::ControlModifier);
  auto track = root->findChild<QObject *>("manualZoomTrack");
  if (track) track->setProperty("dragActive", true);
  key(Qt::Key_Escape);
  checks["escapeDraft"] = track && !track->property("dragActive").toBool() && project.manualFramingSegments().size() == 1;

  QQmlComponent component(qmlEngine(editor));
  component.setData("import QtQuick\nTextInput { width: 220; height: 32; text: \"\" }", QUrl());
  std::unique_ptr<QObject> input(component.create());
  auto item = qobject_cast<QQuickItem *>(input.get());
  if (!item) return 3;
  item->setParentItem(editor);
  item->forceActiveFocus();
  const auto before = project.manualFramingSegments();
  key(Qt::Key_Q, Qt::NoModifier, "q");
  key(Qt::Key_W, Qt::NoModifier, "w");
  key(Qt::Key_S, Qt::NoModifier, "s");
  checks["typingPreserved"] = item->property("text").toString() == "qws" && project.manualFramingSegments() == before;
  key(Qt::Key_Z, Qt::ControlModifier);
  checks["nativeTextUndo"] = item->property("text").toString().isEmpty() && project.manualFramingSegments() == before;
  item->setParentItem(nullptr);
  editor->forceActiveFocus();
  key(Qt::Key_F1);
  auto help = root->findChild<QObject *>("shortcutHelp");
  checks["helpOpens"] = help && help->property("visible").toBool();
  const auto clips = project.clips();
  key(Qt::Key_S, Qt::NoModifier, "s");
  checks["modalBlocksEditing"] = project.clips() == clips && project.manualFramingSegments() == before;
  key(Qt::Key_Escape);
  checks["helpEscape"] = help && !help->property("visible").toBool();
  bool passed = true;
  for (auto it = checks.begin(); it != checks.end(); ++it) passed &= it.value().toBool();
  checks["success"] = passed;
  if (!outputDirectory.isEmpty()) {
    QDir().mkpath(outputDirectory);
    QFile report(QDir(outputDirectory).filePath("editor-shortcut-result.json"));
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(checks).toJson()) < 0) return 4;
  }
  return passed ? 0 : 1;
}
