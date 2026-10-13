import QtQuick
import QtQuick.Controls

Item {
    id: strip
    objectName: "timelineStrip"
    implicitHeight: 118
    property real positionSeconds: 0
    property real rulerHeight: 24
    readonly property real clipTop: rulerHeight + 4
    readonly property real clipHeight: Math.max(56, height - clipTop - 2)
    signal seekRequested(real seconds)
    // Right-click on a clip; the editor shows its clip menu at (x, y) in strip coordinates.
    signal clipMenuRequested(int index, real x, real y)
    property int draggedIndex: -1
    readonly property real scrollX: scroll.contentX
    // Edge trimming draft: -1 left edge, 1 right edge, 0 none.
    property int trimIndex: -1
    property int trimEdge: 0
    property real trimDelta: 0
    readonly property real gap: 4
    function finishInteraction(from, target, seconds, modifiers) {
        if (target !== from) {
            project.moveClip(from, target)
            project.selectClip(target, false, false)
            return
        }
        const extend = (modifiers & Qt.ShiftModifier) !== 0
        const toggle = (modifiers & Qt.ControlModifier) !== 0
        project.selectClip(from, extend, toggle)
        if (!extend && !toggle) seekRequested(seconds)
    }
    function isSelected(index) { return project.selectedClips.indexOf(index) >= 0 || project.selectedClip === index && project.selectedClips.length === 0 }
    // Commit an edge drag as a source-time boundary change.
    function commitTrim(index, edge, deltaPx, width) {
        const clip = project.clips[index]
        if (!clip || Math.abs(deltaPx) < 2) return
        const sourceDelta = deltaPx / Math.max(1, width) * clip.duration * clip.playbackRate
        const end = clip.sourceStart + clip.sourceDuration
        const minimum = Math.min(.05 * clip.playbackRate, clip.sourceDuration)
        if (edge < 0) project.trimClipBoundary(index, Math.max(0, Math.min(end - minimum, clip.sourceStart + sourceDelta)), end)
        else project.trimClipBoundary(index, clip.sourceStart, Math.min(project.sourceDuration, Math.max(clip.sourceStart + minimum, end + sourceDelta)))
    }
    function clipWidth(clip) {
        return Math.max(110, clip.duration / Math.max(.001, project.duration) * (scroll.width - gap * (project.clips.length - 1)))
    }
    // Output time to strip x, following each clip's on-screen width and gaps.
    function xForTime(seconds, endBoundary) {
        let prefix = 0
        const clips = project.clips
        for (let i = 0; i < clips.length; ++i) {
            const clip = clips[i]
            const w = clipWidth(clip)
            if (seconds < clip.outputStart + clip.duration || endBoundary && seconds <= clip.outputStart + clip.duration || i === clips.length - 1)
                return prefix + Math.max(0, Math.min(1, (seconds - clip.outputStart) / clip.duration)) * w
            prefix += w + gap
        }
        return 0
    }
    function timeForX(x) {
        let prefix = 0
        const clips = project.clips
        for (let i = 0; i < clips.length; ++i) {
            const clip = clips[i]
            const w = clipWidth(clip)
            if (x < prefix + w + gap / 2 || i === clips.length - 1)
                return clip.outputStart + Math.max(0, Math.min(1, (x - prefix) / w)) * clip.duration
            prefix += w + gap
        }
        return 0
    }
    function playheadX() { return xForTime(positionSeconds) }
    // Integrate local source distance, rather than subtracting source timestamps:
    // source timestamps jump at reordered/repeated clips; drag distance does not.
    function sourceDeltaForX(from, to) {
        const a = Math.min(from, to), b = Math.max(from, to)
        let prefix = 0, distance = 0
        for (const clip of project.clips) {
            const w = clipWidth(clip)
            distance += Math.max(0, Math.min(b, prefix + w) - Math.max(a, prefix)) / w * clip.sourceDuration
            prefix += w + gap
        }
        return to < from ? -distance : distance
    }
    function rulerLabel(seconds) {
        const m = Math.floor(seconds / 60), s = seconds - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(seconds % 1 === 0 ? 0 : 1)
    }
    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: Math.max(width, clipsRow.width)
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        ScrollBar.horizontal: ScrollBar { policy: scroll.contentWidth > scroll.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        WheelHandler { target: null; orientation: Qt.Vertical | Qt.Horizontal; onWheel: event => { scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, scroll.contentX - (event.angleDelta.y + event.angleDelta.x) / 2)) } }

        // Ruler: ticks follow the same piecewise mapping as clips and playhead.
        Canvas {
            id: ruler
            width: scroll.contentWidth; height: strip.rulerHeight
            property var deps: [project.clips, project.duration, scroll.width]
            onDepsChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                const total = project.duration
                if (total <= 0) return
                const pxPerSecond = Math.max(1, clipsRow.width) / total
                const steps = [.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600]
                let step = steps[steps.length - 1]
                for (const candidate of steps) if (candidate * pxPerSecond >= 84) { step = candidate; break }
                c.font = "10px '" + StudioTheme.fontFamily + "'"
                c.fillStyle = StudioTheme.faint
                c.strokeStyle = StudioTheme.border
                c.lineWidth = 1
                for (let t = 0; t <= total + .001; t += step / 4) {
                    const major = Math.abs(t / step - Math.round(t / step)) < .001
                    const x = Math.round(strip.xForTime(Math.min(t, total))) + .5
                    c.beginPath(); c.moveTo(x, major ? height - 9 : height - 5); c.lineTo(x, height); c.stroke()
                    if (major) c.fillText(strip.rulerLabel(t), Math.min(x + 3, width - 34), 11)
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: strip.enabled && project.hasProject
                cursorShape: Qt.PointingHandCursor
                onPressed: mouse => strip.seekRequested(strip.timeForX(mouse.x))
                onPositionChanged: mouse => { if (pressed) strip.seekRequested(strip.timeForX(Math.max(0, Math.min(width, mouse.x)))) }
            }
        }

        Row {
            id: clipsRow
            y: strip.clipTop
            spacing: strip.gap
            Repeater {
                model: project.clips
                Rectangle {
                    id: clipItem
                    objectName: "timelineClip-" + index
                    required property var modelData
                    required property int index
                    readonly property bool selected: strip.isSelected(index)
                    readonly property bool primary: project.selectedClip === index
                    readonly property real waveHeight: Math.min(22, strip.clipHeight * .28)
                    readonly property int frames: Math.max(1, Math.ceil((width - 6) / 96))
                    width: strip.clipWidth(modelData)
                    height: strip.clipHeight
                    radius: 10
                    gradient: Gradient { GradientStop { position: 0; color: StudioTheme.clipTop } GradientStop { position: 1; color: StudioTheme.clipBottom } }
                    opacity: strip.draggedIndex === index ? .72 : 1
                    // Thumbnails and waveform appear together once the whole
                    // source is sampled; partial results would reload and flicker.
                    Item {
                        x: 3; y: 3; width: parent.width - 6; height: parent.height - clipItem.waveHeight - 6
                        clip: true
                        opacity: timelineMedia.busy ? 0 : 1
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Row {
                            Repeater {
                                model: clipItem.frames
                                Image {
                                    required property int index
                                    width: (clipItem.width - 6) / clipItem.frames + .5; height: clipItem.height - clipItem.waveHeight - 6
                                    fillMode: Image.PreserveAspectCrop
                                    cache: false
                                    // In-memory provider: synchronous keeps every frame in one paint.
                                    asynchronous: false
                                    source: { const revision = timelineMedia.revision; return timelineMedia.thumbnail(clipItem.modelData.sourceStart + clipItem.modelData.sourceDuration * (index + .5) / clipItem.frames) }
                                }
                            }
                        }
                    }
                    Canvas {
                        id: wave
                        x: 6; y: parent.height - clipItem.waveHeight - 1; width: parent.width - 12; height: clipItem.waveHeight - 2
                        opacity: timelineMedia.busy ? 0 : 1
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Connections { target: timelineMedia; function onChanged() { wave.requestPaint() } }
                        onWidthChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset(); ctx.fillStyle = "#86868b"
                            for (let x = 0; x < width; x += 3) {
                                const amplitude = timelineMedia.amplitude(clipItem.modelData.sourceStart + clipItem.modelData.sourceDuration * x / width)
                                const a = Math.max(.06, Math.min(1, Math.sqrt(amplitude) * 1.6))
                                ctx.fillRect(x, height / 2 - a * height / 2, 2, Math.max(1, a * height))
                            }
                        }
                    }
                    Column {
                        objectName: "timelineLoading-" + clipItem.index
                        visible: timelineMedia.busy
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            visible: clipItem.width > 110
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "正在加载预览…"
                            color: StudioTheme.muted
                            font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(160, clipItem.width - 24); height: 3; radius: 1.5
                            color: StudioTheme.well
                            Rectangle {
                                width: parent.width * timelineMedia.progress; height: parent.height; radius: parent.radius
                                color: StudioTheme.muted
                                Behavior on width { NumberAnimation { duration: 120 } }
                            }
                        }
                    }
                    Rectangle {
                        visible: clipItem.modelData.playbackRate !== 1
                        x: 8; y: 8; height: 20; radius: 6; width: rateLabel.implicitWidth + 12
                        color: "#b3000000"
                        Text { id: rateLabel; anchors.centerIn: parent; text: clipItem.modelData.playbackRate + "×"; color: "white"; font.family: StudioTheme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold }
                    }
                    Rectangle {
                        visible: clipItem.width > 84
                        anchors.right: parent.right; anchors.rightMargin: 8; y: 8
                        height: 20; radius: 6; width: durationLabel.implicitWidth + 12
                        color: "#b3000000"
                        Text { id: durationLabel; anchors.centerIn: parent; text: strip.rulerLabel(Math.round(clipItem.modelData.duration * 100) / 100); color: "white"; font.family: StudioTheme.monoFamily; font.pixelSize: 10; font.weight: Font.DemiBold }
                    }
                    Rectangle {
                        anchors.fill: parent; radius: parent.radius; color: "transparent"
                        border.width: clipItem.selected ? (clipItem.primary ? 2.5 : 2) : 1
                        border.color: clipItem.selected ? StudioTheme.accent : StudioTheme.hairline
                    }
                    // Trim handles appear on hover; the amber band previews a drag.
                    Rectangle {
                        visible: clipMouse.containsMouse && clipMouse.mouseX < 10 || strip.trimIndex === clipItem.index && strip.trimEdge < 0
                        x: 0; width: 6; height: parent.height; radius: 3; color: StudioTheme.amber
                    }
                    Rectangle {
                        visible: clipMouse.containsMouse && clipMouse.mouseX > clipItem.width - 10 || strip.trimIndex === clipItem.index && strip.trimEdge > 0
                        x: parent.width - 6; width: 6; height: parent.height; radius: 3; color: StudioTheme.amber
                    }
                    Rectangle {
                        visible: strip.trimIndex === clipItem.index && strip.trimEdge !== 0
                        x: strip.trimEdge < 0 ? Math.min(strip.trimDelta, 0) : parent.width + Math.min(strip.trimDelta, 0)
                        width: Math.abs(strip.trimDelta); height: parent.height
                        color: strip.trimDelta * strip.trimEdge > 0 ? "#40f2ad47" : "#33000000"
                        border.color: StudioTheme.amber
                    }
                    MouseArea {
                        id: clipMouse
                        anchors.fill: parent
                        enabled: strip.enabled
                        preventStealing: true
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property real pressX: 0
                        onPressed: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                if (!clipItem.selected) project.selectClip(clipItem.index, false, false)
                                const p = mapToItem(strip, mouse.x, mouse.y)
                                strip.clipMenuRequested(clipItem.index, p.x, p.y)
                                return
                            }
                            pressX = mouse.x
                            const edge = clipItem.width > 40 ? (mouse.x < 10 ? -1 : mouse.x > clipItem.width - 10 ? 1 : 0) : 0
                            if (edge !== 0) { strip.trimIndex = clipItem.index; strip.trimEdge = edge; strip.trimDelta = 0; return }
                            strip.draggedIndex = clipItem.index
                        }
                        onPositionChanged: mouse => { if (pressed && strip.trimEdge !== 0) strip.trimDelta = mouse.x - pressX }
                        onReleased: mouse => {
                            if (mouse.button === Qt.RightButton) return
                            if (strip.trimEdge !== 0) {
                                const index = strip.trimIndex, edge = strip.trimEdge, delta = strip.trimDelta
                                strip.trimIndex = -1; strip.trimEdge = 0; strip.trimDelta = 0
                                strip.commitTrim(index, edge, delta, clipItem.width)
                                return
                            }
                            const point = mapToItem(clipsRow, mouse.x, mouse.y)
                            let target = clipItem.index
                            let offset = 0
                            for (let i = 0; i < project.clips.length; ++i) {
                                const w = strip.clipWidth(project.clips[i])
                                if (point.x >= offset && point.x < offset + w) { target = i; break }
                                offset += w + strip.gap
                            }
                            const seekTime = clipItem.modelData.outputStart + Math.max(0, Math.min(1, mouse.x / width)) * clipItem.modelData.duration
                            const from = strip.draggedIndex
                            strip.draggedIndex = -1
                            if (from >= 0) strip.finishInteraction(from, target, seekTime, mouse.modifiers)
                        }
                        onCanceled: { strip.draggedIndex = -1; strip.trimIndex = -1; strip.trimEdge = 0; strip.trimDelta = 0 }
                        cursorShape: strip.trimEdge !== 0 || clipItem.width > 40 && (mouseX < 10 || mouseX > clipItem.width - 10) ? Qt.SizeHorCursor : pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    }
                }
            }
        }
        Repeater {
            model: project.timelineMarkers
            Item {
                required property var modelData
                visible: modelData.visible
                x: {
                    let prefix = 0
                    const clips = project.clips
                    for (let i = 0; i < modelData.clipIndex; ++i)
                        prefix += strip.clipWidth(clips[i]) + strip.gap
                    const clip = clips[modelData.clipIndex]
                    return clip ? prefix + (modelData.outputTime - clip.outputStart) / clip.duration * strip.clipWidth(clip) - width / 2 : 0
                }
                y: strip.clipTop - 4
                width: 20; height: 22; z: 3
                Canvas {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 12; height: 16
                    onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = StudioTheme.amber; c.beginPath(); c.moveTo(1, 1); c.lineTo(11, 1); c.lineTo(11, 11); c.lineTo(6, 15); c.lineTo(1, 11); c.closePath(); c.fill() }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) markerMenu.popup()
                        else strip.seekRequested(modelData.outputTime)
                    }
                    ToolTip.visible: containsMouse
                    ToolTip.text: qsTr("标记 %1 · %2s（右键删除）").arg(modelData.number).arg(modelData.outputTime.toFixed(2))
                }
                StudioMenu {
                    id: markerMenu
                    StudioMenuItem { text: qsTr("跳转到标记"); iconName: "flag"; onTriggered: strip.seekRequested(modelData.outputTime) }
                    StudioMenuItem { text: qsTr("删除录制标记"); iconName: "trash"; danger: true; onTriggered: project.removeRecordingMarker(modelData.id) }
                }
            }
        }
        // Playhead with a grab knob on the ruler.
        Item {
            visible: project.hasProject
            x: strip.playheadX() - width / 2
            width: 12; height: scroll.contentHeight; z: 5
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 4; width: 2; height: parent.height - 4; color: StudioTheme.playhead }
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 2; width: 12; height: 12; radius: 6; color: StudioTheme.playhead; border.color: StudioTheme.surface; border.width: 2 }
        }
    }
}
