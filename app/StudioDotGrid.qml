import QtQuick
// Static dotted workspace backdrop; repaints only when resized.
Canvas {
    id: grid
    property real spacing: 18
    property color dotColor: StudioTheme.dotGrid
    renderStrategy: Canvas.Cooperative
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.fillStyle = dotColor
        for (let y = spacing / 2; y < height; y += spacing)
            for (let x = spacing / 2; x < width; x += spacing)
                c.fillRect(x - .6, y - .6, 1.2, 1.2)
    }
}
