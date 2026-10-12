#include "RecordingController.h"
#include <QAudioDevice>
#include <QAudioInput>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QScreen>
#include <QScreenCapture>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWindowCapture>

RecordingController::RecordingController(QObject *parent)
    : QObject(parent), m_session(new QMediaCaptureSession(this)),
      m_screen(new QScreenCapture(this)), m_window(new QWindowCapture(this)),
      m_audio(new QAudioInput(this)), m_sink(new QVideoSink(this)),
      m_watchdog(new QTimer(this)) {
  m_directory =
      QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
          .filePath(QStringLiteral("ScreenPilot"));
  m_session->setVideoSink(m_sink);
  m_watchdog->setSingleShot(true);
  createRecorder();
  connect(m_screen, &QScreenCapture::errorOccurred, this,
          [this](QScreenCapture::Error, const QString &message) {
            if (m_busy)
              fail(tr("屏幕采集失败：%1").arg(message));
          });
  connect(m_window, &QWindowCapture::errorOccurred, this,
          [this](QWindowCapture::Error, const QString &message) {
            if (m_busy)
              fail(tr("窗口采集失败：%1").arg(message));
          });
  connect(m_watchdog, &QTimer::timeout, this, [this] {
    if (m_waitingFrame) {
      fail(tr("未收到采集画面，请检查窗口是否可见及系统采集权限。"));
      return;
    }
    if (!m_busy)
      return;
    m_failed = true;
    m_error = tr("编码器未能及时结束，文件可能不完整。请重新录制。");
    // A failed backend must not leave the UI permanently busy.
    m_session->setRecorder(nullptr);
    delete m_recorder;
    m_recorder = nullptr;
    createRecorder();
    finish();
  });
  connect(m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (m_preview)
              m_preview->setVideoFrame(frame);
            if (!m_waitingFrame || !frame.isValid())
              return;
            m_waitingFrame = false;
            m_watchdog->stop();
            QSize size = frame.size();
            if (size.width() > 1920 || size.height() > 1080)
              size.scale(QSize(1920, 1080), Qt::KeepAspectRatio);
            size.setWidth(qMax(2, size.width() & ~1));
            size.setHeight(qMax(2, size.height() & ~1));
            m_recorder->setVideoResolution(size);
            m_recorder->record();
            if (m_busy && !m_recording)
              m_watchdog->start(15000);
          });
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          [this] { refreshSources(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this](QScreen *screen) {
            if (m_busy && m_screen->screen() == screen)
              fail(tr("正在录制的屏幕已断开。"));
            refreshSources();
          });
  refreshSources();
}
RecordingController::~RecordingController() {
  stopCapture();
  m_recorder->stop();
}
QObject *RecordingController::previewSink() const { return m_preview.data(); }
void RecordingController::setPreviewSink(QObject *value) {
  m_preview = qobject_cast<QVideoSink *>(value);
  emit changed();
}
void RecordingController::setSelectedSource(int value) {
  if (!m_busy && value >= 0 && value < m_targets.size() &&
      value != m_selected) {
    m_selected = value;
    emit changed();
  }
}
void RecordingController::setOutputDirectory(const QString &value) {
  const QUrl url(value);
  const QString path = url.isLocalFile() ? url.toLocalFile() : value;
  if (!m_busy && !path.trimmed().isEmpty() && path != m_directory) {
    m_directory = path;
    emit changed();
  }
}
void RecordingController::setMicrophoneEnabled(bool value) {
  if (!m_busy && value != m_microphone) {
    m_microphone = value;
    emit changed();
  }
}
void RecordingController::createRecorder() {
  m_recorder = new QMediaRecorder(this);
  m_session->setRecorder(m_recorder);
  connect(m_recorder, &QMediaRecorder::durationChanged, this,
          [this](qint64 value) {
            m_duration = value;
            emit changed();
          });
  connect(m_recorder, &QMediaRecorder::errorOccurred, this,
          [this](QMediaRecorder::Error, const QString &message) {
            if (m_busy)
              fail(tr("录制失败：%1").arg(message));
          });
  connect(m_recorder, &QMediaRecorder::recorderStateChanged, this,
          [this](QMediaRecorder::RecorderState state) {
            if (!m_busy)
              return;
            m_recording =
                state == QMediaRecorder::RecordingState && !m_stopRequested;
            if (state == QMediaRecorder::RecordingState) {
              m_watchdog->stop();
              m_status = tr("正在录制");
            }
            if (state == QMediaRecorder::StoppedState) {
              finish();
              return;
            }
            emit changed();
          });
}
void RecordingController::refreshSources() {
  if (m_busy)
    return;
  m_sources.clear();
  m_targets.clear();
  for (auto *screen : QGuiApplication::screens()) {
    const int id = m_targets.size();
    m_targets.append({screen, {}, true});
    m_sources.append(QVariantMap{
        {"label", tr("屏幕 %1 · %2").arg(id + 1).arg(screen->name())},
        {"kind", "screen"},
        {"id", QString::number(id)}});
  }
  for (const auto &window : QWindowCapture::capturableWindows()) {
    if (!window.isValid() ||
        window.description().contains(QStringLiteral("ScreenPilot"),
                                      Qt::CaseInsensitive))
      continue;
    const int id = m_targets.size();
    m_targets.append({nullptr, window, false});
    m_sources.append(QVariantMap{{"label", window.description()},
                                 {"kind", "window"},
                                 {"id", QString::number(id)}});
  }
  m_selected = m_targets.isEmpty() ? -1 : 0;
  emit changed();
}
void RecordingController::start() {
  if (m_busy)
    return;
  m_error.clear();
  m_failed = false;
  m_stopRequested = false;
  m_duration = 0;
  if (m_selected < 0 || m_selected >= m_targets.size()) {
    fail(tr("请先选择可用的屏幕或窗口。"));
    return;
  }
  const auto target = m_targets.at(m_selected);
  if ((target.isScreen && !target.screen) ||
      (!target.isScreen && !target.window.isValid())) {
    fail(tr("采集来源已不可用，请刷新来源。"));
    return;
  }
  if (m_microphone && QMediaDevices::audioInputs().isEmpty()) {
    fail(tr("未检测到麦克风。请关闭麦克风选项或连接设备。"));
    return;
  }
  QMediaFormat format(QMediaFormat::MPEG4);
  format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  format.setAudioCodec(m_microphone ? QMediaFormat::AudioCodec::AAC
                                    : QMediaFormat::AudioCodec::Unspecified);
  if (!format.isSupported(QMediaFormat::Encode)) {
    fail(tr("当前媒体后端不支持所选 MP4/H.264 编码配置。"));
    return;
  }
  QDir dir(m_directory);
  if (!QDir().mkpath(dir.absolutePath()) ||
      !QFileInfo(dir.absolutePath()).isDir()) {
    fail(tr("无法创建输出目录。"));
    return;
  }
  m_pending = dir.absoluteFilePath(
      QStringLiteral("ScreenPilot-%1-%2.mp4")
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"),
               QUuid::createUuid().toString(QUuid::WithoutBraces)));
  // Reserve a unique destination atomically; never truncate an existing
  // recording.
  QFile reservation(m_pending);
  if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    fail(tr("输出目录不可写，或目标文件已存在。"));
    return;
  }
  reservation.close();
  m_recorder->setMediaFormat(format);
  m_recorder->setVideoFrameRate(30);
  m_recorder->setQuality(QMediaRecorder::HighQuality);
  m_recorder->setOutputLocation(QUrl::fromLocalFile(m_pending));
  m_session->setAudioInput(m_microphone ? m_audio : nullptr);
  if (m_microphone)
    m_audio->setDevice(QMediaDevices::defaultAudioInput());
  m_session->setScreenCapture(target.isScreen ? m_screen : nullptr);
  m_session->setWindowCapture(target.isScreen ? nullptr : m_window);
  m_busy = true;
  m_waitingFrame = true;
  m_status = tr("正在启动采集…");
  emit changed();
  m_watchdog->start(10000);
  if (target.isScreen) {
    m_screen->setScreen(target.screen);
    m_screen->start();
  } else {
    m_window->setWindow(target.window);
    m_window->start();
  }
}
void RecordingController::stopCapture() {
  m_screen->stop();
  m_window->stop();
}
void RecordingController::stop() {
  if (!m_busy || m_stopRequested)
    return;
  m_stopRequested = true;
  m_recording = false;
  m_status = tr("正在完成文件…");
  if (m_waitingFrame) {
    m_waitingFrame = false;
    finish();
    return;
  }
  m_watchdog->start(15000);
  m_recorder->stop();
  stopCapture();
  if (m_busy && m_recorder->recorderState() == QMediaRecorder::StoppedState)
    finish();
  emit changed();
}
void RecordingController::fail(const QString &message) {
  m_error = message;
  m_failed = true;
  if (m_busy) {
    m_waitingFrame = false;
    m_stopRequested = true;
    m_recording = false;
    m_watchdog->start(15000);
    m_recorder->stop();
    stopCapture();
    if (m_busy && m_recorder->recorderState() == QMediaRecorder::StoppedState)
      finish();
  } else {
    m_status = tr("无法录制");
    emit changed();
  }
}
void RecordingController::finish() {
  m_watchdog->stop();
  m_waitingFrame = false;
  stopCapture();
  m_session->setAudioInput(nullptr);
  const QFileInfo file(m_pending);
  if (!m_failed && file.exists() && file.size() > 0 &&
      m_recorder->recorderState() == QMediaRecorder::StoppedState) {
    m_last = QUrl::fromLocalFile(file.absoluteFilePath());
    m_status = tr("已保存录制");
  } else {
    if (m_error.isEmpty())
      m_error = tr("录制未产生有效文件。");
    m_status = tr("录制未完成");
    if (file.exists() && file.size() == 0)
      QFile::remove(m_pending);
  }
  m_pending.clear();
  m_busy = false;
  m_recording = false;
  emit changed();
}
void RecordingController::openOutputDirectory() {
  if (!QDesktopServices::openUrl(
          QUrl::fromLocalFile(QDir(m_directory).absolutePath()))) {
    m_error = tr("无法打开输出目录。");
    emit changed();
  }
}
void RecordingController::openLastRecording() {
  if (m_last.isEmpty() || !QDesktopServices::openUrl(m_last)) {
    m_error = tr("无法打开上次录制。");
    emit changed();
  }
}
