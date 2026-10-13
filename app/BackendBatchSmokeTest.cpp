#include "BackendBatchSmokeTest.h"
#include "AudioMix.h"
#include "CameraMotion.h"
#include "CanvasRenderer.h"
#include "ExportController.h"
#include "PcmWaveFile.h"
#include "ProjectController.h"
#include "RecoveryController.h"
#include "ScreenEffects.h"
#include "AppSettings.h"
#include "UpdateController.h"
#include "EffectControllerSmokeTest.h"
#include "ThirdEffectsSmokeTest.h"
#include <QAudioBufferOutput>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTimer>
#include <QSettings>
#include <cmath>
#include <functional>
namespace {
bool wait(const std::function<bool()> &ready) {
  QEventLoop loop;
  QTimer poll, deadline;
  poll.setInterval(10); deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(); deadline.start(20000);
  if (!ready()) loop.exec();
  return ready();
}
}
int runBackendBatchSmokeTest(QGuiApplication &app, const QString &source, const QString &directory) {
  Q_UNUSED(app);
  QDir out(directory);
  if ((out.exists() && !out.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) || !QDir().mkpath(directory)) return 2;
  QJsonObject checks;
  checks["thirdEffectsWiring"] = thirdEffectsSmoke(QUrl::fromLocalFile(source));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,out.filePath("settings"));
  AppSettings applicationSettings;
  const bool directories=applicationSettings.setValue("projectDirectory",out.filePath("projects")) &&
    applicationSettings.setValue("exportDirectory",out.filePath("exports"));
  const auto suggested=applicationSettings.suggestedProjectUrl("Ready.dogscproject");
  checks["settingsDestinations"] = directories && suggested.toLocalFile().endsWith("/projects/Ready.dogscproject") &&
    applicationSettings.suggestedExportUrl("Ready.mp4","mp4").toLocalFile().endsWith("/exports/Ready.mp4") &&
    applicationSettings.suggestedProjectUrl("../escape").isEmpty();
  checks["effectivePreferences"] = applicationSettings.setValue("theme","dark") && applicationSettings.effectiveTheme()=="dark" &&
    applicationSettings.setValue("language","en") && applicationSettings.effectiveLanguage()=="en" &&
    applicationSettings.setValue("completionSound",false) && !applicationSettings.notifyCompletion();
  UpdateController updates(&applicationSettings,false);
  checks["offlineUpdateIdle"] = !updates.busy() && updates.status()=="idle";
  auto makeTone = [&](const QString &name, double hz) {
    QByteArray data(28800 * 8, '\0');
    auto values = reinterpret_cast<float *>(data.data());
    for (int i = 0; i < 28800; ++i)
      values[i*2] = values[i*2+1] = float(.12 * std::sin(i * hz * 6.283185307179586 / 48000));
    PcmWaveWriter writer; QString error;
    return writer.open(out.filePath(name), &error) && writer.append(QAudioBuffer(data, AudioMix::format(), 0), &error) && writer.finalize(&error);
  };
  checks["wavTracks"] = makeTone("system.wav",660) && makeTone("mic.wav",990);
  const QUrl system = QUrl::fromLocalFile(out.filePath("system.wav"));
  const QUrl mic = QUrl::fromLocalFile(out.filePath("mic.wav"));
  const QVariantMap mixSettings{{"systemVolume",.5},{"microphoneVolume",.9},{"systemMuted",false},{"microphoneMuted",true}};
  AudioMix mixer; QString error;
  bool mixed = mixer.open(system,mic,mixSettings,&error);
  const auto pcm = mixer.readFrames(0,4800,&error);
  mixed = mixed && pcm.size()==4800*8 && std::abs(reinterpret_cast<const float *>(pcm.constData())[2] - .06 * std::sin(660*6.283185307179586/48000))<1e-5;
  checks["gainMuteMix"] = mixed;
  checks["shortTrackPadding"] = mixer.readFrames(28800,480,&error)==QByteArray(480*8,'\0');
  ProjectController project;
  bool model = project.newRecording(QUrl::fromLocalFile(source),3) && project.setAudioMedia(system,mic);
  checks["effectEditingApi"] = effectControllerSmoke(project);
  project.setAudioOption("systemVolume",.4); project.undo();
  checks["audioUndo"] = std::abs(project.audioSettings().value("systemVolume").toDouble()-.82)<1e-8;
  model = model && project.split(1) && project.split(2);
  project.selectClip(0); project.selectClip(1,false,true);
  model = model && project.selectedClips().size()==2 && project.setSelectedClipsRate(2);
  const auto range = project.clipsInRange(.25,1.25);
  double duration=0; for (const auto &clip:range) duration+=clip.toMap().value("duration").toDouble();
  checks["selectionRange"] = model && std::abs(duration-1)<1e-8;
  checks["batchDeleteUndo"] = project.removeSelectedClips() && project.clips().size()==1;
  project.undo();
  checks["restoreMerge"] = project.trimClipBoundary(0,.2,1) && project.restoreClipHead(0) && project.mergeAdjacent(0);
  QImage sticker(32,32,QImage::Format_ARGB32); sticker.fill(Qt::yellow); sticker.save(out.filePath("sticker.png"));
  const auto stickerId=project.addSticker(QUrl::fromLocalFile(out.filePath("sticker.png")),0,.5);
  checks["stickerImport"] = !stickerId.isEmpty() && project.updateSticker(stickerId,{{"rotation",15.0}});
  const QUrl package=QUrl::fromLocalFile(out.filePath("Batch.dogscproject"));
  bool persisted=project.saveAs(package);
  ProjectController reopened;
  persisted=persisted && reopened.openProject(package) && !reopened.systemAudioSource().isEmpty() && reopened.canvasSettings().value("stickers").toList().size()==1;
  checks["schema8Roundtrip"] = persisted;
  project.setAudioOption("systemVolume",.35);
  const bool resaved=project.save();
  QFile mainJson(QDir(package.toLocalFile()).filePath("project.json"));
  QByteArray savedJson;
  if(mainJson.open(QIODevice::ReadOnly)){savedJson=mainJson.readAll();mainJson.close();}
  if(mainJson.open(QIODevice::WriteOnly|QIODevice::Truncate)){mainJson.write("{broken");mainJson.close();}
  ProjectController recovered;
  checks["previousJsonRecovery"] = resaved && recovered.openProject(package) &&
    std::abs(recovered.audioSettings().value("systemVolume").toDouble()-.82)<1e-8;
  auto future=QJsonDocument::fromJson(savedJson).object(); future["formatVersion"]=999;
  if(mainJson.open(QIODevice::WriteOnly|QIODevice::Truncate)){mainJson.write(QJsonDocument(future).toJson());mainJson.close();}
  ProjectController rejected;
  checks["futureVersionRejected"] = !savedJson.isEmpty() && !rejected.openProject(package);
  if(mainJson.open(QIODevice::WriteOnly|QIODevice::Truncate)){mainJson.write(savedJson);mainJson.close();}
  project.setAudioOption("systemVolume",.3);
  const auto location=project.projectLocation(); const bool dirty=project.dirty();
  RecoveryController recovery(&project,out.filePath("recovery"));
  checks["recoveryPreservesWork"] = recovery.snapshotNow() && project.projectLocation()==location && project.dirty()==dirty && reopened.openProject(recovery.lastSnapshot());
  checks["safeRename"] = project.renameProject("Renamed");
  auto settings=CanvasRenderer::defaults();
  QImage screen(320,180,QImage::Format_ARGB32); screen.fill(Qt::green);
  const auto base=CanvasRenderer::render(screen,settings,{},QSize(320,180));
  settings["screenRotationY"]=25.;
  checks["projectedPixels"] = CanvasRenderer::render(screen,settings,{},QSize(320,180))!=base;
  settings=CanvasRenderer::defaults();
  settings["blurRegions"]=QVariantList{QVariantMap{{"sourceStart",0.0},{"sourceDuration",1.0},{"x",.25},{"y",.25},{"width",.5},{"height",.5},{"style","spotlight"},{"strength",.8}}};
  checks["spotlightPixels"] = ScreenEffects::applyRegions(screen,settings,.5)!=screen;
  QVariantMap camera{{"offset",.1},{"syncPoints",QVariantList{QVariantMap{{"sourceTime",0.0},{"cameraTime",0.0}},QVariantMap{{"sourceTime",10.0},{"cameraTime",11.0}}}}};
  checks["cameraSyncMapping"] = CameraMotion::validate(camera) && std::abs(CameraMotion::sourceToCameraTime(camera,5)-5.4)<1e-8;
  ExportController exporter;
  checks["freeRateModel"] = project.setClipRate(0,2.5);
  project.setAudioOption("systemVolume",.5);project.setAudioOption("microphoneMuted",true);
  exporter.setProject(&project);
  const auto summary=exporter.describeProjectExport({{"audioMode","audioOnly"}},0,.3);
  checks["exportSummary"] = summary.value("valid").toBool() && summary.value("clipCount").toInt()==1 &&
    !exporter.describeProjectExport({},-.1,.3).value("valid").toBool();
  const bool accepted=exporter.startProject(QUrl::fromLocalFile(out.filePath("mixed.m4a")),{{"audioMode","audioOnly"}},0,.3);
  bool exported=accepted && wait([&]{return !exporter.busy();}) && exporter.error().isEmpty();
  QMediaPlayer player; QAudioBufferOutput decoded; player.setAudioBufferOutput(&decoded);
  double energy=0; qint64 samples=0;
  QObject::connect(&decoded,&QAudioBufferOutput::audioBufferReceived,&player,[&](const QAudioBuffer &buffer){
    if (buffer.format().sampleFormat()!=QAudioFormat::Float) return;
    for(int i=0;i<buffer.sampleCount();++i){const double value=buffer.constData<float>()[i];energy+=value*value;++samples;}
  });
  if(exported){player.setSource(exporter.lastExport());player.play();exported=wait([&]{return player.mediaStatus()==QMediaPlayer::EndOfMedia || player.error()!=QMediaPlayer::NoError;}) && player.error()==QMediaPlayer::NoError;}
  checks["actualMixedAudioExport"] = exported && samples>0 && energy/samples>.00001;
  checks["actualRangeDuration"] = exported && std::abs(player.duration()-300)<=40;
  player.stop();
  project.setAudioOption("systemMuted",true);
  energy=0;samples=0;
  const bool muteAccepted=exporter.startProject(QUrl::fromLocalFile(out.filePath("mixed-muted.m4a")),{{"audioMode","audioOnly"}},0,.3);
  bool mutedExport=muteAccepted && wait([&]{return !exporter.busy();}) && exporter.error().isEmpty();
  if(mutedExport){player.setSource(exporter.lastExport());player.play();mutedExport=wait([&]{return player.mediaStatus()==QMediaPlayer::EndOfMedia || player.error()!=QMediaPlayer::NoError;}) && player.error()==QMediaPlayer::NoError;}
  checks["actualExportMute"] = mutedExport && samples>0 && energy/samples<1e-9;
  player.stop();
  bool passed=true; for(auto it=checks.begin();it!=checks.end();++it) passed=passed && it.value().toBool();
  checks["success"]=passed;
  checks["exportError"]=exporter.error();
  QFile report(out.filePath("backend-batch-result.json"));
  if(!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(checks).toJson())<0) return 3;
  return passed?0:1;
}
