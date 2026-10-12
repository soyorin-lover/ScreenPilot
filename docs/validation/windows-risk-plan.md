# Windows 技术风险验证计划

日期：2026-10-13 · 阶段：第 2 步 · 状态：进行中

产品依据为固定提交 `fa24059a9ba7e769152b3c1de047d96aca873e1c` 和[95项对齐清单](../parity-checklist.md)。本阶段只验证 Windows。实验程序不是产品骨架，不构成采集、渲染、音频或编码架构决定；未通过的能力仍保留在首版目标内。

## 验证顺序与退出条件

按实验依赖顺序推进，每次形成程序、运行步骤、证据和结论；风险严重程度不等于实施先后。

| 实验 | 对齐条目 | 问题与最小闭环 | 完成所需证据 | 当前状态 |
| --- | --- | --- | --- | --- |
| W01 窗口采集与基础编码 | C03、C06、C07、P12–P14 | Qt捕获自有受控动画窗口，H.264/MP4编码，真实回放解码 | 捕获帧/PTS、文件、解码帧/尺寸/变化、明确失败退出；本机结果与限制 | 最小链路实测通过；指定30FPS未通过，见[报告](w01-window-capture.md) |
| W02 鼠标与视频时钟 | C17、C18、C21、E13、E45 | 原生WGC帧时间、鼠标移动/点击与统一时钟；暂停继续映射 | 原始时间戳、偏差分布、DPI/移动窗口/静止数据；不能只检查回调顺序 | 受控模拟输入最小闭环实测通过；完整鼠标/暂停能力未验收，见[报告](w02-mouse-clock.md) |
| W03 音频采集与隔离 | C11–C14、E35 | 麦克风、系统混音、指定PID/子进程音频；与视频同步 | 真实已知音源、进程隔离、静音/断开/切换、音画误差与漂移 | W03A真实系统音频与三音对照通过；QPC偏差导致音画同步未通过，见[报告](w03a-system-audio.md)。先诊断时钟，再独立验证麦克风、进程隔离和漂移 |
| W04 编辑后合成与导出 | E16、E20、E29、E31、P11–P16 | 固定素材裁切/缩放/叠加和时间映射后编码，比较预览与解码结果 | 同时间点帧对照、选段/变速时间关系、取消/错误、30/60FPS | 未开始 |
| W05 录屏边界与可靠性 | C01–C03、C09、C10、C21–C25 | 屏幕/区域/窗口、遮挡/最小化/移动/缩放、多屏DPI、排除窗口 | 每个场景独立结果、资源与异常恢复；基础W01不能替代 | 未开始 |
| W06 特殊能力与编码 | C04、C05、C07、C13、C15、C16 | iOS实时设备取流和声音、摄像头、HEVC/ProRes、HDR | 本机实际设备和实际编码、错误/回退、资源和分发约束 | 未开始；iOS与ProRes为高不确定性 |

W01仅证明最小窗口采集—编码—解码路径，不证明鼠标同步、音频、1080p60、4K、零拷贝、硬件加速或完整原版对齐。后续W02需要比较原生WGC时钟，不因W01成功默认采用Qt高层采集。

## 官方依据与待验证推论

- [Qt 6.8 QWindowCapture](https://doc.qt.io/qt-6.8/qwindowcapture.html)仅支持FFmpeg后端。实验固定Qt 6.8.3，不使用后续版本新增API。
- [Qt 6.8 QMediaRecorder](https://doc.qt.io/qt-6.8/qmediarecorder.html)与[QMediaFormat](https://doc.qt.io/qt-6.8/qmediaformat.html)用于查询运行时编码能力；请求参数不能替代最终文件检查。H.264探针不代表ProRes路径已成立。
- [Windows按进程loopback示例](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)要求build 20348及以上。不能将全系统混音等同于指定应用声音，也不能据此宣称所有Windows 10都支持。
- [WGC窗口创建](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)与[采集说明](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)是原生路径候选依据；最小化、HDR和设备丢失需本机实测。
- 时钟候选：以QPC统一原点，对照[WGC SystemRelativeTime](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime)、[WASAPI GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)和[鼠标hook时间](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct)。这是待验证设计推论，不能直接混用消息毫秒、帧PTS和QPC。
- [Apple有线设备录制流程](https://support.apple.com/guide/quicktime-player/record-a-movie-qtp356b55534/mac)不能证明Windows存在等价公开接口。iOS来源保留目标，需单独协议、设备授权、旋转、音频和断开验证。
- [FFmpeg ProRes编码器](https://ffmpeg.org/ffmpeg-codecs.html#ProRes)说明存在候选编码路径；实际包的性能、可用性和分发条件尚未验证，不新增产品依赖决定。

## 环境与证据规则

构建产物、详细JSON、逐帧/鼠标事件、设备标识和原始媒体仅保存在本地忽略目录；公开仓库保留程序、通用复现步骤与汇总结论。系统混音可能包含其他应用声音，原始WAV不公开。

每次记录：源码提交或工作区状态、Qt/编译器/SDK/系统版本、后端、命令、退出码、原始JSON和文件位置。构建通过、真实运行通过与原版体验通过分开判定。未执行的检查明确写未执行；参考机器性能阈值需多场景测量，不从一次短录制推导。
