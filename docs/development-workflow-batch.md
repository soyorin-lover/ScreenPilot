# 录制辅助与导出交付批次

日期：2026-10-13。平台：Windows，Qt 6.8.3。固定 Mac 参考版本与完整范围见[能力清单](parity-checklist.md)。

## 本批次目标

- 录制中打标记，按扣除暂停后的源时间保存；编辑器按剪切、重排、变速映射定位，删除可撤销，工程保存重开后保留。标记不绘入成片。
- 独立悬浮备忘录：文字、字号、透明度与位置恢复；尝试通过 Windows 辅助窗口排除接口避免录入，实际能力单独记录。
- 导出原始/1080p/1440p/4K 与30/60 FPS选项，保持画布比例；单独导出原始屏幕与已有摄像头素材，不应用剪辑与画面效果。
- 重新录制与放弃录制增加确认，避免误触删除本次素材。

## 验证状态

| 检查 | 实际结果 | 边界 |
| --- | --- | --- |
| Windows 构建 | 构建与部署通过 | 本批使用Qt 6.8.3；不代表所有Windows环境 |
| 标记与工程模型 | 剪切、变速、重排映射，删除撤销、保存重开、旧工程默认值及损坏标记保护通过 | 内部格式5；Mac工程互通不在开发范围 |
| 实际录制标记 | 两个标记跨暂停保存；暂停期间打标记被拒绝；视频3.000秒、音频3.008秒，暂停红色画面0帧，标记暂停时间差检查通过 | 自有窗口短测；长录制漂移待验收 |
| 备忘录 | 默认值、参数边界、隔离设置持久化与窗口几何检查通过；真实应用显示、收起通过 | Windows显示捕获排除请求获系统确认；未逐帧验证备忘录在所有采集路径中不可见 |
| 实际视频导出 | 0.5秒生成素材导出并解码为1920×1080，60 FPS，相邻帧16667微秒 | 代表性档位；1440p/4K及竖屏组合未全部实测 |
| 原始素材导出 | 屏幕与合成摄像头文件SHA256一致；取消清理和摄像头缺失拒绝检查通过 | 当前声音内嵌屏幕文件，没有独立麦克风媒体文件 |
| 编辑器回归 | 实际拖动重排、跨片段播放、变速与合成摄像头同步通过，2.5秒流程完成 | 真实摄像头硬件仍待验收 |
| 声音与变速导出回归 | 12组带声/静音/M4A、合成摄像头偏移与变速音高检查通过 | 短生成素材；极高速听感仍不保证 |

图形检查必须以正常可见测试窗口运行。隐藏启动时曾出现窗口来源不可用、拖动回归失败；改为可见窗口后通过。60FPS首次成片检查发现共享帧时间戳使重复帧折叠，修复为每个输出时刻独立视频帧后重新编码通过。原始媒体与详细日志仅保存在本地忽略目录，以上只发布脱敏摘要。

## 使用

录制中点击“添加标记”，暂停期间此按钮禁用。编辑器时间线金色标记可点击跳转、右键删除；删除支持撤销。被剪掉的源区间中标记暂时隐藏，保留源标记便于撤销恢复。

录制页打开“悬浮备忘录”，可拖动标题区域、缩放窗口、调整字号与背景透明度，收起后文字保留。窗口提示捕获排除请求是否获系统确认，不保证第三方录屏工具遵守。

编辑器第二行选择分辨率与30/60 FPS，设置随工程保存，默认原始分辨率/60FPS。预设尺寸按固定参考的长边/短边规则及画布比例计算，不改变画布比例。原始素材导出在目标目录下创建唯一子文件夹，复制屏幕与已有摄像头文件，保留内嵌声音；不应用剪辑、变速、鼠标或画布效果。现有文件不会被覆盖。

## 复现

完成[Windows构建部署](windows-development-build.md)后，在仓库根目录执行以下命令。每次使用新的空结果目录；生成素材可由既有`--export-options-smoke-test`命令产生，替换`GENERATED_SOURCE.mp4`为其生成的`generated-source.mp4`。图形检查正常显示测试窗口，必要时会播放测试音频。

```powershell
$app = '.\build\desktop\portable-workflow\ScreenPilot.exe'
& $app --memo-smoke-test '.\probe-results\memo-new'
& $app --markers-smoke-test 'GENERATED_SOURCE.mp4' --markers-result-directory '.\probe-results\markers-new'
& $app --workflow-ui-smoke-test '.\probe-results\ui-new'
& $app --recording-controls-smoke-test '.\probe-results\record-controls-new'
& $app --export-delivery-smoke-test 'GENERATED_SOURCE.mp4' --export-delivery-result-directory '.\probe-results\delivery-new'
& $app --editor-batch-smoke-test 'GENERATED_SOURCE.mp4' --editor-result-directory '.\probe-results\editor-new'
```

核对进程退出码0及各目录JSON结果。Windows GUI程序在自动脚本中请通过`Start-Process -PassThru -Wait`读取退出码；包含空格的参数需加引号。重复使用目录可能被拒绝，避免覆盖旧验证材料。
