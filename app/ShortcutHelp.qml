import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

StudioDialog {
    id: help
    objectName: "shortcutHelp"
    title: "快捷键速查"
    subtitle: "在文本框输入时，快捷键保留给文字编辑"
    iconName: "keyboard"
    width: Math.min(parent.width - 32, 600)
    height: Math.min(parent.height - 32, 580)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property var groups: [
        { title: "播放与浏览", rows: [["空格", "播放 / 暂停"], ["← / →", "前后移动 1 帧"], ["Shift + ← / →", "前后移动 5 帧"]] },
        { title: "剪辑与运镜", rows: [["S", "分割选中运镜；未选运镜时分割主片段"], ["Q / W", "修剪运镜起点 / 终点；未选运镜时裁剪主片段"], ["Delete", "删除选中运镜或主片段"], ["Ctrl + C", "复制选中运镜"], ["Ctrl + V", "在播放头粘贴运镜"], ["Ctrl + Shift + V", "粘贴运镜属性，保留位置与时长"], ["Esc", "取消时间线拖动 / 关闭弹窗"]] },
        { title: "工程", rows: [["Ctrl + Z", "撤销"], ["Ctrl + Shift + Z / Ctrl + Y", "重做"], ["Ctrl + S", "保存"], ["Ctrl + Shift + S", "另存为"], ["Ctrl + O", "打开工程"], ["Ctrl + E", "导出"], ["F1 / Ctrl + /", "打开此速查"]] }
    ]
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 18
            Repeater {
                model: help.groups
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: group.modelData.title; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.sectionTitle; font.weight: Font.DemiBold; bottomPadding: 2 }
                    Repeater {
                        model: group.modelData.rows
                        delegate: RowLayout {
                            id: keyRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Text { text: keyRow.modelData[1]; color: StudioTheme.muted; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Rectangle {
                                implicitWidth: keyLabel.implicitWidth + 16; implicitHeight: 24; radius: 6
                                color: StudioTheme.surfaceAlt; border.color: StudioTheme.border
                                Text { id: keyLabel; anchors.centerIn: parent; text: keyRow.modelData[0]; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; font.weight: Font.Medium }
                            }
                        }
                    }
                }
            }
            StudioHelp { text: "逐帧移动使用工程导出帧率（30 / 60 fps）。按钮上的空格键仍用于激活按钮。" }
        }
    }
}
