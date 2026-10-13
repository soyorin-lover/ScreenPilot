import QtQuick
import QtQuick.Controls
// Lightweight anchored panel for recorder and toolbar choices.
Popup {
    id: popover
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    font.family: StudioTheme.fontFamily
    font.pixelSize: StudioTheme.body
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } NumberAnimation { property: "y"; from: popover.y + 6; to: popover.y; duration: 160; easing.type: Easing.OutCubic } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
    background: StudioCard { radius: StudioTheme.radiusCard }
    // A press on the opener closes the popover before its click arrives; remember
    // that so the same click toggles it shut instead of reopening it.
    property real closedAt: 0
    onClosed: closedAt = Date.now()
    function toggle() {
        if (visible) close()
        else if (Date.now() - closedAt > 250) open()
    }
}
