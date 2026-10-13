import QtQuick
import QtQuick.Controls
Menu {
    id: menu
    padding: 6
    implicitWidth: 200
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    background: StudioCard { implicitWidth: 200; radius: StudioTheme.radiusGroup }
    delegate: StudioMenuItem {}
}
