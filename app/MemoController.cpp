#include "MemoController.h"
#include "CaptureExclusion.h"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
double bounded(double value, double low, double high, double fallback) {
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
}
MemoController::MemoController(QObject *parent) : QObject(parent), m_settings(std::make_unique<QSettings>()) { load(); }
MemoController::MemoController(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settings(std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)) { load(); }
void MemoController::load() {
  m_text = m_settings->value("recorder/memo/text").toString();
  m_fontSize = bounded(m_settings->value("recorder/memo/fontSize", 22).toDouble(),16,40,22);
  m_opacity = bounded(m_settings->value("recorder/memo/opacity", .92).toDouble(),.55,1,.92);
  m_editing = m_text.isEmpty();
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(300);
  connect(&m_saveTimer, &QTimer::timeout, this, &MemoController::save);
  connect(qApp, &QCoreApplication::aboutToQuit, this, &MemoController::save);
}
MemoController::~MemoController() { save(); }
void MemoController::setText(const QString &value) {
  if (m_text == value) return;
  m_text = value; emit textChanged(); m_saveTimer.start();
}
void MemoController::setFontSize(double value) {
  value = bounded(value,16,40,22);
  if (m_fontSize == value) return;
  m_fontSize = value; emit fontSizeChanged(); m_saveTimer.start();
}
void MemoController::setBackgroundOpacity(double value) {
  value = bounded(value,.55,1,.92);
  if (m_opacity == value) return;
  m_opacity = value; emit backgroundOpacityChanged(); m_saveTimer.start();
}
void MemoController::setEditing(bool value) {
  if (m_editing == value) return;
  m_editing = value; emit editingChanged(); save();
}
void MemoController::save() {
  m_saveTimer.stop();
  if (m_window) m_settings->setValue("recorder/memo/geometry", m_window->geometry());
  m_settings->setValue("recorder/memo/text",m_text);
  m_settings->setValue("recorder/memo/fontSize",m_fontSize);
  m_settings->setValue("recorder/memo/opacity",m_opacity);
  m_settings->sync();
}
QString MemoController::captureNotice() const {
  return m_captureExcluded
      ? QStringLiteral("已请求 Windows 捕获排除；不保证所有第三方录屏工具遵守。")
      : QStringLiteral("此系统未确认捕获排除，备忘录可能出现在录制中。");
}
void MemoController::prepareWindow(QWindow *window) {
  if (!window) return;
  if (m_window != window) {
    m_window = window;
    const auto scheduleGeometry = [this] { m_saveTimer.start(); };
    connect(window, &QWindow::xChanged, this, scheduleGeometry);
    connect(window, &QWindow::yChanged, this, scheduleGeometry);
    connect(window, &QWindow::widthChanged, this, scheduleGeometry);
    connect(window, &QWindow::heightChanged, this, scheduleGeometry);
  }
  QRect proposed = m_settings->value("recorder/memo/geometry",QRect(100,100,390,420)).toRect();
  QScreen *screen = QGuiApplication::screenAt(proposed.center());
  if (!screen) screen = QGuiApplication::primaryScreen();
  if (screen) {
    const QRect safe = screen->availableGeometry().adjusted(12,12,-12,-12);
    const int width = std::clamp(proposed.width(),std::min(300,safe.width()),std::min(760,safe.width()));
    const int height = std::clamp(proposed.height(),std::min(280,safe.height()),std::min(1000,safe.height()));
    proposed = QRect(std::clamp(proposed.x(),safe.left(),safe.right()-width+1),
                     std::clamp(proposed.y(),safe.top(),safe.bottom()-height+1),width,height);
    window->setGeometry(proposed);
  }
  m_captureExcluded = excludeWindowFromCapture(window);
  emit captureExclusionChanged();
}
void MemoController::saveWindow(QWindow *window) {
  if (window) m_settings->setValue("recorder/memo/geometry",window->geometry());
  save();
}


