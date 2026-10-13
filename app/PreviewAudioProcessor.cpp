#include "PreviewAudioProcessor.h"
#include <QAudioOutput>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
QAudioFormat decodeFormat() {
  // A concrete layout is required before Qt creates its FFmpeg resampler.
  // Keep decoding independent of asynchronous device enumeration.
  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
  format.setSampleFormat(QAudioFormat::Float);
  return format;
}
} // namespace

PreviewAudioProcessor::PreviewAudioProcessor(QObject *parent)
    : QObject(parent), m_buffers(decodeFormat(), this) {
  m_timer.setInterval(5);
  m_timer.setTimerType(Qt::PreciseTimer);
  connect(&m_timer, &QTimer::timeout, this, &PreviewAudioProcessor::pump);
  connect(&m_buffers, &QAudioBufferOutput::audioBufferReceived, this,
          &PreviewAudioProcessor::receive);
  connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
    const auto device = QMediaDevices::defaultAudioOutput();
    if (m_player && m_player->audioOutput() && !device.isNull()) {
      m_player->audioOutput()->setDevice(device);
      m_player->audioOutput()->setVolume((active() || m_muted) ? 0 : m_volume);
    }
    reset(m_player ? m_player->position() / 1000.0 : 0,
          m_end >= 0 ? m_end / 1000000.0 : -1);
  });
  m_timer.start();
}
PreviewAudioProcessor::~PreviewAudioProcessor() {
  if (m_player && m_player->audioBufferOutput() == &m_buffers)
    m_player->setAudioBufferOutput(nullptr);
}
bool PreviewAudioProcessor::active() const {
  return !m_player.isNull();
}
void PreviewAudioProcessor::setMixSources(QUrl system, QUrl microphone, QVariantMap settings) {
  if (system == m_systemSource && microphone == m_microphoneSource && settings == m_mixSettings) return;
  m_systemSource = system;
  m_microphoneSource = microphone;
  m_mixSettings = settings;
  QString error;
  const bool ok = m_mix.open(system, microphone, settings, &error);
  rateChanged();
  if (!ok) { m_error = error; emit changed(); }
}
void PreviewAudioProcessor::setVolume(double value) {
  if (!std::isfinite(value)) return;
  value = std::clamp(value, 0.0, 1.0);
  if (m_volume == value) return;
  m_volume = value;
  if (m_sink) m_sink->setVolume(value);
  if (m_player && m_player->audioOutput())
    m_player->audioOutput()->setVolume((active() || m_muted) ? 0 : m_volume);
  emit changed();
}
void PreviewAudioProcessor::setMuted(bool value) {
  if (m_muted == value)
    return;
  m_muted = value;
  if (m_player && m_player->audioOutput())
    m_player->audioOutput()->setVolume((active() || m_muted) ? 0 : m_volume);
  if (value)
    reset(m_player ? m_player->position() / 1000.0 : 0,
          m_end >= 0 ? m_end / 1000000.0 : -1);
  emit changed();
}
void PreviewAudioProcessor::attach(QMediaPlayer *player) {
  if (m_player == player) return;
  if (m_player) {
    disconnect(m_player, nullptr, this, nullptr);
    if (m_player->audioOutput()) disconnect(m_player->audioOutput(), nullptr, this, nullptr);
    if (m_player->audioBufferOutput() == &m_buffers)
      m_player->setAudioBufferOutput(nullptr);
  }
  m_player = player;
  if (!player) { reset(); emit changed(); return; }
  // A muted QAudioOutput still initializes Qt's native renderer/resampler.
  // Own PCM output at every speed, including 1x, so an asynchronously empty
  // native device format cannot initialize a second invalid renderer.
  player->setAudioOutput(nullptr);
  player->setAudioBufferOutput(&m_buffers);
  connect(player, &QMediaPlayer::audioOutputChanged, this, [this] {
    if (m_player && m_player->audioOutput()) m_player->setAudioOutput(nullptr);
  });
  connect(player, &QMediaPlayer::playbackRateChanged, this,
          &PreviewAudioProcessor::rateChanged);
  connect(player, &QMediaPlayer::sourceChanged, this, [this] { reset(); });
  connect(player, &QMediaPlayer::playbackStateChanged, this,
          [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState) {
              if (m_mix.hasSources())
                reset(m_player->position() / 1000.0,
                      m_end >= 0 ? m_end / 1000000.0 : -1);
              else { flush(); m_draining = true; }
            } else if (m_sink) {
              if (state == QMediaPlayer::PausedState)
                m_sink->suspend();
              else
                m_sink->resume();
            }
          });
  if (auto audio = player->audioOutput()) {
    connect(audio, &QAudioOutput::volumeChanged, this,
            [this, audio](float value) {
              if ((active() || m_muted) && value != 0)
                audio->setVolume(0);
            });
  }
  rateChanged();
}
void PreviewAudioProcessor::rateChanged() {
  reset(m_player ? m_player->position() / 1000.0 : 0,
          m_end >= 0 ? m_end / 1000000.0 : -1);
  if (m_player && m_player->audioOutput())
    m_player->audioOutput()->setVolume((active() || m_muted) ? 0 : m_volume);
  emit changed();
}
void PreviewAudioProcessor::reset(double sourceStart, double sourceEnd) {
  m_pending.clear();
  m_sink.reset();
  m_device = nullptr;
  m_format = {};
  m_waiting = true;
  m_finished = m_draining = false;
  m_inputFrames = 0;
  m_target = qRound64(std::max(0.0, sourceStart) * 1000000);
  m_nextMixFrame = qRound64(std::max(0.0, sourceStart) * 48000);
  m_end = sourceEnd >= 0 ? qRound64(sourceEnd * 1000000) : -1;
  const bool hadError = !m_error.isEmpty();
  m_error.clear();
  if (hadError) emit changed();
}
void PreviewAudioProcessor::receive(const QAudioBuffer &decoded) {
  // Sidecars use their own clock; never also consume embedded audio.
  if (m_mix.hasSources()) return;
  // Queued decoder callbacks can run in a burst while the GUI timer is late.
  // Feed the device on this path as well; the sink's bytesFree is the bound,
  // and PCM remains in order rather than waiting for another timer event.
  drainOutput();
  consume(decoded);
}
void PreviewAudioProcessor::consume(const QAudioBuffer &buffer) {
  if (!buffer.isValid()) {
    if (active() && !m_muted)
      flush();
    return;
  }
  if (!active() || m_muted || !m_error.isEmpty() || m_finished ||
      m_player->playbackState() != QMediaPlayer::PlayingState)
    return;
  const auto format = buffer.format();
  if (buffer.startTime() < 0 || !format.isValid())
    return;
  const qint64 start = buffer.startTime();
  const qint64 end = start + buffer.duration();
  if (m_waiting && (end <= m_target || start > m_target + 250000))
    return;
  const qint64 begin = m_waiting ? std::max(start, m_target) : start;
  const qint64 finish = m_end >= 0 ? std::min(end, m_end) : end;
  if (finish <= begin)
    return;
  const int first = std::clamp(
      int(std::ceil((begin - start) * format.sampleRate() / 1000000.0)), 0,
      int(buffer.frameCount()));
  const int last = std::clamp(
      int(std::ceil((finish - start) * format.sampleRate() / 1000000.0)), first,
      int(buffer.frameCount()));
  if (last <= first)
    return;
  if (m_format != format) {
    m_stretch.reset(format, m_player->playbackRate());
    auto output = m_stretch.outputFormat();
    const auto device = QMediaDevices::defaultAudioOutput();
    m_sinkFormat = device.preferredFormat();
    if (m_sinkFormat.sampleRate() != output.sampleRate() ||
        m_sinkFormat.channelCount() != output.channelCount())
      m_sinkFormat = output;
    if (!device.isFormatSupported(m_sinkFormat)) {
      m_error = tr("当前音频设备不支持变速预览格式");
      emit changed();
      return;
    }
    m_sink = std::make_unique<QAudioSink>(device, m_sinkFormat);
    m_sink->setVolume(m_volume);
    m_sink->setBufferSize(m_sinkFormat.bytesForDuration(40000));
    m_device = m_sink->start();
    if (!m_device || m_sink->error() != QtAudio::NoError) {
      m_error = tr("无法启动变速预览音频输出");
      m_sink.reset();
      m_device = nullptr;
      emit changed();
      return;
    }
    m_format = format;
  }
  m_waiting = false;
  const QByteArray selected(buffer.constData<char>() +
                                first * format.bytesPerFrame(),
                            (last - first) * format.bytesPerFrame());
  m_inputFrames += last - first;
  const QByteArray pcm =
      m_stretch.process(QAudioBuffer(selected, format, begin));
  if (!pcm.isEmpty()) {
    emit processedAudio(QAudioBuffer(pcm, m_stretch.outputFormat()));
    enqueue(pcm);
  }
  drainOutput();
  if (m_pending.size() > m_sinkFormat.bytesForDuration(1000000)) {
    m_pending.clear();
    m_error = tr("变速预览声音缓冲超限，请暂停后重试");
    emit changed();
  }
  if (m_end >= 0 && finish >= m_end)
    flush();
}
void PreviewAudioProcessor::flush() {
  if (m_finished || m_format.isValid() == false || m_inputFrames <= 0 ||
      !m_player)
    return;
  m_finished = true;
  const auto pcm =
      m_stretch.finish(qRound64(m_inputFrames / m_player->playbackRate()));
  if (!pcm.isEmpty()) {
    emit processedAudio(QAudioBuffer(pcm, m_stretch.outputFormat()));
    enqueue(pcm);
  }
  drainOutput();
}
void PreviewAudioProcessor::enqueue(const QByteArray &pcm) {
  if (m_sinkFormat.sampleFormat() == QAudioFormat::Float) {
    m_pending += pcm;
    return;
  }
  const auto *samples = reinterpret_cast<const float *>(pcm.constData());
  const int count = pcm.size() / int(sizeof(float));
  QByteArray output(count * m_sinkFormat.bytesPerSample(), '\0');
  for (int i = 0; i < count; ++i) {
    const double value = std::clamp(
        std::isfinite(samples[i]) ? double(samples[i]) : 0., -1., 1.);
    if (m_sinkFormat.sampleFormat() == QAudioFormat::Int16) {
      const qint16 sample = qint16(value * 32767);
      std::memcpy(output.data() + i * 2, &sample, 2);
    } else if (m_sinkFormat.sampleFormat() == QAudioFormat::Int32) {
      const qint32 sample = qint32(value * 2147483647);
      std::memcpy(output.data() + i * 4, &sample, 4);
    } else if (m_sinkFormat.sampleFormat() == QAudioFormat::UInt8)
      output[i] = char(qRound((value + 1) * 127.5));
  }
  m_pending += output;
}
void PreviewAudioProcessor::scheduleMix() {
  if (!m_player || !m_mix.hasSources() || m_muted ||
      m_player->playbackState() != QMediaPlayer::PlayingState)
    return;
  const double rate = std::max(1.0, m_player->playbackRate());
  const qint64 position = m_player->position() * 48;
  // Exact clip changes use reset(); recover external seeks and stalled output.
  const qint64 tolerance = qRound64(48000 * .3 * rate);
  if (position > m_nextMixFrame + tolerance || position + tolerance < m_nextMixFrame)
    reset(m_player->position() / 1000.0, m_end >= 0 ? m_end / 1000000.0 : -1);
  if (m_finished || !m_error.isEmpty()) return;
  if (m_sink && m_pending.size() > m_sinkFormat.bytesForDuration(80000)) return;
  qint64 end = position + qRound64(48000 * .06 * rate);
  if (m_player->duration() > 0) end = std::min(end, m_player->duration() * 48);
  if (m_end >= 0) end = std::min(end, m_end * 48000 / 1000000);
  // One bounded read per tick, including extreme playback rates.
  const qint64 count = std::min(qint64(48000), end - m_nextMixFrame);
  if (count <= 0) return;
  QString error;
  const auto pcm = m_mix.readFrames(m_nextMixFrame, count, &error);
  if (!error.isEmpty()) { m_error = error; emit changed(); return; }
  const qint64 start = m_nextMixFrame * 1000000 / 48000;
  m_nextMixFrame += count;
  // Match the device clock before WSOLA, retaining Float PCM throughout.
  const auto format = decodeFormat();
  const qint64 frames = qRound64(count * double(format.sampleRate()) / 48000);
  QByteArray converted(frames * format.bytesPerFrame(), '\0');
  const auto *input = reinterpret_cast<const float *>(pcm.constData());
  auto *samples = reinterpret_cast<float *>(converted.data());
  for (qint64 i = 0; i < frames; ++i) {
    const double position = i * 48000.0 / format.sampleRate();
    const qint64 a = std::min(qint64(position), count - 1);
    const qint64 b = std::min(a + 1, count - 1);
    const double fraction = position - std::floor(position);
    for (int channel = 0; channel < format.channelCount(); ++channel) {
      if (channel > 1) continue;
      const double first = format.channelCount() == 1
          ? (input[a * 2] + input[a * 2 + 1]) * .5 : input[a * 2 + channel];
      const double second = format.channelCount() == 1
          ? (input[b * 2] + input[b * 2 + 1]) * .5 : input[b * 2 + channel];
      samples[i * format.channelCount() + channel] = float(first * (1 - fraction) + second * fraction);
    }
  }
  consume(QAudioBuffer(converted, format, start));
  if ((m_end >= 0 && m_nextMixFrame >= m_end * 48000 / 1000000) ||
      (m_player->duration() > 0 && m_nextMixFrame >= m_player->duration() * 48))
    flush();
}
void PreviewAudioProcessor::pump() {
  drainOutput();
  scheduleMix();
  drainOutput();
}
void PreviewAudioProcessor::drainOutput() {
  if (!m_sink || !m_device || !m_player || m_muted ||
      (m_player->playbackState() != QMediaPlayer::PlayingState &&
       !m_draining) ||
      m_pending.isEmpty())
    return;
  const auto bytes = std::min<qint64>(m_pending.size(), m_sink->bytesFree());
  if (bytes <= 0)
    return;
  const qint64 written = m_device->write(m_pending.constData(), bytes);
  if (written > 0)
    m_pending.remove(0, written);
}
