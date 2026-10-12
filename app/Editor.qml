import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import ScreenPilot.Render 1.0

Rectangle {
    id: editor
    objectName: "editorView"
    color: "#111214"
    signal backRequested()
    property real positionSeconds: 0
    property int playingClip: 0
    property bool primingPreview: false
    property bool needsPreviewFrame: false
    property url shownSource: ""
    property string pendingAction: ""
    property alias inspectorTab: inspectorTabs.currentIndex
    property bool exporting: exporter.busy
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
        if (!primingPreview) video.pause()
        positionSeconds = Math.max(0, Math.min(project.duration, seconds))
        var clips = project.clips
        for (var i = 0; i < clips.length; ++i) {
            if (positionSeconds < clips[i].outputStart + clips[i].duration || i === clips.length - 1) {
                playingClip = i
                break
            }
        }
        video.position = Math.round(project.sourceTime(Math.min(positionSeconds, Math.max(0, project.duration - 0.001))) * 1000)
    }
    function playPause() {
        var wasPriming = primingPreview
        // A user action takes over from the one-time poster-frame request.
        needsPreviewFrame = false
        primingPreview = false
        previewAudio.muted = false
        if (!wasPriming && video.playbackState === MediaPlayer.PlayingState) { video.pause(); return }
        seek(positionSeconds >= project.duration - 0.02 ? 0 : positionSeconds)
        video.play()
    }
    function saveProject() {
        video.pause()
        if (project.projectLocation.toString() !== "") project.save()
        else saveDialog.open()
    }
    function requestOpen() {
        if (project.dirty) { pendingAction = "open"; unsaved.open() }
        else openDialog.open()
    }
    function requestNewRecording() {
        video.pause()
        backRequested()
    }
    function loadProject(directory) {
        video.stop()
        video.source = ""
        project.openProject(directory)
        video.source = project.source
    }
    function refresh() {
        if (shownSource.toString() !== project.source.toString()) {
            shownSource = project.source
            video.source = project.source
            positionSeconds = 0
        }
        seek(Math.min(positionSeconds, project.duration))
    }
    Connections { target: project; function onChanged() { editor.refresh() } }
    onVisibleChanged: if (!visible) video.pause()
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
        onPositionChanged: {
            if (editor.primingPreview || playbackState !== MediaPlayer.PlayingState || editor.playingClip >= project.clips.length) return
            var c = project.clips[editor.playingClip]
            var seconds = position / 1000
            if (seconds < c.sourceStart - 0.03) return
            if (seconds >= c.sourceStart + c.sourceDuration - 0.01) {
                if (editor.playingClip + 1 >= project.clips.length) {
                    pause()
                    editor.positionSeconds = project.duration
                } else {
                    editor.playingClip++
                    c = project.clips[editor.playingClip]
                    editor.positionSeconds = c.outputStart
                    position = Math.round(c.sourceStart * 1000)
                }
            } else editor.positionSeconds = c.outputStart + Math.max(0, seconds - c.sourceStart) / c.playbackRate
        }
    }
    FileDialog {
        id: saveDialog
        title: "保存工程"
        fileMode: FileDialog.SaveFile
        nameFilters: ["ScreenPilot 工程 (*.dogscproject)"]
        defaultSuffix: "dogscproject"
        onAccepted: {
            video.stop()
            video.source = ""
            project.saveAs(selectedFile)
            video.source = project.source
        }
    }
    FolderDialog { id: openDialog; title: "选择 .dogscproject 工程文件夹"; onAccepted: editor.loadProject(selectedFolder) }
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
        title: "导出剪辑后的视频（请选择新文件名）"
        fileMode: FileDialog.SaveFile
        nameFilters: ["MP4 视频 (*.mp4)"]
        defaultSuffix: "mp4"
        onAccepted: { video.pause(); exporter.start(project.source, project.clips, selectedFile, project.framingSegments, project.canvasSettings) }
    }
    Dialog {
        id: unsaved
        anchors.centerIn: parent
        title: "当前工程有未保存修改"
        modal: true
        width: 420
        ColumnLayout {
            width: parent.width
            Label { text: "保存后可继续编辑；放弃修改才会替换当前工程。"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Button { text: "先保存"; onClicked: { unsaved.close(); editor.saveProject() } }
                Button { text: "放弃并打开"; onClicked: { unsaved.close(); project.discardChanges(); openDialog.open() } }
                Button { text: "取消"; onClicked: unsaved.close() }
            }
        }
    }
    component ToolButton: Button {
        id: tool
        property bool primary: false
        implicitHeight: 36
        leftPadding: 14; rightPadding: 14
        contentItem: Text { text: tool.text; font: tool.font; color: !tool.enabled ? "#62636a" : tool.primary ? "#18191c" : "#e8e5de"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
        background: Rectangle { radius: 7; color: tool.primary ? "#e9e5dd" : tool.hovered ? "#34353b" : "#26272c"; opacity: tool.enabled ? 1 : 0.5; border.color: tool.primary ? "transparent" : "#3a3b41" }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout {
            ToolButton {
                text: "← 返回录制"
                enabled: !exporter.busy
                onClicked: editor.requestNewRecording()
            }
            Label { text: "ScreenPilot"; font.pixelSize: 20; font.bold: true; color: "#eeeae3" }
            Label { text: "/  " + project.title + (project.dirty ? "  · 未保存" : ""); color: "#a7a49e"; elide: Text.ElideMiddle; Layout.fillWidth: true }
            ToolButton { text: "打开工程"; enabled: !exporter.busy; onClicked: editor.requestOpen() }
            ToolButton { text: "保存工程"; enabled: project.hasProject && !exporter.busy; onClicked: editor.saveProject() }
            ToolButton { text: "另存为"; enabled: project.hasProject && !exporter.busy; onClicked: saveDialog.open() }
            ToolButton { text: "导出 MP4"; primary: true; enabled: project.hasProject && !exporter.busy; onClicked: { video.pause(); exportDialog.open() } }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 16
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#090a0c"; radius: 12; border.color: "#2e3035"; clip: true
                CanvasPreview {
                    id: preview
                    objectName: "canvasPreview"
                    onFrameReady: {
                        if (editor.primingPreview) {
                            editor.primingPreview = false
                            video.pause()
                            previewAudio.muted = false
                        }
                    }
                    anchors.fill: parent
                    anchors.margins: 1
                    canvasSettings: project.canvasSettings
                    camera: editor.framing
                }
                Label { anchors.centerIn: parent; visible: !project.hasProject; text: "录制或打开工程后开始剪辑"; color: "#85858d" }
            }
            Rectangle {
                Layout.preferredWidth: 260; Layout.fillHeight: true
                color: "#1b1c20"; radius: 12; border.color: "#2c2d33"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 14
                    TabBar {
                        id: inspectorTabs
                        Layout.fillWidth: true
                        TabButton { text: "片段" }
                        TabButton { text: "画布" }
                    }
                    ScrollView {
                        id: inspectorScroll
                        Layout.fillWidth: true; Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: inspectorScroll.availableWidth
                            spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: inspectorTabs.currentIndex === 0
                                spacing: 14
                                Label { text: "片段"; font.pixelSize: 17; font.bold: true; color: "#eeece7" }
                                Label { text: project.selectedClip >= 0 ? "主录屏 · " + (project.selectedClip + 1) : "请选择片段"; color: "#c9c5bd" }
                                Label {
                                    property var clip: project.selectedClip >= 0 && project.selectedClip < project.clips.length ? project.clips[project.selectedClip] : null
                                    text: clip ? "源起点  " + editor.clock(clip.sourceStart) + "\n\n保留时长  " + editor.clock(clip.sourceDuration) : ""
                                    color: "#9998a0"; font.pixelSize: 12
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: "#323339" }
                                ToolButton { text: "裁掉播放头前"; Layout.fillWidth: true; enabled: project.hasProject && !exporter.busy; onClicked: project.trimStart(editor.positionSeconds) }
                                ToolButton { text: "裁掉播放头后"; Layout.fillWidth: true; enabled: project.hasProject && !exporter.busy; onClicked: project.trimEnd(editor.positionSeconds) }
                                ToolButton { text: "删除所选片段"; Layout.fillWidth: true; enabled: project.clips.length > 1 && !exporter.busy; onClicked: project.removeSelected() }
                                CheckBox {
                                    text: "自动运镜"
                                    checked: project.autoZoomEnabled
                                    enabled: project.hasProject && !exporter.busy
                                    onClicked: project.autoZoomEnabled = checked
                                    palette.windowText: "#dedbd4"
                                }
                                Label { text: project.pointerSamples.length > 0 ? "开启后按点击区域放大重点\n关闭后保留正常画面" : "此工程尚无鼠标轨迹"; color: "#9998a0"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                            ColumnLayout {
                                visible: inspectorTabs.currentIndex === 1
                                Layout.fillWidth: true
                                spacing: 12
                                Label { text: "画面美化"; color: "#eeece7"; font.pixelSize: 17; font.bold: true }
                                Label { text: "视频比例"; color: "#bcb8b0" }
                                ComboBox {
                                    Layout.fillWidth: true
                                    property var ratios: ["source", "16:9", "4:3", "9:16", "1:1", "3:4", "2.39:1", "1:2.39"]
                                    model: ["适应素材", "16:9 横屏", "4:3", "9:16 竖屏", "1:1 方形", "3:4", "2.39:1", "1:2.39"]
                                    currentIndex: Math.max(0, ratios.indexOf(project.canvasSettings.ratio))
                                    enabled: project.hasProject && !exporter.busy
                                    onActivated: project.setCanvasOption("ratio", ratios[currentIndex])
                                }
                                Label { text: "背景"; color: "#bcb8b0" }
                                RowLayout {
                                    ToolButton { text: "选择颜色"; enabled: project.hasProject && !exporter.busy; onClicked: { backgroundColorDialog.selectedColor = project.canvasSettings.backgroundColor; backgroundColorDialog.open() } }
                                    Rectangle { width: 28; height: 28; radius: 5; color: project.canvasSettings.backgroundColor || "#20242c"; border.color: "#66666c" }
                                }
                                ToolButton { text: "选择背景图片"; Layout.fillWidth: true; enabled: project.hasProject && !exporter.busy; onClicked: backgroundImageDialog.open() }
                                ToolButton { text: "移除背景图片"; Layout.fillWidth: true; visible: project.canvasSettings.backgroundImage !== ""; enabled: !exporter.busy; onClicked: project.clearBackgroundImage() }
                                Label { text: "留白  " + Math.round(project.canvasSettings.padding) + " px"; color: "#bcb8b0" }
                                Slider { Layout.fillWidth: true; from: 0; to: 300; stepSize: 5; value: project.canvasSettings.padding; enabled: project.hasProject && !exporter.busy; onMoved: project.setCanvasOption("padding", value) }
                                Label { text: "圆角  " + Math.round(project.canvasSettings.radius) + " px"; color: "#bcb8b0" }
                                Slider { Layout.fillWidth: true; from: 0; to: 100; stepSize: 1; value: project.canvasSettings.radius; enabled: project.hasProject && !exporter.busy; onMoved: project.setCanvasOption("radius", value) }
                                Label { text: "阴影  " + Math.round(project.canvasSettings.shadow * 100) + "%"; color: "#bcb8b0" }
                                Slider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01; value: project.canvasSettings.shadow; enabled: project.hasProject && !exporter.busy; onMoved: project.setCanvasOption("shadow", value) }
                                Label { text: "设置随工程保存\n预览和导出使用相同效果"; color: "#7c7c84"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            ToolButton { text: video.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"; enabled: project.hasProject && !exporter.busy; onClicked: editor.playPause() }
            Slider { Layout.fillWidth: true; from: 0; to: Math.max(0.01, project.duration); value: editor.positionSeconds; enabled: project.hasProject && !exporter.busy; onMoved: editor.seek(value) }
            Label { text: editor.clock(editor.positionSeconds) + " / " + editor.clock(project.duration); color: "#c1bdb5"; font.family: "Consolas" }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 152
            color: "#1b1c20"; radius: 12; border.color: "#2c2d33"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 12
                RowLayout {
                    Label { text: "时间线"; color: "#dedbd4"; font.bold: true }
                    Item { Layout.fillWidth: true }
                    ToolButton { text: "分割"; enabled: project.hasProject && !exporter.busy; onClicked: project.split(editor.positionSeconds) }
                    ToolButton { text: "撤销"; enabled: project.canUndo && !exporter.busy; onClicked: project.undo() }
                    ToolButton { text: "重做"; enabled: project.canRedo && !exporter.busy; onClicked: project.redo() }
                }
                Flickable {
                    id: timeline
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    contentWidth: Math.max(width, project.duration * 70)
                    contentHeight: height
                    property real scale: contentWidth / Math.max(0.01, project.duration)
                    ScrollBar.horizontal: ScrollBar {}
                    Repeater {
                        model: project.clips
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            x: modelData.outputStart * timeline.scale
                            y: 4
                            width: Math.max(1, modelData.duration * timeline.scale - 3)
                            height: 54; radius: 5
                            color: project.selectedClip === index ? "#b5803e" : "#795a35"
                            border.color: project.selectedClip === index ? "#f0dcc1" : "#b28243"
                            border.width: project.selectedClip === index ? 2 : 1
                            clip: true
                            Text { anchors.fill: parent; anchors.margins: 10; text: "主录屏 " + (index + 1) + " · " + editor.clock(modelData.duration); color: "#f6e8d5"; font.pixelSize: 12; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                            MouseArea { anchors.fill: parent; enabled: !exporter.busy; onClicked: function(mouse) { project.selectedClip = index; editor.seek(modelData.outputStart + mouse.x / timeline.scale) } }
                        }
                    }
                    Rectangle { x: editor.positionSeconds * timeline.scale; y: 0; width: 2; height: 65; color: "#efebe3" }
                }
            }
        }
        RowLayout {
            visible: exporter.busy
            Label { text: exporter.status; color: "#c9c5bd" }
            ProgressBar { Layout.fillWidth: true; value: exporter.progress }
            ToolButton { text: "取消导出"; onClicked: exporter.cancel() }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
            text: exporter.error !== "" ? exporter.error : project.error !== "" ? project.error : video.error !== MediaPlayer.NoError ? video.errorString : exporter.lastExport.toString() !== "" ? exporter.status : project.status
            color: exporter.error !== "" || project.error !== "" ? "#ff9187" : "#919198"
        }
    }
}
