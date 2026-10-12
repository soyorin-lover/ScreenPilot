# W03A：系统声音采集与窗口画面时间关联

日期：2026-10-13 · 状态：本轮闭环完成；音频采集通过，音画同步未通过 · Windows 独立实验

本轮只建立 W03 的第一个小型闭环：Qt 6 受控窗口、WASAPI 默认播放端点系统混音采集、真实测试音检测、原生 WGC 帧时间关联。固定 Mac 原版提交仍为 `fa24059a9ba7e769152b3c1de047d96aca873e1c`，完整范围见[能力清单](../parity-checklist.md)。没有进入产品架构阶段。

三段已知频率的低音量测试音分别对应三种窗口颜色。在实际采集的 PCM 中识别测试音，再用音频包原始 QPC 与采样偏移求出现时间；画面时间取自实际 WGC 纹理变化。程序输出原始包时间、状态标志、PCM/WAV和对照JSON，不能用“发出了播放命令”代替声音已录到。

[WASAPI loopback](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)采集的是播放端点系统混音，不等于指定应用声音。原始音频可能包含其他正在播放的声音，仅保存在本机忽略目录；公开仓库仅保留汇总结论，不上传原始JSON或系统音频。

[GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)提供包首音频帧的设备位置与已转换为100ns的QPC。采样帧数量与QPC单位不同；SILENT、DATA_DISCONTINUITY、TIMESTAMP_ERROR分别记录，不用无效时间戳证明同步。

本轮不采集麦克风、不验证按进程声音隔离、iOS声音、设备切换与断开、长时间漂移或实际媒体暂停。W01指定30FPS未通过和W02真实媒体暂停尚未验证继续保留。这些不是删除原版功能，也不代表W03整体完成。

## 复现

Windows 交互桌面、MSVC x64、Windows SDK、CMake/Ninja 和 Qt 6.8.3 MSVC kit。此机工具路径如下，其他机器替换路径。运行时会显示程序自身窗口并播放三段低音量测试音，每次使用新的输出目录。保留其他声音会影响系统混音检测；程序不会暂停其他应用。

```powershell
& '.\probes\windows-audio\run.ps1' `
  -QtRoot 'C:\Qt\6.8.3\msvc2022_64' `
  -VisualStudioRoot 'C:\Path\To\VisualStudio' `
  -OutputDirectory '.\probe-results\my-w03a-positive'
```

另一次独立调用同一脚本，追加 `-NoTone` 并换新目录作为对照。脚本以探针退出码结束调用；0表示全部同步门槛通过，1表示未通过，2表示参数/已有输出/证据写入错误。`--no-tone`设计为无法通过正向三音关联，退出1不能单独证明负向正确，必须检查实际WAV。

```powershell
python '.\probes\windows-audio\analyze_wav.py' '.\probe-results\my-w03a-positive\system-mix.wav' --expect tones
python '.\probes\windows-audio\inspect_evidence.py' '.\probe-results\my-w03a-positive'
```

对无音源目录使用 `--expect no-tones`。独立检查器校验WAV样本数与格式、包字节/采样偏移、设备位置连续性、时间递增，以及存在时的检测采样偏移与关联帧。检查器退出0表示证据内部一致，不等于同步通过；输出的 `probe_acceptance` 独立记录验收结果。

## 实际结果

最终同一源码/二进制运行：`probe-results/w03a-archived-positive` 与 `w03a-archived-negative`，两次探针退出1。Qt 6.8.3、MSVC 19.51、SDK 10.0.26100.0、Windows 11；环境细节沿用W01/W02报告。

| 项目 | 带测试音 | 无测试音 |
| --- | --- | --- |
| 实际WAV | 48kHz、双声道float32、240000帧、5.00秒 | 同格式、240480帧、5.01秒 |
| 音频包 / 自有窗口帧 | 500 / 720 | 501 / 721 |
| 独立10ms频谱窗命中（800/1200/1600Hz） | 15 / 15 / 15 | 0 / 0 / 0 |
| 首命中采样位置 | 48000 / 96000 / 144000，即文件1/2/3秒 | 无 |
| 音频读取时刻减包QPC，中位数 | −6.9184ms | −10.1071ms |
| 视频读取时刻减帧QPC，中位数 | −2.6544ms | −2.0022ms |
| API标记时间戳错误 / 后续断续 | 0 / 0 | 0 / 0 |
| 首包断续标记 | 1，单独记录 | 1，单独记录 |
| 设备位置缺口 | 0 | 0 |
| 同步门槛 | **未通过** | 对照，不能通过正向门槛 |

真实音频采集、三段测试音在PCM中的存在及无音源对照，本机短测通过。独立分析器和证据检查器均通过。WGC读取到了三次颜色变化；这只是自有窗口纹理实验，不是视频导出。

**同步未通过的直接原因**：所有音频包时间戳、部分视频帧时间戳比紧接着的本地QPC读取时刻更晚，超过本实验“未来不超过1ms”的门槛。音频范围分别为−10.9462至−5.3632ms、−10.8561至−1.4141ms。程序拒绝使用这些时间证明关联，因此onset为0、关联valid为false；JSON中的 `video_minus_audio_ms=0` 是缺失值占位，**不是0ms同步误差**。

1ms是本实验诊断门槛，不是已证实的API保证；本次不能判定Windows API错误或硬件故障，也不能直接放宽门槛宣布通过。QPC值原样保留，未人为平移或校准。下一轮需同时采样原始QPC/IAudioClock相关时间，检查单位、读取位置和时间源偏差，再评估可信同步方式。

早期带音源与无音源运行曾出现大量其他系统音，频谱检测无法稳定归因。WAV格式复核未见布局错误，源音开始前已有明显音频能量；外部混音或端点处理是候选解释，未定位具体进程。最终运行背景较安静并不证明进程隔离。[公开验证摘要](public-results.md)保留失败证据。

## 证据与限制

本轮公开[验证摘要](public-results.md)。详细JSON、设备标识、绝对路径及原始音频仅保存在本地忽略目录。

实验只支持float32/PCM16设备混音格式，其他格式明确失败；独立WAV检查器另支持PCM32。C++检测为5ms窗，独立分析为10ms窗，均非样本级onset测量；若音源与背景同频，系统混音不能证明来源。当前没有音画误差合格值或长时漂移结论。

尚未验证：麦克风、指定应用/子进程隔离、多设备、拔插/切换、静音设备、暂停继续、长录制、音视频封装导出与异常恢复。固定Mac原版能力目标不变，95项清单不因本实验标记完成。
