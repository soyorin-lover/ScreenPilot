import QtQuick
import QtQuick.Layouts
// Helper copy that wraps within the inspector width.
Text {
    Layout.fillWidth: true
    color: StudioTheme.muted
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.helper
    wrapMode: Text.Wrap
    lineHeight: 1.15
}
