#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class ExportController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QUrl lastExport READ lastExport NOTIFY changed)
public:
  explicit ExportController(QObject *parent = nullptr);
  ~ExportController() override;
  bool busy() const;
  double progress() const;
  QString status() const;
  QString error() const;
  QUrl lastExport() const;
  Q_INVOKABLE void start(QUrl source, QVariantList clips, QUrl destination,
                         QVariantList framing = {}, QVariantMap canvas = {});
  Q_INVOKABLE void cancel();
signals:
  void changed();

private:
  struct Private;
  std::unique_ptr<Private> d;
};
