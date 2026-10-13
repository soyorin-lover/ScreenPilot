import QtQuick
import QtQuick.Controls
// One effect track drawn against the main strip's geometry so blocks line up
// with clips. Items are source-time ranges; each is shown wherever its range
// survives in the cut (repeated source ranges appear more than once).
Item {
    id: lane
    property string track: ""
    property var items: []
    property var strip: null
    property real positionSeconds: 0
    property color tint: StudioTheme.laneScreen
    property var label: function(item) { return "" }
    signal activated(string track)
    readonly property string selectedId: effects.selectedIds[track] || ""
    readonly property bool pointTrack: track === "syncPoints"
    readonly property bool cameraTrack: track === "cameraMotionSegments"
    clip: true

    function rangeOf(item) {
        if (pointTrack) return [item.sourceTime, item.sourceTime]
        if (cameraTrack) return [item.start, item.end]
        return [item.sourceStart, item.sourceStart + item.sourceDuration]
    }
    // Map one source range onto every clip it overlaps, in output seconds.
    function pieces(item) {
        const r = rangeOf(item), out = [], clips = project.clips
        for (let i = 0; i < clips.length; ++i) {
            const c = clips[i], cs = c.sourceStart, ce = c.sourceStart + c.sourceDuration
            if (pointTrack) {
                if (r[0] >= cs && (r[0] < ce || r[0] === project.sourceDuration && r[0] === ce)) { const t = c.outputStart + (r[0] - cs) / c.playbackRate; out.push({ start: t, end: t, head: true, tail: true }) }
                continue
            }
            const a = Math.max(r[0], cs), b = Math.min(r[1], ce)
            if (b > a) out.push({ start: c.outputStart + (a - cs) / c.playbackRate, end: c.outputStart + (b - cs) / c.playbackRate, head: a <= r[0] + 1e-6, tail: b >= r[1] - 1e-6 })
        }
        return out
    }
    function xOf(t, endBoundary) { return strip ? strip.xForTime(t, endBoundary) - strip.scrollX : 0 }
    // Translate a drag (in px) into a patch on the item, all in source seconds.
    function commit(item, piece, mode, dx) {
        if (!strip || Math.abs(dx) < 2) return
        const r = rangeOf(item)
        const startX = xOf(piece.start), endX = xOf(piece.end, true)
        const delta = strip.sourceDeltaForX((mode > 0 ? endX : startX) + strip.scrollX,
                                          (mode > 0 ? endX : startX) + strip.scrollX + dx)
        const minimum = Math.min(.05, r[1] - r[0])
        if (mode === 0) {
            const shift = Math.max(-r[0], Math.min(project.sourceDuration - r[1], delta))
            if (pointTrack) effects.update(track, item.id, { sourceTime: Math.max(0, r[0] + shift), cameraTime: Math.max(0, item.cameraTime + shift) })
            else if (cameraTrack) effects.update(track, item.id, { start: Math.max(0, r[0] + shift), end: Math.max(0, r[1] + shift) })
            else effects.update(track, item.id, { sourceStart: Math.max(0, r[0] + shift) })
        } else if (mode < 0) {
            const start = Math.min(r[1] - minimum, Math.max(0, r[0] + delta))
            if (cameraTrack) effects.update(track, item.id, { start: start })
            else effects.update(track, item.id, { sourceStart: start, sourceDuration: r[1] - start })
        } else {
            const end = Math.min(project.sourceDuration, Math.max(r[0] + minimum, r[1] + delta))
            if (cameraTrack) effects.update(track, item.id, { end: end })
            else effects.update(track, item.id, { sourceDuration: end - r[0] })
        }
    }

    Rectangle { anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: 3; radius: 8; color: StudioTheme.well }
    Repeater {
        model: lane.items
        delegate: Item {
            id: itemHost
            required property var modelData
            readonly property bool selected: lane.selectedId === modelData.id
            anchors.fill: parent
            Repeater {
                model: lane.pieces(itemHost.modelData)
                delegate: Rectangle {
                    id: block
                    required property var modelData
                    property real dragDx: 0
                    property int dragMode: 0
                    readonly property real baseX: lane.xOf(modelData.start)
                    readonly property real baseW: Math.max(lane.pointTrack ? 12 : 6, lane.xOf(modelData.end, true) - baseX)
                    x: lane.pointTrack ? baseX - 6 + dragDx : baseX + (dragMode <= 0 ? dragDx : 0)
                    width: lane.pointTrack ? 12 : Math.max(6, baseW + (dragMode > 0 ? dragDx : dragMode < 0 ? -dragDx : 0))
                    y: 5; height: lane.height - 10
                    radius: lane.pointTrack ? 3 : 7
                    rotation: lane.pointTrack ? 45 : 0
                    scale: lane.pointTrack ? .72 : 1
                    color: itemHost.selected ? Qt.darker(lane.tint, StudioTheme.dark ? .8 : 1.25) : lane.tint
                    border.width: itemHost.selected ? 2 : 1
                    border.color: itemHost.selected ? StudioTheme.accent : "#22000000"
                    Text {
                        visible: !lane.pointTrack && block.width > 54
                        anchors.verticalCenter: parent.verticalCenter; x: 10; width: block.width - 20
                        elide: Text.ElideRight
                        text: lane.label(itemHost.modelData)
                        color: StudioTheme.laneInk
                        font.family: StudioTheme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold
                    }
                    Rectangle { visible: !lane.pointTrack && block.modelData.head && itemHost.selected; x: 3; y: 6; width: 3; height: parent.height - 12; radius: 1.5; color: "#aaffffff" }
                    Rectangle { visible: !lane.pointTrack && block.modelData.tail && itemHost.selected; x: parent.width - 6; y: 6; width: 3; height: parent.height - 12; radius: 1.5; color: "#aaffffff" }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: lane.pointTrack ? -4 : 0
                        hoverEnabled: true
                        preventStealing: true
                        property real pressX: 0
                        cursorShape: !lane.pointTrack && (mouseX < 8 && block.modelData.head || mouseX > width - 8 && block.modelData.tail) ? Qt.SizeHorCursor : pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        onPressed: mouse => {
                            effects.select(lane.track, itemHost.modelData.id)
                            lane.activated(lane.track)
                            pressX = mapToItem(lane, mouse.x, 0).x
                            block.dragMode = lane.pointTrack ? 0 : mouse.x < 8 && block.modelData.head ? -1 : mouse.x > width - 8 && block.modelData.tail ? 1 : 0
                            block.dragDx = 0
                        }
                        onPositionChanged: mouse => { if (pressed) block.dragDx = mapToItem(lane, mouse.x, 0).x - pressX }
                        onReleased: { const dx = block.dragDx, mode = block.dragMode; block.dragDx = 0; block.dragMode = 0; lane.commit(itemHost.modelData, block.modelData, mode, dx) }
                        onCanceled: { block.dragDx = 0; block.dragMode = 0 }
                    }
                }
            }
        }
    }
    Rectangle {
        visible: lane.strip !== null && project.duration > 0
        x: lane.xOf(lane.positionSeconds) - 1
        width: 2; height: lane.height; color: StudioTheme.playhead
    }
}
