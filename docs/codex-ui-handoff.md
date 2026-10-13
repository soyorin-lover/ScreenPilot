# 给 Codex 的 UI 交接文档

后续状态：本文保留Claude第二轮UI交接的历史记录。之后Codex已扩展C++后端，当前接口与范围请以[Claude UI接线清单](claude-ui-integration.md)为入口，不再按下文历史缺项推断当前实现。

更新：2026-10-13。Claude 完成第二轮界面改造后交回 Codex 继续开发。本文说明当前界面结构、改动范围、必须保持的契约、验证方法和未完成事项。先读仓库 `AGENTS.md`，再读本文和 `DESIGN.md`。

## 1. 当前状态

- 第二轮界面改造已完成并可运行：录制页、编辑器、时间线、导出面板及所有辅助弹窗已按固定 Mac 原版（提交 `fa24059a9ba7e769152b3c1de047d96aca873e1c`）的三张截图和 `Sources/DogSCApp/EditorDesignTokens.swift` 重做。
- 只改了 QML、`CMakeLists.txt` 的 QML 模块登记和文档。**没有修改任何 C++ 媒体、录制、导出、工程逻辑。**
- 所有改动均未提交、未推送。仓库里还有此前大量未提交工作，不要 reset/clean/checkout。
- 设计基线与令牌见 `DESIGN.md`；本轮变化与检查记录见 `docs/development-ui-batch.md`“第二轮”。

## 2. 文件地图（本轮涉及）

| 文件 | 说明 |
| --- | --- |
| `app/StudioTheme.qml` | 单例令牌：银灰背景、近白卡片、石墨强调、状态色、时间线色、字号、圆角、高度。属性名不要以 `on` + 大写开头（会被当成信号处理器，曾因 `onAccent` 导致整个 QML 加载失败，现名 `accentInk`）。 |
| `app/StudioIcon.qml` | 24 网格矢量图标，`QtQuick.Shapes` + `Shape.CurveRenderer` 渲染（不再用 Canvas）。`build(name)` 用 `line/poly/rrect/circle/arc/dot` 生成 SVG 路径；新增图标在 `switch` 里加一个 case。 |
| `app/StudioButton.qml` | 新增 `tip`（悬停提示）、`iconSize`；无文字时自动成为方形图标按钮。`focusPolicy: Qt.TabFocus`，点击不抢焦点，保证点击按钮后空格/方向键仍控制编辑器。 |
| `app/StudioCheckBox.qml` | 外观改为右侧开关，整行可点；默认 `Layout.fillWidth: true`。 |
| `app/StudioComboBox.qml`、`StudioSlider.qml`、`StudioTextField.qml` | 凹陷底色样式，原有属性/信号不变。 |
| 新增 `StudioCard`、`StudioSegmented`、`StudioSliderRow`、`StudioSection`、`StudioHelp`、`StudioDivider`、`StudioDialog`、`StudioDialogFooter`、`StudioPopover`、`StudioMenu`、`StudioMenuItem`、`StudioDotGrid` | 共享组件，已登记到 `CMakeLists.txt`（`QML_FILES` 与 `QT_RESOURCE_ALIAS` 两处都要加）。 |
| `app/Main.qml` | 录制页：中央舞台 + 底部悬浮工具条 + 弹出面板。录制逻辑函数、关闭/未保存保护、录制结果移交未改。 |
| `app/Editor.qml` | 前约 273 行（播放、定位、跨片段、摄像头同步、保存/打开、对话框）原样保留，仅把 `inspectorTab` 从 TabBar 别名改为 `property int inspectorTab: 0`；其后为新布局、导出面板、未保存弹窗。 |
| `app/TimelineStrip.qml` | 新增刻度尺（可拖动定位）、`xForTime/timeForX`、圆角缩略图片段、波形带、角标、圆头播放头。片段拖动重排逻辑未改。 |
| `app/ManualZoomTrack.qml` | 仅改外观（沙金色片段、`scale` 字段用于标签）；草稿拖动、吸附、提交、取消逻辑未改。 |
| `app/ManualFramingControls.qml`、`VisualSceneControls.qml`、`BackgroundControls.qml` | 分组重排。`VisualSceneControls` 新增 `section: "frame" | "intro" | "all"`。 |
| `app/CropEditor.qml`、`ScenePresetPanel.qml`、`RecentProjectsPanel.qml`、`ShortcutHelp.qml` | 改用 `StudioDialog`；对象名和 `accept/reject/open/close` 行为保留。 |
| `app/MemoWindow.qml` | 工具按钮改为矢量图标，其余不变。 |

## 3. 界面结构速览

- **录制页**：显示器/窗口来源（区域、设备为禁用并带提示，未实现不得做成可用按钮）| 麦克风、摄像头圆形按钮（点击打开设置弹出面板）、系统声音按钮（点击直接开关）| “保存到”卡片 | 备忘录 | 开始录制。关闭的输入显示斜线；系统声音与麦克风都关闭时状态行显示橙色警告。录制中工具条换成计时、标记、暂停/继续、更多（重录/放弃）、停止并保存。
- **编辑器**：顶栏（返回录制、工程菜单、标题与未保存标记、撤销/重做、场景预设/保存/快捷键、导出）；左侧分类栏 片段/画面/运镜/效果/相机 → `inspectorTab` 0/1/3/4/2；画面上方比例/裁切工具条；右侧检查器；底部时间线卡片。
- **导出面板**：分段选择导出内容/分辨率/帧率 → “开始导出”后弹出系统保存对话框（流程与以前一致）。分辨率按短边描述，不显示预计大小。

## 4. 必须保持的契约

- 对象名：`editorView`、`canvasPreview`、`editorPlayer`、`timelineStrip`、`timelineClip-<索引>`、`manualZoomTrack`、`manualSegmentMouse`、`unsavedEditorDialog`、`exportOptionsDialog`、`cropEditor`、`scenePresetPanel`、`shortcutHelp`、`recentProjectsPanel`、`recorderMemoWindow`、`scenePresetName`、`backgroundControls`、`chooseBackgroundImage/Video`、`clearBackgroundImage/Video`。
- `Editor.inspectorTab`：0 片段、1 画面、2 相机、3 运镜、4 效果。
- `Editor` 函数 `seek`、`playPause`、`openCrop`、`requestOpen`、`requestOpenRecent`、`requestExport`、`saveProject`、`requestNewRecording` 有外部调用方。
- `Editor.shortcutsAvailable` 依赖各弹窗的 `visible` 与 Overlay 子项检查；新增模态弹窗要加入该条件，或确保它位于 Overlay 中。
- `StudioDialog` 故意**没有**进场/退场动画：`exportOptionsDialog.opened` 被截图检查读取，动画在不刷新的窗口里会卡住导致 `opened` 一直为 false。
- `StudioPopover` 使用 `CloseOnPressOutside`（父项是 Overlay，`...OutsideParent` 会导致永远关不掉）；打开方式用 `toggle()`，保证再次点同一按钮能关闭。

## 5. 构建与验证

构建脚本：`scripts/build-windows.ps1 -QtRoot <Qt6.8.3-MSVC-kit> [-VisualStudioRoot <VS根目录>] -Deploy [-DeploymentDirectory <目录>]`。

本机注意事项（通用描述）：
- 若 `vswhere.exe` 不在 PATH，脚本里的 VS 自动查找和 `Enter-VsDevShell` 都会失败，表现为编译时找不到 `<type_traits>`。解决：把 VS Installer 目录加入 PATH，并显式传 `-VisualStudioRoot`，其值必须与 `build/desktop/CMakeCache.txt` 里 `CMAKE_CXX_COMPILER` 所在的 VS 安装一致。
- 不要把临时 `.qml` 文件放在仓库目录下（包括 `build/`），配置阶段会扫描并报错。
- `portable-ui` 若正在运行，部署到其他目录（`-DeploymentDirectory`）；本轮使用的是 `build/desktop/portable-claude`。`启动 ScreenPilot.cmd` 仍指向 `portable-ui`，需要用户关闭旧窗口后重新部署才会更新。
- 只改 QML 时 `cmake --build` 增量很快，然后把 `build/desktop/ScreenPilot.exe` 复制进部署目录即可；新增 QML import（如本轮的 `QtQuick.Shapes`）后需重新运行 `windeployqt --qmldir app`。

本轮最后一次全部通过的检查（同一份三秒生成短片，各自空输出目录，结果留在忽略的 `probe-results/`）：

| 参数 | 输出目录参数 | 结果 |
| --- | --- | --- |
| `--ui-snapshot-smoke-test` | `--editing-result-directory` | 通过，正常/最小窗口与导出面板截图已人工查看 |
| `--editor-shortcut-smoke-test` | `--editing-result-directory` | 通过 |
| `--timeline-interaction-smoke-test` | `--editing-result-directory` | 通过 |
| `--recent-ui-smoke-test` | `--editing-result-directory` | 通过 |
| `--scenes-ui-smoke-test` | `--scenes-result-directory` | 通过 |
| `--visual-ui-smoke-test` | `--scenes-result-directory` | 通过 |
| `--editor-batch-smoke-test` | `--editor-result-directory` | 通过 |

调试 QML 加载失败：运行时设 `QT_FORCE_STDERR_LOGGING=1` 可看到 `qrc:` 报错。锁屏或窗口不可见时，动画和新弹出项的帧不会刷新，截图只能部分验证弹出面板与动画；这类视觉验收需在解锁桌面上进行。

## 6. 用户反馈与已修问题

- 录制页弹出面板打开后无法关闭 → 已改关闭策略并支持再次点击关闭。
- “没有声音” → 排查结论：录制链路正常，问题出在当时系统声音和麦克风都处于关闭状态（其中一个文件没有音轨，另一个只有接近静音的轨道）。已改为系统声音按钮直接开关、关闭状态加斜线、两路都关时状态行警告。该设置保存在 QSettings `recording/systemAudio`。若用户在开启状态下仍无声，下一步应排查 Windows 默认输出设备与回环采集设备是否一致。

## 7. 未完成与建议后续

1. 用户在解锁桌面上手动走查录制页弹出面板、悬停/选中状态和录制中工具条（截图检查未覆盖）。
2. 录制期间独立置顶的悬浮工具条窗口（需参照备忘录做捕获排除）。
3. 运镜轨按线性时长排布，与主片段轨（短片段最小宽度 110、片段间隙）不对齐；可改为复用 `TimelineStrip.xForTime`，但要同步更新 `ManualZoomTrack` 拖动换算与 `--timeline-interaction-smoke-test` 的期望。
4. 时间线缩放、深色主题（原版令牌含深色值）、高对比与全 DPI 验收。
5. 画面预览外框阴影、逐页逐状态与原版对照。
6. `docs/requirements.md`、`docs/parity-checklist.md` 中的完整原版范围不变，界面工作不代表这些功能已完成。

发布前遵守 `AGENTS.md`：不公开截图、录屏、日志、个人路径和设备信息；检查全部待推送提交历史。

## 8. Codex 后端接续更新

后续发现并修复了系统声音位置单位错配和界面短阻塞漏录，不能沿用第6节“只有开关关闭”的结论解释所有声音问题。结果与边界见 `docs/development-reconnect-batch.md`。

新增录制规格后端与来源面板中的最小入口：`recorder.resolutionMode`（source/4k/1080p）、`videoCodec`（h264/hevc）、`availableVideoCodecs`（label/value）、`effectiveVideoSize`（QSize）。属性负责偏好持久化、合法值及录制中锁定，UI只传递选择意图。最终位置和视觉由Claude继续设计；保留 `recordingResolutionSelector`、`recordingCodecSelector` 对象名或同步更新调用方。

高分辨率自定义画布的原始导出已与预览尺寸分离。接口、使用行为及验证入口见 `docs/development-native-recording-batch.md`。不把编码能力查询视为HEVC或4K全场景真实录制通过。
