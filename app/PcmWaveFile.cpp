#include "PcmWaveFile.h"
#include <QDataStream>
#include <QtEndian>
#include <limits>
#include <algorithm>
namespace {
bool reject(QString *error, const QString &message) {
  if (error) *error = message;
  return false;
}
QByteArray header(quint32 bytes) {
  QByteArray result;
  QDataStream stream(&result, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4);
  stream << quint32(bytes + 48);
  stream.writeRawData("WAVEfmt ", 8);
  stream << quint32(16) << quint16(3) << quint16(2) << quint32(48000)
         << quint32(384000) << quint16(8) << quint16(32);
  stream.writeRawData("fact", 4);
  stream << quint32(4) << quint32(bytes / 8);
  stream.writeRawData("data", 4);
  stream << bytes;
  return result;
}
}
PcmWaveWriter::~PcmWaveWriter() { if (m_file.isOpen()) discard(); }
bool PcmWaveWriter::open(const QString &path, QString *error) {
  if (m_file.isOpen()) discard();
  m_file.setFileName(path);
  m_bytes = 0;
  m_valid = false;
  if (!m_file.open(QIODevice::ReadWrite | QIODevice::NewOnly))
    return reject(error, m_file.errorString());
  const auto initial = header(0);
  if (m_file.write(initial) != initial.size()) {
    const QString message = m_file.errorString();
    discard();
    return reject(error, message);
  }
  m_valid = true;
  return true;
}
bool PcmWaveWriter::append(const QAudioBuffer &buffer, QString *error) {
  const auto format = buffer.format();
  if (!m_file.isOpen() || !m_valid || format.sampleRate() != 48000 ||
      format.channelCount() != 2 || format.sampleFormat() != QAudioFormat::Float ||
      buffer.byteCount() % 8)
    return reject(error, QStringLiteral("Invalid float48k stereo audio buffer."));
  if (buffer.byteCount() > qint64(std::numeric_limits<quint32>::max()) - 48 - m_bytes) {
    m_valid = false;
    return reject(error, QStringLiteral("Audio sidecar exceeds the 4 GiB WAV limit."));
  }
  QByteArray bytes(buffer.constData<char>(), buffer.byteCount());
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
  for (qsizetype i = 0; i < bytes.size(); i += 4)
    std::reverse(bytes.begin() + i, bytes.begin() + i + 4);
#endif
  if (m_file.write(bytes) != bytes.size()) {
    m_valid = false;
    return reject(error, m_file.errorString());
  }
  m_bytes += bytes.size();
  return true;
}
bool PcmWaveWriter::finalize(QString *error) {
  if (!m_file.isOpen()) return reject(error, QStringLiteral("Audio sidecar is not open."));
  const auto finalHeader = header(quint32(m_bytes));
  const bool ok = m_valid && m_bytes > 0 && m_file.seek(0) &&
                 m_file.write(finalHeader) == finalHeader.size() && m_file.flush();
  const QString message = m_file.errorString();
  m_file.close();
  if (!ok) {
    m_file.remove();
    return reject(error, QStringLiteral("Could not finalize audio sidecar: %1").arg(message));
  }
  return true;
}
void PcmWaveWriter::discard() {
  if (m_file.isOpen()) { m_file.close(); m_file.remove(); }
  m_valid = false;
}
bool PcmWaveReader::open(const QString &path, QString *error) {
  m_file.close(); m_offset = m_bytes = 0;
  m_file.setFileName(path);
  if (!m_file.open(QIODevice::ReadOnly)) return reject(error, m_file.errorString());
  auto invalid = [&] { m_file.close(); return reject(error, QStringLiteral("Invalid or unsupported float48k stereo WAV sidecar.")); };
  const QByteArray top = m_file.read(12);
  if (top.size() != 12 || top.left(4) != "RIFF" || top.mid(8,4) != "WAVE") return invalid();
  const qint64 end = qint64(qFromLittleEndian<quint32>(top.constData() + 4)) + 8;
  if (end > m_file.size() || end < 12) return invalid();
  bool format = false, data = false;
  while (m_file.pos() + 8 <= end) {
    const QByteArray chunk = m_file.read(8);
    const quint32 size = qFromLittleEndian<quint32>(chunk.constData() + 4);
    const qint64 start = m_file.pos();
    if (qint64(size) > end - start) return invalid();
    if (chunk.left(4) == "fmt ") {
      if (size < 16) return invalid();
      const auto f = m_file.read(16);
      format = qFromLittleEndian<quint16>(f.constData()) == 3 &&
        qFromLittleEndian<quint16>(f.constData()+2) == 2 &&
        qFromLittleEndian<quint32>(f.constData()+4) == 48000 &&
        qFromLittleEndian<quint32>(f.constData()+8) == 384000 &&
        qFromLittleEndian<quint16>(f.constData()+12) == 8 &&
        qFromLittleEndian<quint16>(f.constData()+14) == 32;
    } else if (chunk.left(4) == "data") {
      if (data || size % 8) return invalid();
      m_offset = start; m_bytes = size; data = true;
    }
    if (!m_file.seek(start + size + (size & 1))) return invalid();
  }
  if (!format || !data || !m_bytes) return invalid();
  return true;
}
QByteArray PcmWaveReader::readFrames(qint64 first, qint64 count, QString *error) {
  if (!m_file.isOpen() || first < 0 || count < 0 || first > frameCount()) {
    reject(error, QStringLiteral("Invalid WAV read range.")); return {};
  }
  count = qMin(count, frameCount() - first);
  if (!m_file.seek(m_offset + first * 8)) { reject(error, m_file.errorString()); return {}; }
  QByteArray result = m_file.read(count * 8);
  if (result.size() != count * 8) { reject(error, QStringLiteral("Truncated WAV audio data.")); return {}; }
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
  for (qsizetype i = 0; i < result.size(); i += 4)
    std::reverse(result.begin() + i, result.begin() + i + 4);
#endif
  return result;
}
