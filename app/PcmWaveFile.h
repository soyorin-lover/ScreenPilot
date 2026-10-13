#pragma once
#include <QAudioBuffer>
#include <QFile>

// Canonical sidecar format: little-endian IEEE float, 48 kHz, stereo.
class PcmWaveWriter {
public:
  ~PcmWaveWriter();
  bool open(const QString &path, QString *error = nullptr);
  bool append(const QAudioBuffer &buffer, QString *error = nullptr);
  bool finalize(QString *error = nullptr);
  void discard();
  bool isOpen() const { return m_file.isOpen(); }
private:
  QFile m_file;
  qint64 m_bytes = 0;
  bool m_valid = false;
};
class PcmWaveReader {
public:
  bool open(const QString &path, QString *error = nullptr);
  qint64 frameCount() const { return m_bytes / 8; }
  QByteArray readFrames(qint64 first, qint64 count, QString *error = nullptr);
private:
  QFile m_file;
  qint64 m_offset = 0, m_bytes = 0;
};
