# 动态背景、样机外框与开场动画

日期：2026-10-12。平台：Windows，Qt 6.8.3。固定参考及用户批准的范围调整见[能力清单](parity-checklist.md)。Mac工程兼容不开发，Linux暂不开展。

## 本批目标

- 导入MP4/MOV作为循环背景；背景音频不进入预览或成片，剪切/变速/定位后使用输出时间求值。
- 无外框、浅/深窗口、浅/深浏览器、自适应设备外框；可设置标题/网址，预览与导出共用几何。
- 可选开场动画，按输出时间计算；接入已实现屏幕与摄像头元素，提供方式、时长与节奏设置。
- 设置支持工程保存、内部旧版本默认值与场景预设；背景资源复制后独立于原文件和预设删除。

## 验证状态

| 检查 | 实际结果 | 边界 |
| --- | --- | --- |
| 构建 | Windows Qt 6.8.3构建部署通过 | 单机环境 |
| 外框/开场渲染 | 六种外框、裁切几何、无效参数、首帧背景、结束等于静态画面、摄像头延迟顺序及短时长终点通过 | 合成图像；未执行Mac视觉对照 |
| 工程/预设资产 | 图片/视频互斥、一次撤销/重做、格式7相对资源保存、预设删除后资源保留、旧工程6和旧预设默认值通过；缺失资源保护当前工程 | 模型与文件操作；媒体实际解码另测 |
| 实际视频背景 | 生成1.2秒红/绿/蓝MP4，定位、后退、精确循环边界通过；裁短/1.5倍速与重排组合导出2秒视频，外框和开场同时启用，背景留白颜色跨循环检查通过 | 低分辨率生成素材；高分辨率长视频吞吐未验收 |
| 缺失/损坏视频 | 解码明确失败；导出报错且不发布目标文件 | 本地缺失及损坏MP4样例 |
| 真实编辑器 | 视觉预设应用、工程保存重开、保存后定位通过；播放期间背景红/绿各采到6次，最终界面截图显示浏览器外框和效果控件 | 采样实际绘制帧以避免重复GPU截图干扰播放；不是长时性能测试 |
| 播放/声音导出回归 | 编辑器拖动、跨片段变速与合成摄像头同步通过；12组声音/静音/M4A/变速/摄像头偏移导出全部通过 | 真实摄像头硬件与长录制仍待验收 |

联调修复了两处真实运行问题：媒体状态/暂停操作可同步重入，导致背景解码递归崩溃；播放位置连续更新会打断未完成定位，导致背景不更新。现已先解除回调再暂停、避免重复加载处理，并将预览请求合并为一个在途解码与最新目标。修复后实际预览、循环导出和回归通过。首次摄像头短回归未采到目标颜色，重跑通过；不据此宣称全部硬件同步已验收。详细日志、设备信息与生成素材仅保存在忽略目录。

## 使用与限制

“画布”中选择背景图片或MP4/MOV动态背景，两者互斥；清除视频后使用背景颜色。背景按剪辑后的输出时间循环，独立静音解码，背景音频不进入主音轨。资源会随工程及预设复制，删除原文件或预设不会删除已应用副本。设置支持撤销；资源缓存暂未自动回收。

“效果”中可选无外框、浅/深窗口、浅/深浏览器、自适应设备外框，输入标题/网址。网址是显示文字，不发起网络请求。“还原样式”恢复圆角与阴影，保留标题/网址。

开场默认关闭，与固定参考的开关默认值一致；默认时长2.2秒、元素间隔0.16秒。支持淡入、缩放淡入、上移淡入、0.4–8秒总时长、节奏、屏幕/相机先后或同时入场；间隔会约束到保留至少0.18秒动作，元素均在总时长结束前稳定。可从片头预览。

当前开场是Windows二维实现，**尚未完整实现原版converge/sideSlide/light3D编排、贴图参与顺序以及3D组合**，这些继续保留为待补齐范围。外框与阴影视觉是近似实现；源比例自适应设备外框尚未逐设备与原版对照。动态背景采用有界逐帧定位缓存，优先内存与正确性；预览可能有解码延迟，高分辨率长导出仍待优化。当前没有Linux工作，Mac工程兼容不在范围。

## 复现

完成[Windows构建部署](windows-development-build.md)，在仓库根目录运行以下PowerShell步骤。各目录每次换新名称。基础导出检查生成3秒测试视频；动态背景检查自行生成1.2秒视频。检查与界面测试使用隔离存储，不修改个人预设。

```powershell
$projectRoot = (Get-Location).Path
$app = Join-Path $projectRoot 'build\desktop\portable-visuals\ScreenPilot.exe'
function Run-Check([string[]]$arguments) {
    $quoted = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = Start-Process -FilePath $app -ArgumentList $quoted -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "检查失败：$($process.ExitCode)" }
}
$base = Join-Path $projectRoot 'probe-results\visual-base-new'
Run-Check @('--export-options-smoke-test', $base)
$source = Join-Path $base 'generated-source.mp4'
Run-Check @('--visual-effects-smoke-test', (Join-Path $projectRoot 'probe-results\effects-new'))
Run-Check @('--visual-project-smoke-test', (Join-Path $projectRoot 'probe-results\assets-new'), '--visual-project-source', $source)
Run-Check @('--dynamic-background-smoke-test', (Join-Path $projectRoot 'probe-results\dynamic-new'))
Run-Check @('--visual-ui-smoke-test', $source, '--scenes-result-directory', (Join-Path $projectRoot 'probe-results\visual-ui-new'))
```

核对退出码0和各JSON报告。图形检查使用正常可见窗口；声音回归可能播放生成测试音。不要隐藏窗口运行鼠标拖动或窗口采集检查。公开记录只保留本页脱敏结果，原始媒体和逐帧事件留在忽略目录。
