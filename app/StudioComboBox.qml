import QtQuick
import QtQuick.Controls
ComboBox {
    id: control
    implicitHeight: StudioTheme.controlHeight
    implicitWidth: 160
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    font.weight: Font.Medium
    leftPadding: 12
    rightPadding: 30
    opacity: enabled ? 1 : .4
    hoverEnabled: true
    contentItem: Text { text: control.displayText; font: control.font; color: StudioTheme.text; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    indicator: StudioIcon { name: "chevronDown"; width: 14; height: 14; x: control.width - width - 11; y: (control.height - height) / 2; color: StudioTheme.muted }
    background: Rectangle {
        radius: StudioTheme.radiusControl
        color: control.pressed || control.hovered ? StudioTheme.wellHover : StudioTheme.well
        border.width: control.visualFocus ? 2 : 0
        border.color: StudioTheme.accent
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    delegate: ItemDelegate {
        id: row
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : control.width
        height: 32
        leftPadding: 10
        text: control.textRole ? (modelData[control.textRole] !== undefined ? modelData[control.textRole] : "") : modelData
        font: control.font
        highlighted: control.highlightedIndex === index
        contentItem: Row {
            spacing: 8
            StudioIcon { name: "check"; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter; color: StudioTheme.text; opacity: control.currentIndex === row.index ? 1 : 0 }
            Text { text: row.text; font: row.font; color: StudioTheme.text; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter; width: row.width - 40 }
        }
        background: Rectangle { radius: StudioTheme.radiusCompact; color: row.highlighted || row.hovered ? StudioTheme.selection : "transparent" }
    }
    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 180)
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 300)
        padding: 6
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
        background: StudioCard { radius: StudioTheme.radiusGroup }
    }
}
