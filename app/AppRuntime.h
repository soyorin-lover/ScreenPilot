#pragma once
#include "AppSettings.h"
#include "CameraController.h"
#include "EffectController.h"
#include "ExportController.h"
#include "MemoController.h"
#include "PreviewAudioProcessor.h"
#include "ProjectController.h"
#include "RecentProjectsController.h"
#include "RecordingController.h"
#include "RecoveryController.h"
#include "ScenePresetController.h"
#include "TimelineMedia.h"
#include "UpdateController.h"
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

class QMediaPlayer;

// The application object graph: controllers, their cross-wiring and the QML
// engine. Shared by the product entry point and the verification probe so
// both run the same composition.
class AppRuntime {
public:
  struct Options {
    // Probe runs: no update checks or completion notifications.
    bool probe = false;
    // Empty values keep each controller's normal per-user location.
    QString recentProjectsFile, presetStore, recoveryDirectory;
  };
  // Process-wide media and style setup; call before QGuiApplication exists.
  static void prepareProcess();
  explicit AppRuntime(Options options);
  // Loads Main.qml and attaches window-owned objects. False when QML failed.
  bool load();
  QObject *root() const;

  RecordingController recorder;
  ProjectController project;
  ExportController exporter;
  CameraController cameraRecorder;
  TimelineMedia timelineMedia;
  MemoController memoController;
  RecentProjectsController recentProjects;
  ScenePresetController scenePresets;
  PreviewAudioProcessor previewAudioProcessor;
  AppSettings appSettings;
  UpdateController updateController;
  EffectController effects;
  RecoveryController recoveryController;
  QQmlApplicationEngine engine;
  QMediaPlayer *editorPlayback = nullptr;

private:
  void updateRecoveryBlock();
  Options m_options;
  QUrl m_lastRecentLocation, m_notifiedRecording, m_notifiedExport;
  bool m_restartPending = false;
};
