# 运镜轨交互、快捷键与最近工程

日期：2026-10-12。平台：Windows Qt 6.8.3。维护ScreenPilot自有工程，Mac工程兼容不开发，Linux暂不开展。

## 目标

- 手动运镜轨移动与端点调整，拖动草稿一次提交，取消不修改工程；结合剪切、变速和重排映射源时间。
- 运镜拆分、复制/粘贴副本、属性粘贴保留位置时长，以及可关闭的吸附。
- 常用编辑快捷键和速查入口；输入框、模态窗口和导出期间保护原有操作。
- 最近工程持久化与打开入口，默认显示8项、最多20项；忘记记录不删除工程文件，失效路径明确显示。

## 验证状态

| 检查 | 实际结果 | 边界 |
| --- | --- | --- |
| 构建 | Qt 6.8.3 Windows构建部署通过 | 单机环境 |
| 运镜模型/鼠标 | 拆分、内部复制/粘贴、属性粘贴、一次撤销、草稿取消、实际鼠标端点/移动及裁短后2倍速端点映射通过 | 简短生成素材及模型时间；未覆盖全部DPI/缩放组合 |
| 运镜交接 | 依据固定参考左段退出为0、右段进入至少0.15秒，连接前一镜头状态；活动进入阶段与稳定阶段的切点连续性检查通过 | 未运行Swift数值对照；不保证拆分后整条曲线逐帧与未拆分完全相同 |
| 实际快捷键 | 1/5帧定位、主轨/运镜拆分、撤销、复制粘贴、Esc草稿取消通过；输入q/w/s不触发编辑、文本原生撤销、帮助弹窗及模态保护通过 | 30/60步进按工程导出FPS；未对每个文件对话框做自动点击 |
| 最近工程注册 | 20项上限/默认8项、大小写去重、最近排序、重启、失效路径保留、拒绝非本项目工程通过；清空和移除不删除文件 | 注册表测试使用隔离目录 |
| 实际最近工程入口 | 面板显示、未保存打开保护、取消保留修改、打开所选工程及移除后文件保留通过 | 生成的两个本项目工程；不代表中断录制恢复已实现 |
| 播放回归 | 主时间线实际拖动、跨片段变速播放、合成摄像头同步通过，2.5秒流程完成 | 真实摄像头与长时间同步仍待验收 |

联调修正了裁短后可见端点与隐藏源边界不一致、按下即改变选择导致拖动委托重建丢失鼠标抓取两项问题。拖动期间只显示本地草稿，释放时一次提交；取消与历史操作清理草稿。所有界面探针使用隔离的最近工程注册表，不污染个人历史。详细事件、媒体、历史路径与设备信息仅留在忽略目录或本地应用数据。

## 使用

底部运镜轨可拖动整个片段或两端；开启“吸附”时接近播放头、剪辑边界或其他运镜边界会吸附，按住Shift暂时绕过。释放才修改工程，Esc取消。源时间运镜在多段剪辑中可能显示多个关联片段，编辑会同步影响同一源运镜；跨剪辑不连续区间的自由拖动和完整其他效果轨仍待补齐。

选择运镜后可拆分、复制、粘贴副本或粘贴属性。属性粘贴保留目标的起止位置和时长；副本粘贴到播放头对应的源时间，并受可用素材范围约束。复制使用应用内剪贴板，可跨本次应用会话中的工程使用，不替换系统文本剪贴板。当前效果复制只覆盖手动运镜，其余原版效果类型继续待补齐。

点击“快捷键”或F1查看速查：空格播放/暂停，左右1帧、Shift左右5帧；S拆分，Q/W修剪前后，Delete删除；Ctrl+Z撤销，Ctrl+Shift+Z或Ctrl+Y重做；Ctrl+C/V复制/粘贴运镜，Ctrl+Shift+V粘贴属性；Ctrl+S保存、Ctrl+Shift+S另存、Ctrl+O打开、Ctrl+E导出。选择主轨后命令作用于主轨，选择运镜后作用于运镜。输入框/下拉框中不接管编辑快捷键，按钮和滑条保留空格/方向键自身行为；模态窗口、隐藏编辑器、录制与导出期间禁止编辑快捷键。

录制页“最近工程”显示最近8项，可展开最多20项。打开/保存工程会登记本机路径，失效项目标为不可用；移除或清空仅删除历史记录。打开其他工程时提示保存、放弃或取消；选择先保存成功后会继续原先的打开请求。最近列表与中断恢复是两项能力，本批不声明恢复未完成录制。

工程仍使用内部格式7并读取本项目1–6；Mac工程兼容不在范围。

## 最小复现

按[Windows构建](windows-development-build.md)部署后，在根目录用正常可见窗口运行。用既有`--export-options-smoke-test`生成3秒`generated-source.mp4`作为下面`$source`。结果目录每次换新名称；下例均使用绝对路径，测试不会修改个人最近记录。

```powershell
$projectRoot = (Get-Location).Path
$app = Join-Path $projectRoot 'build\desktop\portable-editing\ScreenPilot.exe'
function Run-Check([string[]]$arguments) {
    $quoted = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = Start-Process -FilePath $app -ArgumentList $quoted -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "检查失败：$($process.ExitCode)" }
}
$base = Join-Path $projectRoot 'probe-results\editing-base-new'
Run-Check @('--export-options-smoke-test', $base)
$source = Join-Path $base 'generated-source.mp4'
Run-Check @('--recent-projects-smoke-test', (Join-Path $projectRoot 'probe-results\recent-new'))
Run-Check @('--timeline-interaction-smoke-test', $source, '--editing-result-directory', (Join-Path $projectRoot 'probe-results\track-new'))
Run-Check @('--editor-shortcut-smoke-test', $source, '--editing-result-directory', (Join-Path $projectRoot 'probe-results\keys-new'))
Run-Check @('--recent-ui-smoke-test', $source, '--editing-result-directory', (Join-Path $projectRoot 'probe-results\recent-ui-new'))
```

核对各JSON布尔结果和退出码0。图形检查不能隐藏窗口启动；不要把测试事件日志、真实最近工程路径或媒体推送到仓库。
