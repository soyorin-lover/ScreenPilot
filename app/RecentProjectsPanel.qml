import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

StudioDialog {
    id: panel
    objectName: "recentProjectsPanel"
    title: qsTr("最近工程")
    subtitle: qsTr("移除或清空记录不会删除工程文件。")
    iconName: "history"
    width: Math.min(660, parent ? parent.width - 32 : 660)
    height: Math.min(620, parent ? parent.height - 32 : 620)
    property bool expanded: false
    signal openRequested(url folder)
    onOpened: { expanded = false; recentProjectsController.refresh() }
    contentItem: ColumnLayout {
        spacing: 10
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: panel.expanded ? recentProjectsController.projects : recentProjectsController.recentProjects
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry
                required property var modelData
                width: ListView.view.width
                height: 62
                radius: StudioTheme.radiusGroup
                color: entryHover.hovered ? StudioTheme.well : "transparent"
                border.color: StudioTheme.hairline
                HoverHandler { id: entryHover }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10
                    spacing: 12
                    Rectangle {
                        width: 38; height: 38; radius: 11; color: StudioTheme.well
                        StudioIcon { anchors.centerIn: parent; name: "film"; width: 18; height: 18; color: entry.modelData.exists ? StudioTheme.text : StudioTheme.faint }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold; text: entry.modelData.title; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text {
                            color: entry.modelData.exists ? StudioTheme.muted : StudioTheme.danger
                            font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption
                            text: entry.modelData.exists ? entry.modelData.url.toString() : qsTr("工程不可用（已移动或删除）")
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    StudioButton {
                        text: qsTr("打开")
                        compact: true
                        enabled: entry.modelData.exists
                        onClicked: { panel.close(); panel.openRequested(entry.modelData.url) }
                    }
                    StudioButton { iconName: "close"; tip: qsTr("移除记录"); quiet: true; compact: true; onClicked: recentProjectsController.removeEntry(entry.modelData.url) }
                }
            }
            Column {
                anchors.centerIn: parent; spacing: 8
                visible: recentProjectsController.projects.length === 0
                StudioIcon { name: "history"; width: 30; height: 30; color: StudioTheme.faint; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: qsTr("暂无最近工程。打开或保存工程后会显示在这里。"); color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body }
            }
        }
        Text { color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper;
            text: recentProjectsController.error
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
    footer: StudioDialogFooter {
        StudioButton {
            visible: recentProjectsController.projects.length > 8
            text: panel.expanded ? qsTr("仅显示最近 8 项") : qsTr("显示全部（最多 20 项）")
            quiet: true
            onClicked: panel.expanded = !panel.expanded
        }
        Item { Layout.fillWidth: true }
        StudioButton { text: qsTr("清空记录"); quiet: true; enabled: recentProjectsController.projects.length > 0; onClicked: recentProjectsController.clearHistory() }
        StudioButton { text: qsTr("完成"); onClicked: panel.close() }
    }
}
