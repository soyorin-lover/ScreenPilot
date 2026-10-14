import QtQuick

// Renders the ScreenPilot icon at each ICO size: graphite rounded square,
// white viewfinder corners, recording dot. Small sizes get heavier strokes.
Item {
    id: root
    property var sizes: [256, 128, 64, 48, 32, 24, 16]
    property string outDir: Qt.application.arguments[Qt.application.arguments.length - 1]
    property int done: 0
    width: 512; height: 512
    Repeater {
        model: root.sizes
        Canvas {
            id: canvas
            required property int modelData
            width: modelData; height: modelData
            renderTarget: Canvas.Image
            onPaint: {
                const s = width, ctx = getContext("2d")
                ctx.reset()
                const small = s <= 32
                const pad = s * (small ? 0.02 : 0.04)
                const r = s * 0.23
                // Rounded square with a soft graphite gradient.
                ctx.beginPath()
                ctx.roundedRect(pad, pad, s - 2 * pad, s - 2 * pad, r, r)
                const g = ctx.createLinearGradient(0, pad, 0, s - pad)
                g.addColorStop(0, "#3a3d45")
                g.addColorStop(1, "#16171b")
                ctx.fillStyle = g
                ctx.fill()
                // Viewfinder corners.
                const inset = s * (small ? 0.22 : 0.24)
                const arm = s * (small ? 0.20 : 0.17)
                ctx.lineWidth = Math.max(1.5, s * (small ? 0.085 : 0.06))
                ctx.lineCap = "round"; ctx.lineJoin = "round"
                ctx.strokeStyle = "#f4f5f7"
                const a = inset, b = s - inset
                const corner = function(x, y, dx, dy) {
                    ctx.beginPath()
                    ctx.moveTo(x + dx * arm, y); ctx.lineTo(x, y); ctx.lineTo(x, y + dy * arm)
                    ctx.stroke()
                }
                corner(a, a, 1, 1); corner(b, a, -1, 1); corner(a, b, 1, -1); corner(b, b, -1, -1)
                // Recording dot.
                const dot = s * (small ? 0.15 : 0.13)
                ctx.beginPath()
                ctx.arc(s / 2, s / 2, dot, 0, Math.PI * 2)
                ctx.fillStyle = "#ff453b"
                ctx.fill()
                if (!small) {
                    ctx.beginPath()
                    ctx.arc(s / 2 - dot * 0.3, s / 2 - dot * 0.3, dot * 0.35, 0, Math.PI * 2)
                    ctx.fillStyle = "rgba(255,255,255,0.18)"
                    ctx.fill()
                }
            }
            property bool saved: false
            onPainted: {
                if (saved) return
                saved = true
                save(root.outDir + "/icon-" + modelData + ".png")
                root.done++
                if (root.done === root.sizes.length) Qt.quit()
            }
        }
    }
}
