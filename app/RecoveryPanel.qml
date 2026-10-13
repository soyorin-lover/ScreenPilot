import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Auto-recovery snapshots. Restoring never overwrites unsaved edits: when the
// current project is dirty the panel asks the user to save or discard first.
StudioDialog {
    id: panel
    objectName: "recoveryPanel"
    title: "恢复工程"
    subtitle: "自动快照保存在独立位置，恢复后另存为正式工程即可继续"
    iconName: "history"
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(560, parent ? parent.height - 40 : 560)
    // Emitted after a snapshot was restored so the host can show the editor.
    signal restored()
    property url pendingRestore: ""
    property var beforeRestore: function() {}
    onClosed: pendingRestore = ""
    function formatTime(iso) {
        const d = new Date(iso)
        return isNaN(d.getTime()) ? iso : d.toLocaleString(Qt.locale(), "yyyy-MM-dd HH:mm")
    }
    function restore(url) {
        if (project.dirty) { pendingRestore = url; return }
        beforeRestore()
        if (recoveryController.restoreSnapshot(url)) { pendingRestore = ""; panel.close(); panel.restored() }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Rectangle {
            visible: panel.pendingRestore.toString() !== ""
            Layout.fillWidth: true
            implicitHeight: dirtyRow.implicitHeight + 24
            radius: StudioTheme.radiusGroup
            color: "#1ff2ad47"
            RowLayout {
                id: dirtyRow
                anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 12
                spacing: 10
                StudioIcon { name: "warning"; width: 18; height: 18; color: StudioTheme.warning }
                Text { text: "当前工程有未保存修改。请先保存，或放弃修改后再恢复。"; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
                StudioButton { text: "放弃并恢复"; danger: true; compact: true; onClicked: { const url = panel.pendingRestore; panel.pendingRestore = ""; project.discardChanges(); panel.restore(url) } }
                StudioButton { text: "取消"; compact: true; onClicked: panel.pendingRestore = "" }
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 6
            model: recoveryController.snapshots
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                width: ListView.view.width; height: 62
                radius: StudioTheme.radiusGroup
                color: hover.hovered ? StudioTheme.well : "transparent"
                border.color: StudioTheme.hairline
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 12
                    Rectangle {
                        width: 38; height: 38; radius: 11; color: entry.index === 0 ? StudioTheme.accent : StudioTheme.well
                        StudioIcon { anchors.centerIn: parent; name: "history"; width: 18; height: 18; color: entry.index === 0 ? StudioTheme.accentInk : StudioTheme.text }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Text { text: entry.modelData.name + (entry.index === 0 ? " · 最新" : ""); color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold }
                        Text { text: panel.formatTime(entry.modelData.modified); color: StudioTheme.muted; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.caption }
                    }
                    StudioButton { text: "恢复"; compact: true; primary: entry.index === 0; enabled: !recorder.busy && !exporter.busy; onClicked: panel.restore(entry.modelData.url) }
                    StudioButton { iconName: "trash"; quiet: true; compact: true; tip: "删除此快照"; onClicked: recoveryController.removeSnapshot(entry.modelData.url) }
                }
            }
            Column {
                anchors.centerIn: parent; spacing: 8
                visible: list.count === 0
                StudioIcon { name: "history"; width: 30; height: 30; color: StudioTheme.faint; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: recoveryController.enabled ? "暂无恢复快照。编辑停止约 60 秒后会自动保存。" : "自动恢复已关闭，可在设置中开启。"; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
            }
        }
        StudioHelp { text: "快照只用于恢复编辑中的工程，不能修复录制中途中断的视频文件。" }
        Text { visible: recoveryController.error !== ""; text: recoveryController.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
    footer: StudioDialogFooter {
        StudioCheckBox { Layout.fillWidth: false; implicitWidth: 190; text: "自动保存快照"; checked: recoveryController.enabled; onToggled: recoveryController.enabled = checked }
        Item { Layout.fillWidth: true }
        StudioButton { text: "立即保存快照"; iconName: "save"; enabled: project.hasProject && !recoveryController.blocked; onClicked: recoveryController.snapshotNow() }
        StudioButton { text: "完成"; primary: true; onClicked: panel.close() }
    }
}
