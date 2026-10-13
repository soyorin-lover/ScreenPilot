import QtQuick
import QtQuick.Controls
Slider {
    id: control
    implicitHeight: 24
    implicitWidth: 160
    opacity: enabled ? 1 : .4
    hoverEnabled: true
    background: Rectangle {
        x: control.leftPadding; y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth; height: 4; radius: 2; color: StudioTheme.well
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 2; color: control.pressed ? StudioTheme.text : StudioTheme.muted }
    }
    handle: Item {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18; height: 18
        Rectangle { anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: -1; radius: 9; color: StudioTheme.shadow }
        Rectangle {
            anchors.fill: parent; radius: 9; color: StudioTheme.surface
            border.width: control.visualFocus ? 2 : 1
            border.color: control.visualFocus ? StudioTheme.accent : StudioTheme.border
            scale: control.pressed ? 1.08 : control.hovered ? 1.04 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
        }
    }
}
