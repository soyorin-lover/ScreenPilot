#include "WindowsAudioCapture.h"
#include "AudioPacketTimeline.h"
#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
// clang-format off
#include <windows.h>
#include <propidl.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <wrl/implements.h>
// clang-format on
using Microsoft::WRL::ComPtr;
namespace {
constexpr int rate = 48000, channels = 2, block = 480;
QString failure(HRESULT result) {
  return QStringLiteral("Windows 音频采集失败 (0x%1)")
      .arg(quint32(result), 8, 16, QLatin1Char('0'));
}
qint64 clock100ns() {
  LARGE_INTEGER count, frequency;
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&frequency);
  return qint64(double(count.QuadPart) * 10000000.0 / frequency.QuadPart);
}

// The asynchronous operation retains its agile callback, including when the
// bounded startup wait expires. No callback references the capture QObject.
class ProcessActivation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
  HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HRESULT result = E_PENDING;
  ComPtr<IAudioClient> client;
  AUDIOCLIENT_ACTIVATION_PARAMS activation{};
  PROPVARIANT parameters{};
  ~ProcessActivation() { if (completed) CloseHandle(completed); }
  HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation) override {
    HRESULT activated = E_FAIL;
    ComPtr<IUnknown> unknown;
    result = operation->GetActivateResult(&activated, &unknown);
    if (SUCCEEDED(result)) result = activated;
    if (SUCCEEDED(result)) result = unknown.As(&client);
    SetEvent(completed);
    return S_OK;
  }
};
bool supportsProcessLoopback() {
  using GetVersion = LONG (WINAPI *)(OSVERSIONINFOW *);
  const auto getVersion = reinterpret_cast<GetVersion>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  return getVersion && getVersion(&version) == 0 && version.dwBuildNumber >= 20348;
}
HRESULT activateProcessLoopback(quint32 processId, ComPtr<IAudioClient> &client) {
  auto activation = Microsoft::WRL::Make<ProcessActivation>();
  if (!activation) return E_OUTOFMEMORY;
  if (!activation->completed) return HRESULT_FROM_WIN32(GetLastError());
  activation->activation.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  activation->activation.ProcessLoopbackParams.TargetProcessId = processId;
  activation->activation.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
  activation->parameters.vt = VT_BLOB;
  activation->parameters.blob.cbSize = sizeof(activation->activation);
  activation->parameters.blob.pBlobData = reinterpret_cast<BYTE *>(&activation->activation);
  ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
  const HRESULT requested = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
      __uuidof(IAudioClient), &activation->parameters, activation.Get(), &operation);
  if (FAILED(requested)) return requested;
  const DWORD waited = WaitForSingleObject(activation->completed, 3000);
  if (waited != WAIT_OBJECT_0)
    return waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError());
  if (SUCCEEDED(activation->result)) client = activation->client;
  return activation->result;
}
} // namespace
struct WindowsAudioCapture::Private {
  struct Stream {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    static constexpr qint64 capacity = rate * 2;
    std::vector<std::array<float, channels>> samples =
        std::vector<std::array<float, channels>>(capacity);
    std::vector<qint64> tags = std::vector<qint64>(capacity, -1);
    AudioPacketTimeline timeline;
    bool microphone = false;
    bool processLoopback = false;
    quint32 deviceRate = rate;
    qint64 previousEnd = -1;
    quint64 previousDeviceEnd = 0;
  };
  WindowsAudioCapture *q;
  QTimer timer;
  QList<std::shared_ptr<Stream>> streams;
  qint64 epoch = 0, next = 0;
  bool com = false;
  explicit Private(WindowsAudioCapture *owner) : q(owner) {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com = SUCCEEDED(result);
    timer.setInterval(10);
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, q, [this] { tick(); });
  }
  ~Private() {
    if (com)
      CoUninitialize();
  }
  void tick(bool flush = false) {
    for (auto &stream : streams) {
      UINT32 count = 0;
      HRESULT result = stream->capture->GetNextPacketSize(&count);
      while (SUCCEEDED(result) && count) {
        BYTE *data = nullptr;
        DWORD flags = 0;
        UINT64 position = 0, timestamp = 0;
        result = stream->capture->GetBuffer(&data, &count, &flags, &position,
                                            &timestamp);
        if (FAILED(result))
          break;
        // WASAPI QPC timestamps use 100ns units, including on shared capture.
        qint64 first =
            qRound64(double(qint64(timestamp) - epoch) * rate / 10000000.0);
        const bool timestampError = !timestamp ||
            (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR);
        if (timestampError)
          first = qRound64(double(clock100ns() - epoch) * rate / 10000000.0) -
                  count;
        if (stream->processLoopback) {
          // Virtual process capture can report a constant device position.
          // Anchor with QPC, then advance by delivered PCM until discontinuity.
          if (stream->previousEnd >= 0 && !(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY))
            first = stream->previousEnd;
        } else {
          first = stream->timeline.place(first, position, count,
              timestampError, stream->deviceRate);
        }
        if (qEnvironmentVariableIntValue("SCREENPILOT_AUDIO_DIAGNOSTIC") == 1 &&
            stream->previousEnd >= 0)
          qInfo() << "AudioPacketDelta" << stream->microphone
                  << (first - stream->previousEnd)
                  << (qint64(position) - qint64(stream->previousDeviceEnd))
                  << flags << "pcmFrames" << count << "deviceRate" << stream->deviceRate;
        stream->previousEnd = first + count;
        stream->previousDeviceEnd = position + count;
        const float *input = reinterpret_cast<const float *>(data);
        for (UINT32 frame = 0; frame < count; ++frame) {
          const qint64 target = first + frame;
          if (target < next || target > next + rate * 2)
            continue;
          const auto slot = size_t(target % Stream::capacity);
          stream->tags[slot] = target;
          for (int channel = 0; channel < channels; ++channel)
            stream->samples[slot][channel] =
                (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    ? 0.0f
                    : input[frame * channels + channel];
        }
        stream->capture->ReleaseBuffer(count);
        result = stream->capture->GetNextPacketSize(&count);
      }
      if (FAILED(result)) {
        timer.stop();
        emit q->failed(failure(result));
        return;
      }
    }
    // A bounded 60ms packet arrival allowance; emitted PTS still starts at
    // zero.
    const qint64 due =
        (clock100ns() - epoch) * rate / 10000000 - (flush ? 0 : 2880);
    int batches = 0;
    while (next + block <= due && batches++ < 20) {
      QByteArray bytes(block * channels * int(sizeof(float)), '\0');
      QByteArray systemBytes(bytes.size(), '\0'), microphoneBytes(bytes.size(), '\0');
      float *systemOutput = reinterpret_cast<float *>(systemBytes.data());
      float *microphoneOutput = reinterpret_cast<float *>(microphoneBytes.data());
      float *output = reinterpret_cast<float *>(bytes.data());
      double peak = 0;
      for (int frame = 0; frame < block; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
          const qint64 position = next + frame;
          const auto slot = size_t(position % Stream::capacity);
          float mixed = 0;
          for (auto &stream : streams) {
            const float sample = stream->tags[slot] == position
                                     ? stream->samples[slot][channel]
                                     : 0.0f;
            mixed += sample;
            (stream->microphone ? microphoneOutput : systemOutput)[frame * channels + channel] = sample;
            if (stream->microphone)
              peak = std::max(peak, double(std::abs(sample)));
          }
          output[frame * channels + channel] = std::clamp(mixed, -1.0f, 1.0f);
        }
      }
      for (const auto &stream : streams) {
        const QAudioBuffer source(stream->microphone ? microphoneBytes : systemBytes, WindowsAudioCapture::format(), next * 1000000 / rate);
        if (stream->microphone) emit q->microphoneBufferReady(source);
        else emit q->systemBufferReady(source);
      }
      emit q->levelChanged(std::min(peak, 1.0));
      emit q->bufferReady(QAudioBuffer(bytes, WindowsAudioCapture::format(),
                                       next * 1000000 / rate));
      next += block;
    }
  }
};
WindowsAudioCapture::WindowsAudioCapture(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}
WindowsAudioCapture::~WindowsAudioCapture() { stop(); }
bool WindowsAudioCapture::supportsApplicationAudio() { return supportsProcessLoopback(); }
QAudioFormat WindowsAudioCapture::format() {
  QAudioFormat value;
  value.setSampleRate(rate);
  value.setChannelCount(channels);
  value.setSampleFormat(QAudioFormat::Float);
  return value;
}
QVariantList WindowsAudioCapture::microphones() {
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  QVariantList result;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDeviceCollection> devices;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
      SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                               &devices))) {
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      ComPtr<IMMDevice> device;
      ComPtr<IPropertyStore> properties;
      LPWSTR id = nullptr;
      PROPVARIANT name;
      PropVariantInit(&name);
      if (SUCCEEDED(devices->Item(i, &device)) &&
          SUCCEEDED(device->GetId(&id))) {
        QString label = QStringLiteral("麦克风 %1").arg(i + 1);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
            SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
            name.vt == VT_LPWSTR)
          label = QString::fromWCharArray(name.pwszVal);
        result.append(
            QVariantMap{{"label", label}, {"id", QString::fromWCharArray(id)}});
      }
      CoTaskMemFree(id);
      PropVariantClear(&name);
    }
  }
  if (SUCCEEDED(initialized))
    CoUninitialize();
  return result;
}
bool WindowsAudioCapture::start(bool system, const QString &microphoneId,
                                QString *error, quint32 processId) {
  stop();
  if (system && processId) {
    if (!supportsProcessLoopback()) {
      if (error) *error = QStringLiteral("Application audio capture requires Windows build 20348 or newer.");
      return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    DWORD exitCode = 0;
    const bool alive = process && GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    if (process) CloseHandle(process);
    if (!alive) {
      if (error) *error = QStringLiteral("The selected audio application is unavailable or cannot be accessed.");
      return false;
    }
  }
  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                    CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  auto open = [&](bool loopback) {
    if (FAILED(result))
      return;
    auto stream = std::make_shared<Private::Stream>();
    stream->microphone = !loopback;
    stream->processLoopback = loopback && processId != 0;
    if (stream->processLoopback) {
      result = activateProcessLoopback(processId, stream->client);
      // GetMixFormat is not implemented by some virtual process clients.
      // Their requested shared capture format below is already 48 kHz stereo.
      stream->deviceRate = rate;
    } else {
      ComPtr<IMMDevice> device;
      // Match Windows media playback (and Qt's defaultAudioOutput), which
      // resolves the multimedia role. The console role can be a different,
      // silent endpoint when users assign separate defaults.
      result = loopback ? enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)
                       : enumerator->GetDevice(reinterpret_cast<LPCWSTR>(microphoneId.utf16()), &device);
      if (SUCCEEDED(result))
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                  reinterpret_cast<void **>(stream->client.GetAddressOf()));
      WAVEFORMATEX *nativeFormat = nullptr;
      if (SUCCEEDED(result)) result = stream->client->GetMixFormat(&nativeFormat);
      if (SUCCEEDED(result) && nativeFormat) stream->deviceRate = nativeFormat->nSamplesPerSec;
      CoTaskMemFree(nativeFormat);
    }
    WAVEFORMATEX wave{};
    wave.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wave.nChannels = channels;
    wave.nSamplesPerSec = rate;
    wave.wBitsPerSample = 32;
    wave.nBlockAlign = channels * 4;
    wave.nAvgBytesPerSec = rate * wave.nBlockAlign;
    DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (loopback)
      flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    if (SUCCEEDED(result))
      result = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                          stream->processLoopback ? 0 : 5000000, 0, &wave, nullptr);
    if (SUCCEEDED(result))
      result = stream->client->GetService(IID_PPV_ARGS(&stream->capture));
    if (SUCCEEDED(result))
      d->streams.append(stream);
  };
  if (system)
    open(true);
  if (!microphoneId.isEmpty())
    open(false);
  d->epoch = clock100ns();
  d->next = 0;
  for (auto &stream : d->streams)
    if (SUCCEEDED(result))
      result = stream->client->Start();
  if (FAILED(result) || d->streams.isEmpty()) {
    if (error)
      *error = failure(result);
    stop();
    return false;
  }
  d->timer.start();
  return true;
}
void WindowsAudioCapture::flush() {
  if (d->timer.isActive())
    d->tick(true);
}
void WindowsAudioCapture::stop() {
  d->timer.stop();
  for (auto &stream : d->streams)
    stream->client->Stop();
  d->streams.clear();
  emit levelChanged(0);
}
