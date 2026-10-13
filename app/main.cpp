#include "AudioSmokeTest.h"
#include "FrameConversionSmokeTest.h"
#include "RenderBenchmark.h"
#include "EncoderBenchmark.h"
#include "PerformanceProbe.h"
#include "AppSettings.h"
#include "ThirdUiSmokeTest.h"
#include <QSettings>
#include "UpdateController.h"
#include "EffectController.h"
#include "BackendBatchSmokeTest.h"
#include "RecoveryController.h"
#include "RecordingProfileSmokeTest.h"
#include "NativeExportSmokeTest.h"
#include "AudioCaptureContinuitySmokeTest.h"
#include "AudioTimeStretchSmokeTest.h"
#include "AutoFraming.h"
#include "CameraController.h"
#include "CameraSmokeTest.h"
#include "CanvasPreview.h"
#include "CanvasRenderer.h"
#include "CursorSmokeTest.h"
#include "CropPreview.h"
#include "CropSmokeTest.h"
#include "ManualFramingSmokeTest.h"
#include "PresetSmokeTest.h"
#include "ScenePresetController.h"
#include "ScenesUiSmokeTest.h"
#include "VisualUiSmokeTest.h"
#include "VisualProjectSmokeTest.h"
#include "VisualEffectsSmokeTest.h"
#include "DynamicBackgroundSmokeTest.h"
#include "RecentProjectsController.h"
#include "RecentProjectsSmokeTest.h"
#include "RecentUiSmokeTest.h"
#include "TimelineInteractionSmokeTest.h"
#include "EditorShortcutSmokeTest.h"
#include <QTemporaryDir>
#include "ExportController.h"
#include "ExportOptionsSmokeTest.h"
#include "ExportDeliverySmokeTest.h"
#include "MemoController.h"
#include "MemoSmokeTest.h"
#include "MarkerSmokeTest.h"
#include "PreviewAudioProcessor.h"
#include "PreviewAudioSmokeTest.h"
#include "ProjectController.h"
#include "ProjectFlowSmokeTest.h"
#include "RecordingControlSmokeTest.h"
#include "RecordingController.h"
#include "TimelineMedia.h"
#include "TimelineSmokeTest.h"
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

int main(int argc, char **argv) {
  qputenv("QT_MEDIA_BACKEND", "ffmpeg");
  // Our compositor consumes CPU QImages, not decoder GPU textures. On this
  // Qt 6.8 path D3D texture readback stalls the GUI and costs more than software
  // decode. Keep encoding hardware selection untouched, and honor an explicit
  // decoding override for diagnostics or a future GPU compositor.
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
  QQuickStyle::setStyle("Basic");
  QGuiApplication app(argc, argv);
  app.setApplicationName("ScreenPilot");
  app.setOrganizationName("ScreenPilot");
  app.setApplicationVersion("0.1.0-dev");
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addOption({"media-performance-test", "Measure actual editor playback and export.", "source"});
  cli.addOption({"performance-result-directory", "Performance evidence directory.", "directory"});
  cli.addOption({"performance-rate", "Playback and export rate (default 1).", "rate"});
  cli.addOption({"performance-seconds", "Playback wall seconds (default 15).", "seconds"});
  cli.addOption({"performance-export-seconds", "Export output seconds (default 20).", "seconds"});
  cli.addOption({"performance-resolution", "Export resolution (default 1440p).", "resolution"});
  cli.addOption({"performance-fps", "Export FPS: 30 or 60 (default 60).", "fps"});
  cli.addOption({"performance-no-export", "Measure playback only."});
  cli.addOption({"performance-export-only", "Measure export without editor playback (600 second timeout)."});
  cli.addOption({"performance-render-baseline", "Also measure 30 full 1440p renders."});
  cli.addOption({"performance-cancel-export", "Cancel export after one second and measure settlement."});
  cli.addOption({"performance-muted-preview", "Skip preview audio processing for a control measurement."});
  cli.addOption({"performance-muted-export", "Export muted video for a control measurement."});
  cli.addOption({"performance-preview-image", "Save actual rendered preview to local evidence."});
  cli.addOption({"performance-min-presented-fps", "Opt-in minimum unique scenegraph-submitted preview FPS.", "fps"});
  cli.addOption({"performance-max-presented-gap-ms", "Opt-in maximum interval without a new submitted preview image.", "milliseconds"});
  cli.addOption({"backend-batch-smoke-test", "Check backend model, recovery, effects and independent mixed export.", "source"});
  cli.addOption({"backend-result-directory", "Local backend evidence directory.", "directory"});
  cli.addOption({"third-wiring-smoke-test", "Verify third UI project, modal, recovery and dark flows.", "source"});
  cli.addOption({"wiring-result-directory", "Local third UI evidence.", "directory"});
  cli.addOption({"recording-profile-smoke-test", "Verify native and capped physical recording dimensions.", "directory"});
  cli.addOption({"native-export-smoke-test", "Verify source-resolution export with a high-resolution canvas.", "directory"});
  cli.addOption({"encoder-benchmark", "Time Qt H.264 encoding of BGRA, NV12 and YUV420P input.", "directory"});
  cli.addOption({"render-benchmark", "Time canvas rendering on one decoded frame.", "source"});
  cli.addOption({"frame-conversion-smoke-test", "Compare fast YUV conversion with QVideoFrame::toImage.", "source"});
  cli.addOption({"audio-continuity-smoke-test", "Verify continuous system audio under GUI stalls.", "directory"});
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
  cli.addOption(
      {"preview-performance-test",
       "Measure a generated 1080p preview at 60 Hz; no desktop capture.",
       "directory"});
  cli.addOption(
      {"audio-smoke-test",
       "Record generated tones with system audio, edit and verify decoded A/V.",
       "directory"});
  cli.addOption({"export-options-smoke-test",
                 "Verify M4A, muted MP4, reordered clips and speed exports.",
                 "directory"});
  cli.addOption(
      {"project-flow-smoke-test",
       "Verify model mapping, history, framing and project roundtrip."});
  cli.addOption({"camera-smoke-test",
                 "Verify overlay and short camera capture if a device exists.",
                 "directory"});
  cli.addOption(
      {"timeline-smoke-test",
       "Verify thumbnails and waveform for a local generated fixture.",
       "source"});
  cli.addOption({"timeline-result-directory",
                 "Local output directory for timeline evidence.", "directory"});
  cli.addOption({"editor-batch-smoke-test",
                 "Verify reordered variable-rate playback with camera sidecar "
                 "in the real editor.",
                 "source"});
  cli.addOption({"editor-result-directory",
                 "Local output directory for editor evidence.", "directory"});
  cli.addOption({"audio-packet-smoke-test",
                 "Verify audio packet continuity under timestamp jitter."});
  cli.addOption({"audio-time-stretch-smoke-test",
                 "Verify pitch-preserving time stretch."});
  cli.addOption({"preview-audio-smoke-test",
                 "Verify accelerated preview PCM and pause.", "directory"});
  cli.addOption({"cursor-smoke-test",
                 "Verify cursor rendering and project persistence.",
                 "directory"});
  cli.addOption({"recording-controls-smoke-test",
                 "Verify real pause/resume, restart and discard.",
                 "directory"});
  cli.addOption({"cursor-export-smoke-test",
                 "Verify cursor on/off in encoded video.", "source"});
  cli.addOption({"cursor-result-directory",
                 "Local cursor export evidence directory.", "directory"});
  cli.addOption({"memo-smoke-test", "Verify isolated memo persistence and window affinity.", "directory"});
  cli.addOption({"markers-smoke-test", "Verify marker mapping, history and project persistence.", "source"});
  cli.addOption({"markers-result-directory", "Local marker evidence directory.", "directory"});
  cli.addOption({"export-delivery-smoke-test", "Verify export pixels, frame rate and original bundle.", "source"});
  cli.addOption({"export-delivery-result-directory", "Local export delivery evidence directory.", "directory"});
  cli.addOption({"workflow-ui-smoke-test", "Verify the floating memo in the real application.", "directory"});
  cli.addOption({"crop-smoke-test", "Verify crop, framing and cursor composition.", "directory"});
  cli.addOption({"crop-export-smoke-test", "Verify cropped and zoomed encoded output.", "directory"});
  cli.addOption({"manual-framing-smoke-test", "Verify manual zoom mapping, history and persistence.", "source"});
  cli.addOption({"preset-smoke-test", "Verify isolated scene presets and owned background assets.", "directory"});
  cli.addOption({"scenes-ui-smoke-test", "Verify crop, manual zoom and presets in the real editor.", "source"});
  cli.addOption({"scenes-result-directory", "Local scene batch evidence directory.", "directory"});
  cli.addOption({"visual-effects-smoke-test", "Verify frame geometry and opening animation.", "directory"});
  cli.addOption({"visual-project-smoke-test", "Verify video assets, visual settings and presets.", "directory"});
  cli.addOption({"visual-project-source", "Generated MP4 fixture for the visual asset check.", "source"});
  cli.addOption({"dynamic-background-smoke-test", "Verify silent background video seek, loop and actual export.", "directory"});
  cli.addOption({"visual-ui-smoke-test", "Verify visual effects in the real editor.", "source"});
  cli.addOption({"recent-projects-smoke-test", "Verify isolated recent project history.", "directory"});
  cli.addOption({"recent-ui-smoke-test", "Verify recent project opening and dirty project protection.", "source"});
  cli.addOption({"timeline-interaction-smoke-test", "Verify manual track drag, split and clipboard.", "source"});
  cli.addOption({"editor-shortcut-smoke-test", "Verify actual editor keys and text focus protection.", "source"});
  cli.addOption({"editing-result-directory", "Local editing workflow evidence directory.", "directory"});
  cli.addOption({"ui-snapshot-smoke-test", "Capture recording, editor and export layouts using generated media.", "source"});
  cli.process(app);
  if (cli.isSet("encoder-benchmark")) return runEncoderBenchmark(app, cli.value("encoder-benchmark"));
  if (cli.isSet("render-benchmark")) return runRenderBenchmark(app, cli.value("render-benchmark"));
  if (cli.isSet("frame-conversion-smoke-test")) return runFrameConversionSmokeTest(app, cli.value("frame-conversion-smoke-test"));
  if (cli.isSet("backend-batch-smoke-test")) return runBackendBatchSmokeTest(app, cli.value("backend-batch-smoke-test"), cli.value("backend-result-directory"));
  if (cli.isSet("recording-profile-smoke-test")) return runRecordingProfileSmokeTest(app, cli.value("recording-profile-smoke-test"));
  if (cli.isSet("native-export-smoke-test")) return runNativeExportSmokeTest(app, cli.value("native-export-smoke-test"));
  if (cli.isSet("audio-continuity-smoke-test")) return runAudioCaptureContinuitySmokeTest(app, cli.value("audio-continuity-smoke-test"));
  if (cli.isSet("recent-projects-smoke-test")) return runRecentProjectsSmokeTest(app, cli.value("recent-projects-smoke-test"));
  if (cli.isSet("timeline-interaction-smoke-test")) return runTimelineInteractionSmokeTest(app, cli.value("timeline-interaction-smoke-test"), cli.value("editing-result-directory"));
  if (cli.isSet("visual-effects-smoke-test")) return runVisualEffectsSmokeTest(cli.value("visual-effects-smoke-test"));
  if (cli.isSet("visual-project-smoke-test")) return runVisualProjectSmokeTest(app, cli.value("visual-project-smoke-test"), cli.value("visual-project-source"));
  if (cli.isSet("dynamic-background-smoke-test")) return runDynamicBackgroundSmokeTest(cli.value("dynamic-background-smoke-test"));
  if (cli.isSet("crop-smoke-test")) return runCropSmokeTest(cli.value("crop-smoke-test"));
  if (cli.isSet("crop-export-smoke-test")) return runCropExportSmokeTest(cli.value("crop-export-smoke-test"));
  if (cli.isSet("manual-framing-smoke-test")) return verifyManualFraming(app, cli.value("manual-framing-smoke-test"), cli.value("scenes-result-directory"));
  if (cli.isSet("preset-smoke-test")) return runPresetSmokeTest(app, cli.value("preset-smoke-test"));
  if (cli.isSet("memo-smoke-test"))
    return runMemoSmokeTest(cli.value("memo-smoke-test"));
  if (cli.isSet("markers-smoke-test"))
    return verifyRecordingMarkers(app, cli.value("markers-smoke-test"), cli.value("markers-result-directory"));
  if (cli.isSet("export-delivery-smoke-test"))
    return runExportDeliverySmokeTest(app, cli.value("export-delivery-result-directory"), cli.value("export-delivery-smoke-test"));
  if (cli.isSet("cursor-export-smoke-test"))
    return runCursorExportSmokeTest(app, cli.value("cursor-export-smoke-test"),
                                    cli.value("cursor-result-directory"));
  if (cli.isSet("cursor-smoke-test"))
    return runCursorSmokeTest(cli.value("cursor-smoke-test"));
  if (cli.isSet("recording-controls-smoke-test"))
    return runRecordingControlSmokeTest(
        app, cli.value("recording-controls-smoke-test"));
  if (cli.isSet("audio-time-stretch-smoke-test"))
    return runAudioTimeStretchSmokeTest();
  if (cli.isSet("preview-audio-smoke-test"))
    return runPreviewAudioSmokeTest(app, cli.value("preview-audio-smoke-test"));
  if (cli.isSet("audio-packet-smoke-test"))
    return runAudioPacketSmokeTest();
  if (cli.isSet("project-flow-smoke-test"))
    return runProjectFlowSmokeTest();
  if (cli.isSet("export-options-smoke-test"))
    return runExportOptionsSmokeTest(app,
                                     cli.value("export-options-smoke-test"));
  if (cli.isSet("camera-smoke-test"))
    return runCameraSmokeTest(app, cli.value("camera-smoke-test"));
  if (cli.isSet("timeline-smoke-test"))
    return verifyTimelineMedia(app, cli.value("timeline-smoke-test"),
                               cli.value("timeline-result-directory"));
  if (cli.isSet("audio-smoke-test"))
    return runAudioSmokeTest(app, cli.value("audio-smoke-test"));
  RecordingController recorder;
  ProjectController project;
  ExportController exporter;
  CameraController cameraRecorder;
  TimelineMedia timelineMedia;
  MemoController memoController;
  const QStringList requestedOptions = cli.optionNames();
  const bool probeMode = std::any_of(requestedOptions.cbegin(), requestedOptions.cend(),
      [](const QString &name) { return name.endsWith("-test"); });
  QString probeParent=QDir::current().filePath("probe-results");
  if(probeMode) {
    for(const auto &name:requestedOptions) if(name.endsWith("result-directory") && !cli.value(name).isEmpty()) {
      probeParent=QFileInfo(cli.value(name)).absolutePath();break;
    }
    if(!QDir().mkpath(probeParent)) return 2;
  }
  auto recentProbeDirectory = probeMode ? std::make_unique<QTemporaryDir>(QDir(probeParent).filePath(".screenpilot-runtime-XXXXXX")) : nullptr;
  if (recentProbeDirectory && !recentProbeDirectory->isValid()) return 2;
  if(recentProbeDirectory) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,recentProbeDirectory->filePath("settings"));
  }
  RecentProjectsController recentProjectsController(nullptr,
      recentProbeDirectory ? recentProbeDirectory->filePath("recent-projects.json") : QString{});
  QUrl lastRecentLocation;
  QObject::connect(&project, &ProjectController::changed, &recentProjectsController, [&] {
    const QUrl location = project.projectLocation();
    if (location == lastRecentLocation) return;
    lastRecentLocation = location;
    if (!location.isEmpty()) recentProjectsController.remember(location, project.title());
  });
  QObject::connect(&recorder, &RecordingController::mediaStarted,
                   &cameraRecorder, [&](const QString &directory) {
                     if (cameraRecorder.enabled())
                       cameraRecorder.startRecording(directory);
                   });
  QObject::connect(&recorder, &RecordingController::mediaPaused,
                   &cameraRecorder, &CameraController::setRecordingPaused);
  QObject::connect(&recorder, &RecordingController::mediaDiscarding,
                   &cameraRecorder, &CameraController::discardRecording);
  QObject::connect(&recorder, &RecordingController::mediaStopping,
                   &cameraRecorder, &CameraController::stopRecording);
  QObject::connect(&cameraRecorder, &CameraController::recordingFailed,
                   &recorder, [&](const QString &) { recorder.stop(); });
  QObject::connect(&project, &ProjectController::changed, &timelineMedia, [&] {
    timelineMedia.setSource(project.source(), project.sourceDuration());
  });
  qmlRegisterType<CanvasPreview>("ScreenPilot.Render", 1, 0, "CanvasPreview");
  qmlRegisterType<CropPreview>("ScreenPilot.Render", 1, 0, "CropPreview");
  const QString sceneEvidence = cli.value("scenes-result-directory");
  const bool sceneUiTest = cli.isSet("scenes-ui-smoke-test") || cli.isSet("visual-ui-smoke-test");
  if (sceneUiTest && (sceneEvidence.isEmpty() ||
      (QDir(sceneEvidence).exists() && !QDir(sceneEvidence).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) ||
      !QDir().mkpath(sceneEvidence))) return 2;
  ScenePresetController scenePresets(&project, sceneUiTest ? QDir(sceneEvidence).filePath("preset-store") : QString{});
  PreviewAudioProcessor previewAudioProcessor;
  AppSettings appSettings;
  UpdateController updateController(&appSettings, !probeMode);
  EffectController effects(&project);
  exporter.setProject(&project);
  QUrl notifiedRecording, notifiedExport;
  QObject::connect(&recorder, &RecordingController::changed, &appSettings, [&] {
    if (!recorder.busy() && !recorder.lastRecording().isEmpty() && recorder.lastRecording()!=notifiedRecording) {
      notifiedRecording=recorder.lastRecording();
      if(!probeMode) appSettings.notifyCompletion();
    }
  });
  QObject::connect(&exporter, &ExportController::changed, &appSettings, [&] {
    if (!exporter.busy() && exporter.error().isEmpty() && !exporter.lastExport().isEmpty() && exporter.lastExport()!=notifiedExport) {
      notifiedExport=exporter.lastExport();
      if(!probeMode) appSettings.notifyCompletion();
    }
  });
  RecoveryController recoveryController(&project,
      recentProbeDirectory ? recentProbeDirectory->filePath("recovery") : QString{});
  QMediaPlayer *editorPlayback = nullptr;
  auto updateRecoveryBlock = [&] {
    recoveryController.setBlocked(recorder.busy() || cameraRecorder.busy() || exporter.busy() ||
        (editorPlayback && editorPlayback->playbackState() == QMediaPlayer::PlayingState));
  };
  QObject::connect(&recorder, &RecordingController::changed, &recoveryController, updateRecoveryBlock);
  QObject::connect(&cameraRecorder, &CameraController::changed, &recoveryController, updateRecoveryBlock);
  QObject::connect(&exporter, &ExportController::changed, &recoveryController, updateRecoveryBlock);
  QObject::connect(&project, &ProjectController::changed, &previewAudioProcessor, [&] {
    previewAudioProcessor.setMixSources(project.systemAudioSource(), project.microphoneAudioSource(), project.audioSettings());
  });
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appSettings", &appSettings);
  engine.rootContext()->setContextProperty("updateController", &updateController);
  engine.rootContext()->setContextProperty("effects", &effects);
  engine.rootContext()->setContextProperty("recoveryController", &recoveryController);
  engine.rootContext()->setContextProperty("recentProjectsController", &recentProjectsController);
  engine.rootContext()->setContextProperty("scenePresets", &scenePresets);
  engine.rootContext()->setContextProperty("memoController", &memoController);
  engine.rootContext()->setContextProperty("previewAudioProcessor",
                                           &previewAudioProcessor);
  engine.addImageProvider("timeline", timelineMedia.imageProvider());
  engine.rootContext()->setContextProperty("timelineMedia", &timelineMedia);
  engine.rootContext()->setContextProperty("cameraRecorder", &cameraRecorder);
  engine.rootContext()->setContextProperty("recorder", &recorder);
  engine.rootContext()->setContextProperty("project", &project);
  engine.rootContext()->setContextProperty("exporter", &exporter);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("ScreenPilotApp", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;
  if (auto preview = engine.rootObjects().first()->findChild<CanvasPreview *>("canvasPreview")) {
    auto applyQuality = [&, preview] { preview->setPreviewQuality(appSettings.values().value("previewQuality").toString()); };
    applyQuality();
    QObject::connect(&appSettings, &AppSettings::changed, preview, applyQuality);
  }
  if (auto player = engine.rootObjects().first()->findChild<QMediaPlayer *>(
          "editorPlayer")) {
    editorPlayback = player;
    previewAudioProcessor.attach(player);
    QObject::connect(player, &QMediaPlayer::playbackStateChanged, &recoveryController, updateRecoveryBlock);
  }
  if (cli.isSet("media-performance-test")) return runMediaPerformanceProbe(app, engine.rootObjects().first(), project, exporter, previewAudioProcessor, cli);
  if(cli.isSet("third-wiring-smoke-test")) return thirdUiSmoke(app,engine.rootObjects().first(),project,recoveryController,appSettings,exporter,effects,cli.value("third-wiring-smoke-test"),cli.value("wiring-result-directory"));
  if (cli.isSet("ui-snapshot-smoke-test")) {
    const QDir output(cli.value("editing-result-directory"));
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window || output.path() == "." ||
        (output.exists() && !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) ||
        !QDir().mkpath(output.absolutePath())) return 2;
    window->resize(1280, 860);
    window->show();
    auto editor = window->findChild<QObject *>("editorView");
    auto exportDialog = window->findChild<QObject *>("exportOptionsDialog");
    QJsonObject result;
    int stage = 0;
    QTimer snapshotTimer;
    snapshotTimer.setInterval(900);
    QObject::connect(&snapshotTimer, &QTimer::timeout, &app, [&] {
      const QStringList names = {"recorder", "editor", "export", "editor-minimum", "recorder-minimum"};
      const auto image = window->grabWindow();
      result[names[stage]] = !image.isNull() && image.save(output.filePath(names[stage] + ".png"));
      if (stage == 0) {
        if (!editor || !exportDialog || !project.newRecording(
              QUrl::fromLocalFile(cli.value("ui-snapshot-smoke-test")), 3)) {
          app.exit(2);
          return;
        }
        window->setProperty("editing", true);
        QMetaObject::invokeMethod(editor, "seek", Q_ARG(QVariant, QVariant(.5)));
      } else if (stage == 1) {
        QMetaObject::invokeMethod(exportDialog, "open");
      } else if (stage == 2) {
        result["exportOpened"] = exportDialog->property("opened").toBool();
        QMetaObject::invokeMethod(exportDialog, "close");
        window->resize(860, 640);
      } else if (stage == 3) {
        window->setProperty("editing", false);
      } else {
        snapshotTimer.stop();
        bool passed = true;
        for (auto it = result.begin(); it != result.end(); ++it)
          passed = passed && it.value().toBool();
        result["success"] = passed;
        QFile report(output.filePath("ui-snapshot-result.json"));
        if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(result).toJson()) < 0)
          passed = false;
        app.exit(passed ? 0 : 1);
      }
      ++stage;
    });
    snapshotTimer.start();
    return app.exec();
  }
  if (cli.isSet("editor-shortcut-smoke-test"))
    return verifyEditorShortcuts(app, engine.rootObjects().first(), project,
        cli.value("editor-shortcut-smoke-test"), cli.value("editing-result-directory"));
  if (cli.isSet("recent-ui-smoke-test"))
    return runRecentUiSmokeTest(app, engine.rootObjects().first(), project, recentProjectsController,
        cli.value("recent-ui-smoke-test"), cli.value("editing-result-directory"));
  if (cli.isSet("scenes-ui-smoke-test"))
    return runScenesUiSmokeTest(app, engine.rootObjects().first(), project, scenePresets,
        cli.value("scenes-ui-smoke-test"), sceneEvidence);
  if (cli.isSet("visual-ui-smoke-test"))
    return runVisualUiSmokeTest(app, engine.rootObjects().first(), project, scenePresets,
        cli.value("visual-ui-smoke-test"), sceneEvidence);
  auto memoWindow = engine.rootObjects().first()->findChild<QWindow *>("recorderMemoWindow");
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &memoController, [&] {
    if (memoWindow && memoWindow->isVisible()) memoController.saveWindow(memoWindow);
  });
  if (cli.isSet("workflow-ui-smoke-test")) {
    const QDir output(cli.value("workflow-ui-smoke-test"));
    if ((output.exists() && !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) ||
        !QDir().mkpath(output.absolutePath())) return 2;
    if (!memoWindow || !QMetaObject::invokeMethod(memoWindow, "toggle")) return 1;
    QTimer::singleShot(800, &app, [&, output] {
      const bool shown = memoWindow->isVisible();
      const bool affinity = memoController.captureExcluded();
      QMetaObject::invokeMethod(memoWindow, "toggle");
      const bool hidden = !memoWindow->isVisible();
      QFile report(output.filePath("workflow-ui-result.json"));
      const bool written = report.open(QIODevice::WriteOnly) && report.write(
          QJsonDocument(QJsonObject{{"memoShown", shown}, {"memoHidden", hidden},
                                   {"captureAffinityConfirmed", affinity}}).toJson()) > 0;
      app.exit(shown && hidden && written ? 0 : 1);
    });
    return app.exec();
  }

  bool restartPending = false;
  auto rootWindow = engine.rootObjects().first();
  auto restartWhenReady = [&] {
    if (!restartPending || recorder.busy() || cameraRecorder.busy())
      return;
    restartPending = false;
    if (!rootWindow->property("closeAfterSave").toBool() &&
        cameraRecorder.error().isEmpty())
      QMetaObject::invokeMethod(rootWindow, "beginRecording");
  };
  QObject::connect(&recorder, &RecordingController::restartRequested,
                   rootWindow, [&] {
                     restartPending = true;
                     QTimer::singleShot(0, rootWindow, restartWhenReady);
                   });
  QObject::connect(&cameraRecorder, &CameraController::changed, rootWindow,
                   restartWhenReady);

  if (cli.isSet("editor-batch-smoke-test")) {
    QDir output(cli.value("editor-result-directory"));
    if (cli.value("editor-result-directory").isEmpty() ||
        (output.exists() &&
         !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
              .isEmpty()) ||
        !QDir().mkpath(output.absolutePath()))
      return 2;
    auto root = engine.rootObjects().first();
    root->setProperty("editing", true);
    auto view = root->findChild<QQuickItem *>("editorView");
    auto preview = root->findChild<CanvasPreview *>("canvasPreview");
    auto player = root->findChild<QMediaPlayer *>("editorPlayer");
    const QUrl source =
        QUrl::fromLocalFile(cli.value("editor-batch-smoke-test"));
    if (!view || !preview || !player || !project.newRecording(source, 3) ||
        !project.split(1) || !project.split(2) || !project.moveClip(2, 0) ||
        !project.setClipRate(0, 2) || !project.setCameraMedia(source, 1, false))
      return 3;
    project.setCameraOption("shape", "rectangle");
    view->setProperty("inspectorTab", 2);
    int blue = 0, red = 0, green = 0, cameraGreen = 0;
    bool playing = false, done = false;
    int dragStage = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    auto timer = new QTimer(&app);
    timer->setInterval(40);
    auto finish = [&](bool success, QString error) {
      if (done)
        return;
      done = true;
      timer->stop();
      player->pause();
      if (auto window = qobject_cast<QQuickWindow *>(root))
        window->grabWindow().save(output.filePath("editor-batch.png"));
      QFile report(output.filePath("editor-batch-result.json"));
      report.open(QIODevice::WriteOnly);
      report.write(
          QJsonDocument(
              QJsonObject{{"success", success},
                          {"error", success ? QString{} : error},
                          {"blueFirst", blue},
                          {"redSecond", red},
                          {"greenThird", green},
                          {"cameraGreen", cameraGreen},
                          {"positionSeconds",
                           view->property("positionSeconds").toDouble()},
                          {"durationSeconds", project.duration()}})
              .toJson());
      app.exit(success ? 0 : 1);
    };
    QObject::connect(timer, &QTimer::timeout, &app, [&] {
      if (!playing && elapsed.elapsed() > 800 && preview->hasFrame() &&
          !view->property("primingPreview").toBool()) {
        auto window = qobject_cast<QQuickWindow *>(root);
        auto drag = [&](int from, int to) {
          std::function<QQuickItem *(QQuickItem *, const QString &)> findItem;
          findItem = [&](QQuickItem *item,
                         const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
              return item;
            for (auto child : item->childItems())
              if (auto found = findItem(child, name))
                return found;
            return nullptr;
          };
          auto a = window ? findItem(window->contentItem(),
                                     QString("timelineClip-%1").arg(from))
                          : nullptr;
          auto b = window ? findItem(window->contentItem(),
                                     QString("timelineClip-%1").arg(to))
                          : nullptr;
          if (!a || !b || !window)
            return false;
          const QPointF begin = a->mapToScene(QPointF(a->width() / 2, 20));
          const QPointF end = b->mapToScene(QPointF(b->width() / 2, 20));
          auto send = [&](QEvent::Type type, QPointF point,
                          Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point,
                              window->mapToGlobal(point.toPoint()), button,
                              buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &event);
          };
          send(QEvent::MouseButtonPress, begin, Qt::LeftButton, Qt::LeftButton);
          send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
          send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
          return true;
        };
        if (dragStage == 0) {
          if (!drag(0, 1) || project.selectedClip() != 1 ||
              project.clips().first().toMap().value("sourceStart").toDouble() !=
                  0) {
            finish(false, "Timeline drag reorder failed");
            return;
          }
          dragStage = 1;
          return;
        }
        if (dragStage == 1) {
          if (!drag(1, 0) || project.selectedClip() != 0 ||
              project.clips().first().toMap().value("sourceStart").toDouble() !=
                  2) {
            finish(false, "Timeline drag reorder restore failed");
            return;
          }
          dragStage = 2;
        }
        playing = true;
        QMetaObject::invokeMethod(view, "playPause");
      }
      const double time = view->property("positionSeconds").toDouble();
      const auto image = preview->videoSink()->videoFrame().toImage();
      if (playing && !image.isNull()) {
        const auto color =
            image.pixelColor(image.width() / 2, image.height() / 2);
        if (time > .06 && time < .4 && color.blue() > 150 && color.red() < 80)
          ++blue;
        if (time > .65 && time < 1.35 && color.red() > 150 &&
            color.green() < 80)
          ++red;
        if (time > 1.65 && time < 2.35 && color.green() > 100 &&
            color.red() < 80)
          ++green;
        const auto camera = preview->cameraSink()->videoFrame().toImage();
        if (time > .06 && time < .4 && !camera.isNull()) {
          const auto c =
              camera.pixelColor(camera.width() / 2, camera.height() / 2);
          if (c.green() > 100 && c.red() < 80)
            ++cameraGreen;
        }
      }
      if (playing && !view->property("awaitingSourceFrame").toBool() &&
          preview->frameTime() < 2 && time >= project.duration() - .03 &&
          player->playbackState() != QMediaPlayer::PlayingState) {
        finish(blue > 0 && red > 0 && green > 0 && cameraGreen > 0,
               "Editor clip order, rate and camera decode check");
        return;
      }
      if (elapsed.elapsed() > 12000)
        finish(false, "Editor batch playback timeout");
    });
    timer->start();
    return app.exec();
  }

  if (cli.isSet("preview-performance-test")) {
    QDir output(cli.value("preview-performance-test"));
    if ((output.exists() &&
         !output
              .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
              .isEmpty()) ||
        !QDir().mkpath(output.absolutePath()))
      return 2;
    auto root = engine.rootObjects().first();
    root->setProperty("editing", true);
    auto preview = root->findChild<CanvasPreview *>("canvasPreview");
    if (!preview)
      return 1;
    QImage source(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#274734"));
    {
      QPainter painter(&source);
      for (int x = 0; x < 1920; x += 40) {
        painter.fillRect(x, 0, 20, 1080, QColor("#d4a544"));
      }
    }
    QList<double> costs, gaps;
    QElapsedTimer clock, previous;
    QTimer feed;
    int count = 0, presented = 0;
    if (auto window = qobject_cast<QQuickWindow *>(root))
      QObject::connect(window, &QQuickWindow::frameSwapped, &feed,
                       [&] { ++presented; });
    QObject::connect(&feed, &QTimer::timeout, &app, [&] {
      if (count >= 130) {
        feed.stop();
        auto p95 = [](QList<double> samples) {
          std::sort(samples.begin(), samples.end());
          return samples.isEmpty() ? 0.0
                                   : samples[int((samples.size() - 1) * .95)];
        };
        double total = 0;
        for (double cost : costs)
          total += cost;
        QJsonObject result{
            {"success", preview->hasFrame()},
            {"scope",
             "Generated 1920x1080 frames; 60Hz submission; actual editor "
             "canvas with alternating camera state; no user capture"},
            {"samples", costs.size()},
            {"submission_mean_ms",
             total / std::max(qsizetype(1), costs.size())},
            {"submission_p95_ms", p95(costs)},
            {"tick_gap_p95_ms", p95(gaps)},
            {"presented_frames", presented},
            {"preview_width", preview->width()},
            {"preview_height", preview->height()},
            {"renderer",
             QJsonObject::fromVariantMap(preview->performanceMetrics())}};
        QSaveFile file(output.absoluteFilePath("performance-result.json"));
        if (!file.open(QIODevice::WriteOnly)) {
          app.exit(2);
          return;
        }
        file.write(QJsonDocument(result).toJson());
        app.exit(file.commit() && preview->hasFrame() ? 0 : 1);
        return;
      }
      if (previous.isValid() && count >= 10)
        gaps.append(previous.nsecsElapsed() / 1000000.0);
      previous.restart();
      clock.start();
      preview->setCamera({{"scale", 1.0 + .6 * ((count % 60) / 60.0)},
                          {"centerX", .5},
                          {"centerY", .5}});
      preview->videoSink()->setVideoFrame(QVideoFrame(source));
      if (count >= 10)
        costs.append(clock.nsecsElapsed() / 1000000.0);
      ++count;
    });
    feed.setTimerType(Qt::PreciseTimer);
    QTimer::singleShot(500, &app, [&] { feed.start(16); });
    return app.exec();
  }

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
    const bool previousSystemAudio = recorder.systemAudioEnabled();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &recorder,
                     [&, previousSystemAudio] {
                       recorder.setSystemAudioEnabled(previousSystemAudio);
                     });
    recorder.setSystemAudioEnabled(false);
    recorder.setMicrophoneEnabled(false);
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
            auto preview =
                engine.rootObjects().first()->findChild<CanvasPreview *>(
                    "canvasPreview");
            auto event = [&, view, player, preview](const QString &tag) {
              playbackEvents.append(QJsonObject{
                  {"tag", tag},
                  {"position", double(player->position())},
                  {"state", int(player->playbackState())},
                  {"status", int(player->mediaStatus())},
                  {"priming", view->property("primingPreview").toBool()},
                  {"preview", preview ? QJsonObject::fromVariantMap(
                                            preview->performanceMetrics())
                                      : QJsonObject{}}});
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
                  2000, &app, [&, finish, view, player, event, start] {
                    event("after_play");
                    if (player->playbackState() != QMediaPlayer::PlayingState ||
                        player->position() < start + 1500) {
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
