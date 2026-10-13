# Claude 第三轮 UI：新增入口与待 Codex 核对事项

更新：2026-10-13。本轮按 `docs/claude-ui-integration.md` 为已有后端能力补齐界面入口。分工：Claude 负责设计与 QML 实现；**接口对接核实、行为验证与修 bug 由 Codex 负责**。本轮只做了编译和启动加载检查（`--ui-snapshot-smoke-test` 通过、无 QML 警告），**没有**做功能、鼠标交互或媒体验证。

上段是Claude交接时的历史状态。Codex后续核实与实际检查见本文末尾；不要将历史“未验证”或下面原始假设当作最新结论。

## 新增文件

| 文件 | 内容 |
| --- | --- |
| `InspectorClip.qml` | 片段检查器：单选信息、分割/合并/裁短/恢复片头片尾/前后移/删除（全部绑定 `project.editingState`），预设倍率 + 自由倍率 + 目标时长；多选时切换为批量面板（`setSelectedClipsRate`、`removeSelectedClips`） |
| `InspectorAudio.qml` | 声音：系统/麦克风独立音量与静音（`project.setAudioOption`），旧工程提示；监听音量（`previewAudioProcessor.volume`）与监听静音 |
| `InspectorScreen3D.qml` | 屏幕 3D：静态姿态 7 个参数（`setCanvasOption`）、5 个预设、倾斜卡片预览；3D 运动片段列表与起点/终点姿态编辑（`effects` 的 `screenMotionSegments`） |
| `InspectorFocus.qml` | 模糊/聚光灯：添加、列表、在原始画面上拖框选区（`RegionBox` + `CropPreview`）、样式/强度/淡入淡出/时间 |
| `InspectorStickers.qml` | 贴图：`effects.addSticker` 添加、缩略图列表、画布示意拖放位置、宽度/旋转/不透明度/圆角/描边/颜色/投影/进出动画/时间 |
| `CameraMotionControls.qml` | 相机页新增：摄像头运动片段（位置板、全屏、大小、形状、转场、起止）与分段同步点（当前映射读数、添加、逐帧微调） |
| `EffectLane.qml` | 时间线效果轨：按主轨几何（`TimelineStrip.xForTime/timeForX/scrollX`）绘制，点击选中并切换检查器，拖动移动/两端调整后一次提交 |
| `EffectItemList.qml`、`StudioNumberField.qml`、`RegionBox.qml` | 共享：效果条目列表、数值输入、归一化拖框 |
| `RegionPicker.qml` | 录制区域：按来源比例的示意画布拖框、比例预设、像素读数；`setCaptureRegion` / `clearCaptureRegion` |
| `SettingsDialog.qml` | 设置：主题、语言、预览质量、完成提示音、自动恢复开关、默认工程/导出目录、更新检查与发布页 |
| `RecoveryPanel.qml` | 恢复工程：快照列表、恢复（未保存时先要求放弃或取消）、删除、立即快照、自动保存开关 |

## 改动的现有文件

- `StudioTheme.qml`：新增 `dark` 与整套深色值、效果轨配色；`Main.qml` 用 `Binding` 把它绑定到 `appSettings.effectiveTheme`。
- `Main.qml`：区域按钮可用（打开 `RegionPicker`），删除“设备”按钮（iOS 已取消）；新增“录制规格”卡片与弹出面板（帧率/分辨率来自 `recorder.capabilities`，编码来自 `availableVideoCodecs`，输出尺寸 `effectiveVideoSize`，系统声音范围 `systemAudioScope`，暂不支持项只读显示）；开始按钮改为 `readiness.canStart`，状态行显示 `readiness.reason`；页眉新增设置按钮和“可恢复 N”入口；处理 Editor 的 `settingsRequested` / `recoveryRequested`。
- `Editor.qml`：
  - 新信号 `settingsRequested()`、`recoveryRequested()`；新属性 `monitorMuted`（并入 `previewAudioProcessor.muted` 的 Binding，不直接改 processor）、`editState`、`exportSummary`、导出范围三项。
  - `inspectorTab` 扩展：5 声音、6 屏幕 3D、7 模糊聚光、8 贴图（0–4 含义不变）。左栏改为可滚动的 10 项。
  - 页眉标题可点击重命名（`project.renameProject`）；工程菜单新增重命名、恢复、设置。
  - 时间线：工具栏按钮按 `editingState` 禁用；“添加”菜单（运镜、3D、模糊、聚光灯、贴图、摄像头运动、同步点）；多选计数徽标；监听音量弹出面板；片段右键菜单；有内容的效果轨显示在运镜轨下方。
  - 导出面板：整个工程/自定义范围（起止输出秒、设为播放头、范围条），右侧摘要来自 `describeProjectExport`（片段数、声音、摄像头、错误），`canStart` 控制开始按钮；`startProject` 传入范围。
  - `deleteSelection()` 在多选时调用 `removeSelectedClips()`。
- `TimelineStrip.qml`：Ctrl/Shift 点击调用 `selectClip(index, extend, toggle)`；片段两端 10 px 拖动后调用 `trimClipBoundary`（输出像素 → 源秒按片段倍率换算）；右键发出 `clipMenuRequested`；新增 `scrollX`、`isSelected()`。片段移动重排和中心点击行为保留。

## 需要 Codex 核对的假设与可能问题

1. **选中状态**：`effects.selectedItems[track]` 在未选中时返回空对象，UI 统一按“有 `id` 才算选中”处理。若后端改为返回 null 也兼容。
2. **同步点添加**：`effects.addAtOutput("syncPoints", t, 0)` 以时长 0 调用，随后 `update(..., {cameraTime})`。需确认时长 0 被接受、并且后续写入不违反递增约束。
3. **3D 运动姿态**：编辑单个参数时只发送局部 patch（如 `{from: {screenRotationY: v}}`）；“起终互换”发送完整的 from/to。需确认合并语义。
4. **贴图缩略图**：假设 `imagePath` 是绝对本地路径，拼成 `file:///` URL 显示。
5. **设置目录**：从 FolderDialog 的 URL 转成本地路径字符串（去掉 `file:///` 并解码）后调用 `setValue("projectDirectory"| "exportDirectory", path)`。需确认后端期望的格式。
6. **恢复**：未保存时 UI 先拦截；恢复成功后切到编辑器。需确认 `restoreSnapshot` 之后 `project.changed` 能让编辑器重新加载视频源和摄像头。
7. **效果轨拖动**：移动量 = `effects.sourceTime(timeForX(新 x)) − effects.sourceTime(timeForX(原 x))`。片段重复使用同一段源素材、或跨越变速边界拖动时需要验证结果。摄像头运动用 start/end，其余用 sourceStart/sourceDuration。
8. **片段边界拖动**：没有实时宽度预览，只用琥珀色带示意，松开后一次提交；限制在 [0, 素材时长]，最小 0.05 秒。
9. **区域示意**：显示器来源用 `Qt.application.screens[Number(source.id)]` 取尺寸（假设屏幕来源 id 与屏幕序号一致，且屏幕排在窗口之前）；窗口来源按 16:9 示意，像素尺寸未知。
10. **摄像头运动位置板**：x/y 按 0–1 的“剩余空间比例”示意（与现有画中画 x/y 一致的假设），需要和实际渲染对照。
11. **导出摘要**：只在导出面板可见时调用 `describeProjectExport`，其余时间使用 `projectSummary`。
12. **运镜轨**：手动运镜轨仍按线性时长绘制，和主轨/效果轨（按片段几何）对不齐，这是原有问题，未在本轮修改。
13. **深色主题**：令牌已切换，但仍有少量写死的颜色（录制舞台深色底、个别图标灰、未保存徽标文字等），需要在深色模式下走查。
14. **语言**：只保存设置，界面没有英文翻译资源；设置页已如实说明。
15. **快捷键**：新增弹窗/弹出面板都在 Overlay 中，依赖 `shortcutsAvailable` 的 Overlay 子项检查来屏蔽编辑快捷键；`Delete` 已支持多选删除。

## 没有做成可用入口的能力

ProRes 录制、隐藏系统光标、独立光标形状只在录制规格中以“暂不支持”只读显示。iPhone/iPad 设备输入已移除入口。更新只检查并打开发布页，不下载安装。英文界面未实现。

## Codex核实状态（2026-10-13）

逐条按C++与当前QML检查。下表的源码核实、构建和运行证据分别登记；硬件、长期稳定性与所有平台组合不包含在短测结论中。

| 原假设 | 结论 | 证据与处理 |
| --- | --- | --- |
| 1 选中状态 | 通过 | selectedItems未选择时是空对象；id守卫正确。模型检查包括创建、选择、删除、撤销与旧ID适配。 |
| 2 同步点 | 已修 | 时长0本来被接受；旧UI先插入身份映射再改cameraTime可能临时违反递增约束或重复扣offset。改为一次写入当前原始映射的addSyncAtOutput，generic addAtOutput也采用同样语义；严格递增拒绝不会改变工程。 |
| 3 3D局部patch | 通过 | EffectController只合并被修改的from/to字段，保留其他姿态，完整交换也支持；模型检查通过。 |
| 4 贴图URL | 已修 | 通过effects.localFileUrl/QUrl::fromLocalFile转换，避免手拼URL损坏空格、Unicode、#和%。模型往返与实际QML缩略图解码通过。 |
| 5 设置目录 | 已修 | FolderDialog原URL原样传setValue，由后端QUrl转换；移除手动去前缀/二次解码。目录规则检查通过；UNC只核实转换语义，未访问真实网络共享。 |
| 6 恢复后媒体 | 已修/通过 | 恢复前暂停编辑与摄像头，避免recovery.blocked拒绝；清理过期pendingRestore。实际未保存保护、快照恢复、屏幕与摄像头source重新加载通过。 |
| 7 效果拖动 | 已修 | 不再相减可能跳跃的绝对源时间；TimelineStrip.sourceDeltaForX按各显示片段累计源距离。所有重复出现保持同一源效果身份，移动仍同步影响重复实例。五类轨道的真实鼠标拖动及撤销通过，重排切点与重复实例检查通过。 |
| 8 主轨边界 | 已修/通过 | 片段倍率换算正确；最小长度改为0.05输出秒对应的源长度，保留本来更短片段。2倍速下左右边界实际鼠标拖动、松开提交、0.05秒限制与撤销通过。检查在撤销后等待QML布局刷新，未放宽断言。 |
| 9 区域尺寸 | 已修 | 不再索引Qt.application.screens；recorder.sources提供屏幕物理尺寸估计及DPR，窗口未知仍明确示意。像素读数按floor/ceil计算，最终偶数编码尺寸以effectiveVideoSize为准。中心半幅窗口实录与解码通过；混合DPI多显示器未验收。 |
| 10 摄像头位置 | 已修 | 实际渲染是canvas*x/y后夹在边界，不是剩余空间比例。位置板使用同样算法，并接入实际画布与摄像头帧比例；真实摄像头硬件未验收。 |
| 11 导出摘要 | 通过 | 可见时调用范围查询，隐藏时默认全片；依赖项绑定存在。0.3秒范围M4A实际导出与解码时长通过，静音开关实际导出接近零。 |
| 12 手动运镜轨 | 已修 | 复用主轨xForTime/timeForX/scrollX以及源距离换算；保留孤立组件fallback。更新后的鼠标测试核对新几何，完整结果见下。 |
| 13 深色主题 | 已修/通过 | 修复停止按钮、未保存徽标、未选图标、开关/滑块/卡片阴影等界面色；媒体背景与效果色保留。已查看深色编辑器与设置截图，未另建配色体系。 |
| 14 语言 | 已修 | English原来仍能选择；改为只读说明中文可用、英文未支持，避免只保存参数却假装切换界面。 |
| 15 快捷键 | 通过 | 原有文本输入、原生撤销、帮助弹窗屏蔽通过；新增设置面板打开时Delete不修改片段，多选批量删除与撤销通过。未逐个遍历所有弹出层组合。 |

### 额外发现并修复

- 跨片段播放到源媒体末尾时，Qt的StoppedState可能覆盖同一处理器中的play()；增加有用户操作代次保护的延后恢复。实际蓝/红/绿重排、变速与摄像头同步检查通过。
- Windows工程重命名还受到时间线后台解码器和独立WAV读取器影响。TimelineMedia.releaseMedia等待其拥有线程关闭解码器，混音轨临时释放，编辑器屏幕/摄像头在后续事件回合重命名后恢复；实际重命名、重开、画面重新加载通过。
- 在素材末尾附近按默认2/3秒新增效果会越界。按输出播放头新增的默认时长限制到剩余源长度；贴图新增同样限制。手动修改仍严格校验，不能越过素材末尾。

### 检查与复现

构建使用现有CMakeCache对应编译器。所有输入是程序生成素材；输出路径、截图、JSON和录制仅在本地忽略的probe-results，测试偏好与恢复目录也隔离在该目录。系统音量保持用户设置，不用短测推断主观听感。

在仓库根目录，将$app指向已部署ScreenPilot.exe，$source指向3秒红/绿/蓝生成夹具，$out为新的probe-results子目录：

```powershell
& $app --ui-snapshot-smoke-test $source --editing-result-directory "$out/snapshot"
& $app --editor-shortcut-smoke-test $source --editing-result-directory "$out/keys"
& $app --timeline-interaction-smoke-test $source --editing-result-directory "$out/timeline"
& $app --recent-ui-smoke-test $source --editing-result-directory "$out/recent"
& $app --scenes-ui-smoke-test $source --scenes-result-directory "$out/scenes"
& $app --visual-ui-smoke-test $source --scenes-result-directory "$out/visual"
& $app --editor-batch-smoke-test $source --editor-result-directory "$out/editor"
& $app --backend-batch-smoke-test $source --backend-result-directory "$out/backend"
& $app --third-wiring-smoke-test $source --wiring-result-directory "$out/new-ui"
$env:SCREENPILOT_TEST_REGION='1'
& $app --recording-profile-smoke-test "$out/region"
Remove-Item Env:SCREENPILOT_TEST_REGION
```

每项检查退出码与对应结果文件，不以列出命令代替实际通过。原有录制暂停/重录/丢弃与原始素材复制取消也已运行；最新公开摘要见[验证记录](validation/public-results.md)。

### 本轮实际结果

- 构建和部署通过。最终版本的ui-snapshot、editor-shortcut、timeline-interaction、recent-ui、scenes-ui、visual-ui、editor-batch全部退出0；本轮新增third-wiring、backend-batch、playback检查也退出0。运行日志未发现QML引用错误、赋值错误或绑定循环。
- 时间线真实鼠标检查：五类效果轨移动、保持时长、删除/撤销；主轨2倍速左右边界与最小0.05输出秒限制；Ctrl切换与Shift范围选择；手动运镜共享几何。效果创建/删除的模型检查和真实鼠标移动检查分开进行，不等于逐个点击所有新增按钮。
- 新入口实际QML检查：缩略图解码、多选批量变速/删除/撤销、重命名后重开、未保存恢复保护、快照恢复后屏幕与合成摄像头媒体加载、设置面板屏蔽Delete、返回录制和取消导出通过。
- 媒体检查：中心半幅窗口短录制解码926×544，对照全幅1852×1088；混音M4A有非零音频，静音M4A解码接近零；2.5倍速范围导出约0.3秒并通过40毫秒容差检查。自有窗口录制收尾进入编辑器后播放、暂停、继续通过；重排变速跨片段与合成摄像头检查通过。
- 深色编辑器及设置窗口截图已查看。测试运行时临时目录清理出现过文件占用提示，残留仅位于忽略的probe-results；不影响上述功能断言，也未尝试删除用户目录。

限制：区域实录为自有窗口，未覆盖混合DPI显示器；真实摄像头硬件、UNC共享实际访问、全部弹出层组合、长视频性能和主观音质仍未验收。摄像头位置板与渲染公式完成源码对照，未将此等同于硬件验证。ProRes、系统光标隐藏/形状、更新安装及英文翻译仍不可用。
