# ScreenPilot 原版能力对齐清单

版本：0.2 · 日期：2026-10-13 · 阶段：第 2 步「验证技术风险」（产品依据保持固定）

## 目标与证据边界

将固定提交 `fa24059a9ba7e769152b3c1de047d96aca873e1c` 的 DogSC Mac 产品体验先完整带到 Windows，完成后再逐步移植 Linux。所有表内能力均在首版范围；分批开发仅决定实现顺序。遇到平台约束先验证替代方案，需要改变产品行为时再与用户讨论，不能以实现难度自行删减。

本清单基于该提交的源码静态检查、[原版 README](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/README.md)和随提交保存的四张截图。源码确认表示存在相应控件、处理路径或模型规则，不等于已在 Mac 上运行成功。本文没有执行上游代码；独立 Windows 探针结果见 [W01 报告](validation/w01-window-capture.md)，不替代产品验收。所有功能的 Mac 实测和完整 Windows 验收均为**未执行**，Linux 为**后续移植、当前不开展**。截图只证明展示布局，不证明运行质量。

覆盖方法：从录制入口、来源与设备选择、编辑器检查器、时间线操作、工程命令、导出和设置入口追踪到处理代码。以用户可检查的行为为条目，不将内部辅助类或仅用于旧工程解码的枚举当作新功能；也不把 README 未提到的真实入口排除。选项、范围和默认值以表内固定提交源码为准，用户偏好和旧工程可能覆盖初始化值。

当前每条验收只在声明支持的 Windows 环境执行，记录输入工程/媒体、步骤、预期、实际结果、系统环境及证据。视觉与同步的数值容差在第 2 步测量后冻结，不能凭“看起来差不多”直接判通过。

## 界面基线

| 页面 | 固定参考 | ScreenPilot 要求与验收 |
| --- | --- | --- |
| 录制条 | [recorder.png](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/docs/images/recorder.png) | 保留来源、声音、摄像头、保存目的地、备忘录和关闭等操作关系；检查默认、选源、录制中、停止与错误状态 |
| 编辑器 | [editor.png](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/docs/images/editor.png) | 对齐顶部项目/撤销/预设/导出、左侧分类、中央画布、右侧检查器、底部多轨时间线；逐个入口核对，截图中的选中值不是默认值 |
| 屏幕 3D | [motion.png](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/docs/images/motion.png) | 对齐构图、目标尺寸、倾斜控件和运动片段；选中片段后参数与预览联动 |
| 导出 | [export.png](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/docs/images/export.png) | 对齐文件名/目录、视频与音频、分辨率/帧率、当前画面和输出摘要；另检查处理中、取消和失败状态 |
| 小窗口布局 | [EditorWorkspaceLayout.swift:9](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorWorkspaceLayout.swift#L9) | 原版最小编辑窗口为 840×600 点；压缩工具栏与时间线控件。Windows 在最小窗口、1080p及高 DPI 检查无遮挡且所有主要功能可达，不将点数直接当物理像素 |

## 能力表

表内“验收”描述未来的检查方法，不代表测试已通过。原版行为由对应源码确认；外部设备、系统权限和视觉效果仍需 Mac 实测。

### 录制与输入（C01–C25）

| 编号 | 原版行为与 ScreenPilot 要求 | 源码依据 | 验收方法 |
| --- | --- | --- | --- |
| C01 | 屏幕、窗口、区域、iOS设备四类来源，初始屏幕 | [CaptureConfiguration.swift:65](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/CaptureConfiguration.swift#L65) | 四类选源、取消与重选，检查选择态和素材 |
| C02 | 选源使用专用选择器和确认token，开始前生成录制计划 | [CaptureSetupController.swift:275](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CaptureSetupController.swift#L275) | 多显示器切换、取消重选，不沿用过期目标 |
| C03 | 独立窗口身份采集；窗口关闭和显示器断开有错误 | [ScreenRecorder.swift:157](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ScreenRecorder.swift#L157) | 移动、遮挡、关闭窗口，确认采集和错误 |
| C04 | iOS真实设备选择UI：列表、刷新、Esc取消；未选择不能开始 | [CaptureDeviceSelection.swift:299](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CaptureDeviceSelection.swift#L299) | 接入iPhone/iPad、刷新、选择、取消、开始；空列表禁止开始 |
| C05 | 设备断开清除选择信息，录制计划识别断开 | [CaptureSetupController.swift:421](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CaptureSetupController.swift#L421) | 热插拔前后检查列表、选择态与停止结果 |
| C06 | 录制配置初始60FPS；不因枚举保留90/120而增加界面档位 | [CaptureConfiguration.swift:201](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/CaptureConfiguration.swift#L201) | 核对配置及素材帧时间；录制帧率UI完整范围待补核 |
| C07 | 录制编码HEVC/H.264/ProRes422，初始HEVC；H.264兼容最高4K，ProRes为MOV | [RecorderConfigurationViews.swift:214](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L214) | 三编码分别录制，检查容器、实际编码和尺寸 |
| C08 | 录制分辨率原始/最高4K/最高1080p，初始原始；iOS隐藏屏幕编码和桌面/Dock选项 | [RecorderConfigurationViews.swift:261](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L261) | 横竖屏三档尺寸；切换设备源核对显隐 |
| C09 | 隐藏桌面文件和Dock独立开关，初始均关 | [CaptureSurfaceFilter.swift:15](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CaptureSurfaceFilter.swift#L15) | 四种组合录桌面与普通文件管理器窗口；检查对应行为 |
| C10 | 辅助窗口排除，登记的编辑器窗口可录入；屏幕流关闭原生鼠标并排除自身音频 | [ScreenRecorder.swift:244](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ScreenRecorder.swift#L244) | 录制条、备忘录、编辑器同时出现时检查结果；不得双鼠标 |
| C11 | 系统声音初始开启、范围全部，偏好记忆 | [AppPreferences.swift:525](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppPreferences.swift#L525) | 清空偏好首启；切换开关和范围后重启 |
| C12 | 可仅录所选窗口App声音，需有效应用身份；无输出进程报错 | [CoreAudioSystemAudioTap.swift:78](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CoreAudioSystemAudioTap.swift#L78) | 两个应用同时发声；选择一个应用，退出或停止后检查 |
| C13 | iOS源系统声音入口对应设备声音，隐藏桌面声音范围二选一 | [RecorderConfigurationViews.swift:135](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L135) | 设备与桌面同时发声，核对所录音源及开关 |
| C14 | 麦克风初始关，可选设备，开启后有实时电平 | [RecorderConfigurationViews.swift:48](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L48) | 多麦克风切换、讲话、电平与素材轨道、偏好记忆 |
| C15 | 摄像头初始关，可选设备与自动/可用分辨率，具有加载和预览状态 | [RecorderConfigurationViews.swift:61](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L61) | 切换设备、尺寸，检查预览和素材；测试占用和断开 |
| C16 | 摄像头镜像初始开并记忆；系统视频效果入口；浮动预览形状默认圆形 | [AppModel.swift:182](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel.swift#L182) | 切换镜像并重启；检查预览、实际录制与平台效果差异 |
| C17 | 鼠标位置和形状独立事件轨；跟随窗口坐标，形状采样30Hz，支持暂停时钟 | [PointerEventRecorder.swift:138](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/PointerEventRecorder.swift#L138) | 移动、点击、变换形状、移动窗口和暂停，检查时间与坐标 |
| C18 | 鼠标采集有事件tap与monitor fallback；fallback存在不代表权限降级已验证 | [PointerEventRecorder.swift:593](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/PointerEventRecorder.swift#L593) | 授权/拒绝分别录制，检查事件完整性和提示 |
| C19 | 独立备忘录，文本/字号/透明度持久化；初始空、22pt、0.92；范围16–40、0.55–1 | [RecorderMemo.swift:22](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderMemo.swift#L22) | 编辑、调整、移动/缩放、收起重开和重启，检查恢复 |
| C20 | 录屏/辅助功能权限准备门与系统设置引导；录制实现依赖已完成的权限准备 | [CaptureReadiness.swift:111](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/CaptureReadiness.swift#L111) | 拒绝、授权、返回应用；麦克风/摄像头按需授权另测 |
| C21 | 暂停/继续用分段接续并扣除暂停时间；录制中标记按源时间保存 | [AppModel+CaptureLifecycle.swift:654](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+CaptureLifecycle.swift#L654) | 两次暂停继续并打标记，核对画面/声音/鼠标/标记 |
| C22 | 重新录制/丢弃有破坏性确认，重录回收当前录制并按同配置重启 | [RecorderViews.swift:211](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderViews.swift#L211) | 取消保留录制；确认后核对回收站和配置 |
| C23 | 项目文件夹与文件名可设，空名自动生成；项目和导出目录分离 | [RecorderConfigurationViews.swift:206](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecorderConfigurationViews.swift#L206) | 不同目录、空名/指定名分别录制，检查保存位置 |
| C24 | 先写恢复区、归档后编辑；同名追加后缀；支持继续上次工程及中断录制恢复 | [ProjectStore.swift:607](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectStore.swift#L607) | 同名两次不覆盖；异常退出后恢复有效素材 |
| C25 | 停止/异常持久化或丢弃及退出清理录制资源 | [AppModel+CaptureLifecycle.swift:910](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+CaptureLifecycle.swift#L910) | 正常停止、设备故障、保存失败和退出，确认可恢复且设备释放 |

### 编辑、运镜与效果（E01–E46）

| 编号 | 原版行为与 ScreenPilot 要求 | 源码依据 | 验收方法 |
| --- | --- | --- | --- |
| E01 | 主片段在播放头分割，不满足条件时禁用 | [EditorTimelinePrimaryEditing.swift:45](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L45) | 中间/边界分割，总时长与连续性 |
| E02 | Q/W波纹删除前后内容；选动画时改为修剪动画边界 | [EditorShortcutCheatsheet.swift:33](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L33) | 主轨与动画两种选择状态核对 |
| E03 | 还原开头、结尾、段间已剪素材，可合并连续切缝 | [EditorTimelinePrimaryEditing.swift:97](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L97) | 三位置分别剪掉后还原，同源缝合并 |
| E04 | 单选、Shift连选、Command增减选择，Option点击切分 | [EditorTimelinePrimaryEditing.swift:772](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L772) | 三段选择集合及点击切点；平台修饰键映射 |
| E05 | 主片段拖动重排并更新时间线 | [EditorTimelinePrimaryEditing.swift:904](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L904) | 首段移尾，检查播放顺序、时长与撤销 |
| E06 | 端点修剪；Control拖右端自由变速；菜单1/1.5/2/4/8/12/20/50/100倍 | [EditorTimelinePrimaryEditing.swift:587](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L587) | 菜单/拖动变速，检查声音、时长、撤销 |
| E07 | D/Delete删除当前项，主片段走编辑提交 | [EditorTimelinePrimaryEditing.swift:992](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelinePrimaryEditing.swift#L992) | 删除主片段/效果后选择态正确，可撤销 |
| E08 | 磁吸捕获/释放、目标时间、移动及端点修剪 | [EditorTimelineMagneticSnap.swift:25](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelineMagneticSnap.swift#L25) | 慢拖靠近/离开边缘，不同缩放下检查 |
| E09 | 悬停预览不移播放头；指针锚定缩放；总览条定位 | [EditorShortcutCheatsheet.swift:27](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L27) | 核对原播放头、指向时间、总览范围 |
| E10 | 空格播放暂停，左右1帧，Shift左右5帧 | [EditorShortcutCheatsheet.swift:24](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L24) | 按项目帧率检查步长，输入框不误触 |
| E11 | 光标样式与隐藏；隐藏同时关闭光标和点击反馈 | [EditorInspectorMediaControls.swift:7](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L7) | 预览及导出均隐藏/恢复 |
| E12 | 光标automatic、大小1.25；UI大小0.25–6倍；静止隐藏默认关、延迟1.2秒 | [EditorInspectorMediaControls.swift:23](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L23) | 新工程、静止/移动、延迟0.2–8秒 |
| E13 | 光标时间校准正提前/负延后，10/100ms步长、归零，限±1秒 | [EditorInspectorMediaControls.swift:45](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L45) | ±100ms移动/点击对照，不改变平滑参数 |
| E14 | 点击反馈默认ripple、透明度0.8、范围1，可关闭/选色 | [EditorInspectorMediaControls.swift:79](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L79) | 点击样式/颜色/范围，预览导出对照 |
| E15 | 屏幕focused、光标smooth；弹簧模型默认屏幕2.4/210/42，光标2.8/450/66 | [ProjectModel.swift:1247](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L1247) | 同轨迹不同运动样式与参数对照 |
| E16 | 自动/手动缩放；倍率模型1.6；线性/二次/三次/五次/弹簧/自定义曲线 | [ZoomAnimation.swift:86](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ZoomAnimation.swift#L86) | 六曲线关键时点与持久化；自动规划输入输出对照 |
| E17 | 缩放焦点图面定位和归一化X/Y输入0–1 | [EditorZoomViews.swift:39](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorZoomViews.swift#L39) | 四角、数值输入、裁切后映射 |
| E18 | 新建缩放默认spring、转场0.7秒，模型约束0–5秒；倍率默认可记忆 | [ProjectModel.swift:1256](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L1256) | 新建多段、改配置后检查后续值 |
| E19 | 拖空白创建3D/摄像运动，移动/端点/分割/删除/修剪 | [EditorTimelineMotionEditing.swift:276](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelineMotionEditing.swift#L276) | 两轨全部操作并核对时间与撤销 |
| E20 | 3D位置/缩放/XYZ旋转/透视；默认居中、1倍、0度、透视0.65 | [ScreenMotion.swift:5](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ScreenMotion.swift#L5) | 逐参数调整保存重开 |
| E21 | 3D界面倍率0.25–4、Z角±30度、透视0–2、XY倾斜板 | [MotionInspectorViews.swift:91](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/MotionInspectorViews.swift#L91) | 边界值与倾斜板，预览导出一致 |
| E22 | 组合布局屏幕与摄像运动group配对，同步移动/修剪时序 | [EditorTimelineMotionEditing.swift:737](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelineMotionEditing.swift#L737) | 两侧分别拖动修剪，时序配对与撤销 |
| E23 | 摄像头形状/位置/大小/边框/阴影/圆角/镜像/隐藏/随缩放大小；基础默认square、位置(.96,.47)、大小.29 | [EditorInspectorMediaControls.swift:253](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L253) | 各项独立调整后保存与导出；不同于录制浮窗形状 |
| E24 | 摄像头内部取景平移/缩放/重置，默认中心、1倍 | [EditorInspectorMediaControls.swift:370](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L370) | 放大偏移再重置，外相框不改变 |
| E25 | 摄像运动shape/fullscreen，方/圆/横/纵/原始比例，全屏随画布 | [CameraMotion.swift:367](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/CameraMotion.swift#L367) | 五形与全屏，核对比例和圆角 |
| E26 | 摄像布局当前/全屏/画中画预设；运动目标默认square、(.86,.82)、大小.35 | [EditorInspectorMediaControls.swift:409](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L409) | 三预设，区分基础布局与运动目标 |
| E27 | 摄像全片偏移10ms/1帧微调，归零保留分段点，调整后试听 | [EditorInspectorMediaControls.swift:539](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L539) | 添加点再归零，嘴型与导出同步 |
| E28 | 固定/微调/删除同步点，两点间平滑重映射，空白双击加点 | [EditorInspectorMediaControls.swift:575](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L575) | 两点修正漂移、切片后源时间映射连续 |
| E29 | 柔化blur/spotlight，默认强度.55、圆角.08、压暗.22、无转场、入出.30秒、片段3秒 | [EditorOverlayInspector.swift:25](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorOverlayInspector.swift#L25) | 区域、样式、强度、时长与转场边界 |
| E30 | 缩放/3D可附聚焦效果，普通/线性模式、固定/跟随目标 | [EditorFocusEffectInspector.swift:35](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorFocusEffectInspector.swift#L35) | 两类动画的模式、跟随与固定对照 |
| E31 | 贴图位置/宽/旋转/透明度；默认中心、宽.38、0度、不透明 | [EditorOverlayInspector.swift:146](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorOverlayInspector.swift#L146) | 导入、几何、越界、保存与导出 |
| E32 | 贴图层级/圆角/描边/阴影、录屏虚化、隐藏录屏/摄像 | [EditorOverlayInspector.swift:180](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorOverlayInspector.swift#L180) | 两贴图交换层级，背景处理及遮挡 |
| E33 | 贴图入出动画，默认pop/swift、入.7秒出.22秒 | [OverlayTimeline.swift:254](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/OverlayTimeline.swift#L254) | 边界逐帧、保存重开 |
| E34 | 场景预设保存/更新/设默认/应用/删除；应用画布、摄像、光标、运镜、声音及开场，复制背景资产 | [EditorScenePresets.swift:243](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorScenePresets.swift#L243) | 跨工程应用、整体撤销、重启默认 |
| E35 | 系统音/麦克风独立音量与静音，全局默认.82/.9均不静音，主片段另有控制 | [EditorInspectorMediaControls.swift:849](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorMediaControls.swift#L849) | 全局与片段组合，分别静音及导出 |
| E36 | 效果复制/粘贴副本、同类属性粘贴保留位置时长；指针优先、否则播放头 | [EditorShortcutCheatsheet.swift:41](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L41) | 五类效果复制，同类/异类、指针内外与吸附 |
| E37 | 撤销重做、打开/导出/范围导出、检查器开关、Esc取消；快捷键速查 | [EditorShortcutCheatsheet.swift:49](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L49) | 快捷键、文本焦点、拖动取消和撤销序列 |
| E38 | 画布自适应/预设/自定义比例；裁切开始暂停预览、确认提交、取消还原、重置全画面 | [EditorViewCropAndPreset.swift:28](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorViewCropAndPreset.swift#L28) | 确认/取消/重置裁切，比例切换后的预览与导出尺寸 |
| E39 | 画布留白、素材位置/缩放及重置、圆角、描边色与透明度、阴影 | [EditorInspectorView.swift:645](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorView.swift#L645) | 逐项编辑、直接拖画面、撤销、保存重开及导出 |
| E40 | 图案、动态背景、本机壁纸、导入静态图片或MOV/MP4；视频循环并参与导出；图案密度/强度可调 | [EditorBackgroundInspector.swift:20](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorBackgroundInspector.swift#L20) | 切换各来源，循环边界、素材复制、缺失路径、预览导出 |
| E41 | 样机无/浅深窗口/浅深浏览器/自适应设备；可还原样式而保留标题/网址；随屏幕缩放与3D | [EditorVisualEffectsControls.swift:12](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorVisualEffectsControls.swift#L12) | 各样机、标题/网址、几何、还原、组合3D和导出 |
| E42 | 全局开场开关、方式、动效节奏、0.4–8秒总时长、元素间隔/参与顺序、从片头预览 | [EditorInspectorView.swift:431](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorInspectorView.swift#L431) | 多元素顺序；0秒贴图入场接管不叠加，柔化首帧有效 |
| E43 | 轨道显示管理，隐藏仅收起编辑区，不停用效果 | [EditorTimelineTrackVisibility.swift:161](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelineTrackVisibility.swift#L161) | 隐藏有效果轨后预览导出不变，重新显示编辑状态保留 |
| E44 | 标记在尺规显示，可跳转与删除，编辑删除可撤销 | [EditorTimelineRecordingMarkers.swift:28](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorTimelineRecordingMarkers.swift#L28) | 录制标记重开定位，删除撤销且成片不画标记 |
| E45 | 录制结束鼠标数据生成自动缩放，按录制时长剪裁；旧自动轨可修复 | [RecordingPointerFinalization.swift:29](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/RecordingPointerFinalization.swift#L29) | 点击/移动/静止/边界输入对照算法输出，剪切变速后核对 |
| E46 | 主轨缩略图和音频波形、时间线高度调整与恢复 | [EditorShortcutCheatsheet.swift:46](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorShortcutCheatsheet.swift#L46) | 长素材缩放/滚动时显示正确；双击分栏柄恢复高度 |

### 工程、导出与设置（P01–P24）

| 编号 | 原版行为与 ScreenPilot 要求 | 源码依据 | 验收方法 |
| --- | --- | --- | --- |
| P01 | 工程打开面板单选、初始项目目录、过滤工程类型 | [AppModel+ProjectCommands.swift:621](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+ProjectCommands.swift#L621) | 打开、取消、错误类型、有效工程分别检查 |
| P02 | 按setup/editor/录制中/录制结束状态分流打开请求 | [AppModel+ProjectCommands.swift:672](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+ProjectCommands.swift#L672) | 四状态分别打开旧工程，检查当前内容保护 |
| P03 | 最近工程注册最多20条，列表默认取8条，支持最近/恢复入口 | [ProjectStore.swift:1165](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectStore.swift#L1165) | 多工程、重启、失效路径，核对排序数量 |
| P04 | 默认项目目录影片/DogSC/项目；自定义目录无效回退默认 | [ProjectStore.swift:582](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectStore.swift#L582) | 更改目录再移走目录，检查回退与提示；平台路径对应 |
| P05 | 显式保存、标题提交与去抖自动保存，共用会话持久化 | [ProjectSessionPersistenceController.swift:338](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectSessionPersistenceController.swift#L338) | 修改等待后重开、立即保存重开；去抖时长待补核 |
| P06 | 关闭按持久化/未保存录制/编辑状态处理，可保留、保存、回收；失败保留完成卡 | [AppModel+ProjectCommands.swift:105](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+ProjectCommands.swift#L105) | 各状态关闭与保存失败，确认不丢工程 |
| P07 | 删除当前工程须确认移到废纸篓，成功才关闭 | [AppModel+ProjectCommands.swift:221](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+ProjectCommands.swift#L221) | 取消不变；确认后检查回收站和窗口 |
| P08 | 工程改名重定位包、媒体路径与最近列表，冲突用唯一目标名 | [ProjectStore.swift:1325](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectStore.swift#L1325) | 改名/同名冲突后重开，三类素材可读 |
| P09 | schema当前27、最低23；拒绝过旧/未来工程与编码未来版本 | [ProjectModel.swift:7](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L7) | v22/23/27/28样例，检查接受/拒绝及数据保护 |
| P10 | 迁移备份原JSON；主JSON失败可回退previous；未来版本禁止回退旧快照 | [ProjectStore.swift:1089](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectStore.swift#L1089) | 旧工程备份、损坏恢复、未来版本附旧快照仍拒绝 |
| P11 | 完整工程和选区导出，标题与时长对应范围 | [EditorExportViews.swift:90](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L90) | 完整/选段导出，检查输出起止与时长 |
| P12 | 视频默认，MP4与仅音频M4A两种内容；音频为剪辑后混音 | [EditorExportViews.swift:417](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L417) | 两模式扩展名、摘要、混音和输出内容 |
| P13 | 原始/1080p/1440p/4K，尺寸按画布裁切和素材计算，默认原始 | [EditorExportViews.swift:447](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L447) | 各档横竖画布，摘要与实际像素一致 |
| P14 | 导出只提供30/60FPS，初始60，输出设置存工程 | [EditorExportViews.swift:461](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L461) | 两档实际帧率、重开参数；不新增90/120档 |
| P15 | 文件名内联编辑校验、目的目录可选且记忆 | [EditorExportViews.swift:705](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L705) | 非法名称、取消编辑、换目录和重启 |
| P16 | 未就绪禁止导出；处理中禁改配置、可取消；完成前进度最多99%，阶段明确 | [EditorExportViews.swift:232](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L232) | 准备中、长导出、取消、失败，核对进度状态 |
| P17 | 成功页显示所在文件夹/完成关闭/支持入口；完成提示音默认开，可试听关闭 | [EditorExportViews.swift:190](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/EditorExportViews.swift#L190) | 成功与失败结果区分；文件管理器跳转与提示音开关 |
| P18 | 另有源文件导出：唯一文件夹内复制屏幕/摄像头/麦克风原始媒体，失败清理本次目录 | [ProjectMediaExchange.swift:32](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/ProjectMediaExchange.swift#L32) | 三源/单源、重复导出、失败，确认无重编码与覆盖 |
| P19 | 已确认背景视频导入及已有摄像头替换；未发现任意视频新建主轨工程入口 | [AppModel+ProjectCommands.swift:342](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppModel+ProjectCommands.swift#L342) | 有/无摄像头工程检查替换入口；无声短视频可替换；不扩大主轨导入范围 |
| P20 | 设置五页：通用、编辑器、录制、权限、关于；通用含目录、提示音和重新引导 | [AppSettingsView.swift:7](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppSettingsView.swift#L7) | 逐页控件与持久化，重新显示引导 |
| P21 | 跟随系统/中文/English，保存语言覆盖 | [AppPreferences.swift:80](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppPreferences.swift#L80) | 即时切换、重启、菜单/多窗口/错误文案和系统回退 |
| P22 | 系统/浅/深主题、预览质量、重置窗口布局；录制默认音源与摄像头预览形状 | [AppSettingsView.swift:341](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppSettingsView.swift#L341) | 改变并重启；预览质量不改变导出；质量档位默认值待补核 |
| P23 | 权限页覆盖录屏/辅助功能/摄像头/麦克风与授权状态，首次引导返回后复查 | [AppSettingsView.swift:430](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppSettingsView.swift#L430) | 未决定/拒绝/受限/已授权，打开对应系统页再返回 |
| P24 | Sparkle更新仅正式bundle；手动/自动检查，开发版不替换正式版；自动初值从Sparkle读 | [AppUpdateController.swift:14](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/DogSCApp/AppUpdateController.swift#L14) | 正式/开发渠道隔离、检查/取消/失败、自动偏好记忆；平台更新机制另验 |

## 已确认的默认值与兼容边界

这些是模型初始化或产品选项的源码值，不保证已存偏好或旧工程加载后仍使用同一值。实现时应分别验收“清空偏好的新录制”和“重开已有工程”。

| 对象 | 已确认规则 | 固定源码 |
| --- | --- | --- |
| 导出设置 | 初始化为原始素材分辨率、60 FPS | [ExportSettings.swift:11](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ExportSettings.swift#L11) |
| 帧率产品选项 | 导出只提供 30/60 FPS；90/120 是旧工程解码兼容值，不应据此新增 UI 选项 | [CaptureConfiguration.swift:3](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/CaptureConfiguration.swift#L3) |
| 工程版本 | 当前 v27，支持 v23–v27；旧于 v23、未来版本拒绝解码 | [ProjectModel.swift:6](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L6) |
| 画布初始化 | 自适应比例、黑曜石网格背景、留白100、内容缩放1、居中、完整裁切、圆角24、描边0、阴影0.28、背景模糊0、无样机外框 | [ProjectModel.swift:847](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L847) |
| 画布比例 | 适应、16:9、4:3、9:16、1:1、3:4、2.39:1、1:2.39、自定义 | [ProjectModel.swift:64](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L64) |
| 静态/动态背景 | 模型包含5种网格/点阵图案、3种动态背景，以及工程相对路径或系统绝对路径的图片/视频 | [ProjectModel.swift:291](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L291) |
| 退役功能 | v23 注释明确去掉旧字幕、素材库视频和进度叠层模型；旧样机/光标部分标识仅用于兼容 | [ProjectModel.swift:3](https://github.com/laogou717/dogsc/blob/fa24059a9ba7e769152b3c1de047d96aca873e1c/Sources/RecorderCore/ProjectModel.swift#L3) |

首版对齐固定基线当前暴露的能力和必要的旧工程读取语义，不恢复已经退役的产品功能；导入背景视频也不能自动推断为任意主轨视频编辑入口。

## Mac 原版与 Windows 的差异登记

这些条目保留在 Windows 首版对齐目标内，目前没有批准任何功能删减。Linux 的实现与差异验证在 Windows 版完成后再进行，不阻塞 Windows 首版。

| 项目 | 已知来源与待解决问题 | 当前处理 |
| --- | --- | --- |
| iOS 设备来源 | 原版包含设备来源入口；Windows 上连接、信任、取流与声音能否对应尚未验证 | 首版目标保留；第 2 步单独验证后提出实现或差异方案，不偷偷删除 |
| 编码选项 | 原版录制包含 HEVC/H.264/ProRes 422；Apple 编码设施不能直接迁移 | 保留用户选项与素材语义目标；第 2 步验证可用编解码路径和分发约束 |
| 系统声音范围 | 原版区分全部系统声音与所选窗口应用声音 | 当前只验证 Windows 隔离能力；不能把全系统录音冒充应用录音 |
| 隐藏桌面项目、Dock及排除辅助窗口 | 各系统桌面、任务栏与采集排除机制不同 | 对应行为与限制需实测，不以重绘固定区域替代窗口采集 |
| 壁纸资源与系统路径 | 原版可引用 Mac 系统图片/视频，资源与绝对路径不能直接跨系统使用 | 保留背景能力；明确缺失提示、替代资源和工程跨机策略，禁止静默替换后覆盖原工程 |
| 权限、快捷键、更新与品牌 | 系统授权与 Apple 分发工具需平台适配；应用内操作仍须对齐 | 列出对应机制；作者专属链接/赞赏入口等改动记录为差异，不宣称逐字一致 |

## 固定验收流程

以下是后续测试脚本的最小输入约定，按后续实验逐项执行，也不改变原版功能范围。

| 场景 | 输入与操作 | 必须检查 |
| --- | --- | --- |
| A：软件演示 | 录制窗口；有移动、单击、拖动和静止；添加标记；停止后编辑、保存、重开并导出 | 鼠标与操作对应，标记只在工程出现；裁剪后光标与运镜仍对齐；保存前后参数一致 |
| B：讲解与摄像头 | 麦克风、系统声音和摄像头同时录制；剪切、重排、变速，添加摄像运动与同步点 | 各轨启停与时间关系正确，声音控制生效，摄像头同步不受错误时间映射影响 |
| C：画面效果 | 同一固定素材添加手动/自动缩放、3D、贴图、柔化、裁切、动态背景与样机；复制效果及仅粘贴属性 | 指定时间点的画面、插值和图层关系对照Mac；属性粘贴不移动位置/时长；撤销重做可还原 |
| D：导出与工程 | 重开v23–v27样例；修改后另存；分别导出视频和音频，覆盖分辨率/帧率与选段导出 | 输出实际尺寸/帧率、选段时长、素材路径、Mac往返、取消与已有文件保护 |
| E：异常与环境 | 权限拒绝/取消选源、外设断开、磁盘满或保存失败、缺失素材/未来版本工程；多显示器与混合DPI | 明确反馈、无永久等待、已有数据不破坏；错误结果不能计为功能通过 |
| F：设置与状态 | 清空偏好首次启动；切换语言/主题/预览画质；修改目录；重启与重开多工程 | 立即生效与持久化符合原版；预览质量独立于导出；录制中仍可编辑旧工程；小窗口控件可达 |

## 待补证据与首版完成条件

源码能力盘点可用于定义任务范围，但不能替代以下证据：

- 对应版本 Mac 应用逐页操作记录、默认新工程、固定测试工程、参考导出和指定时间点参考帧；现有四张截图不足以证明全部状态与动画节奏。
- 摄像头/麦克风/iOS设备的接入与断开，权限拒绝，录制中编辑旧工程，导出取消和文件写入失败的实际行为。
- 23–27 工程样例、未支持版本与缺失媒体样例，跨平台修改后 Mac 重开的往返结果。
- 第 2 步的 Windows 录屏、声音、鼠标同步、合成与导出验证程序、结果和明确的性能/视觉容差。

完成判定：每个能力编号都有 Windows 结果，范围内没有未完成能力；所有影响用户体验的平台差异均有依据和用户确认；界面、工程、预览与导出对照有证据。中间演示版本不得计为完整首版。

当前结论：首版产品目标和源码能力已形成可追踪清单；Mac 实测、平台差异结论与量化阈值尚未完成。当前按用户授权进入第 2 步，独立探针结果单独记录，尚未进入产品开发。
