import QtQuick
import QtQuick.Layouts
// Dialog action row with the shell's insets and a hairline above.
Item {
    default property alias content: row.data
    implicitHeight: row.implicitHeight + 40
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: StudioTheme.hairline }
    RowLayout {
        id: row
        anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 20; anchors.rightMargin: 24
        spacing: 8
    }
}
