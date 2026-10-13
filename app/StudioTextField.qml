import QtQuick
import QtQuick.Controls
TextField {
    id: control
    implicitHeight: StudioTheme.controlHeight
    leftPadding: 12
    rightPadding: 12
    color: StudioTheme.text
    placeholderTextColor: StudioTheme.faint
    selectionColor: StudioTheme.accent
    selectedTextColor: StudioTheme.accentInk
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    opacity: enabled ? 1 : .4
    hoverEnabled: true
    background: Rectangle {
        radius: StudioTheme.radiusControl
        color: control.activeFocus ? StudioTheme.surface : control.hovered ? StudioTheme.wellHover : StudioTheme.well
        border.width: control.activeFocus ? 1.5 : 0
        border.color: StudioTheme.accent
    }
}
