# Claude UI接线清单

更新：2026-10-13。此文是当前UI开发入口，详细参数见[后端API文档](backend-api-handoff.md)。先读AGENTS.md、DESIGN.md和本文件，再修改QML。最终视觉、布局、主题色、文案翻译和控件交互由Claude负责；保留现有录制、未保存保护、返回与摄像头收尾流程。

## 范围

只开发Windows/C++/Qt6。用户明确取消iPhone/iPad/iOS输入与Mac工程兼容，不创建这些页面或占位功能。Linux仍不开展。

本次接续针对已实现产品能力的UI和接线。原生光标消除、完整独立光标形状、ProRes录制、长录音RF64、未完成MP4修复和安装升级仍是后端工作；不能通过新增按钮宣布这些能力已实现。Mac算法和外观完整一致性仍待对齐。

## 按界面区域连接

| 界面区域 | 后端对象/API | UI责任 |
| --- | --- | --- |
| 录制来源与区域 | recorder.sources/selectedSource/captureRegion/readiness | 来源选择、区域拖框与预览；开始前显示readiness.reason |
| 录制规格 | recorder.capabilities/availableVideoCodecs/frameRate/resolutionMode/videoCodec | 根据能力生成选项，不能硬编码未支持的编码 |
| 声音录制 | recorder.systemAudioEnabled/systemAudioScope/microphoneEnabled/microphones/selectedMicrophone/microphoneLevel | 范围选择、麦克风菜单、电平；application模式需窗口或有效进程 |
| 摄像头输入 | cameraRecorder.devices/formats/selectedDevice/selectedFormat/enabled/mirror/status/error | 设备选择、预览和加载/错误状态 |
| 后期混音 | project.audioSettings/setAudioOption | 独立音量、静音；参数随工程与撤销保存 |
| 播放音量 | previewAudioProcessor.volume/muted/error | 只控制监听，不改工程音量与导出；不可直接恢复底层AudioOutput音量造成双声 |
| 主时间线 | project.clips/selectedClips/selectClip/editingState | 多选、拖动、边界手柄、按钮禁用态 |
| 自动/手动运镜 | project.framingSettings/framingSegments/manualFramingSegments及既有命令 | 轨道显示、参数和曲线编辑；自动运镜仍为可选项 |
| 3D/模糊/聚光灯/摄像头运动/同步点/贴图 | effects及project.canvasSettings/cameraSettings | 五类效果轨道、选中检查器、源/输出时间转换 |
| 背景、外框与开场 | project.setBackgroundImage/Video、setCanvasOption、setCameraOption | 沿用现有参数和资源选择，不直接修改文件路径 |
| 工程与恢复 | project.save/saveAs/openProject/renameProject；recoveryController | 保存、重命名、恢复列表与未保存确认；保留返回逻辑 |
| 最近工程/场景预设/备忘录 | recentProjectsController/scenePresets/memoController | 沿用已有完整接口与错误反馈 |
| 导出 | exporter.describeProjectExport/startProject/exportProjectOriginal | 全片/范围、选项、文件选择、摘要、进度和取消 |
| 设置/更新 | appSettings/updateController | 设置页、系统/显式主题语言绑定、更新状态与发布页链接 |

## 录制与设备

`recorder.readiness`提供canStart/reason/busy/hasSelectedSource，是当前配置的启动前检查，不能代替启动后的设备错误处理。根窗口beginRecording()还负责未保存保护、旧播放停止及摄像头协调；UI应继续走这个流程。

选择另一个来源会清除旧区域与显式audioProcessId；先选来源，再调用`setCaptureRegion(Qt.rect(x,y,width,height))`，所有值归一化到完整来源。`clearCaptureRegion()`恢复完整来源。刷新尽量保留同一窗口/屏幕身份；原来源消失时要求重选，不静默切换录别的窗口。麦克风原设备消失时不会自动改录另一只麦克风。

capabilities包含sourceTypes、frameRates、resolutions、videoCodecs、applicationAudio和暂不支持的nativeCursorRemoval/independentCursorShapes/proResRecording。实际编码仍以availableVideoCodecs为准；capability为false的能力不能提供可用操作或静默替代。

## 音频与播放

新录制带独立系统/麦克风WAV。编辑音量用`project.setAudioOption("systemVolume",0.8)`等；systemMuted/microphoneMuted是布尔值。预览和导出读取同一设置，老工程无独立轨继续使用内嵌混音。

独立音轨已通过播放器位置定时调度，主视频无内嵌音频也能播放WAV。底层嵌入音频回调在独立混音时绕过，避免双重播放。监听volume默认0.8，范围0–1；不修改系统音量。

所有速度（含1倍）的监听均由PreviewAudioProcessor自定义音频输出负责；QMediaPlayer的原生audioOutput会被断开，避免Qt在异步初始化设备时使用空通道布局。Claude不要另接AudioOutput作为第二路声音，使用processor.volume/muted即可。无独立音轨时仍通过解码PCM播放旧素材。

保留Editor.qml中seek()/分段播放逻辑：精确跳转或片段切换时调用`previewAudioProcessor.reset(sourceStart,sourceEnd)`，参数是源秒，不能写成输出秒。播放/暂停/变速由播放器驱动，状态变化会更新混音。

## 时间线与效果

`project.editingState(outputSeconds)`返回canSplit/canTrimStart/canTrimEnd/canRemove/canRemoveSelected/canMergeNext/canRestoreHead/canRestoreTail/selectedCount/canUndo/canRedo。按钮禁用态绑定此结果；实际命令仍会验证。

主轨：selectClip(index,extend,toggle)、removeSelectedClips()、setSelectedClipsRate(rate)、trimClipBoundary(index,sourceStart,sourceEnd)、restoreClipHead/Tail(index)、mergeAdjacent(index)、setClipOutputDuration(index,seconds)。自由倍率1–100同时支持保存、预览和导出，菜单预设只是快捷入口。

效果轨名称固定：screenMotionSegments、blurRegions、cameraMotionSegments、syncPoints、stickers。直接绑定effects同名列表属性，以及`effects.selectedIds[track]`/`effects.selectedItems[track]`检查器状态；这些属性有changed通知。items(track)、selectedId(track)、selectedItem(track)是命令式读取。新增返回稳定ID，修改/删除只按ID操作，避免重排后误改其他项。

调用C++ invokable函数不会自动跟踪其内部状态。editingState(outputSeconds)、describeProjectExport()等动态查询应在project.changed/播放头或范围变化时重算；不要只写一个函数调用绑定后假设所有工程变化都会刷新。静态全片摘要可直接绑定exporter.projectSummary。

```js
// 输出播放头对应的源位置新增1秒局部效果。
const id = effects.addAtOutput("blurRegions", positionSeconds, 1)
effects.update("blurRegions", id, {style: "spotlight", strength: 0.7})
effects.select("blurRegions", id)
effects.updateSelected("blurRegions", {x: 0.2, y: 0.2, width: 0.5, height: 0.5})
effects.removeSelected("blurRegions")
```

`add(track,sourceSeconds,duration)`、update/remove使用源秒；`addAtOutput()`只转换起点，duration仍是源时长。贴图使用`effects.addSticker(imageUrl,sourceSeconds,duration)`，资源归项目管理。不得用update改id/imagePath。

`effects.outputTimes(sourceSeconds)`返回重排/重复源范围在输出轨中的全部位置；`effects.sourceTime(outputSeconds)`做反向映射。删除、变速后重新计算轨道位置，不自行复制效果数组到新时间线。

摄像头运动与同步点要求已关联摄像头媒体，区间不得重叠、同步点时间必须递增。靠近素材末尾新增效果时缩短duration，否则会明确拒绝；显示effects.error。改变透视from/to姿态时支持局部字段patch。

## 导出入口

```js
const summary = exporter.describeProjectExport({audioMode: "videoAudio"}, rangeStart, rangeEnd)
if (summary.valid && summary.canStart) {
    exporter.startProject(destinationUrl, {audioMode: "videoAudio"}, rangeStart, rangeEnd)
}
```

省略范围是全片，rangeEnd=-1表示工程末尾。参数范围是输出秒；后端负责映射片段、变速、独立声音、鼠标、运镜、摄像头和画布设置。options只接收audioMode/resolution/fps，默认使用工程导出设置。audioMode为videoAudio/muted/audioOnly。

summary返回valid/error/canStart/rangeStart/rangeEnd/duration/clipCount/audioMode/resolution/fps/hasSeparateAudio/hasCamera，不伪造未解析的像素尺寸。`projectSummary`为默认全片摘要。调用后绑定busy/progress/status/error/lastExport，保留cancel()。目标已存在仍拒绝覆盖。

原始素材使用`exporter.exportProjectOriginal(folderUrl)`，自动包含屏幕、摄像头与独立音轨，放入新子目录。

## 设置与更新

`appSettings.values`保存选项，`setValue(key,value)`返回成功；失败显示error。theme/language允许system/light/dark与system/zh/en（分别取对应值）。effectiveTheme/effectiveLanguage和localeName已解析系统设置，并在系统变化时通知。Claude负责实际配色、翻译资源和文案，不应把存储或locale解析当作完整双语界面。

projectDirectoryUrl/exportDirectoryUrl供文件对话框。suggestedProjectUrl(name)和suggestedExportUrl(name,suffix)验证文件名、创建配置目录、避开已有名称，返回建议URL；不会创建或覆盖目标文件。最终保存/导出仍执行冲突检查。目录分别生效，不把原始录制目录混同工程或导出目录。

previewQuality已接渲染尺寸上限，不影响导出。completionSound已接成功录制/导出后的Windows提示音，尊重系统音量；无需UI重复播放。

updateController.checkForUpdates()手动检查；自动检查尊重autoCheckUpdates，最多每日尝试一次。status为idle/checking/available/upToDate/unavailable/error；busy/latestVersion/releaseUrl/error可绑定。没有稳定发布、离线或请求失败不可显示“已是最新版”。当前只检查稳定版本并提供GitHub发布页，未实现下载安装器或自动替换程序。

## 交接完成标准

先接已有真实接口，再做最终视觉。界面需覆盖默认、选择、加载、执行、失败、取消和未保存状态；返回上一级按钮保留。没有完成的底层能力明确登记，不做演示按钮冒充完成。最小验证只需启动、一次保存重开和短导出/播放，避免重复完整矩阵；硬件与长时间场景另行登记。

本批构建与实际结果登记在[公开验证摘要](validation/public-results.md)，原始媒体、路径和设备信息只留本地忽略目录。

2026-10-13接续结果：Release部署通过，效果编辑/设置/工程范围导出闭环通过；自由倍率2.5的M4A实际导出并重新解码通过。无音轨视频+独立WAV两倍速、旧PCM一倍速、暂停/跳转/结束通过。预览只检验处理后PCM，监听音量0，未修改用户系统音量。版本检查仅完成源码/构建与测试不自动联网的验证，没有把离线/未发布状态冒充升级成功。
