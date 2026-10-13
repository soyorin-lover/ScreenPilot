import QtQuick
// Floating island surface. The soft drop is two static translucent layers,
// not a live blur, so it never re-renders with media frames underneath.
Item {
    id: card
    property real radius: StudioTheme.radiusFloating
    property color color: StudioTheme.surface
    property bool elevated: true
    default property alias content: body.data
    Rectangle {
        visible: card.elevated
        anchors.fill: parent; anchors.margins: -1; anchors.topMargin: 1; anchors.bottomMargin: -5
        radius: card.radius + 2; color: StudioTheme.shadow; opacity: .3
    }
    Rectangle {
        visible: card.elevated
        anchors.fill: parent; anchors.topMargin: 1; anchors.bottomMargin: -2
        radius: card.radius; color: StudioTheme.shadow; opacity: .5
    }
    Rectangle {
        id: body
        anchors.fill: parent
        radius: card.radius
        color: card.color
        border.color: StudioTheme.hairline
    }
}
