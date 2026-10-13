import QtCore
import QtQuick
import QtQuick.Controls

// The only window on screen while recording, modelled on the reference
// RecordingBar: breathing state dot, elapsed time, microphone level, marker,
// memo, pause/resume, more (restart/discard with confirmation) and stop.
// It is excluded from capture; menus and confirmations unfold inside this same
// window (above the island) so no separate, capturable popup window appears.
Window {
    id: bar
    objectName: "recordingBar"
    title: "ScreenPilot 录制"
    color: "transparent"
    visible: false
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus

    signal stopRequested()
    signal memoRequested()
    signal restartConfirmed()
    signal discardConfirmed()

    // "bar", "menu", "restart" or "discard".
    property string mode: "bar"
    property bool captureExcluded: false
    readonly property int margin: 14
    readonly property bool active: recorder.recording || recorder.paused
    readonly property bool saving: recorder.busy && !active && starting === false
    property bool starting: true

    width: Math.max(island.width, panel.visible ? panel.width : 0) + margin * 2
    height: island.height + margin * 2 + (panel.visible ? panel.height + 8 : 0)

    Settings {
        id: placement
        category: "recordingBar"
        property real x: NaN
        property real y: NaN
    }

    function clock(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        const mm = Math.floor(seconds / 60) % 60, ss = seconds % 60
        const text = mm.toString().padStart(2, "0") + ":" + ss.toString().padStart(2, "0")
        return seconds >= 3600 ? Math.floor(seconds / 3600) + ":" + text : text
    }
    // Grows or shrinks upward around the island's fixed bottom edge.
    function setMode(next) {
        const bottom = y + height, center = x + width / 2
        mode = next
        y = bottom - height
        x = center - width / 2
    }
    function insideAnyScreen(px, py) {
        for (const s of Qt.application.screens)
            if (px >= s.virtualX && py >= s.virtualY && px < s.virtualX + s.width && py < s.virtualY + s.height)
                return true
        return false
    }
    function present(targetScreen) {
        mode = "bar"
        starting = true
        if (!visible) {
            const savedOk = !isNaN(placement.x) && !isNaN(placement.y)
                && insideAnyScreen(placement.x + 40, placement.y + 40)
            if (savedOk) {
                x = placement.x
                y = placement.y
            } else {
                // Bottom centre of the work area (QScreen), above the taskbar.
                const first = Qt.application.screens[0]
                const area = targetScreen && targetScreen.availableGeometry ? targetScreen.availableGeometry
                    : Qt.rect(first.virtualX, first.virtualY, first.width, first.height - 48)
                x = area.x + (area.width - width) / 2
                y = area.y + area.height - height - 48
            }
        }
        // Before the first frame is captured: the bar must never appear in it.
        captureExcluded = recorder.excludeFromCapture(bar)
        show()
        raise()
    }
    function dismiss() {
        placement.x = x
        placement.y = y
        mode = "bar"
        hide()
    }

    Connections {
        target: recorder
        function onChanged() {
            if (bar.active) bar.starting = false
            if (!bar.active && bar.mode !== "bar") bar.setMode("bar")
        }
    }

    component BarButton: AbstractButton {
        id: button
        property string iconName: ""
        property color iconColor: StudioTheme.text
        implicitWidth: 36; implicitHeight: 36
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        opacity: enabled ? 1 : .35
        Accessible.name: text
        background: Rectangle {
            radius: width / 2
            color: button.down ? StudioTheme.wellHover : button.hovered ? StudioTheme.well : "transparent"
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Item {
            StudioIcon { anchors.centerIn: parent; name: button.iconName; width: 18; height: 18; color: button.iconColor }
        }
    }
    component PanelAction: AbstractButton {
        id: action
        property string iconName: ""
        property bool danger: false
        implicitWidth: 232; implicitHeight: 34
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        background: Rectangle { radius: StudioTheme.radiusCompact; color: action.down ? StudioTheme.wellHover : action.hovered ? StudioTheme.well : "transparent" }
        contentItem: Row {
            leftPadding: 10; spacing: 10
            StudioIcon { anchors.verticalCenter: parent.verticalCenter; name: action.iconName; width: 16; height: 16; color: action.danger ? StudioTheme.danger : StudioTheme.text }
            Text { anchors.verticalCenter: parent.verticalCenter; text: action.text; color: action.danger ? StudioTheme.danger : StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
        }
    }

    // Menu and confirmations, above the island.
    StudioCard {
        id: panel
        objectName: "recordingBarPanel"
        visible: bar.mode !== "bar"
        x: (bar.width - width) / 2; y: bar.margin
        width: 256
        height: panelColumn.implicitHeight + 24
        radius: StudioTheme.radiusGroup
        Column {
            id: panelColumn
            x: 12; y: 12; width: parent.width - 24
            spacing: 4
            Column {
                visible: bar.mode === "menu"
                spacing: 2
                PanelAction { text: "重新录制…"; iconName: "refresh"; onClicked: bar.setMode("restart") }
                PanelAction { text: "丢弃这次录制…"; iconName: "trash"; danger: true; onClicked: bar.setMode("discard") }
            }
            Column {
                visible: bar.mode === "restart" || bar.mode === "discard"
                width: parent.width
                spacing: 8
                Text {
                    width: parent.width; wrapMode: Text.Wrap
                    text: bar.mode === "restart" ? "停止当前录制并重新开始？" : "丢弃这次录制？"
                    color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap
                    text: bar.mode === "restart" ? "当前录制将被删除，随后使用相同设置重新开始。" : "当前录制和相关素材将被删除，随后返回录制页。"
                    color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption
                }
                Row {
                    anchors.right: parent.right
                    spacing: 8
                    StudioButton { text: "取消"; compact: true; focusPolicy: Qt.NoFocus; onClicked: bar.setMode("bar") }
                    // One fixed-style button per action: the style never animates
                    // between modes.
                    StudioButton {
                        objectName: bar.mode === "restart" ? "recordingBarConfirm" : ""
                        visible: bar.mode === "restart"
                        text: "重新录制"; compact: true; primary: true
                        focusPolicy: Qt.NoFocus
                        onClicked: { bar.setMode("bar"); bar.restartConfirmed() }
                    }
                    StudioButton {
                        objectName: bar.mode === "discard" ? "recordingBarConfirm" : ""
                        visible: bar.mode === "discard"
                        text: "丢弃"; compact: true; danger: true
                        focusPolicy: Qt.NoFocus
                        onClicked: { bar.setMode("bar"); bar.discardConfirmed() }
                    }
                }
            }
        }
    }

    StudioCard {
        id: island
        x: (bar.width - width) / 2
        y: bar.height - bar.margin - height
        width: content.implicitWidth + 22
        height: 56
        radius: 28
        // Dragging the island background moves the whole bar.
        MouseArea {
            anchors.fill: parent
            onPressed: bar.startSystemMove()
        }
        Row {
            id: content
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9
                rightPadding: 10
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10; height: 10; radius: 5
                    color: recorder.paused ? StudioTheme.amber : bar.active ? StudioTheme.recording : StudioTheme.faint
                    SequentialAnimation on opacity {
                        running: recorder.recording && !recorder.paused
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) parent.opacity = 1
                        NumberAnimation { to: .35; duration: 650 }
                        NumberAnimation { to: 1; duration: 650 }
                    }
                }
                Text {
                    objectName: "recordingBarTime"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(implicitWidth, 50)
                    text: bar.active ? bar.clock(recorder.duration) : bar.saving ? "正在保存…" : "准备中…"
                    color: recorder.paused ? StudioTheme.muted : StudioTheme.text
                    font.family: bar.active ? StudioTheme.monoFamily : StudioTheme.fontFamily
                    font.pixelSize: bar.active ? 16 : StudioTheme.body
                    font.weight: Font.DemiBold
                }
                // Microphone level as five rising bars.
                Row {
                    visible: recorder.microphoneEnabled && bar.active
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3; radius: 1.5
                            height: 6 + index * 2.5
                            color: !recorder.paused && recorder.microphoneLevel > index / 5 ? StudioTheme.success : StudioTheme.well
                        }
                    }
                }
            }
            BarButton {
                objectName: "recordingBarMarker"
                text: "添加录制标记"
                iconName: "marker"
                enabled: recorder.recording && !recorder.paused
                onClicked: recorder.addRecordingMarker()
                Text {
                    visible: recorder.recordingMarkers.length > 0
                    anchors.right: parent.right; anchors.top: parent.top; anchors.rightMargin: 3; anchors.topMargin: 3
                    text: recorder.recordingMarkers.length
                    color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
            }
            BarButton { text: "悬浮备忘录"; iconName: "memo"; onClicked: bar.memoRequested() }
            BarButton {
                objectName: "recordingBarPause"
                text: recorder.paused ? "继续录制" : "暂停录制"
                iconName: recorder.paused ? "play" : "pause"
                enabled: bar.active
                onClicked: recorder.paused ? recorder.resume() : recorder.pause()
            }
            BarButton {
                objectName: "recordingBarMore"
                text: "更多录制操作"
                iconName: "more"
                iconColor: StudioTheme.muted
                enabled: bar.active
                onClicked: bar.setMode(bar.mode === "bar" ? "menu" : "bar")
            }
            AbstractButton {
                id: stopButton
                objectName: "recordingBarStop"
                text: "结束录制"
                Accessible.name: text
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 0
                implicitWidth: 42; implicitHeight: 36
                enabled: bar.active
                opacity: enabled ? 1 : .35
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: bar.stopRequested()
                background: Item {}
                contentItem: Item {
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36; height: 36; radius: 18
                        color: stopButton.down ? "#e0352c" : stopButton.hovered ? "#ff5a51" : StudioTheme.recording
                        Rectangle { anchors.centerIn: parent; width: 11; height: 11; radius: 2.5; color: "white" }
                    }
                }
            }
        }
    }
}
