import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: root
    width: 1080
    height: 740
    minimumWidth: 860
    minimumHeight: 640
    visible: true
    title: "ScreenPilot"
    color: "#111214"
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 14
    palette.window: "#17181b"
    palette.base: "#202126"
    palette.button: "#292a2f"
    palette.buttonText: "#eeece7"
    palette.text: "#eeece7"
    palette.windowText: "#eeece7"
    palette.highlight: "#e4e0d8"
    palette.highlightedText: "#18191c"
    property bool reviewing: false
    property bool closeAfterSave: false
    property url reviewedFile: ""

    function clock(ms) {
        var seconds = Math.floor(ms / 1000)
        return Math.floor(seconds / 60).toString().padStart(2, "0") + ":" + (seconds % 60).toString().padStart(2, "0")
    }
    onClosing: function(event) {
        if (recorder.busy) {
            event.accepted = false
            closeAfterSave = true
            recorder.stop()
        }
    }
    Connections {
        target: recorder
        function onChanged() {
            if (root.closeAfterSave && !recorder.busy) root.close()
            if (!recorder.busy && recorder.lastRecording.toString() !== ""
                    && recorder.lastRecording.toString() !== root.reviewedFile.toString()) {
                root.reviewedFile = recorder.lastRecording
                playback.source = root.reviewedFile
                root.reviewing = true
            }
        }
    }
    Component.onCompleted: recorder.previewSink = live.videoSink
    FolderDialog {
        id: folderPicker
        title: "选择录制保存文件夹"
        onAccepted: recorder.outputDirectory = selectedFolder.toString()
    }
    MediaPlayer {
        id: playback
        videoOutput: review
        audioOutput: AudioOutput { volume: 0.7 }
    }

    component ActionButton: Button {
        id: action
        property bool primary: false
        implicitHeight: 42
        leftPadding: 18
        rightPadding: 18
        contentItem: Text {
            text: action.text
            font: action.font
            color: !action.enabled ? "#66676d" : action.primary ? "#17181b" : "#e8e5de"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: action.primary ? (action.down ? "#c8c4bc" : "#e9e5dd")
                                  : action.hovered ? "#34353b" : "#26272c"
            opacity: action.enabled ? 1 : 0.5
            border.color: action.primary ? "transparent" : "#3a3b41"
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20
        RowLayout {
            spacing: 12
            Rectangle {
                width: 34; height: 34; radius: 10; color: "#e9e5dd"
                Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 4; color: "#222328" }
            }
            ColumnLayout {
                spacing: 1
                Label { text: "ScreenPilot"; color: "#f1eee8"; font.pixelSize: 21; font.bold: true }
                Label { text: "录制，让演示更清晰"; color: "#929298"; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: 112; implicitHeight: 30; radius: 15; color: "#232429"
                Label { anchors.centerIn: parent; text: "Windows · 开发版"; color: "#aaa8a2"; font.pixelSize: 11 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20
            Rectangle {
                Layout.preferredWidth: 294
                Layout.fillHeight: true
                color: "#1b1c20"; radius: 14; border.color: "#2c2d33"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 16
                    Label { text: "新建录制"; font.pixelSize: 20; font.bold: true; color: "#eeece7" }
                    Label { text: "录制来源"; color: "#aaa8a3"; font.pixelSize: 12 }
                    ComboBox {
                        id: sourceSelector
                        Layout.fillWidth: true
                        model: recorder.sources
                        textRole: "label"
                        currentIndex: recorder.selectedSource
                        enabled: !recorder.busy
                        onActivated: recorder.selectedSource = currentIndex
                    }
                    ActionButton { text: "刷新屏幕与窗口"; Layout.fillWidth: true; enabled: !recorder.busy; onClicked: recorder.refreshSources() }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#303137" }
                    Label { text: "声音"; color: "#aaa8a3"; font.pixelSize: 12 }
                    Switch {
                        text: "麦克风"
                        checked: recorder.microphoneEnabled
                        enabled: !recorder.busy
                        onToggled: recorder.microphoneEnabled = checked
                    }
                    Label { text: "使用 Windows 默认麦克风"; color: "#85858d"; font.pixelSize: 11 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#303137" }
                    Label { text: "保存位置"; color: "#aaa8a3"; font.pixelSize: 12 }
                    Label { text: recorder.outputDirectory; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideMiddle; color: "#c4c1ba"; font.pixelSize: 12 }
                    ActionButton { text: "更改文件夹"; Layout.fillWidth: true; enabled: !recorder.busy; onClicked: folderPicker.open() }
                    Item { Layout.fillHeight: true }
                    Label { text: "MP4 视频 · 自动保存"; color: "#aaa8a3"; font.pixelSize: 12 }
                    ActionButton {
                        Layout.fillWidth: true
                        primary: !recorder.busy
                        text: recorder.recording ? "■  停止并保存" : recorder.busy ? "正在保存…" : "●  开始录制"
                        enabled: recorder.recording || (!recorder.busy && recorder.sources.length > 0)
                        onClicked: {
                            if (recorder.recording) recorder.stop()
                            else { playback.stop(); root.reviewing = false; recorder.start() }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                RowLayout {
                    Label { text: root.reviewing ? "录制回看" : "画面预览"; color: "#dedbd4"; font.pixelSize: 16; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Rectangle { width: 7; height: 7; radius: 4; color: recorder.recording ? "#ff514a" : "#76777d" }
                    Label { text: recorder.busy ? root.clock(recorder.duration) : "准备就绪"; color: "#aaa8a3"; font.family: "Consolas" }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#0b0c0e"; radius: 14; border.color: "#303137"; clip: true
                    VideoOutput { id: live; anchors.fill: parent; anchors.margins: 1; visible: !root.reviewing && recorder.busy; fillMode: VideoOutput.PreserveAspectFit }
                    VideoOutput { id: review; anchors.fill: parent; anchors.margins: 1; visible: root.reviewing; fillMode: VideoOutput.PreserveAspectFit }
                    Column {
                        anchors.centerIn: parent; spacing: 12
                        visible: !root.reviewing && !recorder.busy
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 68; height: 46; radius: 8; color: "transparent"; border.color: "#68686e"; border.width: 2
                            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 52; width: 26; height: 2; color: "#68686e" }
                        }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; topPadding: 12; text: "从一次录制开始"; color: "#ccc9c2"; font.pixelSize: 18 }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: "选择屏幕或窗口，开始后在这里预览"; color: "#777880"; font.pixelSize: 12 }
                    }
                }
                RowLayout {
                    visible: root.reviewing
                    ActionButton { text: playback.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"; onClicked: playback.playbackState === MediaPlayer.PlayingState ? playback.pause() : playback.play() }
                    Slider { Layout.fillWidth: true; from: 0; to: Math.max(1, playback.duration); value: playback.position; onMoved: playback.position = value }
                    Label { text: root.clock(playback.position) + " / " + root.clock(playback.duration); color: "#aaa8a3"; font.family: "Consolas" }
                }
                RowLayout {
                    ActionButton { text: "打开保存文件夹"; onClicked: recorder.openOutputDirectory() }
                    ActionButton { text: "用系统播放器打开"; enabled: recorder.lastRecording.toString() !== ""; onClicked: recorder.openLastRecording() }
                    Item { Layout.fillWidth: true }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: recorder.error !== "" ? recorder.error : playback.error !== MediaPlayer.NoError ? playback.errorString : recorder.status
            color: recorder.error !== "" ? "#ff8e85" : "#99989e"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
    }
}
