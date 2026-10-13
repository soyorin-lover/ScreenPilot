import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    width: editing ? 1280 : 1080
    height: editing ? 860 : 780
    minimumWidth: 860
    minimumHeight: 640
    visible: true
    title: "ScreenPilot"
    color: StudioTheme.background
    font.family: StudioTheme.fontFamily
    font.pixelSize: 13
    palette.window: StudioTheme.surface
    palette.base: StudioTheme.surface
    palette.alternateBase: StudioTheme.surfaceAlt
    palette.button: StudioTheme.surfaceAlt
    palette.buttonText: StudioTheme.text
    palette.text: StudioTheme.text
    palette.windowText: StudioTheme.text
    palette.highlight: StudioTheme.accent
    palette.highlightedText: StudioTheme.accentInk
    palette.mid: StudioTheme.border
    palette.light: StudioTheme.surface
    palette.dark: StudioTheme.muted
    palette.toolTipBase: StudioTheme.accent
    palette.toolTipText: StudioTheme.accentInk
    property bool editing: false
    property string pendingAction: ""
    property bool awaitingProjectSave: false
    property url pendingEditingRecording: ""
    property bool reviewing: false
    property bool closeAfterSave: false
    property url reviewedFile: ""
    property real pendingDuration: 0
    property bool cameraUsedForRecording: false
    property string recordingDestructiveAction: ""
    readonly property bool canBeginRecording: !recorder.busy && !cameraRecorder.busy
        && pendingEditingRecording.toString() === ""
    Binding { target: StudioTheme; property: "dark"; value: appSettings.effectiveTheme === "dark" }
    MemoWindow { id: recorderMemo }
    SettingsDialog { id: settingsDialog }
    RecoveryPanel {
        id: recoveryPanel
        beforeRestore: function() { editingView.cancelTimelineInteraction(); playback.pause() }
        onRestored: root.editing = true
    }
    RegionPicker { id: regionPicker }
    readonly property bool hasCaptureRegion: recorder.captureRegion.width < .999 || recorder.captureRegion.height < .999
    RecentProjectsPanel {
        id: recentProjectsPanel
        onOpenRequested: function(location) {
            if (recorder.busy || exporter.busy) return
            root.editing = true
            editingView.requestOpenRecent(location)
        }
    }

    function clock(ms) {
        var seconds = Math.floor(ms / 1000)
        return Math.floor(seconds / 60).toString().padStart(2, "0") + ":" + (seconds % 60).toString().padStart(2, "0")
    }
    function beginRecording() {
        if (!canBeginRecording) return
        if (project.dirty) { pendingAction = "record"; unsavedProject.open(); return }
        playback.stop()
        reviewing = false
        cameraUsedForRecording = cameraRecorder.enabled
        pendingDuration = 0
        recorder.start()
    }
    function finishProjectSave(saved) {
        if (!awaitingProjectSave) return
        awaitingProjectSave = false
        const action = pendingAction
        pendingAction = ""
        if (!saved) return
        if (action === "close") root.close()
        else if (action === "record") { editing = false; beginRecording() }
    }
    function finishPendingRecording() {
        if (pendingDuration <= 0 || pendingEditingRecording.toString() === "" || cameraRecorder.busy) return
        var recordingUrl = pendingEditingRecording
        pendingEditingRecording = ""
        project.newRecording(recordingUrl, pendingDuration / 1000, recorder.lastPointerSamples, recorder.lastRecordingMarkers)
        if (project.source.toString() === recordingUrl.toString()) {
            project.setAudioMedia(recorder.lastSystemAudio, recorder.lastMicrophoneAudio)
            if (cameraUsedForRecording && cameraRecorder.lastRecording.toString() !== "")
                project.setCameraMedia(cameraRecorder.lastRecording, cameraRecorder.offset, cameraRecorder.mirror)
            scenePresets.applyDefault()
            editing = true
            playback.stop()
        }
    }
    Connections { target: cameraRecorder; function onChanged() { root.finishPendingRecording(); if (root.closeAfterSave && !recorder.busy && !cameraRecorder.busy) root.close() } }
    onEditingChanged: cameraRecorder.previewActive = !editing
    onClosing: function(event) {
        if (exporter.busy || cameraRecorder.busy && !recorder.busy) { event.accepted = false; return }
        if (!recorder.busy && project.dirty) {
            event.accepted = false
            pendingAction = "close"
            unsavedProject.open()
            return
        }
        if (recorder.busy) {
            event.accepted = false
            closeAfterSave = true
            recorder.stop()
        }
    }
    Connections {
        target: recorder
        function onDiscarded() {
            root.pendingEditingRecording = ""
            root.pendingDuration = 0
            root.reviewing = false
            root.editing = false
        }
        function onChanged() {
            if (root.closeAfterSave && !recorder.busy) root.close()
            if (!recorder.busy && recorder.lastRecording.toString() !== ""
                    && recorder.lastRecording.toString() !== root.reviewedFile.toString()) {
                root.reviewedFile = recorder.lastRecording
                root.pendingEditingRecording = root.reviewedFile
                playback.source = root.reviewedFile
                root.reviewing = true
            }
        }
    }
    Component.onCompleted: { recorder.previewSink = live.videoSink; cameraRecorder.previewSink = cameraLive.videoSink }
    FolderDialog {
        id: folderPicker
        title: "选择录制保存文件夹"
        onAccepted: recorder.outputDirectory = selectedFolder.toString()
    }
    StudioDialog {
        id: confirmRecordingAction
        width: 420
        iconName: root.recordingDestructiveAction === "restart" ? "refresh" : "trash"
        title: root.recordingDestructiveAction === "restart" ? "重新录制？" : "放弃本次录制？"
        StudioHelp { width: parent ? parent.width : 360; font.pixelSize: StudioTheme.body; text: "本次尚未保存的录制将被删除，已保存工程不受影响。" }
        footer: StudioDialogFooter {
            Item { Layout.fillWidth: true }
            StudioButton { text: "取消"; onClicked: confirmRecordingAction.reject() }
            StudioButton { text: root.recordingDestructiveAction === "restart" ? "重新录制" : "放弃录制"; primary: true; onClicked: confirmRecordingAction.accept() }
        }
        onAccepted: {
            if (!recorder.busy) return
            if (root.recordingDestructiveAction === "restart") recorder.restart()
            else recorder.discard()
        }
    }
    StudioDialog {
        id: unsavedProject
        onRejected: root.pendingAction = ""
        width: 440
        iconName: "save"
        title: "当前工程有未保存修改"
        StudioHelp { width: parent ? parent.width : 380; font.pixelSize: StudioTheme.body; text: "保存工程以保留剪辑，或明确放弃修改。" }
        footer: StudioDialogFooter {
            StudioButton { text: "放弃修改"; danger: true; quiet: true; onClicked: {
                unsavedProject.close()
                const action = root.pendingAction
                root.pendingAction = ""
                project.discardChanges()
                if (action === "close") root.close()
                else root.beginRecording()
            } }
            Item { Layout.fillWidth: true }
            StudioButton { text: "取消"; onClicked: unsavedProject.reject() }
            StudioButton { text: "先保存"; primary: true; onClicked: { unsavedProject.close(); root.awaitingProjectSave = true; root.editing = true; editingView.saveProject() } }
        }
    }
    Editor {
        id: editingView
        anchors.fill: parent
        visible: root.editing
        onBackRequested: root.editing = false
        onProjectSaveFinished: saved => root.finishProjectSave(saved)
        onSettingsRequested: settingsDialog.open()
        onRecoveryRequested: recoveryPanel.open()
    }
    MediaPlayer {
        id: playback
        videoOutput: review
        audioOutput: AudioOutput { volume: 0.7 }
        onDurationChanged: {
            if (duration > 0 && root.pendingEditingRecording.toString() !== ""
                    && source.toString() === root.pendingEditingRecording.toString()) {
                root.pendingDuration = duration
                root.finishPendingRecording()
            }
        }
    }

    // ---- Recorder building blocks -------------------------------------------------
    component IslandSeparator: Rectangle { implicitWidth: 1; implicitHeight: 44; color: StudioTheme.hairline; Layout.leftMargin: 6; Layout.rightMargin: 6 }

    // Capture source tile: icon over caption, lifted when it is the active kind.
    component SourceTile: AbstractButton {
        id: tile
        property string iconName: ""
        property bool current: false
        property string tip: ""
        implicitWidth: recorderPage.compactBar ? 54 : 66
        implicitHeight: 64
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        opacity: enabled ? 1 : .35
        Accessible.name: text
        ToolTip.visible: tip.length > 0 && hovered
        ToolTip.delay: 300
        ToolTip.text: tip
        background: Rectangle { radius: 14; color: tile.down ? StudioTheme.wellHover : tile.current ? StudioTheme.selection : tile.hovered ? StudioTheme.well : "transparent"; border.width: tile.visualFocus ? 2 : 0; border.color: StudioTheme.accent }
        contentItem: Column {
            spacing: 5
            topPadding: 7
            StudioIcon { name: tile.iconName; width: 30; height: 30; anchors.horizontalCenter: parent.horizontalCenter; color: tile.current ? StudioTheme.text : StudioTheme.muted }
            Text { text: tile.text; anchors.horizontalCenter: parent.horizontalCenter; color: tile.current ? StudioTheme.text : StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; font.weight: tile.current ? Font.DemiBold : Font.Medium }
        }
    }

    // Round input toggle with a live status dot (green on, grey off).
    component InputCircle: AbstractButton {
        id: circle
        property string iconName: ""
        property bool on: false
        property real level: -1
        property bool showStatus: true
        property string tip: ""
        implicitWidth: 54; implicitHeight: 54
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        opacity: enabled ? 1 : .4
        Accessible.name: tip
        ToolTip.visible: tip.length > 0 && hovered
        ToolTip.delay: 300
        ToolTip.text: tip
        background: Item {
            Rectangle { anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: -2; radius: width / 2; color: "#10141a28" }
            Rectangle {
                anchors.fill: parent; radius: width / 2
                color: circle.down ? StudioTheme.surfaceAlt : StudioTheme.surface
                border.width: circle.visualFocus ? 2 : 1
                border.color: circle.visualFocus ? StudioTheme.accent : circle.hovered ? StudioTheme.border : StudioTheme.hairline
            }
            // Microphone level reads as a green fill rising from the base.
            Item {
                visible: circle.showStatus && circle.level >= 0 && circle.on
                anchors.fill: parent; anchors.margins: 1
                clip: true
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                    height: parent.height * (0.14 + 0.5 * Math.min(1, Math.max(0, circle.level)))
                    color: "#2e4dd98c"
                    Behavior on height { NumberAnimation { duration: 80 } }
                }
            }
        }
        contentItem: Item {
            StudioIcon { anchors.centerIn: parent; name: circle.iconName; width: 24; height: 24; color: circle.on ? StudioTheme.text : StudioTheme.muted }
            // Off reads as a struck-through glyph, not only a grey dot.
            Rectangle {
                visible: circle.showStatus && !circle.on
                anchors.centerIn: parent
                width: 2; height: 30; radius: 1; rotation: -45
                color: "#8a8a8e"
                border.color: StudioTheme.surface; border.width: 0
            }
        }
        Rectangle {
            visible: circle.showStatus && circle.level < 0
            width: 9; height: 9; radius: 4.5
            x: parent.width - width - 4; y: parent.height - height - 4
            color: circle.on ? StudioTheme.success : "#b8b8bc"
            border.color: StudioTheme.surface; border.width: 1.5
        }
    }

    // ---- Source / input popovers ----------------------------------------------------
    StudioPopover {
        id: sourcePopover
        property string kind: "screen"
        parent: Overlay.overlay
        width: 320
        x: Math.round((root.width - width) / 2)
        y: island.y - height - 12
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Text { text: sourcePopover.kind === "screen" ? "选择显示器" : "选择窗口"; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold; color: StudioTheme.text; Layout.fillWidth: true }
                StudioButton { iconName: "refresh"; tip: "刷新屏幕与窗口"; quiet: true; compact: true; enabled: !recorder.busy; onClicked: recorder.refreshSources() }
            }
            ListView {
                id: sourceList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 300)
                clip: true
                spacing: 2
                model: {
                    var rows = [], all = recorder.sources
                    for (var i = 0; i < all.length; ++i) if (all[i].kind === sourcePopover.kind) rows.push({label: all[i].label, index: i})
                    return rows
                }
                ScrollIndicator.vertical: ScrollIndicator {}
                delegate: ItemDelegate {
                    id: sourceRow
                    required property var modelData
                    width: sourceList.width
                    height: 38
                    leftPadding: 10
                    enabled: !recorder.busy
                    onClicked: { recorder.selectedSource = modelData.index; sourcePopover.close() }
                    contentItem: RowLayout {
                        spacing: 10
                        StudioIcon { name: sourcePopover.kind === "screen" ? "monitor" : "window"; width: 18; height: 18; color: StudioTheme.muted }
                        Text { text: sourceRow.modelData.label; Layout.fillWidth: true; elide: Text.ElideRight; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
                        StudioIcon { name: "check"; width: 16; height: 16; color: StudioTheme.text; visible: recorder.selectedSource === sourceRow.modelData.index }
                    }
                    background: Rectangle { radius: StudioTheme.radiusCompact; color: sourceRow.hovered || recorder.selectedSource === sourceRow.modelData.index ? StudioTheme.selection : "transparent" }
                }
            }
            StudioHelp { visible: sourceList.count === 0; text: sourcePopover.kind === "screen" ? "未找到可录制的显示器，点击刷新重试。" : "未找到可录制的窗口，打开目标应用后点击刷新。" }
        }
    }
    StudioPopover {
        id: specPopover
        parent: Overlay.overlay
        width: 340
        x: Math.max(12, Math.min(root.width - width - 12, island.x + islandRow.x + specCard.x + specCard.width / 2 - width / 2))
        y: island.y - height - 12
        readonly property var caps: recorder.capabilities
        readonly property var resolutionLabels: ({ source: "原始", "4k": "最高 4K", "1080p": "最高 1080p" })
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "录制规格"; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold; color: StudioTheme.text }
            Text { text: "帧率"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            StudioSegmented {
                objectName: "recordingFrameRateSelector"
                Layout.fillWidth: true
                readonly property var rates: specPopover.caps.frameRates || [30, 60]
                model: rates.map(r => r + " FPS")
                currentIndex: rates.indexOf(recorder.frameRate)
                enabled: !recorder.busy
                onActivated: index => recorder.frameRate = rates[index]
            }
            Text { text: "分辨率"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            StudioSegmented {
                objectName: "recordingResolutionSelector"
                Layout.fillWidth: true
                readonly property var modes: specPopover.caps.resolutions || ["source"]
                model: modes.map(m => specPopover.resolutionLabels[m] || m)
                currentIndex: modes.indexOf(recorder.resolutionMode)
                enabled: !recorder.busy
                onActivated: index => recorder.resolutionMode = modes[index]
            }
            Text { text: "编码"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            StudioSegmented {
                objectName: "recordingCodecSelector"
                Layout.fillWidth: true
                visible: recorder.availableVideoCodecs.length > 0
                model: recorder.availableVideoCodecs.map(c => c.label)
                currentIndex: { const c = recorder.availableVideoCodecs; for (let i = 0; i < c.length; ++i) if (c[i].value === recorder.videoCodec) return i; return -1 }
                enabled: !recorder.busy
                onActivated: index => recorder.videoCodec = recorder.availableVideoCodecs[index].value
            }
            StudioHelp { visible: recorder.availableVideoCodecs.length === 0; color: StudioTheme.danger; text: "本机没有可用的录制编码。" }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 34; radius: StudioTheme.radiusControl; color: StudioTheme.well
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                    Text { text: "输出尺寸"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.fillWidth: true }
                    Text { text: recorder.effectiveVideoSize.width > 0 ? recorder.effectiveVideoSize.width + " × " + recorder.effectiveVideoSize.height : "开始后确定"; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
                }
            }
            StudioDivider { Layout.fillWidth: true }
            Text { text: "系统声音范围"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            StudioSegmented {
                Layout.fillWidth: true
                model: ["全部声音", "仅所选应用"]
                currentIndex: recorder.systemAudioScope === "application" ? 1 : 0
                enabled: !recorder.busy && recorder.systemAudioEnabled && (specPopover.caps.applicationAudio === true || recorder.systemAudioScope === "application")
                onActivated: index => recorder.systemAudioScope = index === 1 ? "application" : "all"
            }
            StudioHelp {
                text: specPopover.caps.applicationAudio !== true ? "当前 Windows 版本不支持只录制单个应用的声音（需要 build 20348 以上）。"
                    : recorder.systemAudioScope === "application" ? "只录制所选窗口所属应用及其子进程的声音，请在窗口来源中选择目标应用。目标没有播放声音时会录到静音。"
                    : "包含全部应用正在播放的声音。"
            }
            StudioDivider { Layout.fillWidth: true }
            Text { text: "暂不支持"; color: StudioTheme.faint; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
            Flow {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: [["proResRecording", "ProRes 录制"], ["nativeCursorRemoval", "隐藏系统光标"], ["independentCursorShapes", "独立光标形状"]]
                    delegate: Rectangle {
                        required property var modelData
                        visible: specPopover.caps[modelData[0]] !== true
                        implicitWidth: unsupported.implicitWidth + 16; implicitHeight: 24; radius: 12
                        color: "transparent"; border.color: StudioTheme.border
                        Text { id: unsupported; anchors.centerIn: parent; text: modelData[1]; color: StudioTheme.faint; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
                    }
                }
            }
        }
    }
    StudioPopover {
        id: micPopover
        parent: Overlay.overlay
        width: 300
        x: Math.max(12, Math.min(root.width - width - 12, island.x + islandRow.x + inputGroup.x + 27 - width / 2))
        y: island.y - height - 12
        contentItem: ColumnLayout {
            spacing: 10
            StudioCheckBox { text: "录制麦克风"; checked: recorder.microphoneEnabled; enabled: !recorder.busy; onToggled: recorder.microphoneEnabled = checked }
            StudioComboBox {
                Layout.fillWidth: true
                model: recorder.microphones
                textRole: "label"
                currentIndex: recorder.selectedMicrophone
                enabled: recorder.microphoneEnabled && !recorder.busy
                onActivated: recorder.selectedMicrophone = currentIndex
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 6; radius: 3; color: StudioTheme.well
                Rectangle { width: parent.width * Math.min(1, Math.max(0, recorder.microphoneLevel)); height: parent.height; radius: 3; color: StudioTheme.success; Behavior on width { NumberAnimation { duration: 80 } } }
            }
            StudioHelp { text: "麦克风电平仅在录制时显示" }
        }
    }
    StudioPopover {
        id: cameraPopover
        parent: Overlay.overlay
        width: 300
        x: Math.max(12, Math.min(root.width - width - 12, island.x + islandRow.x + inputGroup.x + 27 + 62 - width / 2))
        y: island.y - height - 12
        contentItem: ColumnLayout {
            spacing: 10
            StudioCheckBox { text: "录制摄像头"; checked: cameraRecorder.enabled; enabled: !recorder.busy && !cameraRecorder.busy; onToggled: cameraRecorder.enabled = checked }
            StudioComboBox {
                Layout.fillWidth: true
                model: cameraRecorder.devices; textRole: "description"
                currentIndex: { var devices = cameraRecorder.devices; for (var i = 0; i < devices.length; ++i) if (devices[i].id === cameraRecorder.selectedDevice) return i; return -1 }
                enabled: cameraRecorder.enabled && !recorder.busy && !cameraRecorder.busy
                onActivated: cameraRecorder.selectedDevice = cameraRecorder.devices[currentIndex].id
            }
            StudioComboBox {
                Layout.fillWidth: true
                model: { var rows = [{label: "自动"}]; var formats = cameraRecorder.formats; for (var i = 0; i < formats.length; ++i) rows.push(formats[i]); return rows }
                textRole: "label"
                currentIndex: cameraRecorder.selectedFormat + 1
                enabled: cameraRecorder.enabled && !recorder.busy && !cameraRecorder.busy
                onActivated: cameraRecorder.selectedFormat = currentIndex - 1
            }
            StudioCheckBox { text: "镜像摄像头"; checked: cameraRecorder.mirror; enabled: cameraRecorder.enabled && !recorder.busy; onToggled: cameraRecorder.mirror = checked }
            StudioHelp { visible: cameraRecorder.enabled || cameraRecorder.error !== ""; text: cameraRecorder.error !== "" ? cameraRecorder.error : cameraRecorder.status; color: cameraRecorder.error !== "" ? StudioTheme.danger : StudioTheme.muted }
        }
    }
    StudioPopover {
        id: savePopover
        parent: Overlay.overlay
        width: 320
        x: Math.max(12, Math.min(root.width - width - 12, island.x + islandRow.x + saveCard.x + saveCard.width / 2 - width / 2))
        y: island.y - height - 12
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "保存位置"; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold; color: StudioTheme.text }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: pathLabel.implicitHeight + 16; radius: StudioTheme.radiusControl; color: StudioTheme.well
                Text { id: pathLabel; anchors.fill: parent; anchors.margins: 8; text: recorder.outputDirectory; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideMiddle; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            }
            StudioButton { text: "更改文件夹"; iconName: "folder"; Layout.fillWidth: true; enabled: !recorder.busy; onClicked: { savePopover.close(); folderPicker.open() } }
            RowLayout {
                spacing: 6
                StudioButton { text: "打开文件夹"; quiet: true; compact: true; onClicked: recorder.openOutputDirectory() }
                StudioButton { text: "系统播放器"; iconName: "play"; quiet: true; compact: true; enabled: recorder.lastRecording.toString() !== ""; onClicked: recorder.openLastRecording() }
            }
            StudioHelp { text: "MP4 视频 · 录制结束自动保存" }
        }
    }

    // ---- Recorder page ------------------------------------------------------------
    Item {
        id: recorderPage
        visible: !root.editing
        anchors.fill: parent
        readonly property bool compactBar: width < 1040
        readonly property bool active: recorder.recording || recorder.paused
        readonly property var currentSource: recorder.selectedSource >= 0 && recorder.selectedSource < recorder.sources.length ? recorder.sources[recorder.selectedSource] : null
        readonly property string currentKind: currentSource ? currentSource.kind : ""

        StudioDotGrid { anchors.fill: parent }

        RowLayout {
            id: recorderHeader
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 8
            Rectangle {
                width: 30; height: 30; radius: 9; color: StudioTheme.accent
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: StudioTheme.recording }
            }
            Text { text: "ScreenPilot"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.leftMargin: 2 }
            Text { visible: root.width > 960; text: "新建录制"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; Layout.leftMargin: 6 }
            Item { Layout.fillWidth: true }
            StudioButton {
                visible: recoveryController.snapshots.length > 0
                text: "可恢复 " + recoveryController.snapshots.length; iconName: "history"; quiet: true
                tip: "自动保存的工程快照"
                enabled: !recorder.busy && !exporter.busy
                onClicked: recoveryPanel.open()
            }
            StudioButton { text: "最近工程"; iconName: "folder"; quiet: true; enabled: !recorder.busy && !exporter.busy; onClicked: recentProjectsPanel.open() }
            StudioButton {
                text: project.hasProject ? "继续编辑" : "打开工程"
                iconName: project.hasProject ? "edit" : "folder"
                enabled: !recorder.busy
                onClicked: { root.editing = true; if (!project.hasProject) editingView.requestOpen() }
            }
            StudioButton { iconName: "settings"; tip: "设置"; quiet: true; onClicked: settingsDialog.open() }
        }

        // Preview stage: live capture, review playback, or an idle source summary.
        StudioCard {
            id: stage
            anchors.top: recorderHeader.bottom; anchors.topMargin: 18
            anchors.bottom: island.top; anchors.bottomMargin: 22
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - 48, (height - 2) * 16 / 9 + 2, 1100)
            radius: StudioTheme.radiusFloating
            color: root.reviewing || recorder.busy ? "#141518" : StudioTheme.surface
            Item {
                anchors.fill: parent; anchors.margins: 1
                clip: true
                VideoOutput { id: live; anchors.fill: parent; visible: !root.reviewing && recorder.busy; fillMode: VideoOutput.PreserveAspectFit }
                VideoOutput { id: review; anchors.fill: parent; visible: root.reviewing; fillMode: VideoOutput.PreserveAspectFit }
            }
            Column {
                anchors.centerIn: parent; spacing: 10
                visible: !root.reviewing && !recorder.busy
                width: Math.min(parent.width - 48, 420)
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 76; height: 76; radius: 24; color: StudioTheme.well
                    StudioIcon { anchors.centerIn: parent; width: 38; height: 38; iconName: recorderPage.currentKind === "window" ? "window" : "monitor"; color: StudioTheme.text }
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; topPadding: 10; text: recorder.sources.length > 0 ? "准备录制" : "没有可用的录制来源"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
                    text: recorderPage.currentSource ? recorderPage.currentSource.label : "点击下方“显示器”或“窗口”刷新来源"
                    color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body
                }
                Rectangle {
                    visible: root.hasCaptureRegion
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 26; radius: 13; width: regionBadge.implicitWidth + 22
                    color: "#1aff453b"
                    Text { id: regionBadge; anchors.centerIn: parent; color: StudioTheme.recording; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; font.weight: Font.DemiBold
                        text: "仅录制区域 " + Math.round(recorder.captureRegion.width * 100) + "% × " + Math.round(recorder.captureRegion.height * 100) + "%" }
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; topPadding: 6; wrapMode: Text.Wrap
                    text: "开始后这里显示实时画面，结束后自动进入剪辑"
                    color: StudioTheme.faint; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper
                }
            }
            // Live state pill.
            Rectangle {
                visible: recorder.busy || root.reviewing
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 14
                height: 30; radius: 15; width: pillRow.implicitWidth + 24
                color: "#cc141518"
                Row {
                    id: pillRow
                    anchors.centerIn: parent; spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter
                        color: recorder.recording && !recorder.paused ? StudioTheme.recording : recorder.paused ? StudioTheme.amber : StudioTheme.success
                        SequentialAnimation on opacity { running: recorder.recording && !recorder.paused; loops: Animation.Infinite; NumberAnimation { to: .35; duration: 650 } NumberAnimation { to: 1; duration: 650 } }
                    }
                    Text {
                        text: root.reviewing ? "录制回看" : recorder.paused ? "已暂停 · " + root.clock(recorder.duration) : recorder.recording ? "录制中 · " + root.clock(recorder.duration) : "正在保存…"
                        color: "white"; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            // Camera bubble mirrors the selected camera while recording is configured.
            Rectangle {
                visible: cameraRecorder.enabled && !root.reviewing
                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16
                width: Math.min(196, stage.width * .24); height: width * 3 / 4
                radius: 14; color: "#1b1c20"; border.color: "#33ffffff"; clip: true
                VideoOutput {
                    id: cameraLive
                    anchors.fill: parent; anchors.margins: 2
                    fillMode: VideoOutput.PreserveAspectCrop
                    transform: Scale { origin.x: cameraLive.width / 2; xScale: cameraRecorder.mirror ? -1 : 1 }
                }
                Text { anchors.centerIn: parent; visible: cameraRecorder.error !== ""; width: parent.width - 16; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; text: cameraRecorder.error; color: "#ffb4ae"; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
            }
            // Review transport.
            Rectangle {
                visible: root.reviewing
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14
                height: 44; radius: 14; color: "#cc141518"
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 14; spacing: 10
                    StudioButton {
                        iconName: playback.playbackState === MediaPlayer.PlayingState ? "pause" : "play"; quiet: true; compact: true
                        tip: playback.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"
                        contentItem: StudioIcon { name: parent.iconName; width: 16; height: 16; color: "white" }
                        onClicked: playback.playbackState === MediaPlayer.PlayingState ? playback.pause() : playback.play()
                    }
                    StudioSlider { Layout.fillWidth: true; from: 0; to: Math.max(1, playback.duration); value: playback.position; onMoved: playback.position = value }
                    Text { text: root.clock(playback.position) + " / " + root.clock(playback.duration); color: "#d8d8dc"; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper }
                }
            }
        }

        // Floating tool island modelled on the reference recorder bar.
        StudioCard {
            id: island
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: statusLine.top; anchors.bottomMargin: 10
            width: islandRow.implicitWidth + 28
            height: 88
            radius: 28
            RowLayout {
                id: islandRow
                anchors.centerIn: parent
                spacing: 6

                // Setup controls.
                RowLayout {
                    visible: !recorderPage.active
                    spacing: 2
                    SourceTile {
                        text: "显示器"; iconName: "monitor"
                        current: recorderPage.currentKind === "screen"
                        enabled: !recorder.busy
                        tip: "录制整个显示器"
                        onClicked: { if (sourcePopover.kind !== "screen") sourcePopover.closedAt = 0; sourcePopover.kind = "screen"; sourcePopover.toggle() }
                    }
                    SourceTile {
                        text: "窗口"; iconName: "window"
                        current: recorderPage.currentKind === "window"
                        enabled: !recorder.busy
                        tip: "录制单个应用窗口"
                        onClicked: { if (sourcePopover.kind !== "window") sourcePopover.closedAt = 0; sourcePopover.kind = "window"; sourcePopover.toggle() }
                    }
                    SourceTile {
                        text: "区域"; iconName: "region"
                        current: root.hasCaptureRegion
                        enabled: !recorder.busy && recorderPage.currentSource !== null
                        tip: root.hasCaptureRegion ? "已设置录制区域，点击调整" : "在所选显示器或窗口中框选一部分录制"
                        onClicked: regionPicker.open()
                    }
                }
                IslandSeparator { visible: !recorderPage.active }
                RowLayout {
                    id: inputGroup
                    visible: !recorderPage.active
                    spacing: 8
                    InputCircle { iconName: "mic"; on: recorder.microphoneEnabled; level: recorder.microphoneEnabled ? recorder.microphoneLevel : -1; tip: recorder.microphoneEnabled ? "麦克风已开启" : "麦克风已关闭"; onClicked: micPopover.toggle() }
                    InputCircle { iconName: "camera"; on: cameraRecorder.enabled; tip: cameraRecorder.enabled ? "摄像头已开启" : "摄像头已关闭"; onClicked: cameraPopover.toggle() }
                    InputCircle { iconName: "speaker"; on: recorder.systemAudioEnabled; enabled: !recorder.busy; tip: recorder.systemAudioEnabled ? "系统声音已开启（点击关闭）" : "系统声音已关闭（点击开启）"; onClicked: recorder.systemAudioEnabled = !recorder.systemAudioEnabled }
                }
                IslandSeparator { visible: !recorderPage.active }
                AbstractButton {
                    id: saveCard
                    visible: !recorderPage.active
                    implicitWidth: recorderPage.compactBar ? 54 : 196
                    implicitHeight: 60
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    enabled: !recorder.busy
                    Accessible.name: "保存到 " + recorder.outputDirectory
                    ToolTip.visible: hovered
                    ToolTip.delay: 450
                    ToolTip.text: recorder.outputDirectory
                    onClicked: savePopover.toggle()
                    background: Item {
                        Rectangle { anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: -2; radius: 18; color: "#0c141a28" }
                        Rectangle { anchors.fill: parent; radius: 18; color: saveCard.down ? StudioTheme.surfaceAlt : StudioTheme.surface; border.color: saveCard.hovered ? StudioTheme.border : StudioTheme.hairline; border.width: saveCard.visualFocus ? 2 : 1 }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        StudioIcon { name: "folder"; width: 26; height: 26; color: StudioTheme.text; Layout.leftMargin: recorderPage.compactBar ? 14 : 16 }
                        ColumnLayout {
                            visible: !recorderPage.compactBar
                            spacing: 1
                            Layout.fillWidth: true
                            Text { text: "保存到"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: { var parts = recorder.outputDirectory.replace(/[\\\/]+$/, "").split(/[\\\/]/); return parts.length ? parts[parts.length - 1] : "" }
                                color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold
                            }
                        }
                        StudioIcon { visible: !recorderPage.compactBar; name: "chevronDown"; width: 14; height: 14; color: StudioTheme.muted; Layout.rightMargin: 14 }
                    }
                }

                AbstractButton {
                    id: specCard
                    visible: !recorderPage.active
                    implicitWidth: recorderPage.compactBar ? 54 : specLabels.implicitWidth + 30
                    implicitHeight: 60
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    enabled: !recorder.busy
                    Accessible.name: "录制规格"
                    ToolTip.visible: hovered && recorderPage.compactBar
                    ToolTip.text: recorder.frameRate + " FPS · " + recorder.videoCodec.toUpperCase()
                    onClicked: specPopover.toggle()
                    background: Rectangle { radius: 18; color: specCard.down ? StudioTheme.wellHover : specCard.hovered ? StudioTheme.well : "transparent"; border.width: specCard.visualFocus ? 2 : 0; border.color: StudioTheme.accent }
                    contentItem: Item {
                        StudioIcon { visible: recorderPage.compactBar; anchors.centerIn: parent; name: "sliders"; width: 24; height: 24 }
                        ColumnLayout {
                            id: specLabels
                            visible: !recorderPage.compactBar
                            anchors.centerIn: parent
                            spacing: 1
                            Text { text: recorder.frameRate + " FPS"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                            Text { text: (({ source: "原始", "4k": "4K", "1080p": "1080p" })[recorder.resolutionMode] || recorder.resolutionMode) + " · " + (recorder.videoCodec === "hevc" ? "HEVC" : "H.264"); color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; Layout.alignment: Qt.AlignHCenter }
                        }
                    }
                }

                // Active recording controls.
                RowLayout {
                    visible: recorderPage.active
                    spacing: 10
                    Layout.leftMargin: 8
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: recorder.paused ? StudioTheme.amber : StudioTheme.recording
                        SequentialAnimation on opacity { running: recorder.recording && !recorder.paused; loops: Animation.Infinite; NumberAnimation { to: .35; duration: 650 } NumberAnimation { to: 1; duration: 650 } }
                    }
                    ColumnLayout {
                        spacing: 0
                        Text { text: root.clock(recorder.duration); color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: 22; font.weight: Font.DemiBold }
                        Text { text: recorder.paused ? "已暂停" : "正在录制"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
                    }
                    Item { implicitWidth: 8 }
                    StudioButton {
                        text: "标记 " + recorder.recordingMarkers.length
                        iconName: "marker"
                        implicitHeight: 44
                        enabled: recorder.recording && !recorder.paused
                        tip: "在当前时间添加录制标记"
                        onClicked: recorder.addRecordingMarker()
                    }
                    InputCircle {
                        iconName: recorder.paused ? "play" : "pause"; on: true; showStatus: false; tip: recorder.paused ? "继续录制" : "暂停录制"
                        onClicked: recorder.paused ? recorder.resume() : recorder.pause()
                    }
                    InputCircle {
                        iconName: "more"; on: true; showStatus: false; tip: "更多录制操作"
                        onClicked: recordingMenu.popup()
                        StudioMenu {
                            id: recordingMenu
                            StudioMenuItem { text: "重新录制"; iconName: "refresh"; onTriggered: { root.recordingDestructiveAction = "restart"; confirmRecordingAction.open() } }
                            StudioMenuItem { text: "放弃录制"; iconName: "trash"; danger: true; onTriggered: { root.recordingDestructiveAction = "discard"; confirmRecordingAction.open() } }
                        }
                    }
                }

                StudioButton {
                    iconName: "memo"; tip: "悬浮备忘录"
                    implicitWidth: 52; implicitHeight: 52
                    Layout.leftMargin: 4
                    background: Rectangle { radius: 16; color: parent.down ? StudioTheme.wellHover : parent.hovered ? StudioTheme.well : StudioTheme.surfaceAlt; border.color: StudioTheme.hairline }
                    onClicked: recorderMemo.toggle()
                }

                // Primary action: start, or stop and save.
                AbstractButton {
                    id: recordButton
                    readonly property bool stopping: recorderPage.active
                    implicitWidth: recordLabel.implicitWidth + 64
                    implicitHeight: 52
                    Layout.leftMargin: 4
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    text: stopping ? "停止并保存" : recorder.busy ? "正在保存…" : "开始录制"
                    enabled: stopping || (root.canBeginRecording && recorder.readiness.canStart === true)
                    opacity: enabled ? 1 : .4
                    Accessible.name: text
                    onClicked: {
                        if (recorder.recording || recorder.paused) recorder.stop()
                        else root.beginRecording()
                    }
                    background: Rectangle {
                        radius: 26
                        color: recordButton.stopping ? (recordButton.down ? StudioTheme.accentPressed : recordButton.hovered ? StudioTheme.accentHover : StudioTheme.accent)
                            : (recordButton.down ? "#e0352c" : recordButton.hovered ? "#ff5a51" : StudioTheme.recording)
                        border.width: recordButton.visualFocus ? 2 : 0
                        border.color: StudioTheme.accent
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent; spacing: 10
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14; radius: recordButton.stopping ? 3 : 7
                                color: recordButton.stopping ? StudioTheme.accentInk : "white"
                            }
                            Text { id: recordLabel; text: recordButton.text; color: recordButton.stopping ? StudioTheme.accentInk : "white"; font.family: StudioTheme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
        }

        Text {
            id: statusLine
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.bottomMargin: 14
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            readonly property bool silent: !recorder.busy && !recorder.systemAudioEnabled && !recorder.microphoneEnabled
            readonly property string blocker: !recorder.busy && recorder.readiness.canStart !== true ? (recorder.readiness.reason || "") : ""
            text: recorder.error !== "" ? recorder.error : playback.error !== MediaPlayer.NoError ? playback.errorString : blocker !== "" ? blocker : silent ? "系统声音和麦克风都已关闭，本次录制将没有声音" : recorder.status
            color: recorder.error !== "" ? StudioTheme.danger : blocker !== "" || silent ? StudioTheme.warning : StudioTheme.muted
            font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && truncated
            ToolTip.text: text
        }
    }
}
