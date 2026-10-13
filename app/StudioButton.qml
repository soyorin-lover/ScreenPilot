import QtQuick
import QtQuick.Controls
Button {
    id: control
    property string iconName: ""
    property bool primary: false
    property bool quiet: false
    property bool danger: false
    property bool compact: false
    property string tip: ""
    property int iconSize: compact ? 16 : 18
    readonly property bool iconOnly: text.length === 0 && iconName !== ""
    readonly property color inkColor: primary ? StudioTheme.accentInk : danger ? StudioTheme.danger : StudioTheme.text
    Accessible.name: text.length > 0 ? text : tip.length > 0 ? tip : iconName
    implicitHeight: compact ? StudioTheme.compactHeight : StudioTheme.controlHeight
    implicitWidth: iconOnly ? implicitHeight : Math.max(implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: iconOnly ? 0 : compact ? 10 : 14
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    font.weight: primary ? Font.DemiBold : Font.Medium
    opacity: enabled ? 1 : .38
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    ToolTip.visible: tip.length > 0 && hovered
    ToolTip.delay: 450
    ToolTip.text: tip
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            StudioIcon { visible: control.iconName !== ""; name: control.iconName; width: control.iconSize; height: control.iconSize; color: control.inkColor; anchors.verticalCenter: parent.verticalCenter }
            Text { text: control.text; font: control.font; color: control.inkColor; anchors.verticalCenter: parent.verticalCenter; visible: text.length > 0 }
        }
    }
    background: Rectangle {
        radius: StudioTheme.radiusControl
        color: control.primary ? (control.down ? StudioTheme.accentPressed : control.hovered ? StudioTheme.accentHover : StudioTheme.accent)
            : control.down ? StudioTheme.wellHover
            : control.checked || control.highlighted ? StudioTheme.selection
            : control.hovered ? (control.quiet ? StudioTheme.well : StudioTheme.surfaceAlt)
            : control.quiet ? "transparent" : StudioTheme.surface
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? StudioTheme.accent : control.quiet || control.primary || control.checked || control.highlighted ? "transparent" : StudioTheme.border
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
