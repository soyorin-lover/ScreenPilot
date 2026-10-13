import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Mix settings are project data (saved, undoable, exported); the monitor
// volume only changes what you hear while editing.
ColumnLayout {
    id: root
    // Editor owning the processor mute binding; monitorMuted is folded into it.
    property var host: null
    readonly property bool monitorMuted: host ? host.monitorMuted : false
    readonly property var mix: project.audioSettings
    readonly property bool hasSystem: project.systemAudioSource.toString() !== ""
    readonly property bool hasMicrophone: project.microphoneAudioSource.toString() !== ""
    Layout.fillWidth: true
    spacing: 22

    component TrackCard: Rectangle {
        id: card
        property string title: ""
        property string iconName: ""
        property bool available: true
        property real volume: 1
        property bool muted: false
        signal volumeMoved(real v)
        signal muteToggled(bool m)
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 24
        radius: StudioTheme.radiusGroup
        color: StudioTheme.surfaceAlt
        border.color: StudioTheme.hairline
        opacity: available ? 1 : .55
        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
            spacing: 8
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Rectangle {
                    width: 30; height: 30; radius: 9
                    color: card.available && !card.muted ? StudioTheme.accent : StudioTheme.well
                    StudioIcon { anchors.centerIn: parent; name: card.iconName; width: 16; height: 16; color: card.available && !card.muted ? StudioTheme.accentInk : StudioTheme.muted }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Text { text: card.title; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold }
                    Text { text: !card.available ? "此工程没有这条音轨" : card.muted ? "已静音" : Math.round(card.volume * 100) + "%"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
                }
                StudioButton {
                    enabled: card.available
                    iconName: "volume"; compact: true
                    checked: card.muted
                    tip: card.muted ? "取消静音" : "静音"
                    onClicked: card.muteToggled(!card.muted)
                    Rectangle { visible: card.muted; anchors.centerIn: parent; width: 2; height: 20; radius: 1; rotation: -45; color: StudioTheme.text }
                }
            }
            StudioSlider {
                Layout.fillWidth: true
                enabled: card.available && !card.muted
                from: 0; to: 1; stepSize: .01
                value: card.volume
                onMoved: card.volumeMoved(value)
            }
        }
    }

    StudioSection {
        title: "独立音轨混音"; icon: "waveform"
        helper: root.hasSystem || root.hasMicrophone ? "系统声音与麦克风分开录制，可分别调整。设置随工程保存，预览和导出一致。" : "这是旧工程或录制时未分离音轨，播放与导出使用视频内嵌的混合声音。"
        TrackCard {
            title: "系统声音"; iconName: "speaker"
            available: root.hasSystem
            volume: Number(root.mix.systemVolume !== undefined ? root.mix.systemVolume : .82)
            muted: root.mix.systemMuted === true
            onVolumeMoved: v => project.setAudioOption("systemVolume", v)
            onMuteToggled: m => project.setAudioOption("systemMuted", m)
        }
        TrackCard {
            title: "麦克风"; iconName: "mic"
            available: root.hasMicrophone
            volume: Number(root.mix.microphoneVolume !== undefined ? root.mix.microphoneVolume : .9)
            muted: root.mix.microphoneMuted === true
            onVolumeMoved: v => project.setAudioOption("microphoneVolume", v)
            onMuteToggled: m => project.setAudioOption("microphoneMuted", m)
        }
    }

    StudioDivider {}

    StudioSection {
        title: "监听"; icon: "volume"
        helper: "只影响剪辑时听到的音量，不修改工程、导出或系统音量。"
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            StudioButton {
                iconName: "volume"; compact: true; checked: root.monitorMuted
                tip: root.monitorMuted ? "取消监听静音" : "监听静音"
                onClicked: if (root.host) root.host.monitorMuted = !root.host.monitorMuted
                Rectangle { visible: root.monitorMuted; anchors.centerIn: parent; width: 2; height: 20; radius: 1; rotation: -45; color: StudioTheme.text }
            }
            StudioSlider { Layout.fillWidth: true; from: 0; to: 1; stepSize: .01; value: previewAudioProcessor.volume; enabled: !root.monitorMuted; onMoved: previewAudioProcessor.volume = value }
            Text { text: Math.round(previewAudioProcessor.volume * 100) + "%"; color: StudioTheme.muted; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.helper; Layout.preferredWidth: 36 }
        }
        Text { visible: previewAudioProcessor.error !== ""; text: previewAudioProcessor.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
}
