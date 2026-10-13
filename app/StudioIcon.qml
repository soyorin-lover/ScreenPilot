import QtQuick
import QtQuick.Shapes
// Line icons on a 24-unit grid with one stroke weight so every glyph shares
// optical size. Glyphs are vector paths rendered by the scene graph, so they
// stay crisp at any DPI and need no per-icon texture.
Item {
    id: icon
    property string name: ""
    property alias iconName: icon.name
    property color color: StudioTheme.text
    property real strokeWidth: 1.6
    implicitWidth: 18
    implicitHeight: 18
    readonly property real size: Math.max(1, Math.min(width, height))
    readonly property var glyph: build(name)

    function build(name) {
        let stroke = "", fill = ""
        const f = v => Math.round(v * 1000) / 1000
        function add(path, filled) { if (filled) fill += path + " "; else stroke += path + " " }
        function line(p) { let s = "M" + f(p[0]) + " " + f(p[1]); for (let i = 2; i < p.length; i += 2) s += " L" + f(p[i]) + " " + f(p[i + 1]); add(s, false) }
        function poly(p, filled) { let s = "M" + f(p[0]) + " " + f(p[1]); for (let i = 2; i < p.length; i += 2) s += " L" + f(p[i]) + " " + f(p[i + 1]); add(s + " Z", filled) }
        function rrect(x, y, w, h, r, filled) {
            add("M" + f(x + r) + " " + f(y) + " H" + f(x + w - r) + " A" + r + " " + r + " 0 0 1 " + f(x + w) + " " + f(y + r)
                + " V" + f(y + h - r) + " A" + r + " " + r + " 0 0 1 " + f(x + w - r) + " " + f(y + h)
                + " H" + f(x + r) + " A" + r + " " + r + " 0 0 1 " + f(x) + " " + f(y + h - r)
                + " V" + f(y + r) + " A" + r + " " + r + " 0 0 1 " + f(x + r) + " " + f(y) + " Z", filled)
        }
        function circle(x, y, r, filled) { add("M" + f(x - r) + " " + f(y) + " A" + r + " " + r + " 0 1 0 " + f(x + r) + " " + f(y) + " A" + r + " " + r + " 0 1 0 " + f(x - r) + " " + f(y) + " Z", filled) }
        // Canvas-style arc: angles in radians, clockwise on screen unless ccw.
        function arc(x, y, r, a, b, ccw) {
            let span = ccw ? a - b : b - a
            span = ((span % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2)
            add("M" + f(x + Math.cos(a) * r) + " " + f(y + Math.sin(a) * r) + " A" + r + " " + r + " 0 " + (span > Math.PI ? 1 : 0) + " " + (ccw ? 0 : 1) + " " + f(x + Math.cos(b) * r) + " " + f(y + Math.sin(b) * r), false)
        }
        function dot(x, y) { circle(x, y, 1.1, true) }
        function dashedRect(x, y, w, h) {
            const seg = 2.6, gap = 2.2
            function dash(x1, y1, x2, y2) {
                const len = Math.hypot(x2 - x1, y2 - y1), dx = (x2 - x1) / len, dy = (y2 - y1) / len
                for (let t = 0; t < len; t += seg + gap) line([x1 + dx * t, y1 + dy * t, x1 + dx * Math.min(len, t + seg), y1 + dy * Math.min(len, t + seg)])
            }
            dash(x, y, x + w, y); dash(x + w, y, x + w, y + h); dash(x + w, y + h, x, y + h); dash(x, y + h, x, y)
        }
        function rays(cx, cy, inner, outer, count) { for (let i = 0; i < count; ++i) { const a = i * Math.PI * 2 / count; line([cx + Math.cos(a) * inner, cy + Math.sin(a) * inner, cx + Math.cos(a) * outer, cy + Math.sin(a) * outer]) } }
        switch (name) {
        case "check": line([5, 12.5, 10, 17, 19, 7]); break
        case "chevronDown": case "chevron-down": line([7, 9.5, 12, 14.5, 17, 9.5]); break
        case "chevronUp": line([7, 14.5, 12, 9.5, 17, 14.5]); break
        case "chevronRight": line([9.5, 7, 14.5, 12, 9.5, 17]); break
        case "chevronLeft": line([14.5, 7, 9.5, 12, 14.5, 17]); break
        case "back": case "arrowLeft": line([11, 6, 5, 12, 11, 18]); line([5, 12, 19, 12]); break
        case "play": poly([8, 5.5, 18.5, 12, 8, 18.5], true); break
        case "pause": rrect(7, 5.5, 3.2, 13, 1, true); rrect(13.8, 5.5, 3.2, 13, 1, true); break
        case "stop": rrect(6.5, 6.5, 11, 11, 2.2, true); break
        case "record": circle(12, 12, 6.5, true); break
        case "stepBack": rrect(5.5, 6, 2.6, 12, 1, true); poly([18.5, 6.2, 18.5, 17.8, 10, 12], true); break
        case "stepForward": rrect(15.9, 6, 2.6, 12, 1, true); poly([5.5, 6.2, 5.5, 17.8, 14, 12], true); break
        case "close": line([6.5, 6.5, 17.5, 17.5]); line([17.5, 6.5, 6.5, 17.5]); break
        case "plus": case "add": line([12, 5.5, 12, 18.5]); line([5.5, 12, 18.5, 12]); break
        case "minus": line([5.5, 12, 18.5, 12]); break
        case "more": dot(6, 12); dot(12, 12); dot(18, 12); break
        case "folder": line([3.5, 7.5, 3.5, 18, 4.5, 19, 19.5, 19, 20.5, 18, 20.5, 9.5, 19.5, 8.5, 11.5, 8.5, 9.5, 6, 4.5, 6, 3.5, 7.5]); line([3.5, 10.5, 20.5, 10.5]); break
        case "save": rrect(4.5, 4.5, 15, 15, 2.5); line([8.5, 4.5, 8.5, 9, 15.5, 9, 15.5, 4.5]); rrect(8, 13, 8, 6.5, 1); break
        case "export": case "upload": line([12, 14.5, 12, 3.5]); line([8, 7.5, 12, 3.5, 16, 7.5]); line([8.5, 10, 6, 10, 5, 11, 5, 19.5, 6, 20.5, 18, 20.5, 19, 19.5, 19, 11, 18, 10, 15.5, 10]); break
        case "download": line([12, 3.5, 12, 14.5]); line([8, 10.5, 12, 14.5, 16, 10.5]); line([4.5, 15, 4.5, 19.5, 19.5, 19.5, 19.5, 15]); break
        case "monitor": case "screen": rrect(3, 4.5, 18, 12, 2); line([12, 16.5, 12, 19.5]); line([8, 19.5, 16, 19.5]); break
        case "window": rrect(3.5, 4.5, 17, 15, 2.5); line([3.5, 9, 20.5, 9]); dot(6.6, 6.8); dot(9.2, 6.8); dot(11.8, 6.8); break
        case "region": dashedRect(4, 5, 16, 14); break
        case "device": case "phone": rrect(7, 3, 10, 18, 2.5); line([10.5, 18, 13.5, 18]); break
        case "camera": rrect(3, 7, 12.5, 10, 2.5); line([15.5, 10.5, 20.5, 7.5, 20.5, 16.5, 15.5, 13.5]); break
        case "mic": case "microphone": rrect(9, 3.5, 6, 11, 3); arc(12, 11.5, 6, 0, Math.PI); line([12, 17.5, 12, 20.5]); line([9, 20.5, 15, 20.5]); break
        case "volume": case "audio": case "speaker": poly([4, 9.5, 7.5, 9.5, 12, 5.5, 12, 18.5, 7.5, 14.5, 4, 14.5]); arc(13, 12, 3.6, -Math.PI / 3, Math.PI / 3); arc(13, 12, 7, -Math.PI / 3, Math.PI / 3); break
        case "crop": line([7, 3.5, 7, 17, 20.5, 17]); line([3.5, 7, 17, 7, 17, 20.5]); break
        case "scissors": case "cut": circle(6.5, 7, 2.6); circle(6.5, 17, 2.6); line([8.7, 8.5, 19.5, 18]); line([8.7, 15.5, 19.5, 6]); break
        case "trash": case "delete": line([4.5, 6.5, 19.5, 6.5]); line([9.5, 6.5, 9.5, 4.5, 14.5, 4.5, 14.5, 6.5]); line([6.5, 6.5, 7.5, 19.5, 16.5, 19.5, 17.5, 6.5]); line([10.3, 10.5, 10.3, 16]); line([13.7, 10.5, 13.7, 16]); break
        case "undo": line([8.5, 5, 4.5, 9, 8.5, 13]); add("M4.5 9 L14 9 A5 5 0 0 1 14 19 L9 19", false); break
        case "redo": line([15.5, 5, 19.5, 9, 15.5, 13]); add("M19.5 9 L10 9 A5 5 0 0 0 10 19 L15 19", false); break
        case "history": arc(12, 12, 8, Math.PI * .9, Math.PI * 2.75); line([4.2, 8.5, 4.2, 12, 7.5, 12]); line([12, 8, 12, 12, 15, 14]); break
        case "zoom": case "search": circle(10.5, 10.5, 6); line([15, 15, 20, 20]); break
        case "zoomIn": circle(10.5, 10.5, 6); line([15, 15, 20, 20]); line([10.5, 8, 10.5, 13]); line([8, 10.5, 13, 10.5]); break
        case "image": case "background": case "canvas": rrect(3.5, 4.5, 17, 15, 2.5); circle(9, 9.5, 1.7); line([3.5, 17, 9, 12.5, 12.5, 15.5, 16, 11.5, 20.5, 16]); break
        case "layers": poly([12, 4, 20.5, 8.5, 12, 13, 3.5, 8.5]); line([3.5, 12.5, 12, 17, 20.5, 12.5]); line([3.5, 16, 12, 20.5, 20.5, 16]); break
        case "layout": rrect(3.5, 5, 17, 14, 2.5); rrect(7, 8.5, 10, 7, 1.2, true); break
        case "mockup": rrect(3.5, 4.5, 17, 15, 2.5); line([3.5, 8.5, 20.5, 8.5]); break
        case "effects": case "sparkles": poly([10, 3.5, 11.6, 8.4, 16.5, 10, 11.6, 11.6, 10, 16.5, 8.4, 11.6, 3.5, 10, 8.4, 8.4]); line([17.5, 14.5, 17.5, 20.5]); line([14.5, 17.5, 20.5, 17.5]); break
        case "intro": rrect(3.5, 5.5, 17, 13, 2.5); poly([10.2, 9, 15, 12, 10.2, 15]); break
        case "framing": case "motion": line([4, 8.5, 4, 4, 8.5, 4]); line([15.5, 4, 20, 4, 20, 8.5]); line([20, 15.5, 20, 20, 15.5, 20]); line([8.5, 20, 4, 20, 4, 15.5]); rrect(9, 9, 6, 6, 1.2); break
        case "cursor": poly([6, 4, 18, 12.5, 12.6, 13.4, 15.6, 19.2, 13.4, 20.3, 10.4, 14.5, 6.5, 18]); break
        case "film": case "clip": rrect(3.5, 5, 17, 14, 2.5); line([8, 5, 8, 19]); line([16, 5, 16, 19]); line([3.5, 12, 8, 12]); line([16, 12, 20.5, 12]); break
        case "waveform": line([4, 10, 4, 14]); line([7.5, 7, 7.5, 17]); line([11, 4.5, 11, 19.5]); line([14.5, 8, 14.5, 16]); line([18, 10.5, 18, 13.5]); line([20.5, 11.5, 20.5, 12.5]); break
        case "magnet": add("M6 4.5 L6 12 A6 6 0 0 0 18 12 L18 4.5", false); line([9.6, 4.5, 9.6, 12]); line([14.4, 4.5, 14.4, 12]); line([6, 8, 9.6, 8]); line([14.4, 8, 18, 8]); break
        case "eye": add("M3 12 Q12 3 21 12 Q12 21 3 12 Z", false); circle(12, 12, 2.8); break
        case "keyboard": rrect(3, 6.5, 18, 11, 2.2); dot(7, 10); dot(10.3, 10); dot(13.7, 10); dot(17, 10); line([8, 14, 16, 14]); break
        case "copy": rrect(8.5, 8.5, 11, 11, 2.2); line([15.5, 8.5, 15.5, 5.5, 14.5, 4.5, 5.5, 4.5, 4.5, 5.5, 4.5, 14.5, 5.5, 15.5, 8.5, 15.5]); break
        case "paste": rrect(5, 5.5, 14, 15, 2.2); rrect(9, 3.5, 6, 4, 1.2); line([8.5, 12, 15.5, 12]); line([8.5, 15.5, 13, 15.5]); break
        case "split": line([12, 3.5, 12, 20.5]); line([4.5, 8, 8.5, 12, 4.5, 16]); line([19.5, 8, 15.5, 12, 19.5, 16]); break
        case "trimStart": line([8, 4.5, 8, 19.5]); line([12, 8, 16, 12, 12, 16]); line([16, 12, 20, 12]); break
        case "trimEnd": line([16, 4.5, 16, 19.5]); line([12, 8, 8, 12, 12, 16]); line([8, 12, 4, 12]); break
        case "speed": arc(12, 14, 8, Math.PI, Math.PI * 2); line([12, 14, 16, 9]); dot(12, 14); break
        case "arrowLeftRight": line([4, 12, 20, 12]); line([7.5, 8.5, 4, 12, 7.5, 15.5]); line([16.5, 8.5, 20, 12, 16.5, 15.5]); break
        case "refresh": arc(12, 12, 7.5, -Math.PI * .35, Math.PI * 1.45); line([16.3, 3.6, 17.5, 6.6, 14.5, 7.7]); break
        case "edit": poly([4.5, 19.5, 5.5, 15, 15.5, 5, 19, 8.5, 9, 18.5]); line([13.5, 7, 17, 10.5]); break
        case "marker": case "flag": line([6, 3.5, 6, 20.5]); line([6, 4.5, 18, 4.5, 15.5, 8.5, 18, 12.5, 6, 12.5]); break
        case "memo": case "document": rrect(4.5, 4.5, 15, 15, 2.5); line([8, 9, 16, 9]); line([8, 12.5, 16, 12.5]); line([8, 16, 12.5, 16]); break
        case "presets": rrect(3.5, 5, 10, 8, 1.8); rrect(10.5, 11, 10, 8, 1.8); break
        case "sliders": line([4, 7, 20, 7]); line([4, 12, 20, 12]); line([4, 17, 20, 17]); circle(15, 7, 2, true); circle(9, 12, 2, true); circle(13, 17, 2, true); break
        case "aspect": rrect(3.5, 6, 17, 12, 2.2); line([7, 10.5, 7, 9, 9, 9]); line([17, 13.5, 17, 15, 15, 15]); break
        case "settings": circle(12, 12, 3); circle(12, 12, 6.2); rays(12, 12, 6.2, 8.6, 8); break
        case "help": circle(12, 12, 8.5); line([9.5, 9.5, 10, 8, 12, 7.2, 14, 8, 14.5, 9.8, 13.5, 11.3, 12, 12.2, 12, 13.6]); dot(12, 16.6); break
        case "info": circle(12, 12, 8.5); line([12, 11, 12, 16.5]); dot(12, 8); break
        case "warning": poly([12, 4, 21, 19.5, 3, 19.5]); line([12, 10, 12, 14]); dot(12, 16.8); break
        case "link": line([10, 14, 14, 10]); arc(8, 16, 3.4, Math.PI * .25, Math.PI * 1.25); arc(16, 8, 3.4, Math.PI * 1.25, Math.PI * .25); break
        case "sun": circle(12, 12, 3.6); rays(12, 12, 6.3, 8.3, 8); break
        case "cube": poly([12, 3.5, 19.5, 7.5, 19.5, 16.5, 12, 20.5, 4.5, 16.5, 4.5, 7.5]); line([4.5, 7.5, 12, 11.5, 19.5, 7.5]); line([12, 11.5, 12, 20.5]); break
        case "app": rrect(3.5, 4.5, 17, 13, 3); circle(12, 11, 2.6, true); line([9, 20, 15, 20]); break
        default: rrect(5, 5, 14, 14, 2.5); line([9, 10, 15, 10]); line([9, 14, 15, 14]); break
        }
        return { stroke: stroke.length ? stroke : "M0 0", fill: fill.length ? fill : "M0 0" }
    }

    Shape {
        width: 24; height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        x: (icon.width - icon.size) / 2
        y: (icon.height - icon.size) / 2
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            fillColor: "transparent"
            // Hold the on-screen stroke near strokeWidth; larger glyphs thicken slightly.
            strokeWidth: icon.strokeWidth * Math.min(1.3, Math.max(.9, icon.size / 18)) * 24 / icon.size
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.glyph.stroke }
        }
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillColor: icon.color
            PathSvg { path: icon.glyph.fill }
        }
    }
}
