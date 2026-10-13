import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
// Persistent preferences (appSettings) and the stable-release check
// (updateController). Values are written through setValue; failures show error.
StudioDialog {
    id: dialog
    objectName: "settingsDialog"
    title: "设置"
    subtitle: "偏好会保存在本机，对所有工程生效"
    iconName: "settings"
    width: Math.min(720, parent ? parent.width - 40 : 720)
    height: Math.min(640, parent ? parent.height - 40 : 640)
    property int page: 0
    readonly property var values: appSettings.values
    function displayPath(path) { return String(path || "").replace(/\\/g, "/") }
    FolderDialog { id: projectFolder; title: "选择默认工程文件夹"; currentFolder: appSettings.projectDirectoryUrl; onAccepted: appSettings.setValue("projectDirectory", selectedFolder.toString()) }
    FolderDialog { id: exportFolder; title: "选择默认导出文件夹"; currentFolder: appSettings.exportDirectoryUrl; onAccepted: appSettings.setValue("exportDirectory", selectedFolder.toString()) }

    component NavItem: AbstractButton {
        id: nav
        property string iconName: ""
        property int index: 0
        readonly property bool selected: dialog.page === index
        implicitHeight: 38
        Layout.fillWidth: true
        hoverEnabled: true
        onClicked: dialog.page = index
        background: Rectangle { radius: StudioTheme.radiusControl; color: nav.selected ? StudioTheme.selection : nav.hovered ? StudioTheme.well : "transparent" }
        contentItem: RowLayout {
            spacing: 10
            StudioIcon { name: nav.iconName; width: 17; height: 17; Layout.leftMargin: 10; color: nav.selected ? StudioTheme.text : StudioTheme.muted }
            Text { text: nav.text; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: nav.selected ? Font.DemiBold : Font.Medium; Layout.fillWidth: true }
        }
    }
    component FolderRow: ColumnLayout {
        id: folderRow
        property string label: ""
        property string path: ""
        signal choose()
        Layout.fillWidth: true
        spacing: 6
        Text { text: folderRow.label; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.Medium }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 34; radius: StudioTheme.radiusControl; color: StudioTheme.well
                Text { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: Text.AlignVCenter; text: dialog.displayPath(folderRow.path); elide: Text.ElideMiddle; color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper }
            }
            StudioButton { text: "更改"; iconName: "folder"; onClicked: folderRow.choose() }
        }
    }

    contentItem: RowLayout {
        spacing: 18
        ColumnLayout {
            Layout.preferredWidth: 156; Layout.fillHeight: true; spacing: 4
            NavItem { text: "外观与语言"; iconName: "sun"; index: 0 }
            NavItem { text: "编辑与预览"; iconName: "sliders"; index: 1 }
            NavItem { text: "文件位置"; iconName: "folder"; index: 2 }
            NavItem { text: "更新"; iconName: "download"; index: 3 }
            Item { Layout.fillHeight: true }
            StudioButton { text: "恢复默认设置"; quiet: true; compact: true; Layout.fillWidth: true; onClicked: appSettings.reset() }
        }
        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: StudioTheme.hairline }
        ScrollView {
            id: pageScroll
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: pageScroll.availableWidth
                spacing: 22

                // 0 · Appearance
                StudioSection {
                    visible: dialog.page === 0
                    title: "主题"; icon: "sun"
                    helper: "跟随系统时会在 Windows 切换深浅色后自动更新。当前显示：" + (appSettings.effectiveTheme === "dark" ? "深色" : "浅色")
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["跟随系统", "浅色", "深色"]
                        icons: ["monitor", "sun", "eye"]
                        currentIndex: Math.max(0, ["system", "light", "dark"].indexOf(dialog.values.theme))
                        onActivated: index => appSettings.setValue("theme", ["system", "light", "dark"][index])
                    }
                }
                StudioSection {
                    visible: dialog.page === 0
                    title: "语言"; icon: "document"
                    helper: "当前界面仅支持简体中文。英文翻译尚未提供，语言选择暂不可用。"
                    Text {
                        text: "简体中文 · English（暂不支持）"
                        color: StudioTheme.muted
                        font.family: StudioTheme.fontFamily
                        font.pixelSize: StudioTheme.body
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // 1 · Editing
                StudioSection {
                    visible: dialog.page === 1
                    title: "预览质量"; icon: "monitor"
                    helper: "降低编辑预览的渲染尺寸以提升流畅度，不影响导出画质。"
                    StudioSegmented {
                        Layout.fillWidth: true
                        model: ["高", "中", "低"]
                        currentIndex: Math.max(0, ["high", "medium", "low"].indexOf(dialog.values.previewQuality))
                        onActivated: index => appSettings.setValue("previewQuality", ["high", "medium", "low"][index])
                    }
                }
                StudioSection {
                    visible: dialog.page === 1
                    title: "提示"; icon: "volume"
                    StudioCheckBox { text: "录制或导出完成时播放提示音"; checked: dialog.values.completionSound === true; onToggled: appSettings.setValue("completionSound", checked) }
                    StudioHelp { text: "使用 Windows 系统提示音，遵循系统音量。" }
                }
                StudioSection {
                    visible: dialog.page === 1
                    title: "自动恢复"; icon: "history"
                    StudioCheckBox { text: "编辑时自动保存恢复快照"; checked: recoveryController.enabled; onToggled: recoveryController.enabled = checked }
                    StudioHelp { text: "停止编辑约 60 秒后保存一份工程副本，最多保留 3 份；录制、导出和播放时暂停。快照会复制媒体，大工程会占用较多磁盘。" }
                }

                // 2 · Locations
                StudioSection {
                    visible: dialog.page === 2
                    title: "默认位置"; icon: "folder"
                    helper: "保存工程和导出时从这里开始，并自动建议不重名的文件名。录制原始文件的位置在录制页“保存到”中设置。"
                    FolderRow { label: "工程文件夹"; path: dialog.values.projectDirectory; onChoose: projectFolder.open() }
                    FolderRow { label: "导出文件夹"; path: dialog.values.exportDirectory; onChoose: exportFolder.open() }
                }

                // 3 · Updates
                StudioSection {
                    visible: dialog.page === 3
                    title: "检查更新"; icon: "download"
                    helper: "只检查稳定版本并打开发布页，当前不会自动下载或安装。"
                    StudioCheckBox { text: "每天自动检查一次"; checked: dialog.values.autoCheckUpdates === true; onToggled: appSettings.setValue("autoCheckUpdates", checked) }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: updateRow.implicitHeight + 24
                        radius: StudioTheme.radiusGroup
                        color: StudioTheme.surfaceAlt
                        border.color: StudioTheme.hairline
                        RowLayout {
                            id: updateRow
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14
                            spacing: 12
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: updateController.status === "available" ? StudioTheme.amber
                                     : updateController.status === "upToDate" ? StudioTheme.success
                                     : updateController.status === "error" ? StudioTheme.danger : StudioTheme.faint
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Text {
                                    text: ({ idle: "尚未检查", checking: "正在检查…", available: "有新版本 " + updateController.latestVersion, upToDate: "已是最新稳定版", unavailable: "暂无可用的稳定发布", error: "检查失败" })[updateController.status] || updateController.status
                                    color: StudioTheme.text; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.body; font.weight: Font.DemiBold
                                }
                                Text { visible: updateController.error !== ""; text: updateController.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.caption; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                            StudioButton { text: "打开发布页"; visible: updateController.releaseUrl.toString() !== ""; onClicked: Qt.openUrlExternally(updateController.releaseUrl) }
                            StudioButton { text: updateController.busy ? "检查中…" : "立即检查"; iconName: "refresh"; enabled: !updateController.busy; onClicked: updateController.checkForUpdates() }
                        }
                    }
                }
                Text { visible: appSettings.error !== ""; text: appSettings.error; color: StudioTheme.danger; font.family: StudioTheme.fontFamily; font.pixelSize: StudioTheme.helper; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
    footer: StudioDialogFooter {
        Item { Layout.fillWidth: true }
        StudioButton { text: "完成"; primary: true; onClicked: dialog.close() }
    }
}
