import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Label + compact numeric input with unit. Commits on Enter or focus loss;
// the caller writes the value back to the model, so undo and reloads stay bound.
RowLayout {
    id: root
    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property int decimals: 2
    property string suffix: ""
    property real labelWidth: 84
    signal committed(real newValue)
    Layout.fillWidth: true
    spacing: 8
    Text { visible: root.label.length > 0; text: root.label; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; Layout.preferredWidth: root.labelWidth; elide: Text.ElideRight }
    StudioTextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 30
        rightPadding: unit.implicitWidth + 16
        font.family: StudioTheme.monoFamily
        horizontalAlignment: Text.AlignRight
        text: Number(root.value).toFixed(root.decimals)
        validator: DoubleValidator { bottom: root.from; top: root.to; decimals: root.decimals; locale: "C"; notation: DoubleValidator.StandardNotation }
        onEditingFinished: {
            const parsed = Number(text)
            if (!isNaN(parsed) && Math.abs(parsed - root.value) > Math.pow(10, -root.decimals - 1))
                root.committed(Math.max(root.from, Math.min(root.to, parsed)))
            else text = Number(root.value).toFixed(root.decimals)
        }
        Text { id: unit; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: root.suffix; color: StudioTheme.faint; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption }
    }
}
