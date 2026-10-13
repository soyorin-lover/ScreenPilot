import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Main-track clip inspector. Single selection edits one clip; with several
// clips selected (Ctrl/Shift on the timeline) it switches to batch actions.
ColumnLayout {
    id: root
    property real positionSeconds: 0
    property bool editable: true
    // Recomputed on project changes and playhead moves; commands still validate.
    readonly property var edit: { const deps = [project.clips, project.selectedClips, project.canUndo]; return project.editingState(positionSeconds) }
    readonly property int selectedCount: project.selectedClips.length
    readonly property var clip: project.selectedClip >= 0 && project.selectedClip < project.clips.length ? project.clips[project.selectedClip] : null
    readonly property var presetRates: [1, 1.5, 2, 4, 8, 12, 20, 50, 100]
    function clock(s) { const t = Math.max(0, s); return Math.floor(t / 60).toString().padStart(2, "0") + ":" + (t % 60).toFixed(2).padStart(5, "0") }
    Layout.fillWidth: true
    spacing: 22

    component KeyRow: RowLayout {
        id: kv
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        Text { text: kv.label; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; Layout.fillWidth: true }
        Text { text: kv.value; color: StudioTheme.text; font.family: StudioTheme.monoFamily; font.pixelSize: StudioTheme.body }
    }

    // ---- Batch -------------------------------------------------------------------
    Rectangle {
        visible: root.selectedCount > 1
        Layout.fillWidth: true
        implicitHeight: batchColumn.implicitHeight + 28
        radius: StudioTheme.radiusGroup
        color: StudioTheme.accent
        ColumnLayout {
            id: batchColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 4
            Text { text: "已选择 " + root.selectedCount + " 个片段"; color: StudioTheme.accentInk; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold }
            Text { text: "Ctrl 点击增减，Shift 点击连选。下列操作一次撤销。"; color: StudioTheme.accentInk; opacity: .7; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
    StudioSection {
        visible: root.selectedCount > 1
        title: "批量速度"; icon: "speed"
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: root.presetRates
                delegate: StudioButton { required property var modelData; text: modelData + "×"; compact: true; enabled: root.editable; onClicked: project.setSelectedClipsRate(modelData) }
            }
        }
        StudioNumberField { label: "自定义倍率"; suffix: "×"; from: 1; to: 100; value: root.clip ? root.clip.playbackRate : 1; onCommitted: v => project.setSelectedClipsRate(v) }
        StudioButton { text: "删除所选 " + root.selectedCount + " 个片段"; iconName: "trash"; danger: true; Layout.fillWidth: true; enabled: root.editable && root.edit.canRemoveSelected === true; onClicked: project.removeSelectedClips() }
        StudioHelp { visible: root.edit.canRemoveSelected !== true; text: "主轨至少保留一个片段。" }
        StudioButton { text: "只保留当前片段"; quiet: true; Layout.fillWidth: true; onClicked: project.selectClip(project.selectedClip, false, false) }
    }

    // ---- Single clip -------------------------------------------------------------
    StudioSection {
        visible: root.selectedCount <= 1
        title: root.clip ? "主录屏 · 片段 " + (project.selectedClip + 1) : "请选择片段"
        icon: "film"
        KeyRow { visible: root.clip !== null; label: "素材起点"; value: root.clip ? root.clock(root.clip.sourceStart) : "" }
        KeyRow { visible: root.clip !== null; label: "素材长度"; value: root.clip ? root.clock(root.clip.sourceDuration) : "" }
        KeyRow { visible: root.clip !== null; label: "成片位置"; value: root.clip ? root.clock(root.clip.outputStart) : "" }
        KeyRow { visible: root.clip !== null; label: "成片时长"; value: root.clip ? root.clock(root.clip.duration) : "" }
    }
    StudioSection {
        visible: root.selectedCount <= 1
        title: "剪辑"; icon: "scissors"
        GridLayout {
            Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 8
            StudioButton { text: "分割"; iconName: "split"; Layout.fillWidth: true; tip: "在播放头分割 (S)"; enabled: root.editable && root.edit.canSplit === true; onClicked: project.split(root.positionSeconds) }
            StudioButton { text: "合并下一段"; iconName: "link"; Layout.fillWidth: true; tip: "与右侧同倍率、素材连续的片段合并"; enabled: root.editable && root.edit.canMergeNext === true; onClicked: project.mergeAdjacent(project.selectedClip) }
            StudioButton { text: "裁掉前段"; iconName: "trimStart"; Layout.fillWidth: true; tip: "裁掉播放头前 (Q)"; enabled: root.editable && root.edit.canTrimStart === true; onClicked: project.trimStart(root.positionSeconds) }
            StudioButton { text: "裁掉后段"; iconName: "trimEnd"; Layout.fillWidth: true; tip: "裁掉播放头后 (W)"; enabled: root.editable && root.edit.canTrimEnd === true; onClicked: project.trimEnd(root.positionSeconds) }
            StudioButton { text: "恢复片头"; iconName: "chevronLeft"; Layout.fillWidth: true; tip: "把片段起点恢复到素材开头"; enabled: root.editable && root.edit.canRestoreHead === true; onClicked: project.restoreClipHead(project.selectedClip) }
            StudioButton { text: "恢复片尾"; iconName: "chevronRight"; Layout.fillWidth: true; tip: "把片段终点恢复到素材结尾"; enabled: root.editable && root.edit.canRestoreTail === true; onClicked: project.restoreClipTail(project.selectedClip) }
            StudioButton { text: "前移"; iconName: "arrowLeft"; Layout.fillWidth: true; enabled: root.editable && project.selectedClip > 0; onClicked: project.moveClip(project.selectedClip, project.selectedClip - 1) }
            StudioButton { text: "后移"; iconName: "chevronRight"; Layout.fillWidth: true; enabled: root.editable && project.selectedClip < project.clips.length - 1; onClicked: project.moveClip(project.selectedClip, project.selectedClip + 1) }
        }
        StudioHelp { text: "恢复片头/片尾可能让该片段与其他片段重复使用同一段素材。拖动时间线上片段两端也可调整边界。" }
        StudioButton { text: "删除此片段"; iconName: "trash"; danger: true; Layout.fillWidth: true; enabled: root.editable && root.edit.canRemove === true; onClicked: project.removeSelected() }
    }
    StudioSection {
        visible: root.selectedCount <= 1 && root.clip !== null
        title: "速度"; icon: "speed"
        helper: "支持 1–100 倍自由变速，预览与导出保持音高（极高速音质可能下降）。"
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: root.presetRates
                delegate: StudioButton {
                    required property var modelData
                    text: modelData + "×"; compact: true
                    checked: root.clip !== null && Math.abs(root.clip.playbackRate - modelData) < 1e-6
                    enabled: root.editable
                    onClicked: project.setClipRate(project.selectedClip, modelData)
                }
            }
        }
        StudioNumberField { label: "倍率"; suffix: "×"; from: 1; to: 100; value: root.clip ? root.clip.playbackRate : 1; onCommitted: v => project.setClipRate(project.selectedClip, v) }
        StudioNumberField {
            label: "目标时长"; suffix: "秒"
            from: root.clip ? root.clip.sourceDuration / 100 : 0.01
            to: root.clip ? root.clip.sourceDuration : 1e6
            value: root.clip ? root.clip.duration : 0
            onCommitted: v => project.setClipOutputDuration(project.selectedClip, v)
        }
    }
}
