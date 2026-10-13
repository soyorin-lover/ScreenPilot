#pragma once
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QWindow>
#include <memory>

class MemoController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
  Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
  Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)
  Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY editingChanged)
  Q_PROPERTY(bool captureExcluded READ captureExcluded NOTIFY captureExclusionChanged)
  Q_PROPERTY(QString captureNotice READ captureNotice NOTIFY captureExclusionChanged)
public:
  explicit MemoController(QObject *parent = nullptr);
  explicit MemoController(const QString &settingsFile, QObject *parent = nullptr);
  ~MemoController() override;
  QString text() const { return m_text; }
  double fontSize() const { return m_fontSize; }
  double backgroundOpacity() const { return m_opacity; }
  bool editing() const { return m_editing; }
  bool captureExcluded() const { return m_captureExcluded; }
  QString captureNotice() const;
  void setText(const QString &value);
  void setFontSize(double value);
  void setBackgroundOpacity(double value);
  void setEditing(bool value);
  Q_INVOKABLE void prepareWindow(QWindow *window);
  Q_INVOKABLE void saveWindow(QWindow *window);
  Q_INVOKABLE void save();
signals:
  void textChanged();
  void fontSizeChanged();
  void backgroundOpacityChanged();
  void editingChanged();
  void captureExclusionChanged();
private:
  std::unique_ptr<QSettings> m_settings;
  void load();
  QTimer m_saveTimer;
  QPointer<QWindow> m_window;
  QString m_text;
  double m_fontSize = 22;
  double m_opacity = .92;
  bool m_editing = true;
  bool m_captureExcluded = false;
};


