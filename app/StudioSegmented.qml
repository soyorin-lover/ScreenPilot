import QtQuick
import QtQuick.Controls
// Segmented choice inside a recessed well; the selected segment lifts as a card.
Item {
    id: root
    property var model: []
    property var icons: []
    property int currentIndex: 0
    property bool compact: false
    signal activated(int index)
    implicitHeight: compact ? 30 : 36
    implicitWidth: 240
    opacity: enabled ? 1 : .4
    readonly property real segmentWidth: (width - 6) / Math.max(1, model.length)
    // Animate selection changes only; resizes and first layout snap into place.
    property real highlightIndex: currentIndex
    Behavior on highlightIndex { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    Rectangle { anchors.fill: parent; radius: StudioTheme.radiusControl + 1; color: StudioTheme.well }
    Rectangle {
        visible: root.currentIndex >= 0 && root.currentIndex < root.model.length
        x: 3 + root.highlightIndex * root.segmentWidth
        y: 3; width: root.segmentWidth; height: root.height - 6
        radius: StudioTheme.radiusCompact
        color: StudioTheme.surface
        border.color: StudioTheme.hairline
    }
    Row {
        anchors.fill: parent; anchors.margins: 3
        Repeater {
            model: root.model
            delegate: AbstractButton {
                id: segment
                required property int index
                required property string modelData
                readonly property bool selected: root.currentIndex === index
                width: root.segmentWidth; height: parent.height
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                Accessible.name: modelData
                // The caller owns selection so undo, presets and project loads stay bound.
                onClicked: { if (!selected) root.activated(index) }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: 6
                        StudioIcon {
                            visible: root.icons.length > segment.index && root.icons[segment.index] !== ""
                            name: root.icons.length > segment.index ? root.icons[segment.index] : ""
                            width: 15; height: 15; anchors.verticalCenter: parent.verticalCenter
                            color: segment.selected ? StudioTheme.text : StudioTheme.muted
                        }
                        Text {
                            text: segment.modelData
                            font.family: StudioTheme.fontFamily; font.pixelSize: root.compact ? StudioTheme.helper : StudioTheme.body
                            font.weight: segment.selected ? Font.DemiBold : Font.Medium
                            color: segment.selected || segment.hovered ? StudioTheme.text : StudioTheme.muted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                background: Rectangle { radius: StudioTheme.radiusCompact; color: "transparent"; border.width: segment.visualFocus ? 2 : 0; border.color: StudioTheme.accent }
            }
        }
    }
}
