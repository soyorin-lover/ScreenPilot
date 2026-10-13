#include "DynamicBackgroundSmokeTest.h"
#include "BackgroundVideo.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QVideoFrameInput>
#include <QVideoFrame>
#include <functional>
namespace {
bool waitFor(const std::function<bool()> &done) {
  QEventLoop loop; QTimer poll, timeout;
  poll.setInterval(10); timeout.setSingleShot(true);
  QObject::connect(&poll,&QTimer::timeout,&loop,[&]{ if(done()) loop.quit(); });
  QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
  poll.start(); timeout.start(60000); if(!done()) loop.exec(); return done();
}
int color(const QImage &image) {
  if(image.isNull()) return -1;
  const QColor c=image.pixelColor(image.width()/2,image.height()/2);
  return c.red()>c.green() && c.red()>c.blue() ? 0 : c.green()>c.blue() ? 1 : 2;
}
}
int runDynamicBackgroundSmokeTest(const QString &directory) {
  QDir().mkpath(directory);
  const QString source=QDir(directory).filePath("dynamic-background-source.mp4");
  QFile::remove(source);
  QMediaCaptureSession session; QMediaRecorder recorder; QVideoFrameInput input;
  session.setRecorder(&recorder); session.setVideoFrameInput(&input);
  QMediaFormat format(QMediaFormat::MPEG4); format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  recorder.setMediaFormat(format); recorder.setVideoResolution(QSize(160,120)); recorder.setVideoFrameRate(30);
  recorder.setOutputLocation(QUrl::fromLocalFile(source)); recorder.setAutoStop(true);
  int sent=0; bool ended=false, pumping=false;
  auto pump=[&] {
    if(pumping) return; pumping=true;
    while(sent<36) {
      QImage image(160,120,QImage::Format_RGB32); image.fill(sent<12?Qt::red:sent<24?Qt::green:Qt::blue);
      QVideoFrame frame(image); frame.setStartTime(qint64(sent)*1000000/30); frame.setEndTime(qint64(sent+1)*1000000/30);
      if(!input.sendVideoFrame(frame)) break; ++sent;
    }
    if(sent==36 && !ended) ended=input.sendVideoFrame({}); pumping=false;
  };
  QObject::connect(&input,&QVideoFrameInput::readyToSendVideoFrame,&recorder,pump);
  recorder.record(); pump();
  const bool generated=waitFor([&]{ return recorder.error()!=QMediaRecorder::NoError || (ended && recorder.recorderState()==QMediaRecorder::StoppedState); }) && recorder.error()==QMediaRecorder::NoError;
  BackgroundVideo background; background.setPath(source);
  bool seeks=generated; QString error;
  for(const auto &sample : {std::pair<double,int>{.1,0},{.6,1},{1.0,2},{1.2,0},{1.3,0},{.6,1},{0.,0}}) {
    bool done=false; QImage image;
    background.request(sample.first,[&](QImage frame,QString message){image=frame;error=message;done=true;});
    seeks &= waitFor([&]{return done;}) && error.isEmpty() && color(image)==sample.second;
  }
  auto canvas=CanvasRenderer::defaults(); canvas["backgroundVideo"]=source;
  canvas["ratio"]="16:9"; canvas["padding"]=100.; canvas["radius"]=0.; canvas["shadow"]=0.;
  canvas["screenFrame"]="browserLight"; canvas["introEnabled"]=true; canvas["introStyle"]="scale";
  const QString output=QDir(directory).filePath("dynamic-background-export.mp4"); QFile::remove(output);
  ExportController exporter;
  if(generated) exporter.start(QUrl::fromLocalFile(source),{
      QVariantMap{{"sourceStart",.2},{"sourceDuration",.9},{"playbackRate",1.5}},
      QVariantMap{{"sourceStart",0.0},{"sourceDuration",1.2},{"playbackRate",1.0}},
      QVariantMap{{"sourceStart",0.0},{"sourceDuration",.2},{"playbackRate",1.0}}},
      QUrl::fromLocalFile(output),{},canvas,{{"audioMode","muted"},{"fps",30}});
  const bool exported=generated && waitFor([&]{return !exporter.busy();}) && exporter.error().isEmpty();
  BackgroundVideo decoded; decoded.setPath(output); bool padding=exported;
  for(const auto &sample : {std::pair<double,int>{.1,0},{.6,1},{1.0,2},{1.3,0},{1.8,1}}) {
    bool done=false; QImage image; QString message;
    decoded.request(sample.first,[&](QImage frame,QString e){image=frame;message=e;done=true;});
    const bool ready=waitFor([&]{return done;});
    if(!image.isNull()) image=image.copy(2,2,2,2);
    padding &= ready && message.isEmpty() && color(image)==sample.second;
  }
  background.setPath(QDir(directory).filePath("missing-background.mp4")); bool missing=false;
  background.request(0,[&](QImage,QString message){missing=!message.isEmpty();});
  const QString corrupt=QDir(directory).filePath("corrupt-background.mp4");
  {QFile f(corrupt); if(f.open(QIODevice::WriteOnly)) f.write("invalid media");}
  background.setPath(corrupt); bool corruptDone=false, corruptFailed=false;
  background.request(0,[&](QImage,QString message){corruptDone=true;corruptFailed=!message.isEmpty();});
  waitFor([&]{return corruptDone;});
  bool invalidExportRejected=true;
  for (const QString &invalid : {QDir(directory).filePath("missing-background.mp4"),corrupt}) {
    auto invalidCanvas=canvas; invalidCanvas["backgroundVideo"]=invalid;
    ExportController invalidExporter;
    const QString invalidTarget=QDir(directory).filePath("invalid-background-export.mp4"); QFile::remove(invalidTarget);
    invalidExporter.start(QUrl::fromLocalFile(source),{QVariantMap{{"sourceStart",0.0},{"sourceDuration",.5},{"playbackRate",1.0}}},QUrl::fromLocalFile(invalidTarget),{},invalidCanvas,{{"audioMode","muted"}});
    invalidExportRejected &= waitFor([&]{return !invalidExporter.busy();}) && !invalidExporter.error().isEmpty() && !QFile::exists(invalidTarget);
  }
  const bool passed=generated && seeks && exported && padding && missing && corruptFailed && invalidExportRejected;
  QFile report(QDir(directory).filePath("dynamic-background-report.json"));
  if(report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(QJsonObject{{"generated",generated},{"seeksAndLoop",seeks},{"exported",exported},{"paddingColors",padding},{"missingRejected",missing},{"corruptRejected",corruptFailed},{"invalidExportsRejected",invalidExportRejected},{"exportError",exporter.error()},{"passed",passed}}).toJson());
  return passed?0:1;
}
