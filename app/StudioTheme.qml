pragma Singleton
import QtQuick
// Application chrome only. Media canvas colours live in project settings and
// are never derived from these tokens. `dark` is bound by Main.qml to
// appSettings.effectiveTheme; values follow the reference light/dark tokens.
QtObject {
    property bool dark: false

    // Surfaces: soft silver workspace with near-white floating islands.
    readonly property color background: dark ? "#08080a" : "#ecedef"
    readonly property color surface: dark ? "#141517" : "#fdfdfe"
    readonly property color surfaceAlt: dark ? "#1c1d20" : "#f3f3f5"
    readonly property color well: dark ? "#12ffffff" : "#0d000000"
    readonly property color wellHover: dark ? "#1effffff" : "#15000000"
    readonly property color hairline: dark ? "#12ffffff" : "#10000000"
    readonly property color border: dark ? "#1cffffff" : "#17000000"
    readonly property color shadow: dark ? "#66000000" : "#1c1a1f2e"
    readonly property color dotGrid: dark ? "#16ffffff" : "#1f000000"

    // Ink.
    readonly property color text: dark ? "#e8e8ea" : "#292929"
    readonly property color muted: dark ? "#a8a8ad" : "#616161"
    readonly property color faint: dark ? "#6e6e74" : "#9a9a9e"

    // Graphite accent: selection and primary actions (inverts in dark).
    readonly property color accent: dark ? "#f2f2f4" : "#1a1a1a"
    readonly property color accentHover: dark ? "#ffffff" : "#333336"
    readonly property color accentPressed: dark ? "#d8d8dc" : "#000000"
    readonly property color accentInk: dark ? "#141414" : "#fcfcfc"
    readonly property color selection: dark ? "#24ffffff" : "#13000000"

    // Status.
    readonly property color success: "#4dd98c"
    readonly property color recording: "#ff453b"
    readonly property color amber: "#f2ad47"
    readonly property color danger: dark ? "#ff6b63" : "#d64541"
    readonly property color warning: dark ? "#f2ad47" : "#b26a00"

    // Timeline.
    readonly property color clipTop: dark ? "#38383b" : "#e0e0e2"
    readonly property color clipBottom: dark ? "#2b2b2e" : "#d4d4d6"
    readonly property color zoomClip: dark ? "#78634a" : "#ddcaa8"
    readonly property color zoomClipText: dark ? "#f3e6d0" : "#5a4a32"
    readonly property color zoomClipSelected: "#e2a85a"
    readonly property color playhead: dark ? "#f2f2f4" : "#1a1a1a"
    // Effect lanes: one hue per track so lanes read at a glance.
    readonly property color laneScreen: dark ? "#5a6f9e" : "#c4cfea"
    readonly property color laneFocus: dark ? "#6a5a8e" : "#d7cdee"
    readonly property color laneSticker: dark ? "#4f7a68" : "#c5e3d5"
    readonly property color laneCamera: dark ? "#8a5a5a" : "#efcfcb"
    readonly property color laneSync: "#e2a85a"
    readonly property color laneInk: dark ? "#f2f2f4" : "#2f3440"

    // Type.
    readonly property string fontFamily: "Microsoft YaHei UI"
    readonly property string monoFamily: "Consolas"
    readonly property int panelTitle: 16
    readonly property int sectionTitle: 14
    readonly property int body: 13
    readonly property int helper: 12
    readonly property int caption: 11

    // Geometry.
    readonly property int radiusCompact: 8
    readonly property int radiusControl: 10
    readonly property int radiusGroup: 12
    readonly property int radiusCard: 16
    readonly property int radiusFloating: 20
    readonly property int controlHeight: 34
    readonly property int compactHeight: 30
}
