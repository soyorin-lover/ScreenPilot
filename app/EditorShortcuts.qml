import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: shortcuts
    required property var editor
    readonly property var focusItem: editor.Window.window ? editor.Window.window.activeFocusItem : null
    // Inspect ancestors too: editable ComboBox and SpinBox own internal inputs.
    function textFocused() {
        for (let item = focusItem; item; item = item.parent) {
            if (item instanceof TextInput || item instanceof TextEdit || item instanceof ComboBox)
                return true
        }
        return false
    }
    readonly property bool available: editor.shortcutsAvailable && !textFocused()
    function canvasFocused() {
        for (let item = focusItem; item; item = item.parent) {
            if (item instanceof AbstractButton || item instanceof Slider || item instanceof Dial)
                return false
        }
        return true
    }
    component EditorKey: Shortcut {
        context: Qt.WindowShortcut
        enabled: shortcuts.available
        autoRepeat: false
    }
    EditorKey { sequence: "Space"; enabled: shortcuts.available && shortcuts.canvasFocused(); onActivated: editor.playPause() }
    EditorKey { sequence: "Left"; enabled: shortcuts.available && shortcuts.canvasFocused(); onActivated: editor.stepFrames(-1) }
    EditorKey { sequence: "Right"; enabled: shortcuts.available && shortcuts.canvasFocused(); onActivated: editor.stepFrames(1) }
    EditorKey { sequence: "Shift+Left"; enabled: shortcuts.available && shortcuts.canvasFocused(); onActivated: editor.stepFrames(-5) }
    EditorKey { sequence: "Shift+Right"; enabled: shortcuts.available && shortcuts.canvasFocused(); onActivated: editor.stepFrames(5) }
    EditorKey { sequence: "Ctrl+Z"; onActivated: editor.undoEdit() }
    EditorKey { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: editor.redoEdit() }
    EditorKey { sequence: "Ctrl+S"; onActivated: editor.saveProject() }
    EditorKey { sequence: "Ctrl+Shift+S"; onActivated: editor.saveProjectAs() }
    EditorKey { sequence: "Ctrl+O"; onActivated: editor.requestOpen() }
    EditorKey { sequence: "Ctrl+E"; onActivated: editor.requestExport() }
    EditorKey { sequence: "S"; onActivated: editor.splitSelection() }
    EditorKey { sequence: "Q"; onActivated: editor.trimSelection(true) }
    EditorKey { sequence: "W"; onActivated: editor.trimSelection(false) }
    EditorKey { sequence: "Delete"; onActivated: editor.deleteSelection() }
    EditorKey { sequence: "Ctrl+C"; onActivated: editor.copySelection() }
    EditorKey { sequence: "Ctrl+V"; onActivated: editor.pasteSelection() }
    EditorKey { sequence: "Ctrl+Shift+V"; onActivated: editor.pasteSelectionProperties() }
    EditorKey { sequences: ["F1", "Ctrl+/"]; onActivated: editor.showShortcutHelp() }
    EditorKey { sequence: "Escape"; onActivated: editor.cancelTimelineInteraction() }
}
