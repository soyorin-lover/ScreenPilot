#include "CameraController.h"
#include <QCamera>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QMediaFormat>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <algorithm>

CameraController::CameraController(QObject *parent) : QObject(parent) {
  QSettings settings;
  m_mirror = settings.value("camera/mirror", true).toBool();
  m_selectedDevice = settings.value("camera/device").toString();
  m_session.setVideoSink(&m_sink);
  m_session.setRecorder(&m_recorder);
  connect(&m_devices, &QMediaDevices::videoInputsChanged, this,
          &CameraController::refreshDevices);
  connect(&m_sink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (m_previewSink)
              m_previewSink->setVideoFrame(frame);
            if (frame.isValid() && m_awaitingFrame && recording()) {
              m_offset = m_anchor.nsecsElapsed() / 1e9;
              m_awaitingFrame = false;
              emit changed();
            }
          });
  connect(
      &m_recorder, &QMediaRecorder::errorOccurred, this,
      [this](QMediaRecorder::Error, const QString &message) { fail(message); });
  connect(&m_recorder, &QMediaRecorder::recorderStateChanged, this,
          [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::RecordingState)
              m_sawRecording = true;
            if (state == QMediaRecorder::StoppedState && m_pending &&
                m_sawRecording) {
              m_pending = false;
              m_awaitingFrame = false;
              if (m_camera && (!m_enabled || !m_previewActive))
                m_camera->stop();
              const QUrl output = m_recorder.actualLocation().isEmpty()
                                      ? m_requestedOutput
                                      : m_recorder.actualLocation();
              if (m_discard) {
                if (!QFile::remove(m_requestedOutput.toLocalFile()))
                  m_error = tr("无法删除本次未保存的摄像头文件。");
              } else if (m_error.isEmpty() &&
                  QFileInfo(output.toLocalFile()).size() > 0) {
                m_lastRecording = output;
                emit recordingFinished(output, m_offset);
              } else if (m_error.isEmpty())
                fail(tr("摄像头录制未产生有效视频。"));
            }
            emit changed();
          });
  refreshDevices();
}
QVariantList CameraController::devices() const {
  QVariantList result;
  for (const auto &d : m_available)
    result.append(QVariantMap{{"id", QString::fromLatin1(d.id().toBase64())},
                              {"description", d.description()}});
  return result;
}
QVariantList CameraController::formats() const {
  QVariantList result;
  if (!m_camera)
    return result;
  for (const auto &f : m_camera->cameraDevice().videoFormats()) {
    result.append(QVariantMap{{"label", QString("%1 × %2 · %3 fps")
                                            .arg(f.resolution().width())
                                            .arg(f.resolution().height())
                                            .arg(f.maxFrameRate(), 0, 'f', 0)},
                              {"width", f.resolution().width()},
                              {"height", f.resolution().height()},
                              {"fps", f.maxFrameRate()}});
  }
  return result;
}
QString CameraController::status() const {
  if (!m_error.isEmpty())
    return m_error;
  if (m_pending)
    return m_recorder.recorderState() == QMediaRecorder::PausedState
               ? tr("摄像头录制已暂停")
               : recording() ? tr("正在录制摄像头") : tr("正在完成摄像头文件");
  if (!m_enabled)
    return tr("摄像头已关闭");
  if (!m_previewActive)
    return tr("摄像头预览已暂停");
  return m_camera && m_camera->isActive() ? tr("摄像头已就绪")
                                          : tr("正在启动摄像头…");
}
void CameraController::setEnabled(bool value) {
  if (m_pending || m_enabled == value)
    return;
  m_enabled = value;
  m_error.clear();
  if (m_camera)
    m_camera->setActive(value && m_previewActive);
  if (value && !m_camera)
    fail(tr("没有可用摄像头。"));
  emit changed();
}
void CameraController::setPreviewActive(bool value) {
  if (m_previewActive == value)
    return;
  m_previewActive = value;
  if (m_camera && !m_pending)
    m_camera->setActive(m_enabled && value);
  emit changed();
}
void CameraController::setMirror(bool value) {
  if (m_mirror == value)
    return;
  m_mirror = value;
  QSettings().setValue("camera/mirror", value);
  emit changed();
}
void CameraController::setSelectedDevice(const QString &value) {
  if (m_pending || m_selectedDevice == value)
    return;
  m_selectedDevice = value;
  m_selectedFormat = -1;
  QSettings().setValue("camera/device", value);
  configureCamera();
}
void CameraController::setSelectedFormat(int value) {
  if (m_pending || !m_camera || value < -1 ||
      value >= m_camera->cameraDevice().videoFormats().size())
    return;
  m_selectedFormat = value;
  const bool active = m_camera->isActive();
  m_camera->stop();
  m_camera->setCameraFormat(
      value < 0 ? QCameraFormat()
                : m_camera->cameraDevice().videoFormats().at(value));
  if (active)
    m_camera->start();
  emit changed();
}
void CameraController::refreshDevices() {
  m_available = QMediaDevices::videoInputs();
  const bool found = std::any_of(
      m_available.cbegin(), m_available.cend(), [this](const auto &d) {
        return QString::fromLatin1(d.id().toBase64()) == m_selectedDevice;
      });
  if (!found) {
    if (m_pending)
      fail(tr("摄像头已断开。"));
    if (m_enabled && m_camera) {
      m_enabled = false;
      m_error = tr("摄像头已断开，请选择设备并重新开启。");
    }
    m_selectedDevice =
        m_available.isEmpty()
            ? QString()
            : QString::fromLatin1(m_available.first().id().toBase64());
    m_selectedFormat = -1;
  }
  if (!m_pending)
    configureCamera();
  emit changed();
}
void CameraController::configureCamera() {
  m_session.setCamera(nullptr);
  delete m_camera;
  m_camera = nullptr;
  for (const auto &device : m_available) {
    if (QString::fromLatin1(device.id().toBase64()) != m_selectedDevice)
      continue;
    m_camera = new QCamera(device, this);
    m_session.setCamera(m_camera);
    connect(m_camera, &QCamera::activeChanged, this,
            &CameraController::changed);
    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &message) { fail(message); });
    if (m_enabled && m_previewActive)
      m_camera->start();
    break;
  }
  emit changed();
}
void CameraController::setPreviewSink(QObject *object) {
  m_previewSink = qobject_cast<QVideoSink *>(object);
  if (m_previewSink)
    m_previewSink->setVideoFrame(m_sink.videoFrame());
  emit changed();
}
bool CameraController::startRecording(const QString &directory) {
  if (m_pending)
    return false;
  m_lastRecording = {};
  m_offset = 0;
  m_error.clear();
  emit changed();
  if (!m_enabled) {
    fail(tr("摄像头已关闭，无法录制。"));
    return false;
  }
  if (!m_camera || !m_camera->isActive() ||
      m_recorder.recorderState() != QMediaRecorder::StoppedState) {
    fail(tr("摄像头尚未就绪，无法录制。"));
    return false;
  }
  if (!QDir().mkpath(directory)) {
    fail(tr("无法创建摄像头录制目录。"));
    return false;
  }
  QMediaFormat format(QMediaFormat::MPEG4);
  format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  if (!format.isSupported(QMediaFormat::Encode)) {
    fail(tr("当前环境不支持 H.264 MP4 摄像头录制。"));
    return false;
  }
  m_error.clear();
  m_lastRecording = {};
  m_offset = 0;
  m_sawRecording = false;
  m_stopping = false;
  m_discard = false;
  m_awaitingFrame = true;
  m_requestedOutput = QUrl::fromLocalFile(QDir(directory).absoluteFilePath(
      "camera-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp4"));
  QFile reservation(m_requestedOutput.toLocalFile());
  if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    fail(tr("无法创建唯一摄像头录制文件。"));
    return false;
  }
  reservation.close();
  m_recorder.setMediaFormat(format);
  m_recorder.setQuality(QMediaRecorder::HighQuality);
  m_recorder.setOutputLocation(m_requestedOutput);
  m_pending = true;
  m_anchor.start();
  m_recorder.record();
  QTimer::singleShot(10000, this, [this, output = m_requestedOutput] {
    if (m_pending && !m_sawRecording && output == m_requestedOutput)
      fail(tr("摄像头录制启动超时。"));
  });
  emit changed();
  return m_pending;
}
void CameraController::stopRecording() {
  if (!m_pending || m_stopping)
    return;
  m_stopping = true;
  if (!m_sawRecording) {
    fail(tr("摄像头录制在启动前已停止。"));
    return;
  }
  const QUrl output = m_requestedOutput;
  QTimer::singleShot(15000, this, [this, output] {
    if (m_pending && m_stopping && output == m_requestedOutput)
      fail(tr("摄像头文件完成超时，未关联未完成的素材。"));
  });
  m_recorder.stop();
  emit changed();
}
void CameraController::setRecordingPaused(bool paused) {
  if (!m_pending || m_stopping)
    return;
  if (paused && recording())
    m_recorder.pause();
  else if (!paused && m_recorder.recorderState() == QMediaRecorder::PausedState)
    m_recorder.record();
  if (m_recorder.recorderState() != (paused ? QMediaRecorder::PausedState
                                          : QMediaRecorder::RecordingState))
    fail(tr("摄像头无法同步暂停或继续录制。"));
}
void CameraController::discardRecording() {
  if (!m_pending || m_stopping)
    return;
  m_discard = true;
  stopRecording();
}
void CameraController::fail(const QString &message) {
  m_error = message;
  const bool wasPending = m_pending;
  m_pending = false;
  m_awaitingFrame = false;
  m_stopping = false;
  if (wasPending)
    m_recorder.stop();
  if (m_camera && (!m_enabled || !m_previewActive))
    m_camera->stop();
  emit recordingFailed(message);
  emit changed();
}
