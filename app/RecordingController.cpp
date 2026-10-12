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
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

RecordingController::RecordingController(QObject *parent)
    : QObject(parent), m_session(new QMediaCaptureSession(this)),
      m_screen(new QScreenCapture(this)), m_window(new QWindowCapture(this)),
      m_audio(new QAudioInput(this)), m_sink(new QVideoSink(this)),
      m_watchdog(new QTimer(this)), m_pointerTimer(new QTimer(this)) {
  m_pointerTimer->setTimerType(Qt::PreciseTimer);
  m_pointerTimer->setInterval(
      33); // Approximately 30 Hz; UI stalls delay samples.
  connect(m_pointerTimer, &QTimer::timeout, this,
          &RecordingController::samplePointer);
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
            beginPointerCapture();
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
  m_pendingPointerSamples.clear();
  m_pointerClock.invalidate();
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
void RecordingController::beginPointerCapture() {
#ifdef Q_OS_WIN
  // Zero is first valid frame / record() request, not an encoder PTS.
  // Monotonic approximation only: compositor-to-media sync is not verified.
  m_pointerWindow = 0;
  const auto &target = m_targets.at(m_selected);
  if (!target.isScreen) {
    // QCapturableWindow has no public native handle. Match only an exact,
    // unique visible title; duplicate/missing titles disable spatial mapping.
    struct Match {
      QString title;
      HWND window = nullptr;
      int count = 0;
    } match{target.window.description()};
    const bool enumerated = EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
          auto &match = *reinterpret_cast<Match *>(data);
          if (!IsWindowVisible(window))
            return TRUE;
          const int length = GetWindowTextLengthW(window);
          if (length <= 0)
            return TRUE;
          QString title(length + 1, QChar());
          const int copied = GetWindowTextW(
              window, reinterpret_cast<LPWSTR>(title.data()), title.size());
          title.resize(copied);
          if (title == match.title) {
            match.window = window;
            ++match.count;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&match));
    if (enumerated && match.count == 1)
      m_pointerWindow = reinterpret_cast<quintptr>(match.window);
  }
  m_pointerClock.start();
  m_leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  samplePointer();
  m_pointerTimer->start();
#endif
}
void RecordingController::samplePointer() {
#ifdef Q_OS_WIN
  if (!m_busy || m_stopRequested || !m_pointerClock.isValid() ||
      m_selected < 0 || m_selected >= m_targets.size())
    return;
  const double seconds = m_pointerClock.nsecsElapsed() / 1.0e9;
  // Polling detects sampled down edges; a full press/release between ticks
  // can be missed, and event-loop stalls increase this blind interval.
  const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  const bool click = down && !m_leftButtonDown;
  m_leftButtonDown = down;
  POINT cursor{};
  if (!GetCursorPos(&cursor))
    return;
  RECT bounds{};
  const auto &target = m_targets.at(m_selected);
  bool valid = false;
  if (target.isScreen && target.screen) {
    // QScreen geometry is logical; matching display settings give physical
    // bounds without multiplying mixed-DPI desktop origins by one DPR.
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const QString name = target.screen->name();
    if (EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(name.utf16()),
                             ENUM_CURRENT_SETTINGS, &mode)) {
      bounds = {mode.dmPosition.x, mode.dmPosition.y,
                mode.dmPosition.x + static_cast<LONG>(mode.dmPelsWidth),
                mode.dmPosition.y + static_cast<LONG>(mode.dmPelsHeight)};
      valid = true;
    }
  } else if (!target.isScreen) {
    // Refresh each tick to follow movement. GetWindowRect includes non-client
    // and invisible resize borders; Qt capture may crop these. Exact alignment
    // needs backend-specific calibration; no pixel error bound is claimed.
    const auto window = reinterpret_cast<HWND>(m_pointerWindow);
    valid = window && IsWindow(window) && GetWindowRect(window, &bounds);
    if (!valid)
      m_pointerWindow = 0; // Do not rematch a disappeared window mid-recording.
  }
  const double width = bounds.right - bounds.left;
  const double height = bounds.bottom - bounds.top;
  const bool usable = valid && width > 0 && height > 0;
  const double x = usable ? (cursor.x - bounds.left) / width : 0.0;
  const double y = usable ? (cursor.y - bounds.top) / height : 0.0;
  m_pendingPointerSamples.append(
      QVariantMap{{"time", seconds},
                  {"x", qBound(0.0, x, 1.0)},
                  {"y", qBound(0.0, y, 1.0)},
                  {"click", click},
                  {"inside", usable && x >= 0 && x < 1 && y >= 0 && y < 1}});
#endif
}
void RecordingController::stopCapture() {
  m_pointerTimer->stop();
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
    m_lastPointerSamples = m_pendingPointerSamples;
    m_status = tr("已保存录制");
  } else {
    if (m_error.isEmpty())
      m_error = tr("录制未产生有效文件。");
    m_status = tr("录制未完成");
    if (file.exists() && file.size() == 0)
      QFile::remove(m_pending);
  }
  m_pendingPointerSamples.clear();
  m_pointerClock.invalidate();
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
