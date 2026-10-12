#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <array>
#include <audioclient.h>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <vector>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
static LONGLONG frequency;
static LONGLONG clock100() {
  LARGE_INTEGER q;
  QueryPerformanceCounter(&q);
  return LONGLONG((long double)q.QuadPart * 10000000 / frequency);
}
static QColor marker(int s) {
  return s == 1   ? QColor(0, 255, 0)
         : s == 2 ? QColor(0, 0, 255)
         : s == 3 ? QColor(255, 255, 0)
                  : QColor(255, 0, 0);
}
class Surface : public QWidget {
public:
  int state = 0;
  Surface() {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setFixedSize(640, 360);
    setWindowTitle("W03A own-window audio timing probe");
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), marker(state));
    p.setPen(Qt::black);
    p.drawText(20, 330, QString("W03A %1 — system mix capture").arg(state));
  }
};
struct Format {
  WAVEFORMATEX *value = nullptr;
  bool floating = false;
  int bytes = 0;
  ~Format() {
    if (value)
      CoTaskMemFree(value);
  }
  void validate() {
    WORD tag = value->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && value->cbSize >= 22) {
      auto e = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(value);
      if (e->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        tag = WAVE_FORMAT_IEEE_FLOAT;
      else if (e->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
        tag = WAVE_FORMAT_PCM;
    }
    floating = tag == WAVE_FORMAT_IEEE_FLOAT && value->wBitsPerSample == 32;
    bytes = value->wBitsPerSample / 8;
    if (!floating && !(tag == WAVE_FORMAT_PCM && value->wBitsPerSample == 16))
      throw hresult_error(E_NOTIMPL,
                          L"Only float32 and PCM16 mix formats are supported");
    if (!value->nSamplesPerSec || !value->nChannels ||
        value->nBlockAlign != bytes * value->nChannels)
      throw hresult_error(E_INVALIDARG);
  }
  float sample(const BYTE *p) const {
    if (floating) {
      float v;
      memcpy(&v, p, 4);
      return std::isfinite(v) ? v : 0;
    }
    short v;
    memcpy(&v, p, 2);
    return v / 32768.f;
  }
  void write(BYTE *p, float v) const {
    if (floating)
      memcpy(p, &v, 4);
    else {
      short n = short(v * 32767);
      memcpy(p, &n, 2);
    }
  }
};
static bool saveWave(const QString &path, const QByteArray &pcm,
                     const Format &f) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QByteArray fmt(reinterpret_cast<const char *>(f.value),
                 sizeof(WAVEFORMATEX) + f.value->cbSize);
  // WAVEFORMATEX is 18 bytes; PCM uses the canonical 16-byte fmt chunk.
  if (f.value->wFormatTag == WAVE_FORMAT_PCM)
    fmt.resize(16);
  QByteArray head;
  auto u32 = [&](quint32 v) {
    head.append(reinterpret_cast<const char *>(&v), 4);
  };
  head.append("RIFF", 4);
  u32(4 + 8 + fmt.size() + (fmt.size() % 2) + 8 + pcm.size());
  head.append("WAVEfmt ", 8);
  u32(fmt.size());
  head.append(fmt);
  if (fmt.size() % 2)
    head.append('\0');
  head.append("data", 4);
  u32(pcm.size());
  return file.write(head) == head.size() && file.write(pcm) == pcm.size() &&
         file.flush();
}
int main(int argc, char **argv) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  frequency = f.QuadPart;
  init_apartment(apartment_type::single_threaded);
  QApplication app(argc, argv);
  QCommandLineParser args;
  args.addHelpOption();
  args.addOption({"output-dir", "Fresh local evidence directory", "path"});
  args.addOption(
      {"no-tone", "Write silence; audio association must be absent"});
  args.process(app);
  QString out = args.value("output-dir");
  if (out.isEmpty() ||
      (QDir(out).exists() &&
       !QDir(out)
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::System |
                       QDir::NoDotAndDotDot)
            .isEmpty()) ||
      !QDir().mkpath(out))
    return 2;
  Surface window;
  window.move(160, 160);
  window.show();
  HWND hwnd = reinterpret_cast<HWND>(window.winId());
  QJsonArray packets, frames, commands, detections, associations, endpoints;
  QString error;
  QByteArray pcm;
  Format format;
  com_ptr<IMMDeviceEnumerator> enumerator;
  com_ptr<IMMDevice> endpoint;
  com_ptr<IAudioClient> render, capture;
  com_ptr<IAudioRenderClient> writer;
  com_ptr<IAudioCaptureClient> reader;
  com_ptr<ID3D11Device> device;
  com_ptr<ID3D11DeviceContext> context;
  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  UINT32 bufferFrames = 0;
  quint64 rendered = 0, captured = 0;
  LONGLONG origin = clock100();
  std::array<LONGLONG, 3> visual{}, audio{};
  std::array<int, 3> hits{};
  int discontinuities = 0, startupDiscontinuities = 0, timestampErrors = 0,
      implausible = 0, nonmonotonic = 0;
  LONGLONG lastFrame = 0;
  bool startedRender = false, startedCapture = false;
  int state = 0;
  try {
    check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   enumerator.put_void()));
    com_ptr<IMMDeviceCollection> inputs;
    check_hresult(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                                 inputs.put()));
    UINT count = 0;
    check_hresult(inputs->GetCount(&count));
    for (UINT i = 0; i < count; ++i) {
      com_ptr<IMMDevice> e;
      check_hresult(inputs->Item(i, e.put()));
      LPWSTR id = nullptr;
      check_hresult(e->GetId(&id));
      endpoints.append(QString::fromWCharArray(id));
      CoTaskMemFree(id);
    }
    check_hresult(
        enumerator->GetDefaultAudioEndpoint(eRender, eConsole, endpoint.put()));
    check_hresult(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     nullptr, render.put_void()));
    check_hresult(render->GetMixFormat(&format.value));
    format.validate();
    check_hresult(render->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0,
                                     format.value, nullptr));
    check_hresult(render->GetBufferSize(&bufferFrames));
    check_hresult(
        render->GetService(__uuidof(IAudioRenderClient), writer.put_void()));
    check_hresult(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                     nullptr, capture.put_void()));
    check_hresult(capture->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0,
                                      format.value, nullptr));
    check_hresult(
        capture->GetService(__uuidof(IAudioCaptureClient), reader.put_void()));
    check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                    0, D3D11_SDK_VERSION, device.put(), nullptr,
                                    context.put()));
    auto dxgi = device.as<IDXGIDevice>();
    com_ptr<IInspectable> inspectable;
    check_hresult(
        CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
    auto direct = inspectable.as<IDirect3DDevice>();
    auto factory = get_activation_factory<GraphicsCaptureItem,
                                          IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    check_hresult(factory->CreateForWindow(hwnd, guid_of<GraphicsCaptureItem>(),
                                           put_abi(item)));
    pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        direct, DirectXPixelFormat::B8G8R8A8UIntNormalized, 3, item.Size());
    session = pool.CreateCaptureSession(item);
    session.StartCapture();
    BYTE *initial = nullptr;
    check_hresult(writer->GetBuffer(bufferFrames, &initial));
    check_hresult(
        writer->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT));
    rendered = bufferFrames;
    check_hresult(capture->Start());
    startedCapture = true;
    check_hresult(render->Start());
    startedRender = true;
    origin = clock100();
  } catch (hresult_error const &e) {
    error = QString::fromWCharArray(e.message().c_str());
  }
  QTimer timer;
  timer.setTimerType(Qt::PreciseTimer);
  QObject::connect(&timer, &QTimer::timeout, [&] {
    LONGLONG now = clock100();
    try {
      int desired = std::min(3, int((now - origin) / 10000000));
      if (desired > state) {
        state = desired;
        window.state = state;
        window.update();
        commands.append(
            QJsonObject{{"state", state}, {"qpc_100ns", double(now)}});
      }
      if (error.isEmpty()) {
        UINT32 padding = 0;
        check_hresult(render->GetCurrentPadding(&padding));
        UINT32 available = bufferFrames - padding;
        if (available) {
          BYTE *data = nullptr;
          check_hresult(writer->GetBuffer(available, &data));
          for (UINT32 n = 0; n < available; ++n) {
            double t = double(rendered + n) / format.value->nSamplesPerSec;
            float value = 0;
            for (int k = 1; k <= 3; ++k)
              if (t >= k && t < k + .150 && !args.isSet("no-tone"))
                value = float(
                    .10 * sin(6.283185307179586 * (400 + 400 * k) * (t - k)));
            for (int c = 0; c < format.value->nChannels; ++c)
              format.write(data + n * format.value->nBlockAlign +
                               c * format.bytes,
                           value);
          }
          check_hresult(writer->ReleaseBuffer(available, 0));
          rendered += available;
        }
        UINT32 next = 0;
        check_hresult(reader->GetNextPacketSize(&next));
        while (next) {
          BYTE *data = nullptr;
          UINT32 n = 0;
          DWORD flags = 0;
          UINT64 position = 0, qpc = 0;
          check_hresult(reader->GetBuffer(&data, &n, &flags, &position, &qpc));
          LONGLONG packetRead = clock100();
          qint64 offset = pcm.size();
          bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
          QByteArray bytes(n * format.value->nBlockAlign, '\0');
          if (!silent && data)
            memcpy(bytes.data(), data, bytes.size());
          pcm.append(bytes);
          bool stampValid = !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) &&
                            qpc > 0 &&
                            double(qpc) <= double(packetRead) + 10000 &&
                            double(packetRead) - double(qpc) < 20000000;
          if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
            ++timestampErrors;
          if (!stampValid)
            ++implausible;
          if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
            if (captured == 0)
              ++startupDiscontinuities;
            else
              ++discontinuities;
          }
          packets.append(
              QJsonObject{{"device_position_frames", double(position)},
                          {"qpc_100ns", double(qpc)},
                          {"poll_qpc_100ns", double(packetRead)},
                          {"frames", int(n)},
                          {"flags", int(flags)},
                          {"pcm_byte_offset", double(offset)},
                          {"stream_frame_offset", double(captured)},
                          {"timestamp_valid", stampValid}});
          // Independent 5ms windows: frequency dominance avoids calling
          // arbitrary nonzero system audio our tone.
          int block = std::max(1, int(format.value->nSamplesPerSec / 200));
          for (UINT32 start = 0; start + block <= n; start += block) {
            double energy = 0;
            std::array<double, 3> real{}, imag{};
            for (int j = 0; j < block; ++j) {
              double x = 0;
              for (int c = 0; c < format.value->nChannels; ++c)
                x += format.sample(
                    reinterpret_cast<const BYTE *>(bytes.constData()) +
                    (start + j) * format.value->nBlockAlign + c * format.bytes);
              x /= format.value->nChannels;
              energy += x * x;
              for (int k = 0; k < 3; ++k) {
                double a = 6.283185307179586 * (800 + 400 * k) * j /
                           format.value->nSamplesPerSec;
                real[k] += x * cos(a);
                imag[k] += x * sin(a);
              }
            }
            double rms = sqrt(energy / block);
            for (int k = 0; k < 3; ++k) {
              double score = energy > 1e-15
                                 ? 2 * (real[k] * real[k] + imag[k] * imag[k]) /
                                       (block * energy)
                                 : 0;
              if (rms > .015 && score > .75) {
                LONGLONG onset = qpc + LONGLONG(double(start) * 10000000 /
                                                format.value->nSamplesPerSec);
                ++hits[k];
                if (audio[k] == 0 && stampValid) {
                  audio[k] = onset;
                  detections.append(QJsonObject{
                      {"state", k + 1},
                      {"frequency_hz", 800 + 400 * k},
                      {"qpc_100ns", double(onset)},
                      {"packet_qpc_100ns", double(qpc)},
                      {"sample_offset", int(start)},
                      {"stream_frame_offset", double(captured + start)},
                      {"rms", rms},
                      {"spectral_energy_ratio", score}});
                }
              }
            }
          }
          captured += n;
          check_hresult(reader->ReleaseBuffer(n));
          check_hresult(reader->GetNextPacketSize(&next));
        }
      }
      if (pool) {
        auto frame = pool.TryGetNextFrame();
        if (frame) {
          LONGLONG frameRead = clock100();
          auto t = frame.SystemRelativeTime().count();
          if (lastFrame && t <= lastFrame)
            ++nonmonotonic;
          lastFrame = t;
          auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
          com_ptr<ID3D11Texture2D> texture;
          check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                             texture.put_void()));
          D3D11_TEXTURE2D_DESC desc;
          texture->GetDesc(&desc);
          desc.Usage = D3D11_USAGE_STAGING;
          desc.BindFlags = 0;
          desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
          desc.MiscFlags = 0;
          com_ptr<ID3D11Texture2D> staging;
          check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));
          context->CopyResource(staging.get(), texture.get());
          D3D11_MAPPED_SUBRESOURCE map;
          check_hresult(
              context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map));
          auto pixel = static_cast<BYTE *>(map.pData) +
                       (desc.Height / 2) * map.RowPitch + (desc.Width / 2) * 4;
          int b = pixel[0], g = pixel[1], r = pixel[2];
          context->Unmap(staging.get(), 0);
          int detected = r < 20 && g > 235 && b < 20    ? 1
                         : r < 20 && g < 20 && b > 235  ? 2
                         : r > 235 && g > 235 && b < 20 ? 3
                                                        : 0;
          bool valid = t <= frameRead + 10000 && frameRead - t < 20000000;
          if (!valid)
            ++implausible;
          if (detected && valid && !visual[detected - 1])
            visual[detected - 1] = t;
          frames.append(QJsonObject{{"qpc_100ns", double(t)},
                                    {"poll_qpc_100ns", double(frameRead)},
                                    {"pixel_r", r},
                                    {"pixel_g", g},
                                    {"pixel_b", b},
                                    {"detected_state", detected},
                                    {"width", int(desc.Width)},
                                    {"height", int(desc.Height)}});
        }
      }
    } catch (hresult_error const &e) {
      error = QString::fromWCharArray(e.message().c_str());
      app.quit();
    }
    if (now - origin > 50000000 || !error.isEmpty())
      app.quit();
  });
  timer.start(5);
  QTimer::singleShot(8000, &app, &QApplication::quit);
  app.exec();
  timer.stop();
  if (startedRender)
    render->Stop();
  if (startedCapture)
    capture->Stop();
  if (session)
    session.Close();
  if (pool)
    pool.Close();
  bool complete = error.isEmpty() && nonmonotonic == 0 && implausible == 0 &&
                  timestampErrors == 0 && discontinuities == 0;
  bool contamination = implausible != 0 || timestampErrors != 0;
  for (int k = 0; k < 3; ++k) {
    double expected = (k + 1) * 10000000.;
    bool plausible = audio[k] &&
                     std::abs(double(audio[k] - origin) - expected) < 3000000 &&
                     visual[k] && hits[k] >= 8;
    contamination |= audio[k] && !plausible;
    complete &= plausible;
    associations.append(QJsonObject{
        {"state", k + 1},
        {"audio_onset_qpc_100ns", double(audio[k])},
        {"first_pixel_frame_qpc_100ns", double(visual[k])},
        {"video_minus_audio_ms",
         audio[k] && visual[k] ? double(visual[k] - audio[k]) / 10000 : 0},
        {"matching_windows", hits[k]},
        {"valid", plausible}});
  }
  // A no-tone run can detect ambient matching tones, but cannot attribute them
  // to this probe.
  if (args.isSet("no-tone")) {
    for (auto count : hits)
      contamination |= count != 0;
    complete = false;
  }
  bool waveSaved =
      format.value && saveWave(out + "/system-mix.wav", pcm, format);
  complete &= waveSaved;
  QJsonObject result{
      {"probe",
       "W03A experimental default-render WASAPI loopback + own HWND WGC"},
      {"success", complete},
      {"error", error},
      {"no_tone", args.isSet("no-tone")},
      {"origin_qpc_100ns", double(origin)},
      {"qpc_frequency_hz", double(frequency)},
      {"sample_rate", format.value ? int(format.value->nSamplesPerSec) : 0},
      {"channels", format.value ? int(format.value->nChannels) : 0},
      {"bits_per_sample", format.value ? int(format.value->wBitsPerSample) : 0},
      {"float_samples", format.floating},
      {"wave_saved", waveSaved},
      {"pcm_frames", double(captured)},
      {"packets", packets},
      {"video_frames", frames},
      {"marker_commands", commands},
      {"tone_detections", detections},
      {"associations", associations},
      {"startup_discontinuities", startupDiscontinuities},
      {"subsequent_discontinuities", discontinuities},
      {"timestamp_errors", timestampErrors},
      {"implausible_timestamps", implausible},
      {"nonmonotonic_video", nonmonotonic},
      {"ambient_matching_tone_or_uncertain", contamination},
      {"active_capture_endpoint_ids_read_only", endpoints},
      {"scope_limit",
       "System mix may include other applications. Tone attribution is bounded "
       "spectral/time evidence, not process isolation. No microphone "
       "recording, pause, drift or device switching validation."}};
  QSaveFile json(out + "/result.json");
  if (!json.open(QIODevice::WriteOnly))
    return 2;
  auto content = QJsonDocument(result).toJson(QJsonDocument::Indented);
  if (json.write(content) != content.size() || !json.commit())
    return 2;
  return complete ? 0 : 1;
}
