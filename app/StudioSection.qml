import QtQuick
import QtQuick.Layouts
// Inspector group: semibold heading with optional icon and helper, then controls.
ColumnLayout {
    id: root
    property string title: ""
    property string icon: ""
    property string helper: ""
    default property alias content: body.data
    Layout.fillWidth: true
    spacing: 10
    RowLayout {
        visible: root.title.length > 0
        spacing: 7
        StudioIcon { visible: root.icon !== ""; name: root.icon; width: 15; height: 15; color: StudioTheme.text }
        Text { text: root.title; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold }
    }
    StudioHelp { visible: root.helper.length > 0; text: root.helper }
    ColumnLayout { id: body; Layout.fillWidth: true; spacing: 12 }
}
