#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QPointer>
#include <memory>
class ProjectController;

class ExportController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QUrl lastExport READ lastExport NOTIFY changed)
  Q_PROPERTY(QVariantMap projectSummary READ projectSummary NOTIFY changed)
public:
  explicit ExportController(QObject *parent = nullptr);
  ~ExportController() override;
  bool busy() const;
  double progress() const;
  QString status() const;
  QString error() const;
  QUrl lastExport() const;
  Q_INVOKABLE QVariantMap performanceMetrics() const;
  void setProject(ProjectController *project);
  QVariantMap projectSummary() const;
  Q_INVOKABLE QVariantMap describeProjectExport(QVariantMap options = {}, double rangeStart = 0, double rangeEnd = -1) const;
  Q_INVOKABLE bool startProject(QUrl destination, QVariantMap options = {}, double rangeStart = 0, double rangeEnd = -1);
  Q_INVOKABLE bool exportProjectOriginal(QUrl destinationFolder);
  Q_INVOKABLE void start(QUrl source, QVariantList clips, QUrl destination,
                         QVariantList framing = {}, QVariantMap canvas = {},
                         QVariantMap options = {}, QUrl cameraSource = {},
                         QVariantMap cameraSettings = {});
  Q_INVOKABLE void startOriginal(QUrl source, QUrl destinationFolder, QUrl cameraSource = {},
                                 QUrl systemAudioSource = {}, QUrl microphoneAudioSource = {});
  Q_INVOKABLE void cancel();
signals:
  void changed();

private:
  struct Private;
  std::unique_ptr<Private> d;
  QPointer<ProjectController> m_project;
};
