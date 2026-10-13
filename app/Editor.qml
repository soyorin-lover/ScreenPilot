import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import QtMultimedia
import ScreenPilot.Render 1.0

Rectangle {
    id: editor
    objectName: "editorView"
    color: StudioTheme.background
    signal backRequested()
    signal projectSaveFinished(bool saved)
    signal settingsRequested()
    signal recoveryRequested()
    property real positionSeconds: 0
    property int playingClip: 0
    property bool primingPreview: false
    property bool needsPreviewFrame: false
    property url shownSource: ""
    property string pendingAction: ""
    property url pendingRecentUrl: ""
    property bool snappingEnabled: true
    readonly property bool shortcutsAvailable: visible && enabled && project.hasProject && !exporter.busy && !recorder.busy
        && !unsaved.visible && !saveDialog.visible && !openDialog.visible && !exportDialog.visible
        && !exportOptions.visible && !originalExportDialog.visible && !backgroundColorDialog.visible && !backgroundImageDialog.visible
        && !cropEditor.visible && !scenePresetPanel.visible && !shortcutHelp.visible
        && (!editor.Overlay.overlay || editor.Overlay.overlay.children.every(function(child) {
            return !child.visible
        }))
    EditorShortcuts { editor: parent }
    ShortcutHelp { id: shortcutHelp }
    function showShortcutHelp() { video.pause(); cameraVideo.pause(); shortcutHelp.open() }
    function stepFrames(frames) { seek(positionSeconds + frames / (Number(project.exportSettings.fps) === 60 ? 60 : 30)) }
    function undoEdit() { cancelTimelineInteraction(); video.pause(); project.undo() }
    function redoEdit() { cancelTimelineInteraction(); video.pause(); project.redo() }
    function splitSelection() { video.pause(); if (project.selectedManualFraming !== "") project.splitManualFraming(positionSeconds); else project.split(positionSeconds) }
    function trimSelection(start) {
        video.pause()
        const id = project.selectedManualFraming
        if (id === "") { if (start) project.trimStart(positionSeconds); else project.trimEnd(positionSeconds); return }
        const time = project.sourceTime(positionSeconds)
        for (let segment of project.manualFramingSegments) {
            if (segment.id === id && time > segment.start && time < segment.end) {
                project.updateManualFraming(id, start ? {start: time} : {end: time})
                return
            }
        }
    }
    function deleteSelection() {
        video.pause()
        if (project.selectedManualFraming !== "") project.removeManualFraming(project.selectedManualFraming)
        else if (project.selectedClips.length > 1) project.removeSelectedClips()
        else project.removeSelected()
    }
    function copySelection() { project.copyManualFraming() }
    function pasteSelection() { video.pause(); project.pasteManualFraming(positionSeconds) }
    function pasteSelectionProperties() { video.pause(); project.pasteManualFramingProperties() }
    function cancelTimelineInteraction() { playbackEpoch++; manualTrack.cancelInteraction(); video.pause(); cameraVideo.pause() }
    function saveProjectAs() { video.pause(); cameraVideo.pause(); saveDialog.open() }
    property bool renaming: false
    function renameProject(name) {
        if (renaming || exporter.busy || recorder.busy) return false
        renaming = true
        video.stop(); cameraVideo.stop()
        video.source = ""; cameraVideo.source = ""
        timelineMedia.releaseMedia()
        previewAudioProcessor.setMixSources("", "", {})
        // Give native media teardown its event turn before touching the folder.
        Qt.callLater(function() {
            project.renameProject(name)
            renaming = false
            timelineMedia.setSource(project.source, project.sourceDuration)
            previewAudioProcessor.setMixSources(project.systemAudioSource, project.microphoneAudioSource, project.audioSettings)
            video.source = project.source
            cameraVideo.source = Qt.binding(function() { return project.cameraSource })
            refresh()
        })
        return true
    }
    function requestExport() { video.pause(); cameraVideo.pause(); exportOptions.open() }
    function requestOpenRecent(location) {
        video.pause(); cameraVideo.pause()
        if (project.dirty) { pendingRecentUrl = location; pendingAction = "recent"; unsaved.open() }
        else loadProject(location)
    }
    function continuePendingOpen() {
        const action = pendingAction, location = pendingRecentUrl
        pendingAction = ""; pendingRecentUrl = ""
        if (action === "recent") loadProject(location)
        else if (action === "open") openDialog.open()
    }
    // Inspector tab contract used by checks: 0 clip, 1 canvas, 2 camera, 3 framing, 4 effects.
    property int inspectorTab: 0
    property bool exporting: exporter.busy
    property string audioMode: "videoAudio"
    // Export range in output seconds; disabled means the whole project.
    property bool exportRangeEnabled: false
    property real exportRangeStart: 0
    property real exportRangeEnd: 0
    readonly property real effectiveRangeStart: exportRangeEnabled ? exportRangeStart : 0
    readonly property real effectiveRangeEnd: exportRangeEnabled ? exportRangeEnd : -1
    // describeProjectExport is a query, so list everything it depends on.
    readonly property var exportSummary: {
        const deps = [project.clips, project.exportSettings, project.audioSettings, project.cameraSource, audioMode, exportRangeEnabled, exportRangeStart, exportRangeEnd, exporter.busy]
        return exportOptions.visible ? exporter.describeProjectExport({audioMode: audioMode}, effectiveRangeStart, effectiveRangeEnd) : exporter.projectSummary
    }
    // Main-track command availability for the playhead; commands still validate.
    readonly property var editState: { const deps = [project.clips, project.selectedClips, project.canUndo, project.canRedo]; return project.editingState(positionSeconds) }
    property var projectModel: project
    property bool cameraPriming: false
    property bool awaitingSourceFrame: false
    property bool monitorMuted: false
    property int playbackEpoch: 0
    Binding { target: previewAudioProcessor; property: "muted"; value: editor.primingPreview || !editor.visible || editor.monitorMuted }
    property real soughtSourceTime: 0
    property var framing: {
        // Depend on project notifications even when the playhead is paused.
        var clips = project.clips
        var enabled = project.autoZoomEnabled
        return project.framingAt(positionSeconds)
    }

    function clock(seconds) {
        var total = Math.max(0, seconds)
        return Math.floor(total / 60).toString().padStart(2, "0") + ":" + Math.floor(total % 60).toString().padStart(2, "0") + "." + Math.floor((total % 1) * 10)
    }
    function seek(seconds) {
        playbackEpoch++
        if (!primingPreview) video.pause()
        positionSeconds = Math.max(0, Math.min(project.duration, seconds))
        var clips = project.clips
        for (var i = 0; i < clips.length; ++i) {
            if (positionSeconds < clips[i].outputStart + clips[i].duration || i === clips.length - 1) {
                playingClip = i
                break
            }
        }
        video.playbackRate = clips.length > 0 ? clips[playingClip].playbackRate : 1
        soughtSourceTime = project.sourceTime(Math.min(positionSeconds, Math.max(0, project.duration - 0.001)))
        awaitingSourceFrame = true
        previewAudioProcessor.reset(soughtSourceTime, clips.length > 0 ? clips[playingClip].sourceStart + clips[playingClip].sourceDuration : -1)
        video.position = Math.round(soughtSourceTime * 1000)
        syncCamera(false)
    }
    function playPause() {
        playbackEpoch++
        if (!visible || exporter.busy || recorder.busy || !project.hasProject) return
        var wasPriming = primingPreview
        // A user action takes over from the one-time poster-frame request.
        needsPreviewFrame = false
        primingPreview = false
        previewAudio.muted = false
        if (!wasPriming && video.playbackState === MediaPlayer.PlayingState) { video.pause(); return }
        seek(positionSeconds >= project.duration - 0.02 ? 0 : positionSeconds)
        video.play()
    }
    function syncCamera(playing) {
        if (project.cameraSource.toString() === "") return
        var target = project.cameraTimeAt(positionSeconds)
        preview.cameraVisible = target >= 0 && (cameraVideo.duration <= 0 || target * 1000 <= cameraVideo.duration)
        cameraVideo.playbackRate = video.playbackRate * project.cameraRateAt(positionSeconds)
        if (!playing || Math.abs(cameraVideo.position - target * 1000) > 100)
            cameraVideo.position = Math.max(0, Math.round(target * 1000))
        if (playing && preview.cameraVisible) cameraVideo.play()
        else cameraVideo.pause()
    }
    function advanceClip() {
        if (playingClip + 1 >= project.clips.length) {
            video.pause(); cameraVideo.pause(); positionSeconds = project.duration
            var last = project.clips[playingClip]
            soughtSourceTime = Math.max(last.sourceStart, last.sourceStart + last.sourceDuration - .04)
            awaitingSourceFrame = true
            previewAudioProcessor.reset(soughtSourceTime, last.sourceStart + last.sourceDuration)
            video.position = Math.round(soughtSourceTime * 1000)
            syncCamera(false)
            return
        }
        playingClip++
        var c = project.clips[playingClip]
        positionSeconds = c.outputStart
        video.playbackRate = c.playbackRate
        soughtSourceTime = c.sourceStart
        awaitingSourceFrame = true
        previewAudioProcessor.reset(soughtSourceTime, c.sourceStart + c.sourceDuration)
        video.position = Math.round(c.sourceStart * 1000)
        video.play()
        syncCamera(true)
        // EndOfMedia may publish StoppedState after this handler. Resume once
        // that transition has completed, unless a user action superseded it.
        const epoch = playbackEpoch, clip = playingClip
        Qt.callLater(function() {
            if (editor.playbackEpoch === epoch && editor.playingClip === clip && editor.visible
                    && !exporter.busy && !recorder.busy) video.play()
        })
    }
    function saveProject() {
        video.pause()
        if (project.projectLocation.toString() !== "") finishProjectSave(project.save())
        else saveDialog.open()
    }
    function finishProjectSave(saved) {
        if (saved) continuePendingOpen()
        else { pendingAction = ""; pendingRecentUrl = "" }
        projectSaveFinished(saved)
    }
    function requestOpen() {
        if (project.dirty) { pendingAction = "open"; unsaved.open() }
        else openDialog.open()
    }
    function requestNewRecording() {
        video.pause()
        backRequested()
    }
    function openCrop() {
        video.pause()
        cameraVideo.pause()
        cropEditor.open()
    }
    CropEditor { id: cropEditor; objectName: "cropEditor"; sourcePreview: preview; project: editor.projectModel }
    ScenePresetPanel { id: scenePresetPanel }
    function loadProject(directory) {
        video.stop()
        video.source = ""
        project.openProject(directory)
        video.source = project.source
    }
    function refresh() {
        if (renaming) return
        if (shownSource.toString() !== project.source.toString()) {
            shownSource = project.source
            video.source = project.source
            positionSeconds = 0
        }
        seek(Math.min(positionSeconds, project.duration))
    }
    Connections { target: project; function onChanged() { editor.refresh() } }
    onVisibleChanged: if (!visible) { video.pause(); cameraVideo.pause() }
    Component.onCompleted: refresh()
    MediaPlayer {
        id: video
        objectName: "editorPlayer"
        source: project.source
        videoOutput: preview.videoSink
        audioOutput: AudioOutput { id: previewAudio; volume: 0.8 }
        onSourceChanged: {
            editor.needsPreviewFrame = source.toString() !== ""
            editor.primingPreview = false
            previewAudio.muted = false
            preview.clearFrame()
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia && !editor.primingPreview && !editor.awaitingSourceFrame && editor.playingClip < project.clips.length) {
                var clip = project.clips[editor.playingClip]
                if (position / 1000 >= clip.sourceStart + clip.sourceDuration - .05 && clip.sourceStart + clip.sourceDuration >= duration / 1000 - .05) editor.advanceClip()
                return
            }
            if (mediaStatus === MediaPlayer.LoadedMedia && editor.needsPreviewFrame) {
                editor.needsPreviewFrame = false
                editor.primingPreview = true
                previewAudio.muted = true
                // Finish the source/status transition before requesting its first frame.
                Qt.callLater(function() {
                    if (!editor.primingPreview || !editor.visible) return
                    video.position = Math.round(project.sourceTime(editor.positionSeconds) * 1000)
                    video.play()
                })
            }
        }
        onPlaybackStateChanged: { if (playbackState !== MediaPlayer.PlayingState) cameraVideo.pause(); else if (!editor.primingPreview) editor.syncCamera(true) }
        onPositionChanged: function(position) {
            if (editor.primingPreview || editor.awaitingSourceFrame || playbackState !== MediaPlayer.PlayingState || editor.playingClip >= project.clips.length) return
            var c = project.clips[editor.playingClip]
            var seconds = position / 1000
            if (seconds < c.sourceStart - 0.03) return
            if (seconds >= c.sourceStart + c.sourceDuration - 0.01) {
                editor.advanceClip()
            } else editor.positionSeconds = c.outputStart + Math.max(0, seconds - c.sourceStart) / c.playbackRate
            editor.syncCamera(true)
        }
    }
    MediaPlayer {
        id: cameraVideo
        objectName: "editorCameraPlayer"
        source: project.cameraSource
        videoOutput: preview.cameraSink
        onSourceChanged: { preview.clearCameraFrame(); editor.cameraPriming = source.toString() !== "" }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && editor.cameraPriming) { editor.syncCamera(false); play() }
        }
    }
    FileDialog {
        id: saveDialog
        title: "保存工程"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ScreenPilot 工程 (*.spproj)"]
        defaultSuffix: "spproj"
        currentFolder: appSettings.projectDirectoryUrl
        onVisibleChanged: if (visible) selectedFile = appSettings.suggestedProjectUrl()
        onAccepted: {
            video.stop()
            video.source = ""
            const saved = project.saveAs(selectedFile)
            video.source = project.source
            editor.finishProjectSave(saved)
        }
        onRejected: editor.finishProjectSave(false)
    }
    FolderDialog { id: openDialog; title: "选择 .spproj 工程文件夹"; onAccepted: editor.loadProject(selectedFolder) }
    ColorDialog {
        id: backgroundColorDialog
        title: "选择画布背景颜色"
        onAccepted: project.setCanvasOption("backgroundColor", selectedColor.toString())
    }
    FileDialog {
        id: backgroundImageDialog
        title: "选择静态背景图片"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: project.setBackgroundImage(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: editor.audioMode === "audioOnly" ? "导出剪辑后的音频" : "导出剪辑后的视频"
        fileMode: FileDialog.SaveFile
        nameFilters: editor.audioMode === "audioOnly" ? ["M4A 音频 (*.m4a)"] : ["MP4 视频 (*.mp4)"]
        defaultSuffix: editor.audioMode === "audioOnly" ? "m4a" : "mp4"
        currentFolder: appSettings.exportDirectoryUrl
        onVisibleChanged: if (visible) selectedFile = appSettings.suggestedExportUrl("", editor.audioMode === "audioOnly" ? "m4a" : "mp4")
        onAccepted: { video.pause(); exporter.startProject(selectedFile, {audioMode: editor.audioMode}, editor.effectiveRangeStart, editor.effectiveRangeEnd) }
    }
    FolderDialog {
        id: originalExportDialog
        title: "导出原始素材到新子文件夹（屏幕、摄像头及内嵌音频，无剪辑效果）"
        currentFolder: appSettings.exportDirectoryUrl
        onAccepted: { video.pause(); exporter.exportProjectOriginal(selectedFolder) }
    }
    StudioDialog {
        id: unsaved
        objectName: "unsavedEditorDialog"
        onRejected: { editor.pendingAction = ""; editor.pendingRecentUrl = "" }
        title: "当前工程有未保存修改"
        iconName: "save"
        width: 440
        StudioHelp { width: parent ? parent.width : 380; font.pixelSize: StudioTheme.body; text: "保存后可继续编辑；放弃修改才会替换当前工程。" }
        footer: StudioDialogFooter {
            StudioButton { text: "放弃并打开"; danger: true; quiet: true; onClicked: { unsaved.close(); project.discardChanges(); editor.continuePendingOpen() } }
            Item { Layout.fillWidth: true }
            StudioButton { text: "取消"; onClicked: { editor.pendingAction = ""; editor.pendingRecentUrl = ""; unsaved.close() } }
            StudioButton { text: "先保存"; primary: true; onClicked: { unsaved.close(); editor.saveProject() } }
        }
    }

    // ---- Export ---------------------------------------------------------------------
    component SpecRow: RowLayout {
        id: spec
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        Text { text: spec.label; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; Layout.fillWidth: true }
        Text { text: spec.value; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.Medium }
    }
    component FieldLabel: Text { color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold; topPadding: 4 }
    StudioDialog {
        id: exportOptions
        objectName: "exportOptionsDialog"
        width: Math.min(780, editor.width - 48)
        height: Math.min(600, editor.height - 48)
        title: "导出作品"
        subtitle: project.title
        iconName: "export"
        readonly property bool audioOnly: editor.audioMode === "audioOnly"
        readonly property var resolutionKeys: ["source", "1080p", "1440p", "4k"]
        contentItem: RowLayout {
            spacing: 28
            ScrollView {
                id: exportSettingsScroll
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: exportSettingsScroll.availableWidth; spacing: 12
                    FieldLabel { text: "导出内容" }
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["视频成片", "静音视频", "仅音频"]
                        icons: ["film", "monitor", "waveform"]
                        currentIndex: ["videoAudio", "muted", "audioOnly"].indexOf(editor.audioMode)
                        onActivated: index => editor.audioMode = ["videoAudio", "muted", "audioOnly"][index]
                    }
                    FieldLabel { text: "分辨率"; Layout.topMargin: 8 }
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["原始素材", "1080p", "1440p", "4K"]
                        currentIndex: Math.max(0, exportOptions.resolutionKeys.indexOf(project.exportSettings.resolution))
                        enabled: !exportOptions.audioOnly
                        onActivated: index => project.setExportOption("resolution", exportOptions.resolutionKeys[index])
                    }
                    FieldLabel { text: "帧率"; Layout.topMargin: 8 }
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["30 FPS", "60 FPS"]
                        currentIndex: project.exportSettings.fps === 30 ? 0 : 1
                        enabled: !exportOptions.audioOnly
                        onActivated: index => project.setExportOption("fps", index === 0 ? 30 : 60)
                    }
                    FieldLabel { text: "导出范围"; Layout.topMargin: 8 }
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["整个工程", "自定义范围"]
                        icons: ["film", "trimStart"]
                        currentIndex: editor.exportRangeEnabled ? 1 : 0
                        onActivated: index => {
                            editor.exportRangeEnabled = index === 1
                            if (index === 1 && editor.exportRangeEnd <= editor.exportRangeStart) { editor.exportRangeStart = 0; editor.exportRangeEnd = project.duration }
                        }
                    }
                    GridLayout {
                        visible: editor.exportRangeEnabled
                        Layout.fillWidth: true
                        columns: 2; columnSpacing: 8; rowSpacing: 8
                        StudioNumberField { label: "起点"; labelWidth: 36; suffix: "秒"; value: editor.exportRangeStart; to: project.duration; onCommitted: v => editor.exportRangeStart = Math.min(v, editor.exportRangeEnd - .05) }
                        StudioButton { text: "设为播放头"; compact: true; onClicked: editor.exportRangeStart = Math.min(editor.positionSeconds, editor.exportRangeEnd - .05) }
                        StudioNumberField { label: "终点"; labelWidth: 36; suffix: "秒"; value: editor.exportRangeEnd; to: project.duration; onCommitted: v => editor.exportRangeEnd = Math.max(v, editor.exportRangeStart + .05) }
                        StudioButton { text: "设为播放头"; compact: true; onClicked: editor.exportRangeEnd = Math.max(editor.positionSeconds, editor.exportRangeStart + .05) }
                    }
                    // Range bar: the exported part of the project in context.
                    Item {
                        visible: editor.exportRangeEnabled
                        Layout.fillWidth: true; implicitHeight: 18
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 6; radius: 3; color: StudioTheme.well }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.width * editor.exportRangeStart / Math.max(.001, project.duration)
                            width: parent.width * Math.max(0, editor.exportRangeEnd - editor.exportRangeStart) / Math.max(.001, project.duration)
                            height: 6; radius: 3; color: StudioTheme.accent
                        }
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; x: parent.width * editor.positionSeconds / Math.max(.001, project.duration) - 1; width: 2; height: 14; color: StudioTheme.amber }
                    }
                    StudioHelp { Layout.topMargin: 10; text: exportOptions.audioOnly ? "仅导出剪辑后的声音，保存为 M4A。" : "分辨率按画面短边计算，画面比例沿用当前画布。" }
                    StudioHelp { text: "点击“开始导出”后选择文件名与保存位置。" }
                }
            }
            Rectangle { visible: exportOptions.width >= 680; Layout.fillHeight: true; implicitWidth: 1; color: StudioTheme.hairline }
            ColumnLayout {
                visible: exportOptions.width >= 680
                Layout.preferredWidth: 272; Layout.fillHeight: true; spacing: 12
                FieldLabel { text: "当前画面" }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 168
                    color: StudioTheme.surfaceAlt; radius: StudioTheme.radiusCard; border.color: StudioTheme.hairline; clip: true
                    StudioDotGrid { anchors.fill: parent; anchors.margins: 1 }
                    ShaderEffectSource {
                        readonly property real aspect: preview.width / Math.max(1, preview.height)
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 20, (parent.height - 20) * aspect); height: width / aspect
                        sourceItem: preview; live: exportOptions.visible; hideSource: false
                    }
                }
                Item { implicitHeight: 4 }
                SpecRow { label: "格式"; value: exportOptions.audioOnly ? "M4A" : "MP4" }
                SpecRow { label: "分辨率"; value: exportOptions.audioOnly ? "—" : project.exportSettings.resolution === "source" ? "原始素材" : "短边 " + ({"1080p": 1080, "1440p": 1440, "4k": 2160})[project.exportSettings.resolution] + " px" }
                SpecRow { label: "帧率"; value: exportOptions.audioOnly ? "—" : project.exportSettings.fps + " fps" }
                SpecRow { label: "时长"; value: editor.clock(editor.exportSummary.duration !== undefined ? editor.exportSummary.duration : project.duration) }
                SpecRow { label: "片段"; value: (editor.exportSummary.clipCount !== undefined ? editor.exportSummary.clipCount : project.clips.length) + " 段" }
                SpecRow { label: "声音"; value: editor.audioMode === "muted" ? "静音" : editor.exportSummary.hasSeparateAudio ? "独立音轨混音" : "内嵌声音" }
                SpecRow { label: "摄像头"; value: editor.exportSummary.hasCamera ? "包含" : "无" }
                Text { visible: editor.exportSummary.valid === false || (editor.exportSummary.error || "") !== ""; text: editor.exportSummary.error || "当前设置无法导出"; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
            }
        }
        footer: StudioDialogFooter {
            StudioButton { text: "导出原始素材"; iconName: "folder"; quiet: true; tip: "屏幕、摄像头及内嵌音频原样复制，无剪辑效果"; onClicked: { exportOptions.close(); originalExportDialog.open() } }
            Item { Layout.fillWidth: true }
            StudioButton { text: "取消"; quiet: true; onClicked: exportOptions.close() }
            StudioButton { text: "开始导出"; primary: true; iconName: "export"; implicitHeight: 40; enabled: project.hasProject && !exporter.busy && editor.exportSummary.canStart !== false; onClicked: { exportOptions.close(); exportDialog.open() } }
        }
    }

    // ---- Shared editor pieces -------------------------------------------------------
    component ToolButton: StudioButton {}
    component Hairline: Rectangle { implicitWidth: 1; implicitHeight: 20; color: StudioTheme.hairline }
    component KeyValue: RowLayout {
        id: kv
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        Text { text: kv.label; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; Layout.fillWidth: true }
        Text { text: kv.value; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.body }
    }
    // Rail entry: icon over caption, mapped to an inspector tab index.
    component RailButton: AbstractButton {
        id: railButton
        property string iconName: ""
        property int tab: 0
        readonly property bool selected: editor.inspectorTab === tab
        implicitWidth: 56; implicitHeight: editor.height < 760 ? 50 : 56
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: text
        onClicked: editor.inspectorTab = tab
        background: Rectangle { radius: 14; color: railButton.selected ? StudioTheme.selection : railButton.hovered ? StudioTheme.well : "transparent"; border.width: railButton.visualFocus ? 2 : 0; border.color: StudioTheme.accent }
        contentItem: Column {
            spacing: 4
            topPadding: editor.height < 720 ? 6 : 9
            StudioIcon { name: railButton.iconName; width: 21; height: 21; anchors.horizontalCenter: parent.horizontalCenter; color: railButton.selected ? StudioTheme.text : StudioTheme.muted }
            Text { text: railButton.text; anchors.horizontalCenter: parent.horizontalCenter; color: railButton.selected ? StudioTheme.text : StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; font.weight: railButton.selected ? Font.DemiBold : Font.Medium }
        }
    }
    component LaneLabel: Item {
        id: lane
        property string text: ""
        property string iconName: ""
        Row {
            anchors.verticalCenter: parent.verticalCenter; x: 4; spacing: 7
            StudioIcon { name: lane.iconName; width: 15; height: 15; color: StudioTheme.muted; anchors.verticalCenter: parent.verticalCenter }
            Text { text: lane.text; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    readonly property var inspectorTitles: ["片段", "画面", "相机", "运镜", "开场与光标", "声音", "屏幕 3D", "模糊与聚光灯", "贴图"]
    // Effect lanes shown under the main strip; each maps to its inspector tab.
    readonly property var laneDefs: [
        { track: "screenMotionSegments", title: "屏幕 3D", icon: "cube", tint: StudioTheme.laneScreen, tab: 6 },
        { track: "blurRegions", title: "模糊聚光", icon: "zoom", tint: StudioTheme.laneFocus, tab: 7 },
        { track: "stickers", title: "贴图", icon: "image", tint: StudioTheme.laneSticker, tab: 8 },
        { track: "cameraMotionSegments", title: "相机运动", icon: "camera", tint: StudioTheme.laneCamera, tab: 2 },
        { track: "syncPoints", title: "同步点", icon: "flag", tint: StudioTheme.laneSync, tab: 2 }
    ]
    readonly property var visibleLanes: {
        const deps = [effects.screenMotionSegments, effects.blurRegions, effects.stickers, effects.cameraMotionSegments, effects.syncPoints]
        return laneDefs.filter(d => effects[d.track].length > 0)
    }
    readonly property real laneHeight: editor.height < 760 ? 28 : 32
    function laneLabel(track, item) {
        if (track === "screenMotionSegments") return "3D 运动"
        if (track === "blurRegions") return item.style === "spotlight" ? "聚光灯" : "模糊"
        if (track === "stickers") return "贴图"
        if (track === "cameraMotionSegments") return item.fullscreen ? "全屏" : "移动"
        return ""
    }
    readonly property bool editable: project.hasProject && !exporter.busy
    property int canvasSection: 1

    StudioMenu {
        id: projectMenu
        StudioMenuItem { text: "打开工程…"; iconName: "folder"; shortcutText: "Ctrl+O"; enabled: !exporter.busy; onTriggered: editor.requestOpen() }
        StudioMenuItem { text: "保存工程"; iconName: "save"; shortcutText: "Ctrl+S"; enabled: editor.editable; onTriggered: editor.saveProject() }
        StudioMenuItem { text: "另存为…"; iconName: "copy"; shortcutText: "Ctrl+Shift+S"; enabled: editor.editable; onTriggered: editor.saveProjectAs() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: StudioTheme.hairline } }
        StudioMenuItem { text: "场景预设…"; iconName: "presets"; enabled: editor.editable; onTriggered: { video.pause(); scenePresetPanel.open() } }
        StudioMenuItem { text: "重命名工程…"; iconName: "edit"; enabled: editor.editable && project.projectLocation.toString() !== ""; onTriggered: renamePopover.open() }
        StudioMenuItem { text: "导出原始素材…"; iconName: "download"; enabled: editor.editable; onTriggered: originalExportDialog.open() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: StudioTheme.hairline } }
        StudioMenuItem { text: "恢复工程…"; iconName: "history"; onTriggered: editor.recoveryRequested() }
        StudioMenuItem { text: "设置…"; iconName: "settings"; onTriggered: editor.settingsRequested() }
    }
    // Rename the saved project folder; the backend relocates media references.
    StudioPopover {
        id: renamePopover
        parent: Overlay.overlay
        width: 320
        x: Math.min(editor.width - width - 16, titleButton.mapToItem(editor, 0, 0).x)
        y: titleButton.mapToItem(editor, 0, 0).y + titleButton.height + 8
        onOpened: { renameField.text = project.title; renameField.selectAll(); renameField.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "重命名工程"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold }
            StudioTextField { id: renameField; Layout.fillWidth: true; placeholderText: "工程名称"; onAccepted: renameButton.clicked() }
            StudioHelp { text: project.projectLocation.toString() === "" ? "请先保存工程，再修改名称。" : "会同时重命名工程文件夹，名称不能包含 \\ / : * ? \" < > |。" }
            Text { visible: project.error !== "" && renamePopover.visible; text: project.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Item { Layout.fillWidth: true }
                StudioButton { text: "取消"; compact: true; onClicked: renamePopover.close() }
                StudioButton { id: renameButton; text: "重命名"; primary: true; compact: true; enabled: renameField.text.trim().length > 0 && project.projectLocation.toString() !== ""; onClicked: if (editor.renameProject(renameField.text.trim())) renamePopover.close() }
            }
        }
    }
    // Right-click menu for main-track clips; availability follows editingState.
    StudioMenu {
        id: clipMenu
        StudioMenuItem { text: "在播放头分割"; iconName: "split"; shortcutText: "S"; enabled: editor.editable && editor.editState.canSplit === true; onTriggered: project.split(editor.positionSeconds) }
        StudioMenuItem { text: "与下一段合并"; iconName: "link"; enabled: editor.editable && editor.editState.canMergeNext === true; onTriggered: project.mergeAdjacent(project.selectedClip) }
        StudioMenuItem { text: "恢复片头"; iconName: "chevronLeft"; enabled: editor.editable && editor.editState.canRestoreHead === true; onTriggered: project.restoreClipHead(project.selectedClip) }
        StudioMenuItem { text: "恢复片尾"; iconName: "chevronRight"; enabled: editor.editable && editor.editState.canRestoreTail === true; onTriggered: project.restoreClipTail(project.selectedClip) }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: StudioTheme.hairline } }
        StudioMenuItem { text: "速度…"; iconName: "speed"; onTriggered: editor.inspectorTab = 0 }
        StudioMenuItem {
            text: project.selectedClips.length > 1 ? "删除所选 " + project.selectedClips.length + " 段" : "删除片段"
            iconName: "trash"; danger: true; shortcutText: "Delete"
            enabled: editor.editable && (project.selectedClips.length > 1 ? editor.editState.canRemoveSelected === true : editor.editState.canRemove === true)
            onTriggered: project.selectedClips.length > 1 ? project.removeSelectedClips() : project.removeSelected()
        }
    }
    FileDialog {
        id: laneStickerDialog
        title: "选择贴图图片"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: { const id = effects.addSticker(selectedFile, project.sourceTime(editor.positionSeconds), 3); if (id !== "") { effects.select("stickers", id); editor.inspectorTab = 8 } }
    }
    // Add an effect at the playhead from the timeline.
    StudioMenu {
        id: trackMenu
        function addAt(track, tab, duration) { video.pause(); const id = effects.addAtOutput(track, editor.positionSeconds, duration); if (id !== "") { effects.select(track, id); editor.inspectorTab = tab } }
        StudioMenuItem { text: "手动运镜"; iconName: "zoomIn"; enabled: editor.editable; onTriggered: { project.addManualFraming(editor.positionSeconds); editor.inspectorTab = 3 } }
        StudioMenuItem { text: "屏幕 3D 运动"; iconName: "cube"; enabled: editor.editable; onTriggered: trackMenu.addAt("screenMotionSegments", 6, 2) }
        StudioMenuItem { text: "模糊区域"; iconName: "zoom"; enabled: editor.editable; onTriggered: trackMenu.addAt("blurRegions", 7, 2) }
        StudioMenuItem { text: "聚光灯"; iconName: "sun"; enabled: editor.editable; onTriggered: { trackMenu.addAt("blurRegions", 7, 2); effects.updateSelected("blurRegions", { style: "spotlight", strength: .6 }) } }
        StudioMenuItem { text: "贴图…"; iconName: "image"; enabled: editor.editable; onTriggered: laneStickerDialog.open() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: StudioTheme.hairline } }
        StudioMenuItem { text: "摄像头运动"; iconName: "camera"; enabled: editor.editable && project.cameraSource.toString() !== ""; onTriggered: trackMenu.addAt("cameraMotionSegments", 2, 2) }
        StudioMenuItem { text: "同步点"; iconName: "flag"; enabled: editor.editable && project.cameraSource.toString() !== ""; onTriggered: { trackMenu.addAt("syncPoints", 2, 0); effects.updateSelected("syncPoints", { cameraTime: project.cameraTimeAt(editor.positionSeconds) }) } }
    }
    // Monitor volume: hearing only, never the project mix or export.
    StudioPopover {
        id: monitorPopover
        parent: Overlay.overlay
        width: 260
        x: Math.min(editor.width - width - 16, monitorButton.mapToItem(editor, 0, 0).x - width + monitorButton.width)
        y: monitorButton.mapToItem(editor, 0, 0).y - height - 8
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: "监听音量"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold }
            RowLayout {
                spacing: 8
                StudioSlider { Layout.fillWidth: true; from: 0; to: 1; stepSize: .01; value: previewAudioProcessor.volume; enabled: !editor.monitorMuted; onMoved: previewAudioProcessor.volume = value }
                Text { text: Math.round(previewAudioProcessor.volume * 100) + "%"; color: StudioTheme.muted; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
            }
            StudioCheckBox { text: "静音监听"; checked: editor.monitorMuted; onToggled: editor.monitorMuted = checked }
            StudioHelp { text: "只影响剪辑时听到的声音；音轨混音在“声音”面板中调整。" }
        }
    }
    StudioMenu {
        id: ratioMenu
        readonly property var ratios: ["source", "16:9", "4:3", "9:16", "1:1", "3:4", "2.39:1", "1:2.39"]
        readonly property var labels: ["适应素材", "16:9 横屏", "4:3", "9:16 竖屏", "1:1 方形", "3:4", "2.39:1", "1:2.39"]
        Instantiator {
            model: ratioMenu.ratios.length
            delegate: StudioMenuItem {
                required property int index
                text: ratioMenu.labels[index]
                iconName: project.canvasSettings.ratio === ratioMenu.ratios[index] ? "check" : "aspect"
                enabled: editor.editable
                onTriggered: project.setCanvasOption("ratio", ratioMenu.ratios[index])
            }
            onObjectAdded: (index, object) => ratioMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => ratioMenu.removeItem(object)
        }
    }

    StudioDotGrid { anchors.fill: parent }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; anchors.topMargin: 12; spacing: 12

        // ---- Header -----------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToolButton {
                text: editor.width >= 980 ? "返回录制" : ""
                iconName: "back"; quiet: true; tip: "返回录制（保留当前工程）"
                enabled: !exporter.busy
                onClicked: editor.requestNewRecording()
            }
            Hairline { Layout.leftMargin: 4; Layout.rightMargin: 4 }
            ToolButton {
                id: projectButton
                iconName: "folder"; quiet: true; tip: "工程"
                implicitWidth: contentItem.implicitWidth + 16
                leftPadding: 8; rightPadding: 8
                contentItem: Row {
                    spacing: 2
                    StudioIcon { name: "folder"; width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter }
                    StudioIcon { name: "chevronDown"; width: 12; height: 12; color: StudioTheme.muted; anchors.verticalCenter: parent.verticalCenter }
                }
                onClicked: projectMenu.popup(projectButton, 0, projectButton.height + 4)
            }
            AbstractButton {
                id: titleButton
                Layout.maximumWidth: Math.max(120, editor.width * .32); Layout.leftMargin: 2
                implicitWidth: titleRow.implicitWidth + 16; implicitHeight: 32
                hoverEnabled: true
                enabled: project.hasProject
                onClicked: renamePopover.toggle()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: project.projectLocation.toString() !== "" ? "点击重命名工程" : "保存后可重命名"
                background: Rectangle { radius: StudioTheme.radiusCompact; color: titleButton.hovered ? StudioTheme.well : "transparent" }
                contentItem: Row {
                    id: titleRow
                    spacing: 6; leftPadding: 8
                    Text {
                        text: project.hasProject ? project.title : "未打开工程"
                        color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: 15; font.weight: Font.DemiBold
                        elide: Text.ElideMiddle; width: Math.min(implicitWidth, titleButton.Layout.maximumWidth - 40)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    StudioIcon { visible: titleButton.hovered; name: "edit"; width: 13; height: 13; color: StudioTheme.muted; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Rectangle {
                visible: project.dirty
                implicitWidth: dirtyLabel.implicitWidth + 16; implicitHeight: 22; radius: 11; color: "#1ff2ad47"
                Text { id: dirtyLabel; anchors.centerIn: parent; text: "未保存"; color: StudioTheme.warning; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; font.weight: Font.Medium }
            }
            Item { Layout.fillWidth: true }
            StudioCard {
                implicitWidth: undoRow.implicitWidth + 8; implicitHeight: 40; radius: 12
                Row {
                    id: undoRow
                    anchors.centerIn: parent; spacing: 2
                    ToolButton { iconName: "undo"; quiet: true; compact: true; tip: "撤销 (Ctrl+Z)"; enabled: project.canUndo && !exporter.busy; onClicked: editor.undoEdit() }
                    ToolButton { iconName: "redo"; quiet: true; compact: true; tip: "重做 (Ctrl+Y)"; enabled: project.canRedo && !exporter.busy; onClicked: editor.redoEdit() }
                }
            }
            StudioCard {
                implicitWidth: actionRow.implicitWidth + 8; implicitHeight: 40; radius: 12
                Row {
                    id: actionRow
                    anchors.centerIn: parent; spacing: 2
                    ToolButton { text: editor.width >= 1100 ? "场景预设" : ""; iconName: "presets"; quiet: true; compact: true; tip: "场景预设"; enabled: editor.editable; onClicked: { video.pause(); scenePresetPanel.open() } }
                    ToolButton { iconName: "save"; quiet: true; compact: true; tip: "保存工程 (Ctrl+S)"; enabled: editor.editable; onClicked: editor.saveProject() }
                    ToolButton { iconName: "keyboard"; quiet: true; compact: true; tip: "快捷键 (F1)"; onClicked: editor.showShortcutHelp() }
                    ToolButton { iconName: "settings"; quiet: true; compact: true; tip: "设置"; onClicked: editor.settingsRequested() }
                }
            }
            ToolButton { text: "导出"; iconName: "export"; primary: true; implicitHeight: 40; enabled: editor.editable; tip: "导出作品 (Ctrl+E)"; onClicked: editor.requestExport() }
        }

        // ---- Workspace --------------------------------------------------------------
        RowLayout {
            // Only takes the space left after header and timeline; children never inflate it.
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: 0; spacing: 16
            StudioCard {
                Layout.preferredWidth: 68
                Layout.preferredHeight: railColumn.implicitHeight + 12
                Layout.maximumHeight: parent.height
                Layout.minimumHeight: 0
                Layout.alignment: Qt.AlignVCenter
                radius: StudioTheme.radiusFloating
                Flickable {
                    id: railFlick
                    anchors.fill: parent; anchors.margins: 6
                    contentHeight: railColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollIndicator.vertical: ScrollIndicator {}
                    Column {
                        id: railColumn
                        width: railFlick.width
                        spacing: 2
                        RailButton { text: "片段"; iconName: "film"; tab: 0 }
                        RailButton { text: "画面"; iconName: "canvas"; tab: 1 }
                        RailButton { text: "运镜"; iconName: "framing"; tab: 3 }
                        RailButton { text: "3D"; iconName: "cube"; tab: 6 }
                        RailButton { text: "聚焦"; iconName: "zoom"; tab: 7 }
                        RailButton { text: "贴图"; iconName: "image"; tab: 8 }
                        RailButton { text: "开场"; iconName: "intro"; tab: 4 }
                        Rectangle { width: 32; height: 1; color: StudioTheme.hairline; anchors.horizontalCenter: parent.horizontalCenter }
                        RailButton { text: "相机"; iconName: "camera"; tab: 2 }
                        RailButton { text: "声音"; iconName: "waveform"; tab: 5 }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
                StudioCard {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: canvasTools.implicitWidth + 10; implicitHeight: 40; radius: 12
                    Row {
                        id: canvasTools
                        anchors.centerIn: parent; spacing: 2
                        ToolButton {
                            id: ratioButton
                            text: project.canvasSettings.ratio === "source" ? "适应素材" : project.canvasSettings.ratio
                            quiet: true; compact: true; tip: "画面比例"
                            enabled: editor.editable
                            contentItem: Row {
                                spacing: 6
                                StudioIcon { name: "aspect"; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: project.canvasSettings.ratio === "source" ? "适应素材" : project.canvasSettings.ratio; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                                StudioIcon { name: "chevronDown"; width: 12; height: 12; color: StudioTheme.muted; anchors.verticalCenter: parent.verticalCenter }
                            }
                            implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
                            onClicked: ratioMenu.popup(ratioButton, 0, ratioButton.height + 6)
                        }
                        Hairline { anchors.verticalCenter: parent.verticalCenter; implicitHeight: 18 }
                        ToolButton { text: "裁切"; iconName: "crop"; quiet: true; compact: true; tip: "裁切画面"; enabled: editor.editable && preview.hasFrame; onClicked: editor.openCrop() }
                        ToolButton { iconName: "sliders"; quiet: true; compact: true; tip: "画面设置"; checked: editor.inspectorTab === 1; onClicked: editor.inspectorTab = 1 }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    CanvasPreview {
                        id: preview
                        objectName: "canvasPreview"
                        onFrameReady: {
                            if (editor.awaitingSourceFrame && preview.frameTime >= editor.soughtSourceTime - .08 && preview.frameTime <= editor.soughtSourceTime + .5)
                                editor.awaitingSourceFrame = false
                            if (editor.primingPreview) {
                                editor.primingPreview = false
                                video.pause()
                                previewAudio.muted = false
                            }
                        }
                        anchors.fill: parent
                        pointerSamples: project.pointerSamples
                        canvasSettings: project.canvasSettings
                        camera: editor.framing
                        outputTime: editor.positionSeconds
                        cameraSettings: project.cameraSettings
                        onCameraFrameReady: { if (editor.cameraPriming) { editor.cameraPriming = false; if (video.playbackState !== MediaPlayer.PlayingState) cameraVideo.pause() } }
                    }
                    Column {
                        anchors.centerIn: parent; spacing: 10
                        visible: !project.hasProject
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 64; height: 64; radius: 20; color: StudioTheme.well
                            StudioIcon { anchors.centerIn: parent; width: 30; height: 30; name: "film" }
                        }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "录制或打开工程后开始剪辑"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                            ToolButton { text: "打开工程"; iconName: "folder"; onClicked: editor.requestOpen() }
                            ToolButton { text: "新录制"; iconName: "record"; primary: true; onClicked: editor.requestNewRecording() }
                        }
                    }
                }
            }

            // ---- Inspector ----------------------------------------------------------
            StudioCard {
                Layout.preferredWidth: editor.width < 1000 ? 272 : 304; Layout.fillHeight: true
                radius: StudioTheme.radiusFloating
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    Text {
                        Layout.leftMargin: 20; Layout.topMargin: 18; Layout.bottomMargin: 14
                        text: editor.inspectorTitles[editor.inspectorTab]
                        color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.panelTitle; font.weight: Font.DemiBold
                    }
                    StudioDivider {}
                    StudioSegmented {
                        visible: editor.inspectorTab === 1
                        Layout.fillWidth: true; Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 12
                        compact: true
                        model: ["背景", "布局", "样机"]
                        icons: ["layers", "layout", "mockup"]
                        currentIndex: editor.canvasSection
                        onActivated: index => editor.canvasSection = index
                    }
                    ScrollView {
                        id: inspectorScroll
                        Layout.fillWidth: true; Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            x: 20
                            width: inspectorScroll.availableWidth - 40
                            spacing: 22
                            Item { implicitHeight: 2 }

                            // 0 · Clip
                            InspectorClip {
                                Layout.fillWidth: true
                                visible: editor.inspectorTab === 0
                                positionSeconds: editor.positionSeconds
                                editable: editor.editable
                            }

                            // 1 · Canvas
                            ColumnLayout {
                                visible: editor.inspectorTab === 1
                                Layout.fillWidth: true
                                spacing: 22
                                StudioSection {
                                    visible: editor.canvasSection === 0
                                    title: "背景颜色"; icon: "background"
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        Rectangle { width: 34; height: 34; radius: 10; color: project.canvasSettings.backgroundColor || "#20242c"; border.color: StudioTheme.border }
                                        ToolButton { text: "选择颜色"; Layout.fillWidth: true; enabled: editor.editable; onClicked: { backgroundColorDialog.selectedColor = project.canvasSettings.backgroundColor; backgroundColorDialog.open() } }
                                    }
                                }
                                StudioSection {
                                    visible: editor.canvasSection === 0
                                    title: "图片与动态背景"; icon: "image"
                                    BackgroundControls { Layout.fillWidth: true; enabled: editor.editable }
                                }
                                StudioSection {
                                    visible: editor.canvasSection === 1
                                    title: "画面比例"; icon: "aspect"
                                    StudioComboBox {
                                        Layout.fillWidth: true
                                        currentIndex: Math.max(0, ratioMenu.ratios.indexOf(project.canvasSettings.ratio))
                                        model: ratioMenu.labels
                                        enabled: editor.editable
                                        onActivated: project.setCanvasOption("ratio", ratioMenu.ratios[currentIndex])
                                    }
                                    ToolButton { text: "裁切画面"; iconName: "crop"; Layout.fillWidth: true; enabled: editor.editable && preview.hasFrame; onClicked: editor.openCrop() }
                                }
                                StudioSection {
                                    visible: editor.canvasSection === 1
                                    title: "画面留白"; icon: "layout"
                                    StudioSliderRow { label: "边距"; valueText: Math.round(project.canvasSettings.padding) + " px"; from: 0; to: 300; stepSize: 5; value: project.canvasSettings.padding; enabled: editor.editable; onValueMoved: v => project.setCanvasOption("padding", v) }
                                }
                                StudioSection {
                                    visible: editor.canvasSection === 1
                                    title: "圆角与阴影"; icon: "copy"
                                    StudioSliderRow { label: "内容圆角"; valueText: Math.round(project.canvasSettings.radius) + " px"; from: 0; to: 100; stepSize: 1; value: project.canvasSettings.radius; enabled: editor.editable; onValueMoved: v => project.setCanvasOption("radius", v) }
                                    StudioSliderRow { label: "阴影"; valueText: Math.round(project.canvasSettings.shadow * 100) + "%"; from: 0; to: 1; stepSize: .01; value: project.canvasSettings.shadow; enabled: editor.editable; onValueMoved: v => project.setCanvasOption("shadow", v) }
                                }
                                VisualSceneControls {
                                    Layout.fillWidth: true
                                    visible: editor.canvasSection === 2
                                    section: "frame"
                                    enabled: editor.editable
                                }
                                StudioHelp { text: "设置随工程保存，预览和导出使用相同效果。" }
                            }

                            // 2 · Camera
                            ColumnLayout {
                                visible: editor.inspectorTab === 2
                                Layout.fillWidth: true; spacing: 22
                                ColumnLayout {
                                    visible: project.cameraSource.toString() === ""
                                    Layout.fillWidth: true; spacing: 10
                                    Rectangle { width: 52; height: 52; radius: 16; color: StudioTheme.well; Layout.topMargin: 8
                                        StudioIcon { anchors.centerIn: parent; name: "camera"; width: 24; height: 24 } }
                                    Text { text: "此工程没有摄像头素材"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold }
                                    StudioHelp { text: "在录制页打开摄像头后，新录制会带有可调整的摄像头画面。" }
                                }
                                ColumnLayout {
                                    visible: project.cameraSource.toString() !== ""
                                    Layout.fillWidth: true; spacing: 22
                                    enabled: !exporter.busy
                                    StudioSection {
                                        title: "显示"; icon: "camera"
                                        StudioCheckBox { text: "显示摄像头"; checked: project.cameraSettings.enabled; onClicked: project.setCameraOption("enabled", checked) }
                                        StudioSegmented {
                                            Layout.fillWidth: true
                                            model: ["画中画", "全屏"]
                                            currentIndex: project.cameraSettings.fullscreen ? 1 : 0
                                            onActivated: index => {
                                                if (index === 1) { project.setCameraOption("fullscreen", true); return }
                                                project.setCameraOption("fullscreen", false); project.setCameraOption("width", .29); project.setCameraOption("x", .68); project.setCameraOption("y", .68)
                                            }
                                        }
                                        StudioComboBox {
                                            Layout.fillWidth: true
                                            property var shapes: ["rounded", "circle", "rectangle", "landscape", "portrait", "source"]
                                            model: ["圆角方形", "圆形", "方形", "横向", "纵向", "原始比例"]
                                            currentIndex: Math.max(0, shapes.indexOf(project.cameraSettings.shape))
                                            onActivated: project.setCameraOption("shape", shapes[currentIndex])
                                        }
                                        StudioCheckBox { text: "镜像"; checked: project.cameraSettings.mirror; onClicked: project.setCameraOption("mirror", checked) }
                                        StudioCheckBox { text: "阴影"; checked: project.cameraSettings.shadow; onClicked: project.setCameraOption("shadow", checked) }
                                    }
                                    StudioSection {
                                        title: "位置与大小"; icon: "layout"
                                        StudioSliderRow { label: "大小"; valueText: Math.round(project.cameraSettings.width * 100) + "%"; from: .08; to: 1; stepSize: .01; value: project.cameraSettings.width; onValueMoved: v => project.setCameraOption("width", v) }
                                        StudioSliderRow { label: "水平位置"; valueText: Math.round(project.cameraSettings.x * 100) + "%"; from: 0; to: 1; value: project.cameraSettings.x; onValueMoved: v => project.setCameraOption("x", v) }
                                        StudioSliderRow { label: "垂直位置"; valueText: Math.round(project.cameraSettings.y * 100) + "%"; from: 0; to: 1; value: project.cameraSettings.y; onValueMoved: v => project.setCameraOption("y", v) }
                                        StudioSliderRow { label: "圆角"; valueText: Math.round(project.cameraSettings.radius * 200) + "%"; from: 0; to: .5; value: project.cameraSettings.radius; onValueMoved: v => project.setCameraOption("radius", v) }
                                    }
                                    StudioSection {
                                        title: "取景"; icon: "crop"
                                        StudioSliderRow { label: "取景缩放"; valueText: Number(project.cameraSettings.cropScale).toFixed(1) + "×"; from: 1; to: 4; value: project.cameraSettings.cropScale; onValueMoved: v => project.setCameraOption("cropScale", v) }
                                        StudioSliderRow { label: "取景水平"; valueText: Math.round(project.cameraSettings.cropX * 100) + "%"; from: 0; to: 1; value: project.cameraSettings.cropX; onValueMoved: v => project.setCameraOption("cropX", v) }
                                        StudioSliderRow { label: "取景垂直"; valueText: Math.round(project.cameraSettings.cropY * 100) + "%"; from: 0; to: 1; value: project.cameraSettings.cropY; onValueMoved: v => project.setCameraOption("cropY", v) }
                                    }
                                    StudioSection {
                                        title: "同步"; icon: "arrowLeftRight"
                                        StudioSliderRow { label: "同步偏移"; valueText: Math.round((project.cameraSettings.offset || 0) * 1000) + " ms"; from: -1; to: 1; stepSize: .01; value: project.cameraSettings.offset || 0; onValueMoved: v => project.setCameraOption("offset", v) }
                                        ToolButton { text: "同步归零"; Layout.fillWidth: true; onClicked: project.setCameraOption("offset", 0) }
                                    }
                                    StudioDivider {}
                                    CameraMotionControls {
                                        Layout.fillWidth: true; positionSeconds: editor.positionSeconds
                                        canvasAspect: { const deps=[preview.hasFrame,project.canvasSettings]; const size=preview.outputSize(); return size.height>0 ? size.width/size.height : 16/9 }
                                        cameraAspect: preview.cameraFrameSize.height>0 ? preview.cameraFrameSize.width/preview.cameraFrameSize.height : 1
                                    }
                                }
                            }

                            // 3 · Framing
                            ColumnLayout {
                                visible: editor.inspectorTab === 3
                                Layout.fillWidth: true; spacing: 22
                                StudioSection {
                                    title: "自动运镜"; icon: "framing"
                                    helper: project.pointerSamples.length > 0 ? "开启后按点击区域放大重点；关闭后保留正常画面。" : "此工程尚无鼠标轨迹。"
                                    StudioCheckBox {
                                        text: "启用自动运镜"
                                        checked: project.autoZoomEnabled
                                        enabled: editor.editable
                                        onClicked: project.autoZoomEnabled = checked
                                    }
                                    StudioSliderRow { label: "放大倍率"; valueText: project.framingSettings.scale.toFixed(1) + "×"; from: 1; to: 6; stepSize: .1; value: project.framingSettings.scale; enabled: editor.editable; onValueMoved: v => project.setFramingOption("scale", v) }
                                    StudioSliderRow { label: "过渡"; valueText: project.framingSettings.transition.toFixed(1) + " 秒"; from: 0; to: 5; stepSize: .1; value: project.framingSettings.transition; enabled: editor.editable; onValueMoved: v => project.setFramingOption("transition", v) }
                                    StudioCheckBox { text: "跟随鼠标重点"; checked: project.framingSettings.follow; enabled: editor.editable; onClicked: project.setFramingOption("follow", checked) }
                                    StudioCheckBox { text: "弹簧过渡"; checked: project.framingSettings.spring; enabled: editor.editable; onClicked: project.setFramingOption("spring", checked) }
                                }
                                StudioDivider {}
                                ManualFramingControls {
                                    Layout.fillWidth: true
                                    enabled: !exporter.busy
                                    positionSeconds: editor.positionSeconds
                                    sourcePreview: preview
                                }
                            }

                            // 4 · Effects
                            ColumnLayout {
                                visible: editor.inspectorTab === 4
                                Layout.fillWidth: true; spacing: 22
                                VisualSceneControls {
                                    Layout.fillWidth: true
                                    section: "intro"
                                    enabled: editor.editable
                                    onPreviewIntroRequested: { editor.seek(0); editor.playPause() }
                                }
                                StudioDivider {}
                                StudioSection {
                                    title: "光标强调"; icon: "cursor"
                                    helper: "原录制鼠标已嵌入画面；以下只控制额外强调层。"
                                    StudioCheckBox { text: "启用鼠标强调"; checked: project.canvasSettings.cursorEnabled; enabled: editor.editable; onToggled: project.setCanvasOption("cursorEnabled", checked) }
                                    StudioCheckBox { text: "显示强调指针"; checked: project.canvasSettings.cursorVisible; enabled: project.canvasSettings.cursorEnabled && !exporter.busy; onToggled: project.setCanvasOption("cursorVisible", checked) }
                                    StudioSegmented {
                                        Layout.fillWidth: true
                                        model: ["光晕", "箭头", "十字"]
                                        property var styles: ["halo", "arrow", "crosshair"]
                                        currentIndex: Math.max(0, styles.indexOf(project.canvasSettings.cursorStyle))
                                        enabled: project.canvasSettings.cursorEnabled && !exporter.busy
                                        onActivated: index => project.setCanvasOption("cursorStyle", styles[index])
                                    }
                                    StudioSliderRow { label: "指针大小"; valueText: Math.round(project.canvasSettings.cursorSize); from: 8; to: 96; stepSize: 1; value: project.canvasSettings.cursorSize; enabled: project.canvasSettings.cursorEnabled && !exporter.busy; onValueMoved: v => project.setCanvasOption("cursorSize", v) }
                                    StudioCheckBox { text: "点击扩散环"; checked: project.canvasSettings.clickRing; enabled: project.canvasSettings.cursorEnabled && !exporter.busy; onToggled: project.setCanvasOption("clickRing", checked) }
                                }
                            }
                            // 5 · Audio
                            InspectorAudio { Layout.fillWidth: true; visible: editor.inspectorTab === 5; host: editor; enabled: !exporter.busy }
                            // 6 · Screen 3D
                            InspectorScreen3D { Layout.fillWidth: true; visible: editor.inspectorTab === 6; positionSeconds: editor.positionSeconds; enabled: editor.editable }
                            // 7 · Blur and spotlight
                            InspectorFocus { Layout.fillWidth: true; visible: editor.inspectorTab === 7; positionSeconds: editor.positionSeconds; sourcePreview: preview; enabled: editor.editable }
                            // 8 · Stickers
                            InspectorStickers { Layout.fillWidth: true; visible: editor.inspectorTab === 8; positionSeconds: editor.positionSeconds; enabled: editor.editable }
                            Item { implicitHeight: 12 }
                        }
                    }
                }
            }
        }

        // ---- Timeline ---------------------------------------------------------------
        StudioCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 44 + 1 + 10 + timelineStrip.implicitHeight + (manualTrack.visible ? manualTrack.Layout.preferredHeight + 4 : 0) + editor.visibleLanes.length * (editor.laneHeight + 4) + 22
            Layout.minimumHeight: Layout.preferredHeight
            radius: StudioTheme.radiusFloating
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; anchors.topMargin: 6; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 44; spacing: 2
                    RowLayout {
                        spacing: 2
                        Layout.preferredWidth: 300
                        ToolButton { iconName: "scissors"; quiet: true; compact: true; tip: "分割 (S)"; enabled: editor.editable && (project.selectedManualFraming !== "" || editor.editState.canSplit === true); onClicked: editor.splitSelection() }
                        ToolButton {
                            iconName: "trash"; quiet: true; compact: true
                            tip: project.selectedClips.length > 1 ? "删除所选 " + project.selectedClips.length + " 段" : "删除所选 (Delete)"
                            enabled: editor.editable && (project.selectedManualFraming !== "" || (project.selectedClips.length > 1 ? editor.editState.canRemoveSelected === true : editor.editState.canRemove === true))
                            onClicked: project.selectedManualFraming === "" && project.selectedClips.length > 1 ? project.removeSelectedClips() : editor.deleteSelection()
                        }
                        Hairline { Layout.leftMargin: 4; Layout.rightMargin: 4; implicitHeight: 18 }
                        ToolButton { iconName: "copy"; quiet: true; compact: true; tip: "复制运镜 (Ctrl+C)"; enabled: project.selectedManualFraming !== "" && !exporter.busy; onClicked: editor.copySelection() }
                        ToolButton { iconName: "paste"; quiet: true; compact: true; tip: "粘贴运镜 (Ctrl+V)"; enabled: project.hasManualClipboard && !exporter.busy; onClicked: editor.pasteSelection() }
                        ToolButton {
                            id: addTrackButton
                            text: editor.width >= 1100 ? "添加" : ""; iconName: "plus"; quiet: true; compact: true
                            tip: "在播放头添加运镜、3D、模糊、贴图或摄像头效果"
                            enabled: editor.editable
                            onClicked: trackMenu.popup(addTrackButton, 0, addTrackButton.height + 4)
                        }
                        Rectangle {
                            visible: project.selectedClips.length > 1
                            implicitWidth: multiLabel.implicitWidth + 16; implicitHeight: 22; radius: 11; color: StudioTheme.accent
                            Text { id: multiLabel; anchors.centerIn: parent; text: "已选 " + project.selectedClips.length; color: StudioTheme.accentInk; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; font.weight: Font.DemiBold }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Item { Layout.fillWidth: true }
                    Row {
                        spacing: 6
                        ToolButton { iconName: "stepBack"; compact: true; tip: "上一帧 (←)"; iconSize: 14; implicitWidth: 40; enabled: editor.editable; onClicked: editor.stepFrames(-1) }
                        ToolButton {
                            iconName: video.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                            tip: video.playbackState === MediaPlayer.PlayingState ? "暂停 (空格)" : "播放 (空格)"
                            implicitWidth: 48; compact: true; iconSize: 16
                            enabled: editor.editable
                            onClicked: editor.playPause()
                        }
                        ToolButton { iconName: "stepForward"; compact: true; tip: "下一帧 (→)"; iconSize: 14; implicitWidth: 40; enabled: editor.editable; onClicked: editor.stepFrames(1) }
                        Item { width: 10; height: 1 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: editor.clock(editor.positionSeconds)
                            color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "/ " + editor.clock(project.duration)
                            color: StudioTheme.faint; font.family: StudioTheme.monoFamily; font.pixelSize: 14
                        }
                    }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        spacing: 2
                        Layout.preferredWidth: 300
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            text: editor.width >= 1000 ? "吸附" : ""; iconName: "magnet"; compact: true; quiet: !checked
                            tip: editor.snappingEnabled ? "吸附已开（拖动时按住 Shift 临时关闭）" : "吸附已关"
                            checkable: true; checked: editor.snappingEnabled
                            onClicked: editor.snappingEnabled = checked
                        }
                        ToolButton {
                            id: monitorButton
                            iconName: "volume"; quiet: true; compact: true
                            tip: editor.monitorMuted ? "监听已静音" : "监听音量 " + Math.round(previewAudioProcessor.volume * 100) + "%"
                            onClicked: monitorPopover.toggle()
                            Rectangle { visible: editor.monitorMuted; anchors.centerIn: parent; width: 2; height: 20; radius: 1; rotation: -45; color: StudioTheme.text }
                        }
                        ToolButton { iconName: "help"; quiet: true; compact: true; tip: "快捷键速查 (F1)"; onClicked: editor.showShortcutHelp() }
                    }
                }
                StudioDivider { Layout.bottomMargin: 6 }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                    ColumnLayout {
                        Layout.preferredWidth: editor.width >= 1000 ? 92 : 30
                        Layout.fillWidth: false
                        Layout.fillHeight: true; spacing: 4
                        Item {
                            Layout.fillWidth: true; Layout.preferredHeight: timelineStrip.implicitHeight
                            LaneLabel { anchors.left: parent.left; y: timelineStrip.clipTop; height: timelineStrip.clipHeight; text: editor.width >= 1000 ? "屏幕录制" : ""; iconName: "monitor" }
                        }
                        LaneLabel { visible: manualTrack.visible; Layout.fillWidth: true; Layout.preferredHeight: manualTrack.Layout.preferredHeight; text: editor.width >= 1000 ? "运镜" : ""; iconName: "zoomIn" }
                        Repeater {
                            model: editor.visibleLanes
                            delegate: LaneLabel { required property var modelData; Layout.fillWidth: true; Layout.preferredHeight: editor.laneHeight; text: editor.width >= 1000 ? modelData.title : ""; iconName: modelData.icon }
                        }
                        Item { Layout.fillHeight: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                        TimelineStrip {
                            id: timelineStrip
                            Layout.fillWidth: true
                            implicitHeight: editor.height < 760 ? 104 : 124
                            Layout.preferredHeight: implicitHeight
                            positionSeconds: editor.positionSeconds
                            enabled: !exporter.busy
                            onSeekRequested: seconds => editor.seek(seconds)
                            onClipMenuRequested: (index, x, y) => clipMenu.popup(timelineStrip, x, y)
                        }
                        ManualZoomTrack { id: manualTrack; strip: timelineStrip; objectName: "manualZoomTrack"; snappingEnabled: editor.snappingEnabled; Layout.fillWidth: true; Layout.preferredHeight: 44; visible: project.manualFramingSegments.length > 0; compact: true; positionSeconds: editor.positionSeconds; enabled: !exporter.busy; onSeekRequested: seconds => editor.seek(seconds) }
                        Repeater {
                            model: editor.visibleLanes
                            delegate: EffectLane {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: editor.laneHeight
                                track: modelData.track
                                items: effects[modelData.track]
                                tint: modelData.tint
                                strip: timelineStrip
                                positionSeconds: editor.positionSeconds
                                enabled: editor.editable
                                label: item => editor.laneLabel(modelData.track, item)
                                onActivated: { video.pause(); editor.inspectorTab = modelData.tab }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // ---- Export progress and status --------------------------------------------
        StudioCard {
            visible: exporter.busy
            Layout.fillWidth: true; Layout.preferredHeight: 48; radius: 14
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 8; spacing: 12
                StudioIcon { name: "export"; width: 18; height: 18 }
                Text { text: exporter.status; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; elide: Text.ElideRight; Layout.maximumWidth: 320 }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 6; radius: 3; color: StudioTheme.well
                    Rectangle { width: parent.width * Math.max(0, Math.min(1, exporter.progress)); height: parent.height; radius: 3; color: StudioTheme.accent; Behavior on width { NumberAnimation { duration: 200 } } }
                }
                Text { text: Math.round(exporter.progress * 100) + "%"; color: StudioTheme.muted; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
                ToolButton { text: "取消导出"; compact: true; onClicked: exporter.cancel() }
            }
        }
        Text {
            id: statusText
            Layout.fillWidth: true; Layout.topMargin: -4
            elide: Text.ElideRight; maximumLineCount: 1
            font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper
            text: exporter.error !== "" ? exporter.error : preview.error !== "" ? preview.error : scenePresets.error !== "" ? scenePresets.error : previewAudioProcessor.error !== "" ? previewAudioProcessor.error : project.error !== "" ? project.error : video.error !== MediaPlayer.NoError ? video.errorString : exporter.lastExport.toString() !== "" ? exporter.status : project.status
            color: exporter.error !== "" || preview.error !== "" || scenePresets.error !== "" || project.error !== "" ? StudioTheme.danger : StudioTheme.muted
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && truncated
            ToolTip.text: text
        }
    }
}
