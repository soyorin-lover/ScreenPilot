import QtQuick
import QtQuick.Controls
MenuItem {
    id: item
    property string iconName: ""
    property string shortcutText: ""
    property bool danger: false
    implicitHeight: 32
    leftPadding: 10
    rightPadding: 10
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    contentItem: Item {
        implicitWidth: row.implicitWidth + (item.shortcutText.length ? hint.implicitWidth + 24 : 0)
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            StudioIcon { visible: item.iconName !== ""; name: item.iconName; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter; color: item.danger ? StudioTheme.danger : StudioTheme.text }
            Text { text: item.text; font: item.font; color: item.danger ? StudioTheme.danger : StudioTheme.text; anchors.verticalCenter: parent.verticalCenter }
        }
        Text { id: hint; visible: item.shortcutText.length > 0; text: item.shortcutText; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; color: StudioTheme.faint; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
    }
    opacity: enabled ? 1 : .4
    background: Rectangle { radius: StudioTheme.radiusCompact; color: item.highlighted ? StudioTheme.selection : "transparent" }
}
