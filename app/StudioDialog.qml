import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Shared modal shell: icon, title and subtitle header with a close button,
// floating card background and a soft dim. Footer content is per dialog.
Dialog {
    id: dialog
    property string subtitle: ""
    property string iconName: ""
    property bool showClose: true
    modal: true
    anchors.centerIn: parent
    padding: 24
    topPadding: 20
    bottomPadding: 20
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    palette.window: StudioTheme.surface
    palette.windowText: StudioTheme.text
    palette.text: StudioTheme.text
    palette.button: StudioTheme.surfaceAlt
    palette.buttonText: StudioTheme.text
    palette.base: StudioTheme.surface
    palette.highlight: StudioTheme.selection
    palette.highlightedText: StudioTheme.text
    Overlay.modal: Rectangle { color: "#38101418" }
    background: StudioCard { radius: StudioTheme.radiusFloating }
    header: Item {
        implicitHeight: headerRow.implicitHeight + 40
        RowLayout {
            id: headerRow
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: 24; anchors.rightMargin: 18; anchors.topMargin: 22
            spacing: 14
            StudioIcon { visible: dialog.iconName !== ""; name: dialog.iconName; width: 24; height: 24; color: StudioTheme.text; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Text { text: dialog.title; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { visible: dialog.subtitle.length > 0; text: dialog.subtitle; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; elide: Text.ElideMiddle; Layout.fillWidth: true }
            }
            StudioButton {
                visible: dialog.showClose
                iconName: "close"; tip: "关闭"; quiet: true
                Layout.alignment: Qt.AlignTop
                implicitWidth: 34; implicitHeight: 34
                background: Rectangle { radius: 17; color: parent.hovered ? StudioTheme.wellHover : StudioTheme.well }
                onClicked: dialog.reject()
            }
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: StudioTheme.hairline }
    }
}
