#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
class ProjectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap framingSettings READ framingSettings NOTIFY changed)
  Q_PROPERTY(QVariantMap canvasSettings READ canvasSettings NOTIFY changed)
  Q_PROPERTY(QVariantMap exportSettings READ exportSettings NOTIFY changed)
  Q_PROPERTY(QVariantList recordingMarkers READ recordingMarkers NOTIFY changed)
  Q_PROPERTY(QVariantList timelineMarkers READ timelineMarkers NOTIFY changed)
  Q_PROPERTY(QUrl source READ source NOTIFY changed)
  Q_PROPERTY(double sourceDuration READ sourceDuration NOTIFY changed)
  Q_PROPERTY(QUrl systemAudioSource READ systemAudioSource NOTIFY changed)
  Q_PROPERTY(QUrl microphoneAudioSource READ microphoneAudioSource NOTIFY changed)
  Q_PROPERTY(QVariantMap audioSettings READ audioSettings NOTIFY changed)
  Q_PROPERTY(QVariantList selectedClips READ selectedClips NOTIFY changed)
  Q_PROPERTY(QUrl cameraSource READ cameraSource NOTIFY changed)
  Q_PROPERTY(QVariantMap cameraSettings READ cameraSettings NOTIFY changed)
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
  Q_PROPERTY(QVariantList manualFramingSegments READ manualFramingSegments NOTIFY changed)
  Q_PROPERTY(QString selectedManualFraming READ selectedManualFraming WRITE setSelectedManualFraming NOTIFY changed)
  Q_PROPERTY(bool hasManualClipboard READ hasManualClipboard NOTIFY changed)
  Q_PROPERTY(bool autoZoomEnabled READ autoZoomEnabled WRITE setAutoZoomEnabled
                 NOTIFY changed)
public:
  explicit ProjectController(QObject *parent = nullptr);
  QVariantMap framingSettings() const { return m_framingSettings; }
  Q_INVOKABLE void setFramingOption(QString key, QVariant value);
  QVariantMap canvasSettings() const { return m_canvasSettings; }
  Q_INVOKABLE void setCanvasOption(QString key, QVariant value);
  Q_INVOKABLE bool setBackgroundImage(QUrl);
  Q_INVOKABLE void clearBackgroundImage();
  Q_INVOKABLE bool setBackgroundVideo(QUrl);
  Q_INVOKABLE void clearBackgroundVideo();
  QVariantMap exportSettings() const { return m_exportSettings; }
  Q_INVOKABLE void setExportOption(QString key, QVariant value);
  QVariantList recordingMarkers() const { return m_markers; }
  QVariantList timelineMarkers() const;
  Q_INVOKABLE bool removeRecordingMarker(QString id);
  Q_INVOKABLE double markerTime(QString id) const;
  QUrl source() const { return m_source; }
  double sourceDuration() const { return m_sourceDuration; }
  QUrl cameraSource() const { return m_cameraSource; }
  QVariantMap cameraSettings() const { return m_cameraSettings; }
  Q_INVOKABLE bool setCameraMedia(QUrl source, double offset = 0,
                                  bool mirror = true);
  Q_INVOKABLE void setCameraOption(QString key, QVariant value);
  QUrl systemAudioSource() const { return m_systemAudioSource; }
  QUrl microphoneAudioSource() const { return m_microphoneAudioSource; }
  QVariantMap audioSettings() const { return m_audioSettings; }
  Q_INVOKABLE bool setAudioMedia(QUrl system, QUrl microphone);
  Q_INVOKABLE void setAudioOption(QString key, QVariant value);
  QVariantList selectedClips() const;
  Q_INVOKABLE void selectClip(int index, bool extend = false, bool toggle = false);
  Q_INVOKABLE bool removeSelectedClips();
  Q_INVOKABLE bool setSelectedClipsRate(double rate);
  Q_INVOKABLE bool trimClipBoundary(int index, double sourceStart, double sourceEnd);
  Q_INVOKABLE bool restoreClipHead(int index);
  Q_INVOKABLE bool restoreClipTail(int index);
  Q_INVOKABLE bool mergeAdjacent(int index);
  Q_INVOKABLE QVariantList clipsInRange(double start, double end) const;
  Q_INVOKABLE bool saveRecovery(QUrl directory);
  Q_INVOKABLE bool renameProject(QString name);
  Q_INVOKABLE QString addSticker(QUrl image, double sourceStart, double duration);
  Q_INVOKABLE bool updateSticker(QString id, QVariantMap changes);
  Q_INVOKABLE bool removeSticker(QString id);
  QVariantList clips() const;
  double duration() const;
  int selectedClip() const { return m_selected; }
  void setSelectedClip(int);
  bool dirty() const {
    return hasProject() && (m_unsaved || m_clips != m_baseline ||
                            m_autoZoom != m_baselineAutoZoom ||
                            m_canvasSettings != m_baselineCanvasSettings ||
                            m_framingSettings != m_baselineFramingSettings ||
                            m_cameraSettings != m_baselineCameraSettings ||
                            m_markers != m_baselineMarkers ||
                            m_exportSettings != m_baselineExportSettings ||
                            m_manualFraming != m_baselineManualFraming ||
                            m_audioSettings != m_baselineAudioSettings);
  }
  bool canUndo() const { return !m_undo.isEmpty(); }
  bool canRedo() const { return !m_redo.isEmpty(); }
  bool hasProject() const { return !m_clips.isEmpty(); }
  QString title() const;
  QString error() const { return m_error; }
  QString status() const { return m_status; }
  QUrl projectLocation() const { return m_location; }
  QVariantList pointerSamples() const { return m_pointerSamples; }
  QVariantList framingSegments() const;
  QVariantList manualFramingSegments() const { return m_manualFraming; }
  QString selectedManualFraming() const { return m_selectedManual; }
  void setSelectedManualFraming(QString id);
  Q_INVOKABLE QString addManualFraming(double outputSeconds);
  Q_INVOKABLE bool updateManualFraming(QString id, QVariantMap changes);
  Q_INVOKABLE bool removeManualFraming(QString id);
  Q_INVOKABLE bool splitManualFraming(double outputSeconds);
  Q_INVOKABLE bool copyManualFraming();
  Q_INVOKABLE QString pasteManualFraming(double outputSeconds);
  Q_INVOKABLE bool pasteManualFramingProperties();
  bool hasManualClipboard() const { return !m_manualClipboard.isEmpty(); }
  Q_INVOKABLE QVariantMap sceneSettings() const;
  Q_INVOKABLE bool applySceneSettings(QVariantMap settings);
  Q_INVOKABLE bool setCanvasCrop(double x, double y, double width, double height);
  bool autoZoomEnabled() const { return m_autoZoom; }
  void setAutoZoomEnabled(bool);
  Q_INVOKABLE QVariantMap framingAt(double outputSeconds) const;
  Q_INVOKABLE bool newRecording(QUrl, double, QVariantList pointerSamples = {},
                                 QVariantList recordingMarkers = {});
  Q_INVOKABLE bool split(double);
  Q_INVOKABLE bool trimStart(double);
  Q_INVOKABLE bool trimEnd(double);
  Q_INVOKABLE bool removeSelected();
  Q_INVOKABLE bool moveClip(int from, int to);
  Q_INVOKABLE bool setClipRate(int index, double rate);
  Q_INVOKABLE bool setClipOutputDuration(int index, double seconds);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE double sourceTime(double) const;
  Q_INVOKABLE QVariantMap editingState(double outputSeconds) const;
  Q_INVOKABLE double cameraTimeAt(double outputSeconds) const;
  Q_INVOKABLE double cameraRateAt(double outputSeconds) const;
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
    QVariantMap framingSettings;
    QVariantMap cameraSettings;
    QVariantList markers;
    QVariantMap exportSettings;
    QVariantList manualFraming;
    QVariantMap audioSettings;
  };
  QVector<History> m_undo, m_redo;
  QVariantMap m_canvasSettings, m_baselineCanvasSettings;
  QVariantMap m_framingSettings{
      {"scale", 1.6}, {"transition", .7}, {"follow", true}, {"spring", true}};
  QVariantMap m_baselineFramingSettings = m_framingSettings;
  QVariantList m_pointerSamples, m_framing;
  QVariantList m_manualFraming, m_baselineManualFraming;
  QString m_selectedManual;
  QVariantMap m_manualClipboard;
  QVariantList m_markers, m_baselineMarkers;
  QVariantMap m_exportSettings{{"resolution", "source"}, {"fps", 60}};
  QVariantMap m_baselineExportSettings = m_exportSettings;
  bool m_autoZoom = false, m_baselineAutoZoom = false;
  QUrl m_source, m_location, m_cameraSource;
  QVariantMap m_cameraSettings, m_baselineCameraSettings;
  QUrl m_systemAudioSource, m_microphoneAudioSource;
  QVariantMap m_audioSettings{{"systemVolume", .82}, {"microphoneVolume", .9},
                            {"systemMuted", false}, {"microphoneMuted", false}};
  QVariantMap m_baselineAudioSettings = m_audioSettings;
  QSet<QString> m_selectedIds;
  QTemporaryDir m_stickerCache;
  bool prepareStickerAssets(const QString &, QVariantList &, QStringList &);
  void useSavedStickers(const QString &);
  bool copyAudio(const QString &directory, QStringList &copied);
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
  void reconcileManualSelection();
  void saved(const QString &);
  void useSavedBackground(const QString &);
};
