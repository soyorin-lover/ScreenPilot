#include "TimelineInteractionSmokeTest.h"
#include "ProjectController.h"
#include "EffectController.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QJSValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <memory>
int runTimelineInteractionSmokeTest(QGuiApplication &app, const QString &source, const QString &directory) {
  ProjectController p;
  bool model=p.newRecording(QUrl::fromLocalFile(source),20);
  auto id=p.addManualFraming(2);
  model=model && !id.isEmpty() && p.updateManualFraming(id,{{"end",6.0},{"scale",2.5}});
  model=model && p.split(10) && p.setClipRate(0,2) && p.moveClip(1,0);
  // Output 11.5 maps to source 3 after reorder and a 2x clip.
  model=model && std::abs(p.sourceTime(11.5)-3)<1e-8;
  // Active-enter and settled split handoffs stay continuous at the cut.
  for (const double point : {2.2, 4.0}) {
    p.setSelectedManualFraming(id);
    const double output=10+point/2;
    const double left=p.framingAt(output-0.000001).value("scale").toDouble();
    model=model && p.splitManualFraming(output);
    const double right=p.framingAt(output+0.000001).value("scale").toDouble();
    model=model && std::abs(left-right)<.001;
    p.undo();
  }
  p.setSelectedManualFraming(id);
  const auto before=p.manualFramingSegments();
  model=model && p.splitManualFraming(11.5) && p.manualFramingSegments().size()==2;
  model=model && p.manualFramingSegments().last().toMap().value("scale").toDouble()==2.5;
  p.undo(); model=model && p.manualFramingSegments()==before;
  p.setSelectedManualFraming(id);
  model=model && p.copyManualFraming();
  auto pasted=p.pasteManualFraming(12);
  model=model && !pasted.isEmpty() && pasted!=id;
  const auto pastedMap=p.manualFramingSegments().last().toMap();
  model=model && pastedMap.value("start").toDouble()==4 && pastedMap.value("end").toDouble()==8;
  p.updateManualFraming(pasted,{{"scale",1.2}});
  model=model && p.pasteManualFramingProperties() && p.manualFramingSegments().last().toMap().value("start").toDouble()==4;
  p.setSelectedClip(p.selectedClip()); model=model && p.selectedManualFraming().isEmpty();
  p.discardChanges(); model=model && p.newRecording(QUrl::fromLocalFile(source),20) && p.hasManualClipboard();
  id=p.addManualFraming(2);
  EffectController effects(&p);
  QQmlEngine engine; engine.rootContext()->setContextProperty("project",&p);
  engine.rootContext()->setContextProperty("effects",&effects);
  QQmlComponent mediaComponent(&engine);
  mediaComponent.setData("import QtQuick; QtObject { signal changed(); property int revision: 0; function thumbnail(t) { return ''; } function amplitude(t) { return 0; } }", QUrl());
  std::unique_ptr<QObject> media(mediaComponent.create());
  engine.rootContext()->setContextProperty("timelineMedia",media.get());
  QQmlComponent stripComponent(&engine,QUrl("qrc:/qt/qml/ScreenPilotApp/TimelineStrip.qml"));
  std::unique_ptr<QObject> stripObject(stripComponent.create());
  auto strip=qobject_cast<QQuickItem *>(stripObject.get());
  QQmlComponent component(&engine,QUrl("qrc:/qt/qml/ScreenPilotApp/ManualZoomTrack.qml"));
  std::unique_ptr<QObject> object(component.create());
  auto item=qobject_cast<QQuickItem *>(object.get());
  bool mouse=item && strip;
  QJsonObject effectDrags;
  QJsonObject clipChecks;
  bool clipMouse=false;
  if(item) {
    QQuickWindow window; window.resize(1000,80); item->setParentItem(window.contentItem()); item->setWidth(1000); item->setHeight(48); item->setProperty("compact",true); item->setProperty("snappingEnabled",false);
    if (strip) { strip->setWidth(1000); strip->setHeight(118); item->setProperty("strip",QVariant::fromValue(static_cast<QObject *>(strip))); }
    window.show();
    // Let the first polish pass create and lay out the repeater delegates
    // before sending input; processEvents alone can precede that frame.
    QElapsedTimer polish;
    polish.start();
    while (polish.elapsed() < 150)
      app.processEvents(QEventLoop::AllEvents, 10);
    auto send=[&](QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
      QMouseEvent event(type,pos,window.mapToGlobal(pos.toPoint()),button,buttons,modifiers); QGuiApplication::sendEvent(&window,&event); app.processEvents();
    };
    const auto initial=p.manualFramingSegments();
    send(QEvent::MouseButtonPress,{150,20},Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseMove,{200,20},Qt::NoButton,Qt::LeftButton);
    mouse=mouse && item->property("dragActive").toBool() && p.manualFramingSegments()==initial;
    send(QEvent::MouseButtonRelease,{200,20},Qt::LeftButton,Qt::NoButton);
    mouse=mouse && !item->property("dragActive").toBool() && std::abs(p.manualFramingSegments().first().toMap().value("start").toDouble()-3)<.01;
    p.undo(); mouse=mouse && p.manualFramingSegments()==initial;
    send(QEvent::MouseButtonPress,{150,20},Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseMove,{200,20},Qt::NoButton,Qt::LeftButton);
    QMetaObject::invokeMethod(item,"cancelDrag");
    send(QEvent::MouseButtonRelease,{200,20},Qt::LeftButton,Qt::NoButton);
    mouse=mouse && p.manualFramingSegments()==initial;
    p.updateManualFraming(id,{{"end",6.0}});
    p.setSelectedClip(0); mouse=mouse && p.trimStart(3) && p.setClipRate(0,2);
    app.processEvents();
    send(QEvent::MouseButtonPress,{2,20},Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseMove,{52,20},Qt::NoButton,Qt::LeftButton);
    send(QEvent::MouseButtonRelease,{52,20},Qt::LeftButton,Qt::NoButton);
    mouse=mouse && std::abs(p.manualFramingSegments().first().toMap().value("start").toDouble()-3.85)<.01;
    // Reordered source ranges and minimum-width clips share the same geometry.
    // Source-distance integration must remain positive across a backwards cut.
    mouse=mouse && p.split(4) && p.moveClip(1,0);
    app.processEvents();
    QVariant xBefore, xAfter, distance;
    const double cut=p.clips().first().toMap().value("duration").toDouble();
    QMetaObject::invokeMethod(strip,"xForTime",Q_RETURN_ARG(QVariant,xBefore),Q_ARG(QVariant,cut-.1),Q_ARG(QVariant,false));
    QMetaObject::invokeMethod(strip,"xForTime",Q_RETURN_ARG(QVariant,xAfter),Q_ARG(QVariant,cut+.1),Q_ARG(QVariant,false));
    QMetaObject::invokeMethod(strip,"sourceDeltaForX",Q_RETURN_ARG(QVariant,distance),Q_ARG(QVariant,xBefore),Q_ARG(QVariant,xAfter));
    mouse=mouse && std::abs(distance.toDouble()-.4)<.001;
    // Effect movement at that cut preserves its source duration and direction.
    const auto effectId=effects.add("blurRegions",4,1);
    QQmlComponent laneComponent(&engine,QUrl("qrc:/qt/qml/ScreenPilotApp/EffectLane.qml"));
    std::unique_ptr<QObject> laneObject(laneComponent.create());
    auto lane=qobject_cast<QQuickItem *>(laneObject.get());
    mouse=mouse && lane && !effectId.isEmpty();
    if(lane) {
      lane->setParentItem(window.contentItem()); lane->setWidth(1000); lane->setHeight(44);
      lane->setProperty("track","blurRegions"); lane->setProperty("strip",QVariant::fromValue(static_cast<QObject *>(strip)));
      lane->setProperty("items",effects.blurRegions());
      polish.restart();
      while(polish.elapsed()<100) app.processEvents(QEventLoop::AllEvents,10);
      QVariant effectX;
      QMetaObject::invokeMethod(strip,"xForTime",Q_RETURN_ARG(QVariant,effectX),Q_ARG(QVariant,cut+.7),Q_ARG(QVariant,false));
      const double px=effectX.toDouble();
      send(QEvent::MouseButtonPress,{px,20},Qt::LeftButton,Qt::LeftButton);
      send(QEvent::MouseMove,{px+10,20},Qt::NoButton,Qt::LeftButton);
      mouse=mouse && effects.blurRegions().first().toMap().value("sourceStart").toDouble()==4;
      send(QEvent::MouseButtonRelease,{px+10,20},Qt::LeftButton,Qt::NoButton);
      const auto moved=effects.blurRegions().first().toMap();
      mouse=mouse && moved.value("sourceStart").toDouble()>4 && moved.value("sourceDuration").toDouble()==1;
      mouse=mouse && p.trimClipBoundary(0,3,11);
      QVariant pieces;
      QMetaObject::invokeMethod(lane,"pieces",Q_RETURN_ARG(QVariant,pieces),Q_ARG(QVariant,effects.blurRegions().first()));
      if(pieces.metaType()==QMetaType::fromType<QJSValue>()) pieces=pieces.value<QJSValue>().toVariant();
      mouse=mouse && pieces.toList().size()==2;
      lane->setParentItem(nullptr);
    }
    item->setParentItem(nullptr);
    QDir().mkpath(directory);
    QTemporaryDir fixtures(QDir(directory).filePath("fixtures-XXXXXX"));
    const QString cameraPath=QDir(fixtures.path()).filePath("timeline-camera.mp4");
    const bool cameraReady=QFile::copy(source,cameraPath) && p.setCameraMedia(QUrl::fromLocalFile(cameraPath));
    QImage sticker(24,24,QImage::Format_ARGB32); sticker.fill(Qt::cyan);
    const QString stickerPath=QDir(fixtures.path()).filePath("timeline-sticker.png");
    const bool stickerReady=sticker.save(stickerPath);
    for(const QString track : {QString("screenMotionSegments"),QString("blurRegions"),QString("stickers"),QString("cameraMotionSegments"),QString("syncPoints")}) {
      const bool point=track=="syncPoints", camera=track=="cameraMotionSegments";
      const QString effectId=track=="stickers" ? (stickerReady ? effects.addSticker(QUrl::fromLocalFile(stickerPath),4,1) : QString()) : effects.add(track,4,point ? 0 : 1);
      QQmlComponent laneComponent(&engine,QUrl("qrc:/qt/qml/ScreenPilotApp/EffectLane.qml"));
      std::unique_ptr<QObject> laneObject(laneComponent.create());
      auto lane=qobject_cast<QQuickItem *>(laneObject.get());
      bool passed=lane && !effectId.isEmpty() && (!(camera || point) || cameraReady);
      if(passed) {
        lane->setParentItem(window.contentItem()); lane->setWidth(1000); lane->setHeight(44);
        lane->setProperty("track",track); lane->setProperty("strip",QVariant::fromValue(static_cast<QObject *>(strip)));
        lane->setProperty("items",effects.items(track));
        polish.restart(); while(polish.elapsed()<100) app.processEvents(QEventLoop::AllEvents,10);
        const auto original=effects.items(track);
        QVariant effectX;
        QMetaObject::invokeMethod(strip,"xForTime",Q_RETURN_ARG(QVariant,effectX),Q_ARG(QVariant,point ? .5 : .7),Q_ARG(QVariant,false));
        const double px=effectX.toDouble();
        send(QEvent::MouseButtonPress,{px,22},Qt::LeftButton,Qt::LeftButton);
        send(QEvent::MouseMove,{px+10,22},Qt::NoButton,Qt::LeftButton);
        passed=effects.items(track)==original;
        send(QEvent::MouseButtonRelease,{px+10,22},Qt::LeftButton,Qt::NoButton);
        const auto moved=effects.selectedItem(track);
        passed=passed && moved.value(point ? "sourceTime" : camera ? "start" : "sourceStart").toDouble()>4;
        if(camera) passed=passed && std::abs(moved.value("end").toDouble()-moved.value("start").toDouble()-1)<1e-8;
        else if(!point) passed=passed && moved.value("sourceDuration").toDouble()==1;
        p.undo(); passed=passed && effects.items(track)==original;
        lane->setParentItem(nullptr);
      }
      effectDrags[track]=passed; mouse=mouse && passed;
    }
    if(strip) {
      clipChecks["mouseBeforeClip"]=mouse;
      window.resize(1000,160); strip->setParentItem(window.contentItem());
      auto settle=[&]() { polish.restart(); while(polish.elapsed()<100) app.processEvents(QEventLoop::AllEvents,10); };
      settle();
      const auto original=p.clips();
      clipChecks["originalFirst"]=QJsonObject::fromVariantMap(original.first().toMap());
      QVariant endX;
      QMetaObject::invokeMethod(strip,"xForTime",Q_RETURN_ARG(QVariant,endX),Q_ARG(QVariant,4.0),Q_ARG(QVariant,true));
      send(QEvent::MouseButtonPress,{2,60},Qt::LeftButton,Qt::LeftButton);
      send(QEvent::MouseMove,{52,60},Qt::NoButton,Qt::LeftButton);
      clipMouse=p.clips()==original;
      clipChecks["draftUncommitted"]=clipMouse;
      send(QEvent::MouseButtonRelease,{52,60},Qt::LeftButton,Qt::NoButton);
      clipMouse=clipMouse && std::abs(p.clips().first().toMap().value("sourceStart").toDouble()-(3+50/endX.toDouble()*8))<.001;
      clipChecks["width"]=endX.toDouble();
      clipChecks["leftStart"]=p.clips().first().toMap().value("sourceStart").toDouble();
      p.undo(); clipChecks["leftUndo"]=p.clips()==original; clipMouse=clipMouse && p.clips()==original;
      settle();
      const double right=endX.toDouble()-2;
      send(QEvent::MouseButtonPress,{right,60},Qt::LeftButton,Qt::LeftButton);
      send(QEvent::MouseMove,{right+50,60},Qt::NoButton,Qt::LeftButton);
      send(QEvent::MouseButtonRelease,{right+50,60},Qt::LeftButton,Qt::NoButton);
      clipMouse=clipMouse && std::abs(p.clips().first().toMap().value("sourceDuration").toDouble()-(8+50/endX.toDouble()*8))<.001;
      clipChecks["rightSourceDuration"]=p.clips().first().toMap().value("sourceDuration").toDouble();
      p.undo();
      clipChecks["rightUndo"]=p.clips()==original;
      settle();
      send(QEvent::MouseButtonPress,{2,60},Qt::LeftButton,Qt::LeftButton);
      send(QEvent::MouseMove,{1002,60},Qt::NoButton,Qt::LeftButton);
      send(QEvent::MouseButtonRelease,{1002,60},Qt::LeftButton,Qt::NoButton);
      clipMouse=clipMouse && std::abs(p.clips().first().toMap().value("duration").toDouble()-.05)<.001;
      clipChecks["minimumDuration"]=p.clips().first().toMap().value("duration").toDouble();
      p.undo(); clipChecks["minimumUndo"]=p.clips()==original; clipMouse=clipMouse && p.clips()==original;
      settle();
      auto click=[&](double x,Qt::KeyboardModifiers modifiers) {
        send(QEvent::MouseButtonPress,{x,60},Qt::LeftButton,Qt::LeftButton,modifiers);
        send(QEvent::MouseButtonRelease,{x,60},Qt::LeftButton,Qt::NoButton,modifiers);
      };
      click(250,Qt::NoModifier); click(750,Qt::ControlModifier);
      clipMouse=clipMouse && p.selectedClips().size()==2;
      clipChecks["controlAddCount"]=p.selectedClips().size();
      click(750,Qt::ControlModifier); clipMouse=clipMouse && p.selectedClips().size()==1;
      clipChecks["controlRemoveCount"]=p.selectedClips().size();
      click(250,Qt::NoModifier); click(750,Qt::ShiftModifier);
      clipMouse=clipMouse && p.selectedClips().size()==2;
      clipChecks["shiftCount"]=p.selectedClips().size();
      mouse=mouse && clipMouse;
      strip->setParentItem(nullptr);
    }
  }
  QDir().mkpath(directory); QFile report(QDir(directory).filePath("timeline-interaction-smoke.json"));
  if(!report.open(QIODevice::WriteOnly)) return 2;
  report.write(QJsonDocument(QJsonObject{{"passed",model&&mouse},{"model",model},{"mouse",mouse},{"effectDrags",effectDrags},{"clipMouse",clipMouse},{"clipChecks",clipChecks},{"qmlError",component.errorString()}}).toJson());
  return model&&mouse ? 0 : 3;
}

