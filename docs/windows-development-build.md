# Windows 可运行开发版

2026-10-13：优先交付可操作的 Windows 应用，验证保持为必要构建和实际流程检查。完整首版范围继续以固定 Mac 提交和95项能力清单为依据。

## 启动与使用

双击仓库根目录的 `启动 ScreenPilot.cmd`，或运行 `build/desktop/portable/ScreenPilot.exe`。移动程序时保留整个部署目录。本轮是内部开发版，没有安装器。

最新后端接口、已有入口及待接线部分见[后端交接文档](backend-api-handoff.md)。最终UI由Claude继续负责；本批优先实现录制、声音、编辑模型、效果和恢复能力。

最终界面接续直接使用[Claude UI接线清单](claude-ui-integration.md)，包含每个页面的对象、调用方式、错误与禁用状态。

本轮界面设计依据见 [DESIGN.md](../DESIGN.md)，使用浅色工作台、左侧分类导航和独立导出面板。

最新运镜端点/移动、吸附、拆分/复制粘贴、快捷键及最近工程入口见[编辑操作批次](development-editing-batch.md)。

动态背景在“画布”设置，样机和开场在“效果”设置，参数可随工程和场景预设保存；结果与限制见[视觉效果批次](development-visuals-batch.md)。

裁切、手动运镜和预设的新入口及复现步骤见[场景编辑批次](development-scenes-batch.md)。

录制辅助批次入口：录制页“悬浮备忘录”和“添加标记”，编辑器时间线金色标记点击跳转、右键删除；点击右上角“导出”选择导出尺寸与30/60FPS，或“导出原始素材”复制屏幕与已有摄像头文件。参数持久化、实际结果、运行命令及限制见[录制辅助与导出批次](development-workflow-batch.md)。

1. 选择屏幕或窗口；新打开的窗口可刷新来源。可分别开启系统声音与麦克风，选择麦克风设备，并选择录制保存文件夹。
2. 点击开始录制；可暂停/继续，暂停期间不计入成片时间。停止并保存后自动进入编辑页面；重录/丢弃只清理本次未交付的临时素材。
3. 播放或拖动进度定位；点击时间线片段选中，在所选片段内部使用分割、裁掉播放头前/后，或删除所选片段。保留至少一个片段，可撤销/重做。
4. 点击保存工程，选择一个新的 `.dogscproject` 名称。工程是文件夹，内含 `project.json` 和复制的原始媒体。打开工程时选择这个文件夹。原始视频不会因剪辑被改写。
5. 需要自动放大操作区域时，在右侧勾选“自动运镜”；当前开发版默认关闭。需在新录制的画面内点击，旧工程没有轨迹时不能生成效果。运镜开关可撤销/重做，并随工程保存。
6. 右侧切换“画布”，选择视频比例、背景颜色或静态图片，调整留白、圆角和阴影。背景图片会随工程复制，设置支持撤销/重做。
7. 选择保留声音的 MP4、静音 MP4 或音频 M4A，选择新文件名；导出显示进度并允许取消，不覆盖已有文件。未保存也可导出，但需保存工程才能保留剪辑供下次继续。
8. 点击左上角“← 返回录制”回到录制页面，当前工程和剪辑会保留，也可点击“继续编辑”返回。开始新录制时，有未保存修改会提示保存、放弃或取消。“先保存”完成后需再次触发原先的打开、录制或关闭操作。

默认录制保存到 Windows“视频”目录的 ScreenPilot 文件夹，使用时间和唯一ID命名。录制或保存过程中关闭主窗口，会先请求停止并完成视频保存。导出期间请完成或取消导出后再关闭。屏幕录制可能包含应用自身，可使用 Windows 最小化按钮移开主窗口；自动排除辅助窗口尚未实现。

## 构建

```powershell
& '.\scripts\build-windows.ps1' -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Deploy
```

传入自己的 Qt 6.8 MSVC kit 路径；Visual Studio 通过 vswhere 自动查找，也可传 `-VisualStudioRoot`。示例路径为通用占位。追加 `-Launch` 构建后打开应用；`-DeploymentDirectory` 可指定独立部署目录。默认部署到 `build/desktop/portable`。源码入口为根 CMakeLists.txt 和 app 目录，probes 为历史独立实验。

## 必要验证与复现

构建和 Qt 依赖部署通过。运行下面命令，输出位置必须不存在或为空；不要提前在该目录内建立日志文件。

```powershell
$app = Resolve-Path '.\build\desktop\portable\ScreenPilot.exe'
$output = Join-Path $PWD ('probe-results\editing-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$process = Start-Process -FilePath $app -ArgumentList @('--editing-smoke-test', ('"' + $output + '"')) -PassThru -Wait
$process.ExitCode
Get-Content (Join-Path $output 'smoke-result.json')
```

验证仅录制程序生成的动画窗口，不采集用户桌面或麦克风；实际执行裁短、分割、删除、撤销/重做、保存并重新打开工程、导出 MP4，再打开成片解码并比较时长。成功退出0。结果见[公开验证摘要](validation/public-results.md)。原始视频、截图和详细日志仅留本地忽略目录。

`--smoke-test <新的空目录>` 仍保留原有单纯录制—保存—解码检查。无需为使用本轮应用额外运行全部独立探针。

## 当前实现与限制

本轮支持基础录制、单视频时间线编辑、内部工程保存重开和 H.264 MP4 导出。工程使用显式 `producer: ScreenPilot`、`formatVersion: 7`（可读取旧的版本1/2/3/4/5/6）；时间线遵循原版 `sourceStart/sourceDuration/playbackRate` 秒单位，支持重排及1/1.5/2/4/8/12/20/50/100倍速。按钮重排和拖动重排均已实现；实际鼠标拖动、选中状态及跨片段变速播放检查通过。扩展名相同不代表 Mac 互通；当前拒绝打开 Mac 或未知版本工程，避免覆盖未知内容。

导出暂用现有 Qt 多媒体输入接口，按编辑顺序逐段定位解码并重新编码，耗时通常接近选中片段的源时长之和；原始导出按素材与裁切后的画布像素计算，显式1080p/1440p/4K档位继续可选。本轮录制新增原始/最高4K/最高1080p选择，详见[原生录制批次](development-native-recording-batch.md)。导出保留被剪辑片段对应的音频，并将保留片段拼接到输出时间线；系统声音录制及两段剪辑后的 AAC 音轨导出已经实际解码验证。保存工程复制媒体时暂为同步操作，大文件可能短暂阻塞界面。

录制30FPS仍为请求参数，实际录制帧率可能偏高；本批导出改用独立30/60Hz输出时间网格，1080p60成片已实际解码验证；历史 W01 帧率与 W03A 系统声音时钟问题继续跟踪；真实媒体暂停已有受控窗口短测，长录制仍待验收。屏幕、多设备、长录制、长视频导出和取消恢复未逐项运行验收；麦克风仅完成单设备与系统声音混录短测。

区域与设备来源、摄像头真实硬件验收、指定应用声音、完整鼠标样式与原版视觉对齐、美化效果、本项目工程可靠性，以及其余原版能力继续保留在首版范围。本轮完成的是一个基础产品流程，不代表完整首版完成，也不冻结最终媒体架构。Linux 尚不开发。


## 鼠标轨迹与可选自动运镜

新录制按约30Hz采样鼠标归一化位置与左键按下边沿，仅用于后期效果，不主动移动或点击用户鼠标。开启自动运镜时，按点击的时间及空间分组生成缩放，可调整倍率、过渡时间、弹簧和安全区域跟随，带保持及实际活动延长；静止定时样本不会延长保持。预览和导出共用确定性求值，按原始素材时间定位，裁短与删除后仍使用对应源时间。

源码核对发现固定原版自动生成缩放默认为开启；当前内部开发版先默认关闭，差异明确保留，并非原版行为已经完全对齐。本批次加入可选弹簧及安全区域跟随；原版连续envelope、剪切重锚、完整鼠标样式和视觉一致性仍待验收。参考与改写来源为固定提交的 `AutoZoomPlanner.swift`，许可见 `third_party/DogSC-LICENSE`。

窗口轨迹通过唯一可见标题关联系统窗口；同名或无法匹配时标记为画面外，不猜测目标。窗口边框裁剪、混合DPI和移动中的像素精度尚未验收；鼠标时钟从首有效帧的编码请求建立，未证明与编码PTS精确同步。短点击可能被定时采样漏过，当前不支持右键事件和原生光标移除；额外强调层见本批次说明。

专项闭环复现可将上方命令参数换成 `--framing-smoke-test`。它实际运行窗口录制和鼠标采样，但使用确定性点击轨迹验证效果生成、默认关闭、开关撤销重做、源时间剪辑映射、保存重开、带运镜导出与解码；不会注入鼠标操作。结果须区分实际采样运行与模拟点击轨迹效果，不能据此声称真实点击/音画同步或完整原版视觉验收通过。


## 基础画布美化

新工程默认适应素材比例、留白100、圆角24、阴影28%、纯色背景；静态图片以覆盖画布方式铺满，录屏保持比例居中。尺寸单位按输出画布短边1080缩放。当前支持适应素材、16:9、4:3、9:16、1:1、3:4、2.39:1和1:2.39，输出保持偶数尺寸且位于1920×1080范围内。自定义比例、图案、动态背景、背景模糊、描边及样机框等完整原版能力继续待实现；当前纯色默认与原版网格默认有差异。

预览和导出调用同一CPU画布渲染器；自动运镜先作用于录屏内容，再绘制圆角和背景。阴影暂用多层渐近绘制，尚未对齐原版模糊核或完成Mac视觉对照。预览可能受CPU负载影响，暂不承诺高分辨率/长视频性能。

工程版本3将背景资源复制到assets目录，JSON仅存工程内相对路径；保存后原图可以移走。读取旧版本1/2时保留无留白、无圆角及无阴影的旧画面。背景缺失或无法解码会明确拒绝打开/保存，避免静默替换素材。每次保存含背景的工程暂会生成唯一新资源，旧资源尚不自动清理。

专项复现将上方参数换成 `--canvas-smoke-test`。使用程序自有录制窗口与生成的背景图片，检查比例/画布撤销重做、保存后删除原图再重开工程、保留剪辑和测试运镜、导出MP4并实际解码检查方形尺寸和背景颜色；不会采集用户图片或注入鼠标。效果夹具与真实点击验收继续区分。

预览接入直接绑定QVideoSink，避免自定义Item属性与Qt元方法接口的差异；接口依据[Qt 6.8.3 QMediaPlayer源码](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/playback/qmediaplayer.cpp#L926)。首次载入静音取得首帧后暂停，再调整画布时重绘已有帧。

## 编辑器播放修复

首帧预览现在只在素材变更时请求一次，并在素材切换完成后启动；手动播放会接管预览状态，避免加载状态再次触发自动暂停。暂停或定位时保留最近有效画面，切换素材时清空。使用上方复现命令，将参数替换为 `--playback-smoke-test`，可验证自有窗口录制后在实际编辑器内播放、暂停和继续，并检查播放位置推进；结果不代表音轨或完整编辑器验收。

## 编辑器预览卡顿修复

定位到预览每帧按导出尺寸进行CPU合成，运镜变化和视频到帧还会重复合成。现按预览区域的实际像素尺寸绘制，同一个事件循环内的画面、运镜和布局更新合并一次；缩放裁切直接绘制到画布，避免中间全尺寸图像。导出仍使用原输出尺寸，画布和运镜能力保留。

`--preview-performance-test <新的空目录>` 向实际编辑器预览注入生成的1920×1080画面，不录用户桌面；输出 `performance-result.json`。旧同步更新平均42.3毫秒；优化后在954×502逻辑预览区域，合成平均7.0毫秒、最大10.4毫秒，60Hz提交间隔P95为17.8毫秒。此项是生成画面的合成测量，不包含硬件视频解码成本。

另以自有窗口真实录制的视频执行 `--playback-smoke-test`：连续播放约2秒产生124次预览合成，平均约6.6毫秒，暂停及继续通过；画布设置、工程保存重开和1080×1080导出解码回归通过。该短测不保证所有硬件、4K素材或长视频都达到相同流畅度。


## 声音录制与剪辑导出

录制页可独立开启“系统声音（全部应用）”与麦克风；系统声音使用默认播放设备的 WASAPI loopback，麦克风可选设备并在录制时显示电平。两路转换为48kHz双声道浮点样本、按时间混合，再编码为 MP4 内的 AAC 音轨；关闭两项则录制无音轨视频。系统声音开关会记住上次选择。

音频以10毫秒块送入编码器，对等待中的数据设上限，失败会明确提示。Qt 6.8.3 带格式提示的 QAudioBufferInput 曾在实际运行中持续拒收首块；当前让首块声明格式以启动编码，录制与导出均应用此修复。依据为固定版本的 [Qt 编码线程](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/plugins/multimedia/ffmpeg/recordingengine/qffmpegencoderthread_p.h)与[初始化流程](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/plugins/multimedia/ffmpeg/recordingengine/qffmpegencodinginitializer.cpp)。这只记录当前实现，不冻结最终媒体架构。

将上方复现命令参数替换为 `--audio-smoke-test`，读取 `audio-smoke-result.json`。程序播放轻音量660/990Hz音源并改变自有窗口颜色，真实录制系统混音，再将两段各1秒的素材剪辑导出、重新解码检测声音、时长和音画标记；不会录制麦克风。系统混音可能包含其他应用当时播放的声音，原始音视频与日志仅保留本地忽略目录。

实际结果：原录制4.290秒，两段测试音均检测到，音画标记偏差−52.6毫秒；剪辑输出1.998秒，音频从0至2.005秒，两段音源均保留，音画偏差−32.5毫秒，进程退出0。测试使用250毫秒门槛，不代表精确同步或长录制漂移验收。

当前系统声音是全部应用混音，尚无指定进程采集或排除本应用声音；支持剪辑后M4A单独导出，尚无独立音轨编辑或音量自动化。音频采集仍依赖界面事件循环，长时间阻塞、设备切换与长录制漂移未验收；这些与其余原版能力继续保留在完整首版范围。


另一次真实混录检查启用一个麦克风与系统声音，检测到非零麦克风输入电平，录制及带音轨剪辑导出通过：原录制4.230秒、音画标记偏差−51.9毫秒，导出1.998秒、偏差−31.4毫秒。复现时在启动 `--audio-smoke-test` 前设置 `$env:SCREENPILOT_TEST_MICROPHONE = '1'`，运行后 `Remove-Item Env:SCREENPILOT_TEST_MICROPHONE`；此模式会录制第一个可用麦克风，证据只存本地。尚未单独验收麦克风讲解清晰度、不同设备或声学回声消除。


## 四项批次与最小复现

时间线从真实源视频解码缩略图，并显示稀疏音频峰值，当前不是完整逐样本波形。摄像头开启后录为独立素材，可调形状、位置、大小、取景、镜像、显示及同步偏移，随工程复制和保存。偏移由首个预览帧估计，尚非精确硬件同步。历史版本变速会改变音高；本批次改为 WSOLA 保音高，区别于 Mac spectral 实现，实际预览和导出音高验收见本批次记录。

在仓库根目录完成构建部署后运行以下最小检查。每次使用新的结果目录；生成素材与详细结果仅留本地。

```powershell
$app = (Resolve-Path '.\build\desktop\portable\ScreenPilot.exe').Path
$batch = Join-Path $PWD ('probe-results\batch-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& $app --project-flow-smoke-test | Out-Host
& $app --export-options-smoke-test (Join-Path $batch 'export') | Out-Host
$source = Join-Path $batch 'export\generated-source.mp4'
& $app --timeline-smoke-test $source --timeline-result-directory (Join-Path $batch 'timeline') | Out-Host
& $app --camera-smoke-test (Join-Path $batch 'camera') | Out-Host
& $app --editor-batch-smoke-test $source --editor-result-directory (Join-Path $batch 'editor') | Out-Host
```

逐条检查退出码与生成的 `export-options-smoke-result.json`、`timeline-smoke.json`、`camera-smoke.json`。模型、三种导出、多速度、摄像头偏移/隐藏合成导出和时间线媒体已有通过证据；摄像头检查在无设备时明确跳过硬件。摄像头检查在有设备时会进行短录制，应在合适的拍摄环境运行。实际编辑器鼠标拖动及恢复顺序、选中状态、2倍速跨片段播放与摄像头同步检查通过；记录为 `editor-batch-result.json`。现有录制、画布、保存重开与导出流程也已回归通过。详细脱敏结论见[公开验证摘要](validation/public-results.md)与[开发批次](development-batch.md)。


## 系统声音滋滋声修复

发现每个 WASAPI 包独立按 QPC 时间戳取整，会在连续设备样本之间插入空白或覆盖样本。改为首次用 QPC 对齐，此后按设备采样位置拼接；保留真实丢包间隔，处理时间戳错误和设备计数回退。接口语义见 [Microsoft GetBuffer 文档](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)。

最小复现：运行 `ScreenPilot.exe --audio-packet-smoke-test`。同一组300包抖动夹具，旧算法171次错误边界，修复后0次；真实缺口、错误时间戳和计数回退检查通过。真实系统声音短测中，修复前607个边界有194次位置跳动，设备位置全部连续；修复后609个边界全部连续。

本轮真实混音含其他应用声音，完整音画门槛未通过（非静音或解码失败），不能据此宣称纯音失真或音画同步验收通过。原始录制已编码的杂音不会自动恢复；需要新版重新录制。长录制时钟漂移仍未验收。诊断可设置 `SCREENPILOT_AUDIO_DIAGNOSTIC=1` 配合 `--audio-smoke-test`，包日志仅留本地忽略目录，使用后清除环境变量。


## 光标、保音高与录制控制批次

右侧“画布”可开启“鼠标强调”，默认关闭；可调光晕/箭头/十字样式、大小、强调指针显示及点击扩散环。预览与导出共用源时间计算，设置随内部版本4工程保存并支持撤销/重做；旧工程补齐默认值。当前 Qt 6.8.3 屏幕/窗口采集公开接口没有隐藏原生鼠标的开关，录制中的原生鼠标像素仍保留。“显示强调指针”只控制额外强调层，不能删除原鼠标。

暂停/继续会压缩视频、音频及鼠标的暂停区间。受控窗口录制1.5秒、暂停1.9秒、继续录制1.5秒，得到视频3.000秒、音频3.008秒；暂停期间红色画面未进入成片。重录/丢弃旧临时素材及保留既有素材检查通过。真实摄像头未连接，摄像头暂停同步只完成源码实现，尚无硬件实录证据。

WSOLA 处理按剪辑源时间保留目标样本数。20秒440Hz纯音在全部九档速度测得439.817–440.182Hz，目标样本数和系统音频包连续性回归通过。该算法不同于固定 Mac 参考的 spectral 实现；50/100倍速的音质与可懂度没有保证。实际预览2倍速输出1.5秒PCM，测得440.00946Hz，暂停后输出保持稳定；QMediaPlayer、处理器与QAudioSink设备格式初始化通过，此项没有声学loopback测量。实际导出12组检查全部通过，三种导出选项、其余速度及摄像头偏移重新解码通过，多数测得440Hz，100倍速约435Hz，时长检查通过。

```powershell
$app = (Resolve-Path '.\build\desktop\portable\ScreenPilot.exe').Path
$batch = Join-Path $PWD ('probe-results\controls-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
& $app --cursor-smoke-test (Join-Path $batch 'cursor') | Out-Host
& $app --audio-time-stretch-smoke-test | Out-Host
& $app --audio-packet-smoke-test | Out-Host
& $app --preview-audio-smoke-test (Join-Path $batch 'preview-audio') | Out-Host
& $app --recording-controls-smoke-test (Join-Path $batch 'recording') | Out-Host
& $app --export-options-smoke-test (Join-Path $batch 'export') | Out-Host
$source = Join-Path $batch 'export\generated-source.mp4'
& $app --cursor-export-smoke-test $source --cursor-result-directory (Join-Path $batch 'cursor-export') | Out-Host
```

实际H.264光标导出检查使用源素材2至3秒片段并以2倍速导出：关闭强调时0帧检测到标记，开启时15帧检测到标记，检查通过。

每次使用新的输出目录，逐条检查退出码。预览检查会播放测试声音，录制控制检查使用程序自有窗口；原始录音、视频与事件日志只留本地忽略目录。各命令通过与否以[本批次记录](development-controls-batch.md)和[公开摘要](validation/public-results.md)为准，不以列出命令代表验收通过。
