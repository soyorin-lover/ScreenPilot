#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSysInfo>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <d3d11.h>
#include <thread>
#include <tuple>
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
static constexpr ULONG_PTR inputTag = 0x53505732;
static LONGLONG freq = 0;
// SendInput can wait for activation/hook delivery. Keep the Qt/hook thread
// pumping while the input call runs, rather than blocking its own callbacks.
static UINT sendInputPumping(UINT count, INPUT *input) {
  std::atomic<bool> done{false};
  UINT sent = 0;
  std::thread sender([&] {
    sent = SendInput(count, input, sizeof(INPUT));
    done.store(true);
  });
  QEventLoop loop;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (done.load())
      loop.quit();
  });
  poll.start(2);
  if (!done.load())
    loop.exec();
  sender.join();
  return sent;
}
static bool mouseCaptureAllows(HWND target) {
  GUITHREADINFO info{sizeof(GUITHREADINFO)};
  const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
  return thread && GetGUIThreadInfo(thread, &info) &&
         (!info.hwndCapture || info.hwndCapture == target);
}
static LONGLONG now100() {
  LARGE_INTEGER x;
  QueryPerformanceCounter(&x);
  return static_cast<LONGLONG>((long double)x.QuadPart * 10000000 / freq);
}
static QColor color(int n) {
  return n == 0   ? QColor(255, 0, 0)
         : n == 1 ? QColor(0, 255, 0)
         : n == 2 ? QColor(0, 0, 255)
                  : QColor(255, 255, 0);
}
struct Click {
  LONGLONG t;
  int state = 0;
  bool matched = false;
  double qtX = -1, qtY = -1;
  LONG physicalX = -1, physicalY = -1;
};
class Window : public QWidget {
public:
  int state = 0, tick = 0;
  std::vector<Click> *clicks = nullptr;
  Window() {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setFixedSize(640, 360);
    setWindowTitle("W02 own-window synthetic timing probe");
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.fillRect(rect(), color(state));
    p.fillRect(QRect(0, 300, tick % 640, 60), Qt::white);
    p.setPen(Qt::black);
    p.drawText(20, 330, QString("W02 state %1").arg(state));
  }
  void mousePressEvent(QMouseEvent *e) override {
    if (e->button() == Qt::LeftButton && state < 3 && clicks &&
        !clicks->empty() && clicks->back().state == 0) {
      ++state;
      if (clicks && !clicks->empty()) {
        clicks->back().state = state;
        clicks->back().qtX = e->position().x();
        clicks->back().qtY = e->position().y();
      }
      update();
    }
  }
};
static POINT requested{};
static int injectionCoordinateFailures = 0;
static Window *own = nullptr;
static HWND hwnd = nullptr;
static QJsonArray events;
static std::vector<Click> clicks;
static bool injecting = false, interference = false;
static LONGLONG origin = 0;
static QJsonObject timeline(LONGLONG t) {
  auto d = t - origin;
  return {{"raw_qpc_100ns", double(t)},
          {"active_100ns",
           double(d - std::clamp<LONGLONG>(d - 25000000, 0, 10000000))},
          {"paused", d >= 25000000 && d < 35000000}};
}
static LRESULT CALLBACK hook(int code, WPARAM w, LPARAM l) {
  if (code == HC_ACTION) {
    auto *m = reinterpret_cast<MSLLHOOKSTRUCT *>(l);
    bool injected = (m->flags & LLMHF_INJECTED) != 0;
    bool ours = injected && m->dwExtraInfo == inputTag;
    RECT r;
    GetClientRect(hwnd, &r);
    POINT p = m->pt;
    ScreenToClient(hwnd, &p);
    bool inside = PtInRect(&r, p);
    if (!ours && injecting)
      interference = true;
    // The low-level hook must not perform cross-window hit testing. Input is
    // checked against our HWND before sending; only our tagged events are
    // saved.
    if (inside && ours) {
      auto t = now100();
      auto j = timeline(t);
      j["message"] = int(w);
      j["msll_time_ms"] = double(m->time);
      j["injected"] = injected;
      j["probe_injected"] = ours;
      j["screen_x"] = int(m->pt.x);
      j["screen_y"] = int(m->pt.y);
      j["client_physical_x"] = int(p.x);
      j["client_physical_y"] = int(p.y);
      j["logical_x"] = p.x / own->devicePixelRatioF();
      j["logical_y"] = p.y / own->devicePixelRatioF();
      events.append(j);
      if (w == WM_LBUTTONDOWN && ours) {
        if (injected && (std::abs(m->pt.x - requested.x) > 2 ||
                         std::abs(m->pt.y - requested.y) > 2))
          ++injectionCoordinateFailures;
        clicks.push_back({t});
        clicks.back().physicalX = p.x;
        clicks.back().physicalY = p.y;
      }
    }
  }
  return CallNextHookEx(nullptr, code, w, l);
}
int main(int argc, char **argv) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  freq = f.QuadPart;
  init_apartment(apartment_type::single_threaded);
  QApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({"output-dir", "Fresh evidence directory", "path"});
  parser.addOption({"no-input", "Capture only; must fail click association"});
  parser.process(app);
  QString out = parser.value("output-dir");
  if (out.isEmpty() ||
      (QDir(out).exists() &&
       !QDir(out)
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                       QDir::System)
            .isEmpty()))
    return 2;
  if (!QDir().mkpath(out))
    return 2;
  Window window;
  own = &window;
  window.clicks = &clicks;
  window.move(160, 160);
  window.show();
  hwnd = reinterpret_cast<HWND>(window.winId());
  SetForegroundWindow(hwnd);
  QTimer::singleShot(200, &window, [&] {
    window.raise();
    window.activateWindow();
    SetForegroundWindow(hwnd);
  });
  origin = now100();
  bool mappingTests = true;
  for (auto [offset, expected, paused] :
       std::vector<std::tuple<LONGLONG, LONGLONG, bool>>{
           {15000000, 15000000, false},
           {25000000, 25000000, true},
           {30000000, 25000000, true},
           {35000000, 25000000, false},
           {40000000, 30000000, false}}) {
    auto mapped = timeline(origin + offset);
    mappingTests &= mapped["active_100ns"].toDouble() == expected &&
                    mapped["paused"].toBool() == paused;
  }
  POINT originalCursor{};
  const bool originalCursorKnown = GetCursorPos(&originalCursor);
  bool pendingSyntheticRelease = false;
  LONGLONG activationDeadline = 0;
  HHOOK mouseHook =
      SetWindowsHookExW(WH_MOUSE_LL, hook, GetModuleHandleW(nullptr), 0);
  QJsonArray frames, latencies, positions;
  QString error;
  int nonmonotonic = 0, wrongSize = 0, pausedFrames = 0, resumedFrames = 0,
      coordinateFailures = 0;
  LONGLONG last = -1;
  bool moved = false;
  com_ptr<ID3D11Device> device;
  com_ptr<ID3D11DeviceContext> context;
  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  try {
    if (!mouseHook)
      throw hresult_error(HRESULT_FROM_WIN32(GetLastError()));
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
  } catch (hresult_error const &e) {
    error = QString::fromStdWString(e.message().c_str());
  }
  QTimer timer;
  bool inTick = false;
  int pulse = 0;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    if (inTick)
      return;
    QScopedValueRollback<bool> tickGuard(inTick, true);
    auto current = now100();
    auto elapsed = current - origin;
    window.tick += 11;
    window.update();
    if (elapsed > 18000000 && !moved) {
      window.move(240, 220);
      moved = true;
    }
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT corner{0, 0};
    ClientToScreen(hwnd, &corner);
    if (positions.isEmpty() || moved && positions.size() == 1)
      positions.append(QJsonObject{{"qpc_100ns", double(current)},
                                   {"x", int(corner.x)},
                                   {"y", int(corner.y)},
                                   {"width", int(rect.right)},
                                   {"height", int(rect.bottom)}});
    if (!parser.isSet("no-input") && error.isEmpty() &&
        elapsed > 8000000 + pulse * 14000000 && pulse < 3) {
      POINT point{rect.right / 2, rect.bottom / 2};
      ClientToScreen(hwnd, &point);
      requested = point;
      if ((pulse > 0 && GetForegroundWindow() != hwnd) ||
          WindowFromPoint(point) != hwnd || !mouseCaptureAllows(hwnd) ||
          interference || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        error = "Synthetic input aborted: foreground changed or real mouse "
                "interference";
      } else {
        injecting = true;
        INPUT move{};
        move.type = INPUT_MOUSE;
        move.mi.dwExtraInfo = inputTag;
        move.mi.dwFlags =
            MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        move.mi.dx = LONG((point.x - GetSystemMetrics(SM_XVIRTUALSCREEN)) *
                          65535.0 / (GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1));
        move.mi.dy = LONG((point.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) *
                          65535.0 / (GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1));
        if (sendInputPumping(1, &move) != 1)
          error = "Synthetic move failed";
        POINT actual{};
        if (!GetCursorPos(&actual) || WindowFromPoint(actual) != hwnd ||
            (pulse > 0 && GetForegroundWindow() != hwnd) ||
            !mouseCaptureAllows(hwnd) || interference)
          error =
              "Synthetic input aborted: actual cursor or foreground changed";
        if (error.isEmpty()) {
          INPUT input[2]{};
          input[0].type = INPUT_MOUSE;
          input[0].mi.dwExtraInfo = inputTag;
          input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
          input[1].type = INPUT_MOUSE;
          input[1].mi.dwExtraInfo = inputTag;
          input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
          const UINT sent = sendInputPumping(2, input);
          pendingSyntheticRelease = sent == 1;
          if (sent != 2)
            error = "SendInput failed";
          if (pulse == 0)
            activationDeadline = now100() + 2000000;
        }
      }
      ++pulse;
    }
    try {
      if (pool) {
        auto frame = pool.TryGetNextFrame();
        if (frame) {
          auto t = frame.SystemRelativeTime().count();
          if (last >= 0 && t <= last)
            ++nonmonotonic;
          last = t;
          auto j = timeline(t);
          bool paused = j["paused"].toBool();
          if (paused)
            ++pausedFrames;
          if (t - origin >= 35000000)
            ++resumedFrames;
          j["readback_start_qpc_100ns"] = double(now100());
          auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
          com_ptr<ID3D11Texture2D> texture;
          check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                             texture.put_void()));
          D3D11_TEXTURE2D_DESC desc;
          texture->GetDesc(&desc);
          j["width"] = int(desc.Width);
          j["height"] = int(desc.Height);
          if (std::abs(int(desc.Width) - rect.right) > 2 ||
              std::abs(int(desc.Height) - rect.bottom) > 2)
            ++wrongSize;
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
          auto pixel = static_cast<unsigned char *>(map.pData) +
                       int(desc.Height / 4) * map.RowPitch +
                       int(desc.Width / 4) * 4;
          QColor observed(pixel[2], pixel[1], pixel[0]);
          j["sample_rgb"] =
              QJsonArray{observed.red(), observed.green(), observed.blue()};
          context->Unmap(staging.get(), 0);
          int state = -1;
          for (int i = 0; i < 4; ++i) {
            auto c = color(i);
            if (std::abs(c.red() - observed.red()) < 30 &&
                std::abs(c.green() - observed.green()) < 30 &&
                std::abs(c.blue() - observed.blue()) < 30)
              state = i;
          }
          j["pixel_state"] = state;
          j["readback_end_qpc_100ns"] = double(now100());
          for (auto &c : clicks)
            if (!c.matched && c.state > 0 && c.state == state && t >= c.t) {
              c.matched = true;
              latencies.append(
                  QJsonObject{{"state", state},
                              {"qt_logical_x", c.qtX},
                              {"qt_logical_y", c.qtY},
                              {"hook_client_physical_x", int(c.physicalX)},
                              {"hook_client_physical_y", int(c.physicalY)},
                              {"hook_qpc_100ns", double(c.t)},
                              {"frame_qpc_100ns", double(t)},
                              {"latency_ms", (t - c.t) / 10000.0},
                              {"paused", paused}});
            }
          frames.append(j);
        }
      }
    } catch (hresult_error const &e) {
      error = QString::fromStdWString(e.message().c_str());
    }
    if (injecting && error.isEmpty()) {
      if (interference)
        error = "External mouse input detected";
      else if (now100() > activationDeadline && GetForegroundWindow() != hwnd)
        error = "Own window lost foreground";
    }
    const bool enoughEvidence = latencies.size() == 3 && resumedFrames >= 20 &&
                                pausedFrames > 0 && elapsed >= 40000000;
    if (enoughEvidence || elapsed >= 60000000 || !error.isEmpty())
      app.quit();
  });
  timer.start(16);
  QTimer::singleShot(10000, &app, &QApplication::quit);
  app.exec();
  timer.stop();
  if (pendingSyntheticRelease) {
    POINT actual{};
    if (GetForegroundWindow() == hwnd && GetCursorPos(&actual) &&
        WindowFromPoint(actual) == hwnd) {
      INPUT release{};
      release.type = INPUT_MOUSE;
      release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
      release.mi.dwExtraInfo = inputTag;
      pendingSyntheticRelease = sendInputPumping(1, &release) != 1;
    }
    if (pendingSyntheticRelease)
      error += "; synthetic button-up cleanup could not be confirmed";
  }
  if (mouseHook)
    UnhookWindowsHookEx(mouseHook);
  if (session)
    session.Close();
  if (pool)
    pool.Close();
  if (originalCursorKnown && injecting && !interference &&
      GetForegroundWindow() == hwnd)
    SetCursorPos(originalCursor.x, originalCursor.y);
  for (auto v : events) {
    auto e = v.toObject();
    if (std::abs(e["logical_x"].toDouble() * window.devicePixelRatioF() -
                 e["client_physical_x"].toDouble()) > 2)
      ++coordinateFailures;
  }
  coordinateFailures += injectionCoordinateFailures;
  for (const auto &click : clicks) {
    if (click.qtX < 0 || click.qtY < 0 ||
        std::abs(click.qtX * window.devicePixelRatioF() - click.physicalX) >
            2 ||
        std::abs(click.qtY * window.devicePixelRatioF() - click.physicalY) > 2)
      ++coordinateFailures;
  }
  bool passed = mappingTests && !parser.isSet("no-input") && !interference &&
                error.isEmpty() && frames.size() >= 10 &&
                latencies.size() == 3 && nonmonotonic == 0 && wrongSize == 0 &&
                coordinateFailures == 0 && pausedFrames > 0 &&
                resumedFrames > 0 && positions.size() == 2;
  QJsonObject result{
      {"probe", "W02"},
      {"qt_version", qVersion()},
      {"os", QSysInfo::prettyProductName()},
      {"backend", "native WGC CreateForWindow + D3D11 staging readback"},
      {"scope",
       "own Qt HWND only; synthetic SendInput; pause is timeline mapping only"},
      {"passed", passed},
      {"reason", passed ? "controlled_clock_coordinate_checks_passed"
                 : parser.isSet("no-input") && error.isEmpty()
                     ? "expected_click_evidence_missing"
                     : "evidence_checks_failed"},
      {"error", error},
      {"input_enabled", !parser.isSet("no-input")},
      {"synthetic", true},
      {"pause_boundary_checks_passed", mappingTests},
      {"input_interference", interference},
      {"synthetic_release_unconfirmed", pendingSyntheticRelease},
      {"qpc_frequency", double(freq)},
      {"origin_qpc_100ns", double(origin)},
      {"dpr", window.devicePixelRatioF()},
      {"expected_physical_width", window.width() * window.devicePixelRatioF()},
      {"expected_physical_height",
       window.height() * window.devicePixelRatioF()},
      {"frames", frames},
      {"events", events},
      {"click_visual_latencies", latencies},
      {"positions", positions},
      {"pause_start_qpc_100ns", double(origin + 25000000)},
      {"pause_end_qpc_100ns", double(origin + 35000000)},
      {"frame_count", frames.size()},
      {"event_count", events.size()},
      {"matched_clicks", latencies.size()},
      {"nonmonotonic_frames", nonmonotonic},
      {"wrong_dimensions", wrongSize},
      {"coordinate_failures", coordinateFailures},
      {"paused_frames", pausedFrames},
      {"resumed_frames", resumedFrames}};
  QSaveFile file(out + "/result.json");
  if (!file.open(QIODevice::WriteOnly))
    return 2;
  const QByteArray json = QJsonDocument(result).toJson();
  if (file.write(json) != json.size() || !file.commit())
    return 2;
  return passed ? 0 : 1;
}
