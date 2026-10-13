import QtQuick
// Draggable normalized rectangle over an image rect: move by dragging inside,
// resize from the corners. Emits a draft while dragging and commits on release.
Item {
    id: box
    property rect imageRect: Qt.rect(0, 0, width, height)
    property real rx: 0
    property real ry: 0
    property real rw: 1
    property real rh: 1
    property color tint: "white"
    property bool dimOutside: true
    property real minSize: .03
    signal committed(real x, real y, real w, real h)
    property real dx: rx
    property real dy: ry
    property real dw: rw
    property real dh: rh
    property bool dragging: false
    onRxChanged: if (!dragging) dx = rx
    onRyChanged: if (!dragging) dy = ry
    onRwChanged: if (!dragging) dw = rw
    onRhChanged: if (!dragging) dh = rh
    readonly property real sx: imageRect.x + dx * imageRect.width
    readonly property real sy: imageRect.y + dy * imageRect.height
    readonly property real sw: dw * imageRect.width
    readonly property real sh: dh * imageRect.height
    // Outside dim, drawn as four bands so the selection stays clear.
    Rectangle { visible: box.dimOutside; x: box.imageRect.x; y: box.imageRect.y; width: box.imageRect.width; height: box.sy - box.imageRect.y; color: "#80000000" }
    Rectangle { visible: box.dimOutside; x: box.imageRect.x; y: box.sy + box.sh; width: box.imageRect.width; height: box.imageRect.y + box.imageRect.height - box.sy - box.sh; color: "#80000000" }
    Rectangle { visible: box.dimOutside; x: box.imageRect.x; y: box.sy; width: box.sx - box.imageRect.x; height: box.sh; color: "#80000000" }
    Rectangle { visible: box.dimOutside; x: box.sx + box.sw; y: box.sy; width: box.imageRect.x + box.imageRect.width - box.sx - box.sw; height: box.sh; color: "#80000000" }
    Rectangle { x: box.sx; y: box.sy; width: box.sw; height: box.sh; color: "transparent"; border.color: box.tint; border.width: 2; radius: 2 }
    Repeater {
        model: 4
        Rectangle {
            required property int index
            x: box.sx + (index % 2 ? box.sw : 0) - 6
            y: box.sy + (index >= 2 ? box.sh : 0) - 6
            width: 12; height: 12; radius: 6; color: "white"; border.color: "#33000000"
        }
    }
    MouseArea {
        anchors.fill: parent
        property int mode: 0   // 0 move, 1..4 corners, -1 ignore
        property real px: 0
        property real py: 0
        property real ox: 0
        property real oy: 0
        property real ow: 0
        property real oh: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
        function nx(v) { return (v - box.imageRect.x) / Math.max(1, box.imageRect.width) }
        function ny(v) { return (v - box.imageRect.y) / Math.max(1, box.imageRect.height) }
        onPressed: mouse => {
            px = nx(mouse.x); py = ny(mouse.y); ox = box.dx; oy = box.dy; ow = box.dw; oh = box.dh
            const near = (a, b) => Math.abs(a - b) < 12
            const l = near(mouse.x, box.sx), r = near(mouse.x, box.sx + box.sw), t = near(mouse.y, box.sy), b = near(mouse.y, box.sy + box.sh)
            mode = (l || r) && (t || b) ? (l ? 1 : 2) + (t ? 0 : 2) : (px >= ox && px <= ox + ow && py >= oy && py <= oy + oh) ? 0 : -1
            box.dragging = mode >= 0
        }
        onPositionChanged: mouse => {
            if (!pressed || mode < 0) return
            const x = Math.max(0, Math.min(1, nx(mouse.x))), y = Math.max(0, Math.min(1, ny(mouse.y)))
            if (mode === 0) {
                box.dx = Math.max(0, Math.min(1 - ow, ox + x - px)); box.dy = Math.max(0, Math.min(1 - oh, oy + y - py))
            } else {
                const ax = mode === 1 || mode === 3 ? ox + ow : ox, ay = mode <= 2 ? oy + oh : oy
                box.dx = Math.min(ax, x); box.dy = Math.min(ay, y)
                box.dw = Math.max(box.minSize, Math.abs(x - ax)); box.dh = Math.max(box.minSize, Math.abs(y - ay))
                box.dw = Math.min(box.dw, 1 - box.dx); box.dh = Math.min(box.dh, 1 - box.dy)
            }
        }
        onReleased: { if (mode >= 0) box.committed(box.dx, box.dy, box.dw, box.dh); box.dragging = false }
        onCanceled: { box.dragging = false; box.dx = box.rx; box.dy = box.ry; box.dw = box.rw; box.dh = box.rh }
    }
}
