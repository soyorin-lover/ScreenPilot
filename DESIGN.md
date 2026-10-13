# Design

## Source of truth
Active · 2026-10-13 (second pass). Windows recording, editor, inspectors, export and supporting dialogs. Reference commit fa24059a9ba7e769152b3c1de047d96aca873e1c: docs/images/recorder.png, editor.png, export.png and `Sources/DogSCApp/EditorDesignTokens.swift` (colour, type, radius and height tokens). Current QML and the functional probes are implementation evidence. This is a close structural and tonal alignment, not a claim of pixel-identical Mac rendering.

## Brand
Quiet, precise, approachable creative tool. Content first. Soft silver workspace, near-white floating islands, graphite ink. Avoid dark diagnostic dashboards, emoji or font glyph icons, crowded toolbars, giant decorative headings and invented statistics (no fake size estimates).

## Product goals
Match the reference's information hierarchy and finish while preserving every implemented recording, editing and export action. No new capture or effect algorithms in UI passes. Success: compact recorder island, dominant preview, discoverable grouped settings, calm ruler-based timeline, explicit export choices and safe return navigation.

## Personas and jobs
Chinese-speaking Windows desktop users making demos and tutorials, using mouse and keyboard at normal desk distance. They need quick capture, clear editing and confidence that work is saved.

## Information architecture
- Recorder: slim header (brand, 最近工程, 打开工程/继续编辑); central stage (idle source summary, live capture, review, camera bubble, state pill); bottom floating island modelled on the reference bar: source tiles 显示器/窗口 (区域/设备 shown disabled with tooltip, not faked) | input circles 麦克风/摄像头/系统声音 with status dots and mic level | 保存到 card | memo | primary 开始录制. During recording the island swaps to timer, 标记, pause/resume, more (重录/放弃), memo and 停止并保存. Device lists, formats, mirror and folder actions live in anchored popovers.
- Editor: header (返回录制, project menu, title + 未保存 pill, undo/redo group, 场景预设/保存/快捷键 group, primary 导出); left floating rail 片段/画面/运镜/效果/相机 mapped to `inspectorTab` 0/1/3/4/2; canvas toolbar pill (ratio menu, 裁切, 画面设置); preview on dotted workspace; right inspector card with sections; full-width timeline card.
- 画面 inspector uses sub-tabs 背景/布局/样机. 效果 holds 开场动画 and 光标强调. 运镜 holds 自动运镜 and 手动运镜.
- Timeline: tool row (split, delete, copy/paste/add 运镜 | frame-step and play with timecode | 吸附, help); lane labels (屏幕录制, 运镜); ruler that follows clip geometry and scrubs on drag; filmstrip clips with waveform band and duration/speed badges; knobbed playhead; amber recording markers.
- Third pass (see docs/claude-ui-wiring-notes.md): rail grows to 片段/画面/运镜/3D/聚焦/贴图/开场 | 相机/声音 (inspectorTab 0,1,3,6,7,8,4 | 2,5), scrollable at small heights. Effect lanes (3D, 模糊聚光, 贴图, 相机运动, 同步点) sit under the 运镜 lane, coloured per track and aligned to clip geometry. Recorder gains 区域 picker and a 录制规格 card; header gains 设置 and 恢复. Dark tokens follow the reference and bind to appSettings.effectiveTheme.
- Export dialog: segmented 导出内容 / 分辨率 / 帧率, right preview and spec list, footer 导出原始素材 · 取消 · 开始导出. File name and location come from the system save dialog.

## Design principles
One main action per region. Progressive disclosure through popovers, menus and sub-tabs instead of stacked controls. Separate chrome from media: UI tokens never touch canvas, background or export settings. Preserve object names, `inspectorTab` indices and editor functions used by checks. Unsupported capabilities stay visible only as disabled, explained entries.

## Visual language
`StudioTheme.qml` owns tokens (from the reference light theme): background #ecedef, surface #fdfdfe, surfaceAlt #f3f3f5, well rgba(0,0,0,.05), hairline rgba(0,0,0,.06), border rgba(0,0,0,.09), text #292929, muted #616161, faint #9a9a9e, accent (graphite) #1a1a1a with accentInk #fcfcfc, selection wash rgba(0,0,0,.075), success #4dd98c, recording #ff453b, amber #f2ad47, danger #d64541, timeline clip #e0e0e2→#d4d4d6, zoom segment #ddcaa8 (selected #e2a85a). Microsoft YaHei UI; Consolas for timecodes. Type: panel 16 semibold, section 14 semibold, body/control 13 medium, helper 12, caption 11. Radii: compact 8, control 10, group 12, card 16, floating 20 (island 28). Heights: compact 30, control 34, action 40–52. Floating cards use two static translucent drop layers, never live blur. Icons are vector paths on a 24 grid with one 1.6 px stroke (`StudioIcon`, QtQuick.Shapes curve renderer). Motion: 120–160 ms colour/position easing on controls only; dialogs appear without transition; no motion on media.

## Components
StudioButton (iconName, primary, quiet, danger, compact, tip, iconSize), StudioComboBox, StudioSlider, StudioSliderRow (label + value pill + slider, `valueMoved`), StudioCheckBox (rendered as trailing switch), StudioTextField, StudioSegmented (model, icons, `activated(index)`), StudioSection (title, icon, helper), StudioHelp, StudioDivider, StudioCard, StudioDialog (icon, title, subtitle, close) + StudioDialogFooter, StudioPopover, StudioMenu/StudioMenuItem (icon, shortcut hint, danger), StudioDotGrid, StudioIcon. All dialogs (unsaved, recording confirm, export, crop, scene presets, recent projects, shortcut help) use StudioDialog.

## Accessibility
Readable contrast on silver surfaces; visible keyboard focus rings on buttons, tiles, rail, segments and switches; tooltips and Accessible names for icon-only actions. Buttons take focus only by Tab so Space/arrow editing shortcuts keep working after clicks. Shortcuts and native text editing preserved. Controls 30–34 px; primary record/stop 52 px.

## Responsive behavior
Target 1280×860 editor, minimum 860×640. Below ~1040 px the recorder island hides the save-folder label; below ~1000 px the editor header and timeline drop text labels to icons with tooltips and lane labels collapse to icons. Inspector 272–304 px and scrolls; preview takes remaining space; timeline strip shrinks under 760 px height. Export dialog hides its preview column under 680 px.

## Interaction states
Recorder: idle (selected source), no sources, recording (pulsing red pill and timer), paused (amber), saving, review, errors in the status line. Editor: empty state with 打开工程/新录制, 未保存 pill, export progress card with percent and cancel, single-line status with full text on hover. Never show success for failed media operations.

## Content voice
Concise Chinese action labels. Consistent terms: 画面、运镜、效果、相机、工程、导出. Microcopy explains user decisions, not internal architecture. Keep destructive confirmations and unsaved guards.

## Implementation constraints
C++20 / Qt 6.8.3 Qt Quick Basic style with shared styled controls; only Qt's own QtQuick.Shapes added for icons. Windows only. Mac project compatibility excluded. Build, capture real window snapshots at normal/minimum sizes, inspect them, then run focused shortcut, timeline drag, recent/unsaved, scenes, visual and editor-batch checks. Snapshots of popups and animations need an unlocked desktop; a locked session stalls frame-driven animation. Public docs contain sanitized conclusions; raw screenshots/media/logs stay in ignored directories.

## Open questions
- [x] Dark theme uses shared reference tokens; editor and settings screenshots inspected (2026-10-13).
- [ ] High-contrast preference; owner product/UI.
- [ ] Recorder as a separate always-on-top floating bar window during capture (needs capture-exclusion handling like the memo window); owner product/platform.
- [x] Align 运镜 lane with TimelineStrip clip geometry and scrolling; mouse checks passed (2026-10-13).
- [ ] Timeline zoom; owner editing.
- [ ] Canvas shadow/frame chrome around the preview canvas and full per-state Mac comparison; owner product/UI/rendering.
