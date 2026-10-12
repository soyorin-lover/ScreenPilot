#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVector>
class ProjectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap canvasSettings READ canvasSettings NOTIFY changed)
  Q_PROPERTY(QUrl source READ source NOTIFY changed)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY changed)
  Q_PROPERTY(double duration READ duration NOTIFY changed)
  Q_PROPERTY(
      int selectedClip READ selectedClip WRITE setSelectedClip NOTIFY changed)
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
  Q_PROPERTY(bool hasProject READ hasProject NOTIFY changed)
  Q_PROPERTY(QString title READ title NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QUrl projectLocation READ projectLocation NOTIFY changed)
  Q_PROPERTY(QVariantList pointerSamples READ pointerSamples NOTIFY changed)
  Q_PROPERTY(QVariantList framingSegments READ framingSegments NOTIFY changed)
  Q_PROPERTY(bool autoZoomEnabled READ autoZoomEnabled WRITE setAutoZoomEnabled
                 NOTIFY changed)
public:
  explicit ProjectController(QObject *parent = nullptr);
  QVariantMap canvasSettings() const { return m_canvasSettings; }
  Q_INVOKABLE void setCanvasOption(QString key, QVariant value);
  Q_INVOKABLE bool setBackgroundImage(QUrl);
  Q_INVOKABLE void clearBackgroundImage();
  QUrl source() const { return m_source; }
  QVariantList clips() const;
  double duration() const;
  int selectedClip() const { return m_selected; }
  void setSelectedClip(int);
  bool dirty() const {
    return hasProject() && (m_unsaved || m_clips != m_baseline ||
                            m_autoZoom != m_baselineAutoZoom ||
                            m_canvasSettings != m_baselineCanvasSettings);
  }
  bool canUndo() const { return !m_undo.isEmpty(); }
  bool canRedo() const { return !m_redo.isEmpty(); }
  bool hasProject() const { return !m_clips.isEmpty(); }
  QString title() const;
  QString error() const { return m_error; }
  QString status() const { return m_status; }
  QUrl projectLocation() const { return m_location; }
  QVariantList pointerSamples() const { return m_pointerSamples; }
  QVariantList framingSegments() const {
    return m_autoZoom ? m_framing : QVariantList{};
  }
  bool autoZoomEnabled() const { return m_autoZoom; }
  void setAutoZoomEnabled(bool);
  Q_INVOKABLE QVariantMap framingAt(double outputSeconds) const;
  Q_INVOKABLE bool newRecording(QUrl, double, QVariantList pointerSamples = {});
  Q_INVOKABLE bool split(double);
  Q_INVOKABLE bool trimStart(double);
  Q_INVOKABLE bool trimEnd(double);
  Q_INVOKABLE bool removeSelected();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE double sourceTime(double) const;
  Q_INVOKABLE bool saveAs(QUrl);
  Q_INVOKABLE bool save();
  Q_INVOKABLE bool openProject(QUrl);
  Q_INVOKABLE void discardChanges();
signals:
  void changed();

private:
  struct Clip {
    QString id;
    double start = 0, length = 0, rate = 1;
    bool operator==(const Clip &) const = default;
  };
  using Snapshot = QVector<Clip>;
  Snapshot m_clips, m_baseline;
  struct History {
    Snapshot clips;
    bool autoZoom;
    QVariantMap canvasSettings;
  };
  QVector<History> m_undo, m_redo;
  QVariantMap m_canvasSettings, m_baselineCanvasSettings;
  QVariantList m_pointerSamples, m_framing;
  bool m_autoZoom = false, m_baselineAutoZoom = false;
  QUrl m_source, m_location;
  double m_sourceDuration = 0;
  int m_selected = -1;
  bool m_unsaved = false;
  QString m_error, m_status;
  bool fail(const QString &);
  void commit(const Snapshot &, int);
  bool selectedPoint(double, double &) const;
  bool writeJson(const QString &, const QString &backgroundRelative);
  bool prepareBackground(const QString &, QString &relative, QString &copied);
  void pushHistory();
  void saved(const QString &);
  void useSavedBackground(const QString &);
};
