import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
StudioDialog {
    id: panel
    objectName: "scenePresetPanel"
    title: qsTr("场景预设")
    subtitle: qsTr("复用画布、摄像头布局、取景和自动缩放设置；尚未包含音频混音和片头。")
    iconName: "presets"
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(580, parent ? parent.height - 40 : 580)
    property string selectedId: ""
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            spacing: 8
            StudioTextField { id: sceneName; objectName: "scenePresetName"; placeholderText: qsTr("场景名称"); Layout.fillWidth: true }
            StudioButton { text: qsTr("保存新场景"); iconName: "plus"; primary: true; onClicked: if (scenePresets.savePreset(sceneName.text)) sceneName.clear() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: scenePresets.presets
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: list.width
                height: 48
                leftPadding: 14
                rightPadding: 14
                highlighted: panel.selectedId === modelData.id
                onClicked: { panel.selectedId = modelData.id; sceneName.text = modelData.name }
                onDoubleClicked: scenePresets.applyPreset(modelData.id)
                contentItem: RowLayout {
                    spacing: 10
                    StudioIcon { name: "presets"; width: 18; height: 18; color: row.highlighted ? StudioTheme.text : StudioTheme.muted }
                    Text { text: row.modelData.name; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: row.highlighted ? Font.DemiBold : Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Rectangle {
                        visible: scenePresets.defaultPresetId === row.modelData.id
                        implicitWidth: defaultLabel.implicitWidth + 14; implicitHeight: 22; radius: 11; color: StudioTheme.well
                        Text { id: defaultLabel; anchors.centerIn: parent; text: qsTr("新录制默认"); color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
                    }
                }
                background: Rectangle { radius: StudioTheme.radiusGroup; color: row.highlighted ? StudioTheme.selection : row.hovered ? StudioTheme.well : "transparent"; border.color: row.highlighted ? "transparent" : StudioTheme.hairline }
            }
            ScrollBar.vertical: ScrollBar {}
            Column {
                anchors.centerIn: parent; spacing: 8
                visible: list.count === 0
                StudioIcon { name: "presets"; width: 30; height: 30; color: StudioTheme.faint; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: qsTr("将当前录制的外观保存为第一个场景。"); color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
            }
        }
        StudioHelp { text: scenePresets.defaultPresetId === "" ? qsTr("尚未选择默认场景。双击场景可直接应用。") : qsTr("默认场景仅应用于新录制。双击场景可直接应用。") }
        Text { color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; text: scenePresets.error; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    footer: StudioDialogFooter {
        StudioButton { text: qsTr("删除"); iconName: "trash"; danger: true; quiet: true; enabled: panel.selectedId !== ""; onClicked: if (scenePresets.deletePreset(panel.selectedId)) { panel.selectedId = ""; sceneName.clear() } }
        StudioButton { text: qsTr("取消默认"); quiet: true; enabled: scenePresets.defaultPresetId !== ""; onClicked: scenePresets.resetDefault() }
        Item { Layout.fillWidth: true }
        StudioButton { text: qsTr("设为默认"); enabled: panel.selectedId !== ""; onClicked: scenePresets.setDefaultPreset(panel.selectedId) }
        StudioButton { text: qsTr("用当前设置更新"); enabled: panel.selectedId !== ""; onClicked: scenePresets.updatePreset(panel.selectedId, sceneName.text) }
        StudioButton { text: qsTr("应用"); primary: true; enabled: panel.selectedId !== ""; onClicked: scenePresets.applyPreset(panel.selectedId) }
    }
}
