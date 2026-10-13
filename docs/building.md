# 构建、运行与测试

## 构建

```powershell
.\scripts\build-windows.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Deploy
```

| 参数 | 说明 |
| --- | --- |
| `-QtRoot` | Qt 6.8 MSVC 64-bit kit 的路径（必填） |
| `-VisualStudioRoot` | Visual Studio 安装路径；不传时用 vswhere 自动查找 |
| `-Deploy` | 构建后用 windeployqt 部署到 `build\desktop\portable\` |
| `-DeploymentDirectory` | 部署到其他目录 |
| `-Launch` | 构建完成后直接启动 |

脚本使用 Ninja、Release 配置，构建目录为 `build\desktop\`。

## 运行

双击仓库根目录的 `启动 ScreenPilot.cmd`，或运行 `build\desktop\portable\ScreenPilot.exe`。移动程序时要连同整个部署目录一起移动。

默认录制保存在 Windows "视频"文件夹下的 ScreenPilot 目录；工程是 `.spproj` 文件夹，内含 `project.json` 和复制进来的素材，打开工程时选择这个文件夹。早期版本保存的 `.dogscproject` 工程仍可以打开。

## 代码结构

| 目标 | 内容 |
| --- | --- |
| `screenpilot_core` | `app/` 下的全部代码和 ScreenPilotApp QML 模块（静态库）；`AppRuntime` 负责创建控制器、连接信号和加载界面 |
| `ScreenPilot.exe` | 程序入口，只有 `app/main.cpp` |
| `ScreenPilotProbe.exe` | `tests/` 下的冒烟测试、性能探针和基准，和程序共用同一套 `AppRuntime`；不随程序部署 |

几个关键模块：

- `RecordingController`：屏幕/窗口采集、帧调度、编码、鼠标采样
- `WindowsAudioCapture`：WASAPI 系统声音和麦克风采集
- `ProjectController`：工程模型、剪辑操作、撤销/重做、保存与打开
- `CanvasRenderer` / `CanvasPreview`：画面合成与编辑器预览
- `ExportController`：导出（多线程合成，按顺序送入编码器）
- `FrameConversion`：YUV 与 RGB 之间的快速转换（多线程、AVX2）

## 测试

测试程序在 `build\desktop\ScreenPilotProbe.exe`。直接从构建目录运行时，需要把 Qt 的 `bin` 目录加入 `PATH`，并设置 `QT_PLUGIN_PATH`、`QML_IMPORT_PATH` 指向 Qt 的 `plugins` 和 `qml` 目录。

每个检查用一个命令行参数启动，结果写成 JSON，退出码 0 表示通过。输出目录必须不存在或为空。测试只录制程序自己生成的窗口和素材，不会采集桌面或麦克风（`SCREENPILOT_TEST_BAR_SCREEN=1` 时例外，见下表）。

常用检查：

| 参数 | 检查内容 |
| --- | --- |
| `--project-flow-smoke-test` | 工程模型、历史记录、保存与重新打开 |
| `--editing-smoke-test <目录>` | 录制 → 剪辑 → 保存 → 重新打开 → 导出 → 解码校验 |
| `--recording-controls-smoke-test <目录>` | 暂停/继续、重新录制、丢弃 |
| `--recording-bar-smoke-test <目录>` | 录制浮条的完整流程；设 `SCREENPILOT_TEST_BAR_SCREEN=1` 时改为录制整个屏幕，以检查浮条没有被录进视频 |
| `--export-options-smoke-test <目录>` | 各种导出组合；同时生成 `generated-source.mp4`，供其他检查当素材 |
| `--ui-snapshot-smoke-test <素材> --editing-result-directory <目录>` | 录制页、编辑器、导出面板在正常和最小窗口下的截图 |
| `--editor-batch-smoke-test <素材> --editor-result-directory <目录>` | 编辑器里的拖动、变速播放、摄像头同步 |
| `--frame-conversion-smoke-test <素材>` | 快速 YUV/RGB 转换与 Qt 结果的逐像素对比 |

所有参数见 `ScreenPilotProbe.exe --help`。

## 性能基准

| 参数 | 用途 |
| --- | --- |
| `--media-performance-test <素材> --performance-result-directory <目录>` | 编辑器播放与导出的实测（帧率、界面卡顿、导出耗时） |
| `--render-benchmark <素材>` | 单帧画布合成耗时 |
| `--encoder-benchmark <目录>` | Qt H.264 编码器对 BGRA/NV12/YUV420P 输入的吞吐 |

设置 `SCREENPILOT_RECORDING_METRICS=1` 时，录制会额外记录帧调度和编码背压等指标。
