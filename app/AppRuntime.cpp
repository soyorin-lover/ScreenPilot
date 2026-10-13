#include "AppRuntime.h"
#include "CanvasPreview.h"
#include "CropPreview.h"
#include <QCoreApplication>
#include <QMediaPlayer>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QWindow>

void AppRuntime::prepareProcess() {
  qputenv("QT_MEDIA_BACKEND", "ffmpeg");
  // Our compositor consumes CPU QImages, not decoder GPU textures. On this
  // Qt 6.8 path D3D texture readback stalls the GUI and costs more than software
  // decode. Keep encoding hardware selection untouched, and honor an explicit
  // decoding override for diagnostics or a future GPU compositor.
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
  QQuickStyle::setStyle("Basic");
}

AppRuntime::AppRuntime(Options options)
    : recentProjects(nullptr, options.recentProjectsFile),
      scenePresets(&project, options.presetStore),
      updateController(&appSettings, !options.probe),
      effects(&project),
      recoveryController(&project, options.recoveryDirectory),
      m_options(std::move(options)) {
  QObject::connect(&project, &ProjectController::changed, &recentProjects, [this] {
    const QUrl location = project.projectLocation();
    if (location == m_lastRecentLocation) return;
    m_lastRecentLocation = location;
    if (!location.isEmpty()) recentProjects.remember(location, project.title());
  });
  QObject::connect(&recorder, &RecordingController::mediaStarted,
                   &cameraRecorder, [this](const QString &directory) {
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
                   &recorder, [this](const QString &) { recorder.stop(); });
  QObject::connect(&project, &ProjectController::changed, &timelineMedia, [this] {
    timelineMedia.setSource(project.source(), project.sourceDuration());
  });
  exporter.setProject(&project);
  QObject::connect(&recorder, &RecordingController::changed, &appSettings, [this] {
    if (!recorder.busy() && !recorder.lastRecording().isEmpty() && recorder.lastRecording() != m_notifiedRecording) {
      m_notifiedRecording = recorder.lastRecording();
      if (!m_options.probe) appSettings.notifyCompletion();
    }
  });
  QObject::connect(&exporter, &ExportController::changed, &appSettings, [this] {
    if (!exporter.busy() && exporter.error().isEmpty() && !exporter.lastExport().isEmpty() &&
        exporter.lastExport() != m_notifiedExport) {
      m_notifiedExport = exporter.lastExport();
      if (!m_options.probe) appSettings.notifyCompletion();
    }
  });
  QObject::connect(&recorder, &RecordingController::changed, &recoveryController, [this] { updateRecoveryBlock(); });
  QObject::connect(&cameraRecorder, &CameraController::changed, &recoveryController, [this] { updateRecoveryBlock(); });
  QObject::connect(&exporter, &ExportController::changed, &recoveryController, [this] { updateRecoveryBlock(); });
  QObject::connect(&project, &ProjectController::changed, &previewAudioProcessor, [this] {
    previewAudioProcessor.setMixSources(project.systemAudioSource(), project.microphoneAudioSource(), project.audioSettings());
  });
}

void AppRuntime::updateRecoveryBlock() {
  recoveryController.setBlocked(recorder.busy() || cameraRecorder.busy() || exporter.busy() ||
      (editorPlayback && editorPlayback->playbackState() == QMediaPlayer::PlayingState));
}

QObject *AppRuntime::root() const {
  return engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().first();
}

bool AppRuntime::load() {
  qmlRegisterType<CanvasPreview>("ScreenPilot.Render", 1, 0, "CanvasPreview");
  qmlRegisterType<CropPreview>("ScreenPilot.Render", 1, 0, "CropPreview");
  QQmlContext *context = engine.rootContext();
  context->setContextProperty("appSettings", &appSettings);
  context->setContextProperty("updateController", &updateController);
  context->setContextProperty("effects", &effects);
  context->setContextProperty("recoveryController", &recoveryController);
  context->setContextProperty("recentProjectsController", &recentProjects);
  context->setContextProperty("scenePresets", &scenePresets);
  context->setContextProperty("memoController", &memoController);
  context->setContextProperty("previewAudioProcessor", &previewAudioProcessor);
  engine.addImageProvider("timeline", timelineMedia.imageProvider());
  context->setContextProperty("timelineMedia", &timelineMedia);
  context->setContextProperty("cameraRecorder", &cameraRecorder);
  context->setContextProperty("recorder", &recorder);
  context->setContextProperty("project", &project);
  context->setContextProperty("exporter", &exporter);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, qApp,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("ScreenPilotApp", "Main");
  QObject *rootWindow = root();
  if (!rootWindow)
    return false;
  if (auto preview = rootWindow->findChild<CanvasPreview *>("canvasPreview")) {
    auto applyQuality = [this, preview] { preview->setPreviewQuality(appSettings.values().value("previewQuality").toString()); };
    applyQuality();
    QObject::connect(&appSettings, &AppSettings::changed, preview, applyQuality);
  }
  if (auto player = rootWindow->findChild<QMediaPlayer *>("editorPlayer")) {
    editorPlayback = player;
    previewAudioProcessor.attach(player);
    QObject::connect(player, &QMediaPlayer::playbackStateChanged, &recoveryController, [this] { updateRecoveryBlock(); });
  }
  auto memoWindow = rootWindow->findChild<QWindow *>("recorderMemoWindow");
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, &memoController, [this, memoWindow] {
    if (memoWindow && memoWindow->isVisible()) memoController.saveWindow(memoWindow);
  });
  // A restart request waits for both recorders to finish tearing down.
  auto restartWhenReady = [this, rootWindow] {
    if (!m_restartPending || recorder.busy() || cameraRecorder.busy())
      return;
    m_restartPending = false;
    if (!rootWindow->property("closeAfterSave").toBool() && cameraRecorder.error().isEmpty())
      QMetaObject::invokeMethod(rootWindow, "beginRecording");
  };
  QObject::connect(&recorder, &RecordingController::restartRequested, rootWindow, [this, rootWindow, restartWhenReady] {
    m_restartPending = true;
    QTimer::singleShot(0, rootWindow, restartWhenReady);
  });
  QObject::connect(&cameraRecorder, &CameraController::changed, rootWindow, restartWhenReady);
  return true;
}
