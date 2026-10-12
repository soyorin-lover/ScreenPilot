#include "AutoFraming.h"
#include "CanvasPreview.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include "ProjectController.h"
#include "RecordingController.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <cmath>
#include <memory>

int main(int argc, char **argv) {
  qputenv("QT_MEDIA_BACKEND", "ffmpeg");
  QQuickStyle::setStyle("Basic");
  QGuiApplication app(argc, argv);
  app.setApplicationName("ScreenPilot");
  app.setOrganizationName("ScreenPilot");
  app.setApplicationVersion("0.1.0-dev");
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addVersionOption();
  cli.addOption({"smoke-test",
                 "Record only a generated test window, decode, then exit.",
                 "directory"});
  cli.addOption({"editing-smoke-test",
                 "Exercise recording, edits, project roundtrip and export.",
                 "directory"});
  cli.addOption({"framing-smoke-test",
                 "Check captured pointer samples and deterministic optional "
                 "framing through export.",
                 "directory"});
  cli.addOption(
      {"canvas-smoke-test",
       "Exercise canvas styling, background copy, project reopen and export.",
       "directory"});
  cli.addOption(
      {"playback-smoke-test",
       "Record a generated window and check editor play/pause/resume.",
       "directory"});
  cli.process(app);
  RecordingController recorder;
  ProjectController project;
  ExportController exporter;
  qmlRegisterType<CanvasPreview>("ScreenPilot.Render", 1, 0, "CanvasPreview");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("recorder", &recorder);
  engine.rootContext()->setContextProperty("project", &project);
  engine.rootContext()->setContextProperty("exporter", &exporter);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("ScreenPilotApp", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;

  // One optional end-to-end check exercises the actual product controller.
  // It captures only this generated window, never the user's desktop.
  const bool playbackSmoke = cli.isSet("playback-smoke-test");
  const bool canvasSmoke = cli.isSet("canvas-smoke-test");
  const bool framingSmoke = cli.isSet("framing-smoke-test") || canvasSmoke;
  const bool editingSmoke =
      cli.isSet("editing-smoke-test") || framingSmoke || playbackSmoke;
  const QString smokeDirectory =
      playbackSmoke  ? cli.value("playback-smoke-test")
      : canvasSmoke  ? cli.value("canvas-smoke-test")
      : framingSmoke ? cli.value("framing-smoke-test")
      : editingSmoke ? cli.value("editing-smoke-test")
                     : cli.value("smoke-test");
  std::unique_ptr<QObject> target;
  QMediaPlayer decoder;
  QVideoSink decoded;
  int decodedFrames = 0;
  bool finished = false;
  int smokeStage = 0;
  bool roundtripPassed = false;
  double expectedEditedDuration = 0;
  bool framingPassed = false;
  double framingPeak = 1;
  bool canvasPassed = false;
  QSize exportedSize;
  QJsonArray playbackEvents;

  if (!smokeDirectory.isEmpty()) {
    QDir output(smokeDirectory);
    if (output.exists() &&
        !output
             .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
             .isEmpty())
      return 2;
    if (!QDir().mkpath(smokeDirectory))
      return 2;
    recorder.setOutputDirectory(output.absolutePath());
    QQmlComponent pattern(&engine);
    pattern.setData(R"(
            import QtQuick
            Window {
                visible: true; width: 640; height: 360; title: "Recording check"
                color: "#244836"
                Rectangle {
                    width: 80; height: 80; y: 120; color: "#f1b55a"
                    SequentialAnimation on x {
                        loops: Animation.Infinite
                        NumberAnimation { from: 20; to: 540; duration: 900 }
                        NumberAnimation { from: 540; to: 20; duration: 900 }
                    }
                }
                Text { anchors.centerIn: parent; text: "ScreenPilot recording check"; color: "white"; font.pixelSize: 24 }
            }
        )",
                    QUrl("qrc:/SmokeTarget.qml"));
    target.reset(pattern.create());
    if (!target)
      return 1;
    decoder.setVideoSink(&decoded);
    auto finish = [&, output](bool success, const QString &reason) {
      if (finished)
        return;
      finished = true;
      QJsonObject result{
          {"success", success},
          {"reason", reason},
          {"file", recorder.lastRecording().toLocalFile()},
          {"file_bytes",
           double(QFileInfo(recorder.lastRecording().toLocalFile()).size())},
          {"decoded_frames", decodedFrames},
          {"decoded_duration_ms", double(decoder.duration())},
          {"editing_roundtrip", roundtripPassed},
          {"captured_pointer_samples", recorder.lastPointerSamples().size()},
          {"framing_fixture_passed", framingPassed},
          {"framing_peak", framingPeak},
          {"canvas_fixture_passed", canvasPassed},
          {"playback_events", playbackEvents},
          {"exported_width", exportedSize.width()},
          {"exported_height", exportedSize.height()},
          {"expected_edited_duration_seconds", expectedEditedDuration},
          {"scope", editingSmoke
                        ? "Generated own window; captured sampling; optional "
                          "deterministic framing fixture if requested; basic "
                          "edit/save/reopen/export; no "
                          "audio or Mac compatibility acceptance"
                        : "Generated own window recording only"}};
      QSaveFile json(output.absoluteFilePath("smoke-result.json"));
      bool saved = json.open(QIODevice::WriteOnly);
      const auto bytes = QJsonDocument(result).toJson();
      saved = saved && json.write(bytes) == bytes.size() && json.commit();
      if (playbackSmoke) {
        auto player = engine.rootObjects().first()->findChild<QMediaPlayer *>(
            "editorPlayer");
        if (player)
          QObject::disconnect(player, nullptr, &app, nullptr);
      }
      QCoreApplication::exit(success && saved ? 0 : 1);
    };
    // Value captures retain the completion handler after this block exits.
    QObject::connect(
        &decoded, &QVideoSink::videoFrameChanged, &app,
        [&, finish, output](const QVideoFrame &frame) {
          if (frame.isValid())
            ++decodedFrames;
          if (decodedFrames < 12 || decoder.duration() < 1000)
            return;
          if (!editingSmoke) {
            finish(true, "Saved MP4 opened and decoded");
            return;
          }
          if (smokeStage == 2) {
            exportedSize = frame.size();
            if (canvasSmoke) {
              const QImage exported = frame.toImage();
              const QColor corner = exported.pixelColor(4, 4),
                           expected("#3157a5");
              if (exported.width() != exported.height() ||
                  std::abs(corner.red() - expected.red()) > 12 ||
                  std::abs(corner.green() - expected.green()) > 12 ||
                  std::abs(corner.blue() - expected.blue()) > 12) {
                finish(false,
                       "Canvas ratio/background pixels differ after export");
                return;
              }
              exported.save(output.absoluteFilePath("exported-frame.png"));
              canvasPassed = true;
            }
            const bool durationMatches =
                std::abs(decoder.duration() / 1000.0 - expectedEditedDuration) <
                0.20;
            finish(
                durationMatches,
                durationMatches
                    ? "Record/edit/undo/redo/save/reopen/export/decode passed"
                    : "Export duration differs from edited sequence");
            return;
          }
          if (smokeStage != 0)
            return;
          if (playbackSmoke) {
            smokeStage = 10;
            decoder.stop();
            auto view = engine.rootObjects().first()->findChild<QObject *>(
                "editorView");
            auto player =
                engine.rootObjects().first()->findChild<QMediaPlayer *>(
                    "editorPlayer");
            if (!view || !player) {
              finish(false, "Editor objects missing");
              return;
            }
            auto event = [&, view, player](const QString &tag) {
              playbackEvents.append(QJsonObject{
                  {"tag", tag},
                  {"position", double(player->position())},
                  {"state", int(player->playbackState())},
                  {"status", int(player->mediaStatus())},
                  {"priming", view->property("primingPreview").toBool()}});
            };
            QObject::connect(player, &QMediaPlayer::playbackStateChanged, &app,
                             [event] { event("state"); });
            QObject::connect(player, &QMediaPlayer::mediaStatusChanged, &app,
                             [event] { event("status"); });
            QTimer::singleShot(700, &app, [&, finish, view, player, event] {
              event("before_play");
              const qint64 start = player->position();
              QMetaObject::invokeMethod(view, "playPause");
              QTimer::singleShot(
                  650, &app, [&, finish, view, player, event, start] {
                    event("after_play");
                    if (player->playbackState() != QMediaPlayer::PlayingState ||
                        player->position() < start + 150) {
                      finish(false, "Editor play did not advance");
                      return;
                    }
                    QMetaObject::invokeMethod(view, "playPause");
                    const qint64 paused = player->position();
                    QTimer::singleShot(
                        350, &app, [&, finish, view, player, event, paused] {
                          event("after_pause");
                          if (player->playbackState() !=
                                  QMediaPlayer::PausedState ||
                              std::abs(player->position() - paused) > 80) {
                            finish(false, "Editor pause did not hold");
                            return;
                          }
                          QMetaObject::invokeMethod(view, "playPause");
                          QTimer::singleShot(
                              500, &app, [finish, player, event, paused] {
                                event("after_resume");
                                finish(player->playbackState() ==
                                               QMediaPlayer::PlayingState &&
                                           player->position() > paused + 150,
                                       "Editor play/pause/resume check");
                              });
                        });
                  });
            });
            return;
          }
          smokeStage = 1;
          const double originalDuration = decoder.duration() / 1000.0;
          const QImage decodedSample = frame.toImage();
          decoder.stop();
          if (!project.hasProject() &&
              !project.newRecording(recorder.lastRecording(),
                                    originalDuration)) {
            finish(false, project.error());
            return;
          }
          if (framingSmoke) {
            // Deterministic effect fixture; do not move or click the user's
            // mouse.
            QVariantList fixture;
            for (int i = 0; i <= int(originalDuration * 30); ++i)
              fixture.append(QVariantMap{{"time", i / 30.0},
                                         {"x", .72},
                                         {"y", .4},
                                         {"click", i == 9},
                                         {"inside", true}});
            project.discardChanges();
            if (!project.newRecording(recorder.lastRecording(),
                                      originalDuration, fixture) ||
                recorder.lastPointerSamples().isEmpty() ||
                project.autoZoomEnabled() ||
                project.framingAt(.8).value("scale").toDouble() != 1) {
              finish(false, "Pointer sampling or default-off check failed");
              return;
            }
            project.setAutoZoomEnabled(true);
            for (double t = 0; t < originalDuration; t += .05)
              framingPeak = std::max(
                  framingPeak, project.framingAt(t).value("scale").toDouble());
            project.undo();
            if (project.autoZoomEnabled()) {
              finish(false, "Framing undo failed");
              return;
            }
            project.redo();
            const auto sample = decodedSample;
            const auto identity = AutoFraming::render(sample, {});
            const auto zoomed = AutoFraming::render(sample, {1.8, .65, .4});
            if (!project.autoZoomEnabled() || framingPeak <= 1.1 ||
                identity != sample || zoomed == sample) {
              finish(false, "Framing transform, redo or identity failed");
              return;
            }
            framingPassed = true;
          }
          project.setSelectedClip(0);
          if (!project.trimStart(0.2) ||
              !project.trimEnd(project.duration() - 0.2)) {
            finish(false, project.error());
            return;
          }
          const double cut1 = project.duration() / 3.0;
          const double cut2 = project.duration() * 2.0 / 3.0;
          if (!project.split(cut1) || !project.split(cut2)) {
            finish(false, project.error());
            return;
          }
          project.setSelectedClip(1);
          if (!project.removeSelected()) {
            finish(false, project.error());
            return;
          }
          const QVariantList kept = project.clips();
          project.undo();
          if (project.clips().size() != 3) {
            finish(false, "Undo failed");
            return;
          }
          project.redo();
          if (project.clips() != kept) {
            finish(false, "Redo failed");
            return;
          }
          if (canvasSmoke) {
            project.setCanvasOption("ratio", "1:1");
            project.undo();
            if (project.canvasSettings().value("ratio").toString() == "1:1") {
              finish(false, "Canvas undo failed");
              return;
            }
            project.redo();
            project.setCanvasOption("padding", 100.0);
            project.setCanvasOption("radius", 40.0);
            project.setCanvasOption("shadow", .4);
            project.setCanvasOption("backgroundColor", "#a04b34");
            QImage background(400, 300, QImage::Format_RGB32);
            background.fill(QColor("#3157a5"));
            const QString backgroundFile =
                output.absoluteFilePath("background-fixture.png");
            if (!background.save(backgroundFile) ||
                !project.setBackgroundImage(
                    QUrl::fromLocalFile(backgroundFile))) {
              finish(false, "Background fixture import: " + project.error());
              return;
            }
            project.setCanvasOption("shadow", .45);
            project.setCanvasOption("shadow", .4);
            const auto styled =
                CanvasRenderer::render(decodedSample, project.canvasSettings());
            if (styled.isNull() || styled.width() != styled.height() ||
                styled.pixelColor(4, 4) != QColor("#3157a5")) {
              finish(false, "Canvas shared render failed");
              return;
            }
          }
          expectedEditedDuration = project.duration();
          const auto package = QUrl::fromLocalFile(
              output.absoluteFilePath("Edited.dogscproject"));
          if (!project.saveAs(package) || project.dirty()) {
            finish(false, "Project save: " + project.error());
            return;
          }
          if (canvasSmoke) {
            QFile::remove(output.absoluteFilePath("background-fixture.png"));
            project.undo();
            if (!QFileInfo(project.canvasSettings()
                               .value("backgroundImage")
                               .toString())
                     .isFile()) {
              finish(
                  false,
                  "Saved background history still depends on original image");
              return;
            }
            project.redo();
          }
          if (!project.openProject(package) || project.clips() != kept) {
            finish(false, "Project roundtrip: " + project.error());
            return;
          }
          if (framingSmoke && (!project.autoZoomEnabled() ||
                               project.pointerSamples().isEmpty() ||
                               project.framingSegments().isEmpty())) {
            finish(false, "Framing project roundtrip failed");
            return;
          }
          if (framingSmoke) {
            if (project.framingAt(project.duration() - .05)
                    .value("scale")
                    .toDouble() > 1.001) {
              finish(false, "Source-time framing after deletion failed");
              return;
            }
            auto view = engine.rootObjects().first()->findChild<QObject *>(
                "editorView");
            if (view)
              QMetaObject::invokeMethod(view, "seek",
                                        Q_ARG(QVariant, QVariant(.65)));
          }
          if (canvasSmoke) {
            const auto settings = project.canvasSettings();
            if (settings.value("ratio").toString() != "1:1" ||
                settings.value("radius").toDouble() != 40 ||
                !QFileInfo(settings.value("backgroundImage").toString())
                     .isFile()) {
              finish(false, "Canvas project roundtrip failed");
              return;
            }
            auto view = engine.rootObjects().first()->findChild<QObject *>(
                "editorView");
            if (view)
              view->setProperty("inspectorTab", 1);
          }
          roundtripPassed = true;
          QTimer::singleShot(800, &app, [&, output, finish] {
            auto window =
                qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (canvasSmoke && window) {
              auto previewItem =
                  window->findChild<CanvasPreview *>("canvasPreview");
              if (!previewItem || !previewItem->hasFrame()) {
                auto player = window->findChild<QMediaPlayer *>("editorPlayer");
                finish(false,
                       QString("Canvas preview missing frame: state=%1 "
                               "status=%2 sink=%3 error=%4")
                           .arg(player ? int(player->playbackState()) : -1)
                           .arg(player ? int(player->mediaStatus()) : -1)
                           .arg(player && previewItem &&
                                player->videoSink() == previewItem->videoSink())
                           .arg(player ? player->errorString() : QString{}));
                return;
              }
            }
            if (window)
              window->grabWindow().save(output.absoluteFilePath("editor.png"));
          });
          exporter.start(
              project.source(), project.clips(),
              QUrl::fromLocalFile(output.absoluteFilePath("Edited.mp4")),
              project.framingSegments(), project.canvasSettings());
        });
    QObject::connect(&exporter, &ExportController::changed, &app, [&, finish] {
      if (smokeStage != 1 || exporter.busy())
        return;
      if (!exporter.error().isEmpty()) {
        finish(false, exporter.error());
        return;
      }
      if (exporter.lastExport().isEmpty())
        return;
      smokeStage = 2;
      decodedFrames = 0;
      decoder.setSource(exporter.lastExport());
      decoder.play();
    });
    QObject::connect(&recorder, &RecordingController::changed, &app,
                     [&, finish] {
                       if (smokeStage == 0 && !recorder.busy() &&
                           !recorder.lastRecording().isEmpty() &&
                           decoder.source().isEmpty()) {
                         decoder.setSource(recorder.lastRecording());
                         decoder.play();
                       }
                     });
    QTimer::singleShot(1000, &app, [&, finish, output] {
      auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
      if (window)
        window->grabWindow().save(output.absoluteFilePath("application.png"));
      recorder.refreshSources();
      const auto sources = recorder.sources();
      int selected = -1;
      for (int i = 0; i < sources.size(); ++i)
        if (sources[i].toMap().value("label").toString().contains(
                "Recording check"))
          selected = i;
      if (selected < 0) {
        finish(false, "Generated target not found");
        return;
      }
      recorder.setSelectedSource(selected);
      recorder.start();
      QTimer::singleShot(3500, &app, [&] { recorder.stop(); });
    });
    QTimer::singleShot(editingSmoke ? 35000 : 18000, &app, [&, finish] {
      finish(false, "Recording/decode timeout: " + recorder.error());
    });
  }
  return app.exec();
}
