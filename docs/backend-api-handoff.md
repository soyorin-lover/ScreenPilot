# ScreenPilot 后端 API 与 Claude UI 接续

2026-10-13。本批以实现为主，最终视觉与入口组织由Claude负责。下列接口已进入真实工程、录制、预览或导出代码；构建与运行结果另行记录，不把代码存在等同于全部场景验收。

后续接线以[Claude UI接线清单](claude-ui-integration.md)为入口。用户已取消iPhone/iPad/iOS输入；下方批次证据保留历史时间顺序，最新实现状态见接线清单。

## 录制与声音

`recorder.frameRate`：30/60，默认60；使用独立视频输入和帧调度，避免此前仅请求帧率而直接录入全部采集帧。CPU读回/裁切路径会丢弃迟到时隙以保持时间线，不保证所有机器持续4K60。

`recorder.captureRegion`：归一化矩形，默认(0,0,1,1)。`setCaptureRegion(QRectF)`、`clearCaptureRegion()`；裁切实际物理像素，并调整预览和鼠标坐标。切换来源时后端清除旧选区与显式进程ID；UI先选来源再设置区域。

`recorder.systemAudioScope`：`all`/`application`。`audioProcessId`为本次会话的进程ID，0表示从唯一匹配的所选窗口推导。应用声音模式使用真实Windows进程树回环；无有效进程、无系统能力或启动失败时明确报错，不回退成全系统声音。

进程回环依据[微软官方示例](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)，要求Windows构建20348或以上。目标无输出音频时会得到静音；这与采集接口故障不同。

`recorder.lastSystemAudio`、`lastMicrophoneAudio`：完成录制后发布独立48kHz立体声Float WAV路径。录屏仍保留内嵌混音以兼容旧流程。暂停不写入原始轨；重录/丢弃只清理本次拥有的临时素材。经典WAV单文件4GiB上限会明确失败，未实现RF64与分卷。

`project.systemAudioSource`、`microphoneAudioSource`、`audioSettings`：音轨与工程设置。`setAudioMedia(system,microphone)`用于新录制移交；`setAudioOption(key,value)`支持撤销。键为systemVolume/microphoneVolume（0–1）、systemMuted/microphoneMuted；默认音量.82/.9。

独立音轨导出选项：在原 `exporter.start()` options中传入systemAudioSource、microphoneAudioSource和audioSettings。新工程已接通；老工程无独立轨时沿用内嵌音频。混音按源时间读取，并复用裁短、重排、保音高变速链路，不叠加内嵌混音。

`exporter.startOriginal(source,folder,camera,system,microphone)`可复制独立原始声音。编辑预览已接入独立混音与设备格式转换；跨设备和长视频仍需验收。

## 时间线

- `selectedClips`：当前选择索引列表；`selectClip(index,extend,toggle)`支持连续与增减选择。
- `removeSelectedClips()`、`setSelectedClipsRate(rate)`：批量修改，一次撤销，不能删光主轨。
- `trimClipBoundary(index,sourceStart,sourceEnd)`：源时间边界调整。
- `setClipOutputDuration(index,seconds)`：按源长度计算自由变速；倍率1–100，不限菜单预设值。
- `restoreClipHead/Tail(index)`：恢复该片段到源素材起点/终点。基础实现可能重新引入与其他片段重复的源范围，UI须明确操作语义。
- `mergeAdjacent(leftIndex)`：合并同倍率、源时间连续的相邻片段。
- `clipsInRange(start,end)`：按输出时间裁出导出片段映射，输出从0起，不修改工程；将结果传给原导出接口即可做范围导出。

## 摄像头运动与分段同步

通过 `project.setCameraOption()`修改motionSegments/syncPoints，自动进入工程与撤销系统。

`motionSegments`每项：start/end（源秒）、x/y（归一化位置）、size、fullscreen、shape、enter/exit；有序、不重叠、转场总长不超过片段。支持位置/大小/全屏转场和形状切换。

`syncPoints`每项：sourceTime/cameraTime，要求两组时间严格递增。区间内线性映射，区间外保持端点偏移；全局offset仍按“源时间减offset”应用。`project.cameraTimeAt(outputSeconds)`和`cameraRateAt(outputSeconds)`供UI播放使用；导出也使用同一映射。

内部取景参数cropScale/cropX/cropY继续有效。实际摄像头硬件、复杂多点同步与组合布局仍需验收。

## 屏幕3D、局部效果与贴图

通过 `project.setCanvasOption()`修改以下设置，统一保存、撤销、预览与导出。

静态投影：screenX/screenY（−1–1画布位移）、screenScale（.1–3）、screenRotationX/Y（±80度）、screenRotationZ（±180度）、screenPerspective（1–10的投影距离系数，默认3）。这是Windows后端参数定义，尚未与Mac透视滑杆数值逐项校准。

`screenMotionSegments`：sourceStart/sourceDuration、from/to姿态字典，平滑插值。屏幕、外框、阴影和光标一起投影，背景和摄像头独立。

`blurRegions`：sourceStart/sourceDuration，x/y/width/height归一化区域，strength、style（blur/spotlight）、enterDuration/exitDuration。模糊目前采用降采样重建近似，聚光灯压暗外部区域；不声称等同Mac滤镜内核。

贴图API：`addSticker(imageUrl,sourceStart,duration)`返回ID，`updateSticker(id,changes)`、`removeSticker(id)`。参数包括中心x/y、width、rotation、opacity、radius、borderWidth/borderColor、shadow、enterDuration/exitDuration。图片进入拥有的不可变缓存；工程、恢复快照、场景预设各自打包资源，删除或撤销不会依赖原始导入文件。

贴图在屏幕之上、摄像头之下，先实现平面覆盖与淡入/缩放，不宣称全部原版层级、开场风格与焦点虚化组合已对齐。

所有新增运动区间按源时间处理，裁短、重排和变速后保留源片段关系。UI创建区间时需使用 `project.sourceTime(outputSeconds)`，不要直接写输出秒数。

## 工程、恢复与设置

工程格式版本8，读取本项目1–8；新增独立音轨和参数，继续拒绝Mac或未来版本。保存前保留project.previous.json，主JSON损坏时可读取备份；有效未来版本不会回退旧快照。

`project.renameProject(name)`重定位工程与媒体/资产引用，拒绝路径字符及目标冲突。`saveRecovery(newDirectoryUrl)`保存独立副本，不改变当前工程位置或dirty状态。

`recoveryController`上下文：enabled、blocked、snapshots、lastSnapshot、error；snapshotNow()/restoreSnapshot(url)/removeSnapshot(url)。编辑停止变化60秒后尝试快照，录制/导出/编辑播放时阻止，保留最多3个拥有标记的快照。恢复仍执行未保存保护，不覆盖当前修改。

当前快照会复制媒体，尚未做异步增量；大工程可能耗时和占用磁盘。它是编辑工程恢复基础，不等于正在录制的未完成MP4能在崩溃后修复。

`appSettings.values`、setValue(key,value)、reset()提供持久化设置：language(system/zh/en)、theme(system/light/dark)、previewQuality(high/medium/low)、projectDirectory/exportDirectory、completionSound、autoCheckUpdates。预览质量已连接到独立渲染尺寸上限，目录与安全文件名建议已接保存/导出对话框，完成提示音已接成功录制/导出；updateController执行有界版本检查。effectiveTheme/effectiveLanguage/localeName供Claude绑定，实际配色与翻译资源由UI完成；未实现更新包下载安装。

## 本批刻意不伪装成完成的部分

原生光标关闭与完整独立形状轨、全部Mac效果/算法对齐、RF64长录音、录制崩溃修复、安装器与更新安装执行仍未完成。iPhone/iPad输入由用户取消。时间线缩放/悬停等大部分呈现交互留给Claude；模型与接口不能替代完整操作界面。

最终UI仍由Claude负责；在剩余关键后端闭环前，不把本文件当作“整个后端已全部完成”的交付声明。

## 本批运行证据与最小复现

2026-10-13：Release构建与Qt部署通过。单个后端闭环程序检查独立WAV读写、音量/静音混音与短轨补零、音轨撤销、批量删除撤销、时间范围映射、边界恢复合并、贴图导入、版本8保存重开、快照不改变当前工程、重命名、损坏JSON回退和未来版本拒绝，均通过；投影/聚光灯像素变化与摄像头同步点计算通过。实际0.3秒M4A混音导出重新解码有非零声音。像素变化只证明效果执行，不是Mac视觉一致验收。

自有窗口录制暂停/继续、重录和丢弃通过：视频3.016秒、音频3.008秒，暂停期间红色画面0帧，继续后的蓝色画面90帧。独立系统WAV实际生成。指定本进程声音短测录得两种测试音并完成剪辑导出解码：原录制4.366秒、音画偏差84毫秒；导出2秒、偏差76.7毫秒。不包含另一个进程的排除对照，因此不宣布应用间隔离验收完成。没有新增真实麦克风/摄像头硬件证据。

首次声音检查失败：测试音的QMediaPlayer输出重采样报空通道布局，未产生有效刺激。测试程序改用明确立体声格式的QAudioSink后重新检查；不把首次静音掩盖为采集成功，也没有据此修改WASAPI采集算法。

随后全系统声音短测仍为静音，用户明确提供当前系统音量为0。因此该结果受运行条件影响，不能直接归因于录音故障，也不能记为全系统录音通过；保持用户音量设置，停止重复实录，正常音量条件下待确认。指定进程通过的结果与全系统音量条件分别登记。

源码检查另修正两处独立的边界：默认回环端点使用eMultimedia，与[Qt 6.8默认播放端点](https://raw.githubusercontent.com/qt/qtmultimedia/6.8/src/multimedia/windows/qwindowsaudiodevices.cpp)一致；设备返回零QPC时间戳时按无效时间戳处理，避免首次锚定到负时间。属于源码修正并构建验证，不据此声称已证实静音原因或实际修复了用户的录音问题。

录制区域和30FPS接口、复杂摄像头运动、效果与变速组合属于已实现并构建通过，尚未逐项做真实成片检查。后续UI接续批次已补独立WAV预览调度，不再依赖主视频内嵌音频回调；最新检查结果见公开摘要。

从仓库根目录运行，使用FFmpeg生成公共测试夹具，不使用私人录制。每次输出到新目录，查看退出码和结果JSON：

```powershell
$app = (Resolve-Path 'build/desktop/portable/ScreenPilot.exe').Path
$batch = Join-Path $PWD ('probe-results/backend-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $batch | Out-Null
$source = Join-Path $batch 'source.mp4'
ffmpeg -v error -f lavfi -i 'testsrc2=size=640x360:rate=30:duration=3' -c:v libx264 -pix_fmt yuv420p $source
& $app --backend-batch-smoke-test $source --backend-result-directory (Join-Path $batch 'model')
& $app --recording-controls-smoke-test (Join-Path $batch 'pause')
$env:SCREENPILOT_TEST_PROCESS_AUDIO = '1'
& $app --audio-smoke-test (Join-Path $batch 'application-audio')
Remove-Item Env:SCREENPILOT_TEST_PROCESS_AUDIO
```

声音检查播放程序生成的低音量测试音；程序自有窗口可见，保持交互桌面与有效默认播放设备。原始媒体、详细日志和绝对路径只保存在忽略的本地目录。
