import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Label with a value pill above a full-width slider, as in the reference inspector.
ColumnLayout {
    id: root
    property string label: ""
    property string valueText: ""
    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias value: slider.value
    property alias slider: slider
    signal valueMoved(real newValue)
    Layout.fillWidth: true
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Text { text: root.label; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight }
        Rectangle {
            visible: root.valueText.length > 0
            implicitWidth: Math.max(44, valueLabel.implicitWidth + 18); implicitHeight: 24; radius: 12; color: StudioTheme.well
            Text { id: valueLabel; anchors.centerIn: parent; text: root.valueText; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; font.weight: Font.Medium }
        }
    }
    StudioSlider { id: slider; Layout.fillWidth: true; onMoved: root.valueMoved(value) }
}
