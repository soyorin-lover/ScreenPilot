import QtQuick
import QtQuick.Controls
Item {
    id: root
    property double positionSeconds: 0
    property bool compact: false
    property var strip: null
    clip: compact
    function xOf(time, endBoundary) { return strip ? strip.xForTime(time, endBoundary) - strip.scrollX : project.duration > 0 ? time / project.duration * width : 0 }
    function timeAt(x) { return strip ? strip.timeForX(x + strip.scrollX) : x / width * project.duration }
    property bool snappingEnabled: true
    property bool dragActive: false
    property var draft: null
    property double pressX: 0
    property int dragEdge: 0
    property var dragMapping: null
    function cancelInteraction() { cancelDrag() }
    function cancelDrag() { dragActive=false; draft=null; dragMapping=null }
    function startDrag(mapping, edge, px) {
        for (const value of project.manualFramingSegments) if(value.id===mapping.id) {
            draft=Object.assign({},value); dragMapping=Object.assign({},mapping,{originalStart:value.start,originalEnd:value.end})
            pressX=px; dragEdge=edge; dragActive=true; return
        }
    }
    function updateDrag(px, modifiers) {
        if (!dragActive || width<=0 || project.duration<=0) return
        const m=dragMapping, rate=m.rate
        const moving=dragEdge===1 ? m.start+m.duration : m.start
        let dx=px-pressX
        if(snappingEnabled && !(modifiers & Qt.ShiftModifier)) {
            let targets=[positionSeconds]
            for(const c of project.clips) { targets.push(c.outputStart); targets.push(c.outputStart+c.duration) }
            for(const edge of mappedSegments) if(edge.id!==m.id) { targets.push(edge.start); targets.push(edge.start+edge.duration) }
            let best=8
            const rawDelta=dx
            const edges=dragEdge===0 ? [m.start,m.start+m.duration] : [moving]
            for(const edge of edges) for(const target of targets) {
                const correction=xOf(target)-(xOf(edge)+rawDelta)
                if(Math.abs(correction)<best) { best=Math.abs(correction); dx=xOf(target)-xOf(edge) }
            }
        }
        const anchor=xOf(moving,dragEdge===1)+(strip ? strip.scrollX : 0)
        const sourceDelta=strip ? strip.sourceDeltaForX(anchor,anchor+dx) : dx/width*project.duration*rate
        let a=m.originalStart, b=m.originalEnd
        if(dragEdge===-1) a=Math.max(0,Math.min(b-.001,Math.max(m.originalStart,m.sourceStart)+sourceDelta))
        else if(dragEdge===1) b=Math.min(project.sourceDuration,Math.max(a+.001,Math.min(m.originalEnd,m.sourceEnd)+sourceDelta))
        else { const shift=Math.max(-a,Math.min(project.sourceDuration-b,sourceDelta)); a+=shift; b+=shift }
        draft=Object.assign({},draft,{start:a,end:b})
    }
    function finishDrag() {
        if(!dragActive) return
        const value=draft; cancelDrag()
        project.updateManualFraming(value.id,{start:value.start,end:value.end})
        project.selectedManualFraming=value.id
    }
    signal seekRequested(double seconds)
    implicitHeight: compact ? 48 : list.implicitHeight
    property var mappedSegments: {
        let result=[]
        const manual=project.manualFramingSegments, clips=project.clips
        for(let i=0;i<manual.length;++i) for(let j=0;j<clips.length;++j) {
            const value=manual[i]
            const a=Math.max(value.start,clips[j].sourceStart)
            const b=Math.min(value.end,clips[j].sourceStart+clips[j].sourceDuration)
            if(b>a) result.push({id:manual[i].id, scale:manual[i].scale, start:clips[j].outputStart+(a-clips[j].sourceStart)/clips[j].playbackRate, duration:(b-a)/clips[j].playbackRate, rate:clips[j].playbackRate, sourceStart:clips[j].sourceStart, sourceEnd:clips[j].sourceStart+clips[j].sourceDuration, outputStart:clips[j].outputStart})
        }
        return result
    }
    Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4; visible: root.compact; color: StudioTheme.well; radius: 8 }
    Repeater {
        model: root.compact ? root.mappedSegments : []
        delegate: Rectangle {
            required property var modelData
            property bool drafting: root.dragActive && root.draft && root.draft.id===modelData.id
            property double visualStart: drafting ? modelData.outputStart+(Math.max(root.draft.start,modelData.sourceStart)-modelData.sourceStart)/modelData.rate : modelData.start
            property double visualDuration: drafting ? Math.max(0,(Math.min(root.draft.end,modelData.sourceEnd)-Math.max(root.draft.start,modelData.sourceStart))/modelData.rate) : modelData.duration
            x: root.xOf(visualStart)
            width: Math.max(2,root.xOf(visualStart+visualDuration,true)-root.xOf(visualStart))
            y: 6; height: root.height - 12; radius: 8
            readonly property bool selected: project.selectedManualFraming===modelData.id
            color: selected ? StudioTheme.zoomClipSelected : StudioTheme.zoomClip
            border.width: selected ? 2 : 1
            border.color: selected ? "#b97a2c" : "#24000000"
            Text {
                anchors.centerIn: parent; visible: parent.width>48
                width: parent.width - 16; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                text: "运镜 " + Number(modelData.scale || 1).toFixed(1) + "×"
                color: parent.selected ? "white" : StudioTheme.zoomClipText
                font.family: StudioTheme.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold
            }
            Rectangle { width: 3; height: parent.height-14; y: 7; x: 4; radius: 1.5; color: parent.selected ? "#ccffffff" : "#40000000" }
            Rectangle { width: 3; height: parent.height-14; y: 7; anchors.right: parent.right; anchors.rightMargin: 4; radius: 1.5; color: parent.selected ? "#ccffffff" : "#40000000" }
            MouseArea {
                objectName: "manualSegmentMouse"
                anchors.fill: parent
                cursorShape: mouseX<8 || mouseX>width-8 ? Qt.SizeHorCursor : Qt.OpenHandCursor
                onPressed: mouse => {
                    root.startDrag(modelData,mouse.x<8 ? -1 : mouse.x>width-8 ? 1 : 0,mapToItem(root,mouse.x,mouse.y).x)
                }
                onPositionChanged: mouse => { if(pressed) root.updateDrag(mapToItem(root,mouse.x,mouse.y).x,mouse.modifiers) }
                onReleased: root.finishDrag()
                onCanceled: root.cancelDrag()
            }
        }
    }
    Rectangle {
        visible: root.compact && project.duration>0
        x: root.xOf(root.positionSeconds)
        width: 2; height: root.height; color: StudioTheme.playhead
    }
    Column {
    id: list
    width: root.width
    visible: !root.compact
    spacing: 4
    Repeater {
        model: project.manualFramingSegments
        delegate: StudioButton {
            required property var modelData
            width: root.width
            highlighted: project.selectedManualFraming === modelData.id
            text: "素材 " + Number(modelData.start).toFixed(2) + "–" + Number(modelData.end).toFixed(2) + " 秒 · " + Number(modelData.scale).toFixed(1) + "×"
            onClicked: project.selectedManualFraming = modelData.id
        }
    }
}
}
