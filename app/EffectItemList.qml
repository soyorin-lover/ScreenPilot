import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Rows for one effect track. Selection lives in `effects`, not here, so the
// timeline lane and this list always agree. Times shown are output seconds.
ColumnLayout {
    id: root
    property string track: ""
    property var items: []
    property string iconName: "effects"
    // function(item, index) -> string; caller supplies the row title.
    property var describe: function(item, index) { return "片段 " + (index + 1) }
    property string emptyText: "还没有片段，点击上方按钮在播放头添加。"
    readonly property string selectedId: effects.selectedIds[track] || ""
    Layout.fillWidth: true
    spacing: 4
    function startOf(item) { return item.sourceStart !== undefined ? item.sourceStart : item.start !== undefined ? item.start : item.sourceTime }
    function outputLabel(item) {
        const times = effects.outputTimes(startOf(item))
        if (!times || times.length === 0) return "已剪掉"
        return times.map(t => Number(t).toFixed(2) + "s").join(" · ")
    }
    Repeater {
        model: root.items
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            readonly property bool selected: root.selectedId === modelData.id
            Layout.fillWidth: true
            implicitHeight: 40
            leftPadding: 10; rightPadding: 4
            onClicked: effects.select(root.track, modelData.id)
            contentItem: RowLayout {
                spacing: 8
                Rectangle {
                    width: 24; height: 24; radius: 7
                    color: row.selected ? StudioTheme.accent : StudioTheme.well
                    StudioIcon { anchors.centerIn: parent; name: root.iconName; width: 14; height: 14; color: row.selected ? StudioTheme.accentInk : StudioTheme.text }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Text { text: root.describe(row.modelData, row.index); color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: row.selected ? Font.DemiBold : Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { text: "成片 " + root.outputLabel(row.modelData); color: StudioTheme.muted; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.caption; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                StudioButton { iconName: "trash"; quiet: true; compact: true; tip: "删除"; onClicked: effects.remove(root.track, row.modelData.id) }
            }
            background: Rectangle { radius: StudioTheme.radiusControl; color: row.selected ? StudioTheme.selection : row.hovered ? StudioTheme.well : "transparent" }
        }
    }
    StudioHelp { visible: root.items.length === 0; text: root.emptyText }
}
