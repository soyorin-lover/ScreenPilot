#include "VisualEffectsSmokeTest.h"
#include "FrameConversion.h"
#include "CanvasRenderer.h"
#include "CameraOverlay.h"
#include "CanvasPreview.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <functional>
#include <algorithm>
#include <memory>
#include <cstring>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <limits>
int runVisualEffectsSmokeTest(const QString &directory) {
  auto await = [](const std::function<bool()> &ready) {
    QElapsedTimer timeout; timeout.start();
    while (!ready() && timeout.elapsed() < 2500) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
      QThread::msleep(1);
    }
    return ready();
  };
  auto videoFrame = [](QSize size, QRgb color, qint64 timestamp) {
    QVideoFrame frame(QVideoFrameFormat(size, QVideoFrameFormat::Format_BGRA8888));
    if (frame.map(QVideoFrame::WriteOnly)) {
      for (int y = 0; y < size.height(); ++y) {
        auto pixels = reinterpret_cast<QRgb *>(frame.bits(0) + y * frame.bytesPerLine(0));
        std::fill(pixels, pixels + size.width(), color);
      }
      frame.unmap();
    }
    frame.setStartTime(timestamp);
    return frame;
  };
  QImage source(320,180,QImage::Format_RGB32); source.fill(Qt::red);
  auto s=CanvasRenderer::defaults(); s["introEnabled"]=true; s["screenFrame"]="browserDark";
  s["screenFrameBrowserAddress"]="example.com"; s["shadow"]=0.;
  const QImage first=CanvasRenderer::render(source,s,{}, {},{},QSize(640,360),{},0);
  const QImage end=CanvasRenderer::render(source,s,{}, {},{},QSize(640,360),{},8);
  auto disabled=s; disabled["introEnabled"]=false;
  const bool endMatches=end==CanvasRenderer::render(source,disabled,{}, {},{},QSize(640,360));
  bool startBackground=!first.isNull();
  for(int y=0;y<first.height();++y) for(int x=0;x<first.width();++x)
    startBackground &= first.pixelColor(x,y)==QColor(s.value("backgroundColor").toString());
  bool styles=true;
  for(const QString style : {"none","windowLight","windowDark","browserLight","browserDark","device"}) {
    disabled["screenFrame"]=style;
    styles &= !CanvasRenderer::render(source,disabled,{}, {},{},QSize(640,360)).isNull();
  }
  bool invalid=true;
  for(const QString key : {"introDuration","introTempo","introElementGap"}) { auto bad=s; bad[key]=std::numeric_limits<double>::quiet_NaN(); invalid &= !CanvasRenderer::validate(bad); }
  auto bad=s; bad["screenFrame"]="future"; invalid &= !CanvasRenderer::validate(bad);
  bad=s; bad["introEnabled"]=1; invalid &= !CanvasRenderer::validate(bad);
  QImage cameraFirst(640,360,QImage::Format_RGB32); cameraFirst.fill(Qt::black);
  {QPainter p(&cameraFirst); CameraOverlay::paint(p,cameraFirst.size(),source,CameraOverlay::defaults(),s,0);}
  QImage black(cameraFirst.size(),cameraFirst.format()); black.fill(Qt::black);
  const bool cameraHidden=cameraFirst==black;
  auto crop=disabled; crop["screenFrame"]="windowLight"; crop["cropX"]=.5; crop["cropWidth"]=.5;
  QImage split(320,180,QImage::Format_RGB32); split.fill(Qt::red);
  { QPainter p(&split); p.fillRect(160,0,160,180,Qt::green); }
  const QImage cropped=CanvasRenderer::render(split,crop,{}, {},{},QSize(640,360));
  const bool cropGeometry=cropped.pixelColor(320,200)==QColor(Qt::green) && CanvasRenderer::sourceRect(split.size(),crop)==QRectF(160,0,160,180);
  disabled["screenFrame"]="browserDark";
  auto ordered=s; ordered["introDuration"]=.4; ordered["introElementGap"]=1.2;
  const bool shortSettles=CanvasRenderer::render(source,ordered,{}, {},{},QSize(640,360),{},.4)==CanvasRenderer::render(source,disabled,{}, {},{},QSize(640,360));
  QImage cameraEnd(640,360,QImage::Format_RGB32); cameraEnd.fill(Qt::black);
  QImage cameraStatic=cameraEnd;
  {QPainter p(&cameraEnd); CameraOverlay::paint(p,cameraEnd.size(),source,CameraOverlay::defaults(),ordered,.4);}
  {QPainter p(&cameraStatic); CameraOverlay::paint(p,cameraStatic.size(),source,CameraOverlay::defaults(),disabled,.4);}
  const bool cameraSettles=cameraEnd==cameraStatic && cameraEnd!=black;
  QImage cameraDelayed=black;
  {QPainter p(&cameraDelayed); CameraOverlay::paint(p,cameraDelayed.size(),source,CameraOverlay::defaults(),ordered,.1);}
  const bool orderDelay=cameraDelayed==black;
  // A fully settled intro takes the uncached path with the same final pixels.
  // Compare cold/warm backgrounds, changed sources, transforms and dimensions.
  bool staticRasterMatches = true;
  for (const QString style : {"none", "browserDark", "device"}) {
    auto cachedSettings = CanvasRenderer::defaults();
    cachedSettings["screenFrame"] = style;
    cachedSettings["screenRotationY"] = 12.;
    auto referenceSettings = cachedSettings;
    referenceSettings["introEnabled"] = true;
    for (QSize target : {QSize(640,360), QSize(800,600)}) {
      for (int i = 0; i < 3; ++i) {
        const QImage &input = i == 1 ? split : source;
        const AutoFraming::State zoom{1.3, .55, .5};
        const QImage reference = CanvasRenderer::render(input, referenceSettings, zoom, {}, {}, target, {}, 8, .5);
        const QImage cold = CanvasRenderer::render(input, cachedSettings, zoom, {}, {}, target, {}, 8, .5);
        const QImage warm = CanvasRenderer::render(input, cachedSettings, zoom, {}, {}, target, {}, 9, .5);
        staticRasterMatches &= !reference.isNull() && reference == cold && reference == warm;
      }
    }
  }
  bool rotatedFrameDimensions = true;
  for (const auto rotation : {QtVideo::Rotation::None, QtVideo::Rotation::Clockwise90, QtVideo::Rotation::Clockwise270}) {
    QVideoFrameFormat format(QSize(320,180), QVideoFrameFormat::Format_BGRA8888);
    format.setRotation(rotation);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly)) { rotatedFrameDimensions = false; continue; }
    std::memset(frame.bits(0), 255, frame.mappedBytes(0));
    frame.unmap();
    frame.setRotation(QtVideo::Rotation::Clockwise90); // Presentation metadata is ignored by toImage.
    const QSize expected = FrameConversion::toImage(frame).size();
    CanvasPreview preview;
    preview.setSize(QSizeF(320,180));
    preview.videoSink()->setVideoFrame(frame);
    preview.cameraSink()->setVideoFrame(frame);
    rotatedFrameDimensions &= !expected.isEmpty() && preview.sourceFrameSize() == expected && preview.cameraFrameSize() == expected;
    rotatedFrameDimensions &= await([&] { return preview.renderedSerial() > 0 && preview.performanceMetrics().value("pending_jobs").toInt() == 0; });
    rotatedFrameDimensions &= preview.sourceFrameSize() == expected && preview.cameraFrameSize() == expected && !preview.renderedImage().isNull();
    preview.clearCameraFrame();
    rotatedFrameDimensions &= preview.cameraFrameSize().isEmpty() && await([&] { return preview.performanceMetrics().value("pending_jobs").toInt() == 0; }) && preview.cameraFrameSize().isEmpty();
  }
  bool asyncPixels = false, newestFrameCoalesced = false, clearInflight = false, cameraClearInflight = false, destructionPending = false;
  {
    CanvasPreview preview; preview.setSize(QSizeF(640,360));
    const auto frame = videoFrame(QSize(320,180), qRgb(15,95,185), 1000000);
    preview.videoSink()->setVideoFrame(frame);
    asyncPixels = await([&] { return preview.renderedSerial() > 0 && preview.performanceMetrics().value("pending_jobs").toInt() == 0; }) &&
        preview.renderedImage() == CanvasRenderer::render(FrameConversion::toImage(frame), preview.canvasSettings(), {}, QSize(640,360), {}, {}, {}, 0, 1);
    const quint64 before = preview.renderedSerial();
    QVideoFrame newest;
    for (int i = 0; i < 50; ++i) {
      newest = videoFrame(QSize(320,180), qRgb(i*4, 80, 180), 2000000+i*10000);
      preview.videoSink()->setVideoFrame(newest);
    }
    newestFrameCoalesced = await([&] { return preview.renderedSerial() > before && preview.performanceMetrics().value("pending_jobs").toInt() == 0; }) &&
        preview.renderedFrameTime() == 2.49 && preview.sourceImage() == FrameConversion::toImage(newest) &&
        preview.renderedImage() == CanvasRenderer::render(FrameConversion::toImage(newest), preview.canvasSettings(), {}, QSize(640,360), {}, {}, {}, 0, 2.49) &&
        preview.renderedSerial() - before < 50;
  }
  {
    CanvasPreview preview; preview.setSize(QSizeF(1920,1080));
    preview.videoSink()->setVideoFrame(videoFrame(QSize(2560,1440), qRgb(180,20,40), 3000000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    const bool busy = await([&] { return preview.performanceMetrics().value("pending_jobs").toInt() > 0; });
    preview.clearFrame();
    clearInflight = busy && !preview.hasFrame() && preview.renderedImage().isNull() &&
        await([&] { return preview.performanceMetrics().value("pending_jobs").toInt() == 0; }) &&
        preview.renderedImage().isNull() && !preview.hasFrame();
  }
  {
    CanvasPreview preview; preview.setSize(QSizeF(1920,1080));
    const auto frame = videoFrame(QSize(2560,1440), qRgb(20,180,40), 4000000);
    preview.videoSink()->setVideoFrame(frame);
    preview.cameraSink()->setVideoFrame(videoFrame(QSize(320,180), qRgb(180,30,40), 4000000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    preview.clearCameraFrame();
    cameraClearInflight = preview.cameraFrameSize().isEmpty() &&
        await([&] { return preview.renderedSerial() > 0 && preview.performanceMetrics().value("pending_jobs").toInt() == 0; }) &&
        preview.cameraFrameSize().isEmpty() && preview.renderedImage() ==
        CanvasRenderer::render(FrameConversion::toImage(frame), preview.canvasSettings(), {}, QSize(1920,1080), {}, {}, {}, 0, 4);
  }
  {
    auto preview = std::make_unique<CanvasPreview>(); preview->setSize(QSizeF(1920,1080));
    preview->videoSink()->setVideoFrame(videoFrame(QSize(2560,1440), qRgb(20,40,180), 5000000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    destructionPending = preview->performanceMetrics().value("pending_jobs").toInt() > 0;
    preview.reset();
    QCoreApplication::processEvents();
  }
  const bool passed=asyncPixels&&newestFrameCoalesced&&clearInflight&&cameraClearInflight&&destructionPending&&rotatedFrameDimensions&&staticRasterMatches&&cameraSettles&&orderDelay&&cropGeometry&&shortSettles&&endMatches&&startBackground&&styles&&invalid&&cameraHidden&&first!=end;
  QDir().mkpath(directory); QFile file(QDir(directory).filePath("visual-effects-smoke.json"));
  if(!file.open(QIODevice::WriteOnly))return 2;
  file.write(QJsonDocument(QJsonObject{{"passed",passed},{"asyncPreviewPixels",asyncPixels},{"newestFrameCoalesced",newestFrameCoalesced},{"clearInflight",clearInflight},{"cameraClearInflight",cameraClearInflight},{"destructionPending",destructionPending},{"rotatedFrameDimensions",rotatedFrameDimensions},{"staticRasterMatches",staticRasterMatches},{"cameraSettles",cameraSettles},{"orderDelay",orderDelay},{"cropGeometry",cropGeometry},{"shortDurationSettles",shortSettles},{"startBackground",startBackground},{"endMatchesDisabled",endMatches},{"frameStyles",styles},{"invalidParameters",invalid},{"cameraHiddenAtStart",cameraHidden}}).toJson());
  return passed?0:1;
}

