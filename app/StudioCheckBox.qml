import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// A checkbox drawn as a trailing switch so inspector rows read label, then state.
CheckBox {
    id: control
    Layout.fillWidth: true
    implicitHeight: 30
    implicitWidth: label.implicitWidth
    spacing: 12
    leftPadding: 0
    rightPadding: 0
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    font.weight: Font.Medium
    opacity: enabled ? 1 : .4
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    indicator: Rectangle {
        x: control.width - width; y: (control.height - height) / 2
        width: 34; height: 20; radius: 10
        color: control.checked ? StudioTheme.accent : control.hovered ? StudioTheme.wellHover : StudioTheme.selection
        border.width: control.visualFocus ? 2 : 0
        border.color: StudioTheme.accentHover
        Behavior on color { ColorAnimation { duration: 140 } }
        Rectangle {
            width: 16; height: 16; radius: 8; y: 2
            x: control.checked ? parent.width - width - 2 : 2
            color: control.checked ? StudioTheme.accentInk : StudioTheme.text
            border.color: StudioTheme.border
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Text {
        id: label
        text: control.text; font: control.font; color: StudioTheme.text
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        rightPadding: control.indicator.width + control.spacing
    }
}
