#pragma once
#include "AppSettings.h"
#include "CanvasPreview.h"
#include "ProjectController.h"
#include "RecoveryController.h"
#include "ExportController.h"
#include "EffectController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QQuickItem>
#include <QQuickWindow>
#include <functional>
inline int thirdUiSmoke(QGuiApplication &app,QObject *root,ProjectController &project,
                        RecoveryController &recovery,AppSettings &settings,ExportController &exporter,EffectController &effects,const QString &source,const QString &directory) {
  QDir out(directory);
  if(directory.isEmpty() || (out.exists() && !out.entryList(QDir::NoDotAndDotDot|QDir::AllEntries).isEmpty()) || !QDir().mkpath(directory)) return 2;
  auto window=qobject_cast<QQuickWindow *>(root);
  auto editor=root->findChild<QQuickItem *>("editorView");
  auto player=root->findChild<QMediaPlayer *>("editorPlayer");
  auto camera=root->findChild<QMediaPlayer *>("editorCameraPlayer");
  auto panel=root->findChild<QObject *>("recoveryPanel");
  auto preferences=root->findChild<QObject *>("settingsDialog");
  auto preview=root->findChild<CanvasPreview *>("canvasPreview");
  if(!window || !editor || !player || !camera || !panel || !preferences || !preview) return 3;
  auto settle=[&](int ms) { QElapsedTimer t;t.start();while(t.elapsed()<ms) app.processEvents(QEventLoop::AllEvents,10); };
  auto wait=[&](const std::function<bool()> &ok) { QElapsedTimer t;t.start();while(!ok() && t.elapsed()<6000) settle(20);return ok(); };
  auto pause=[&] {QMetaObject::invokeMethod(editor,"cancelTimelineInteraction");player->pause();camera->pause();settle(80);};
  auto key=[&](int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress,code,modifiers),release(QEvent::KeyRelease,code,modifiers);
    QGuiApplication::sendEvent(window,&press);QGuiApplication::sendEvent(window,&release);settle(80);
  };
  QJsonObject checks;
  checks["loaded"] = project.newRecording(QUrl::fromLocalFile(source),3) && project.setCameraMedia(QUrl::fromLocalFile(source));
  root->setProperty("editing",true);window->show();window->requestActivate();
  checks["initialMedia"] = wait([&]{return preview->hasFrame() && preview->cameraFrameSize().isValid();});pause();
  QImage image(24,24,QImage::Format_ARGB32);image.fill(Qt::cyan);
  const QString stickerPath=out.filePath(QString::fromUtf8("贴图 # %.png"));image.save(stickerPath);
  const auto sticker=effects.addSticker(QUrl::fromLocalFile(stickerPath),.1,.5);
  checks["stickerCreated"] = !sticker.isEmpty();
  editor->setProperty("inspectorTab",8);
  std::function<QQuickItem *(QQuickItem *,const QString &)> findItem=[&](QQuickItem *parent,const QString &name)->QQuickItem * {
    if(parent->objectName()==name)return parent;
    for(auto child:parent->childItems())if(auto found=findItem(child,name))return found;
    return nullptr;
  };
  checks["thumbnailDecoded"] = wait([&]{auto item=findItem(window->contentItem(),"stickerThumbnail-"+sticker);return item && item->property("status").toInt()==1;});
  editor->setProperty("inspectorTab",0);pause();
  project.split(1);project.split(2);project.selectClip(0);project.selectClip(1,false,true);
  checks["multiRate"] = project.selectedClips().size()==2 && project.setSelectedClipsRate(2.5);
  QMetaObject::invokeMethod(editor,"deleteSelection");checks["multiDelete"] = project.clips().size()==1;
  project.undo();checks["multiUndo"] = project.clips().size()==3;pause();
  checks["saved"] = project.saveAs(QUrl::fromLocalFile(out.filePath("Before.dogscproject")));pause();
  QVariant renamed;
  QMetaObject::invokeMethod(editor,"renameProject",Q_RETURN_ARG(QVariant,renamed),Q_ARG(QVariant,QString("After")));
  checks["renameInEditor"] = renamed.toBool() && wait([&]{return !editor->property("renaming").toBool() && project.projectLocation().toLocalFile().endsWith("After.dogscproject");});
  checks["renameError"]=project.error();pause();
  const QUrl renamedUrl=project.projectLocation();
  QMetaObject::invokeMethod(editor,"loadProject",Q_ARG(QVariant,renamedUrl));
  checks["reopenMedia"] = wait([&]{return player->source()==project.source() && camera->source()==project.cameraSource() && preview->hasFrame();});pause();
  project.setAudioOption("systemVolume",.4);pause();
  checks["snapshot"] = recovery.snapshotNow();
  const QUrl snapshot=recovery.lastSnapshot();
  project.setAudioOption("systemVolume",.6);pause();
  QMetaObject::invokeMethod(panel,"restore",Q_ARG(QVariant,snapshot));
  checks["dirtyRestoreGuard"] = project.projectLocation()==renamedUrl && panel->property("pendingRestore").toUrl()==snapshot;
  project.discardChanges();pause();
  QMetaObject::invokeMethod(panel,"restore",Q_ARG(QVariant,snapshot));
  checks["restoreMedia"] = wait([&]{return project.projectLocation()==snapshot && player->source()==project.source() && camera->source()==project.cameraSource() && preview->hasFrame();});pause();
  editor->forceActiveFocus();QMetaObject::invokeMethod(preferences,"open");settle(150);
  const auto before=project.clips();key(Qt::Key_Delete);
  checks["settingsBlocksKeys"] = preferences->property("visible").toBool() && before==project.clips();
  QMetaObject::invokeMethod(preferences,"close");settle(150);
  checks["darkSetting"] = settings.setValue("theme","dark");settle(250);
  checks["darkScreenshot"] = window->grabWindow().save(out.filePath("dark-editor.png"));
  QMetaObject::invokeMethod(preferences,"open");settle(200);
  checks["darkSettingsScreenshot"] = window->grabWindow().save(out.filePath("dark-settings.png"));
  QMetaObject::invokeMethod(preferences,"close");pause();
  const auto destination=QUrl::fromLocalFile(out.filePath("cancelled.mp4"));
  const bool started=exporter.startProject(destination,{{"audioMode","muted"}});
  exporter.cancel();settle(150);
  checks["exportCancel"] = started && !exporter.busy() && !QFileInfo::exists(destination.toLocalFile());
  QMetaObject::invokeMethod(editor,"requestNewRecording");settle(100);
  checks["backToRecorder"] = !root->property("editing").toBool();
  bool success=true;for(auto it=checks.begin();it!=checks.end();++it)if(it.value().isBool())success=success && it.value().toBool();
  checks["success"]=success;
  checks["projectError"]=project.error();checks["recoveryError"]=recovery.error();
  QFile report(out.filePath("third-ui-result.json"));if(!report.open(QIODevice::WriteOnly))return 4;
  report.write(QJsonDocument(checks).toJson());return success?0:1;
}
