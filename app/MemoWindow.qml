import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: memoWindow
    objectName: "recorderMemoWindow"
    width: 390
    height: 420
    minimumWidth: 300
    minimumHeight: 280
    title: qsTr("备忘录")
    visible: false
    color: "transparent"
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    function toggle() {
        if (visible) { memoController.saveWindow(memoWindow); hide() }
        else { memoController.prepareWindow(memoWindow); show() }
    }
    onClosing: function(close) {
        close.accepted = false
        memoController.saveWindow(memoWindow)
        hide()
    }
    onActiveChanged: if (!active) memoController.saveWindow(memoWindow)
    component MemoButton: AbstractButton {
        id: memoButton
        property string iconName: ""
        property string label: ""
        property string tip: ""
        implicitWidth: 30; implicitHeight: 30
        hoverEnabled: true
        opacity: enabled ? 1 : .35
        ToolTip.visible: tip.length > 0 && hovered
        ToolTip.text: tip
        background: Rectangle { radius: 9; color: memoButton.down ? "#33ffffff" : memoButton.hovered ? "#22ffffff" : "transparent" }
        contentItem: Item {
            StudioIcon { visible: memoButton.iconName !== ""; anchors.centerIn: parent; name: memoButton.iconName; width: 16; height: 16; color: "#eef1f5" }
            Text { visible: memoButton.label !== ""; anchors.centerIn: parent; text: memoButton.label; color: "#eef1f5"; font.family: StudioTheme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: 20
        color: Qt.rgba(.08, .085, .1, memoController.backgroundOpacity)
        border.color: "#30ffffff"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 25
                    Layout.preferredHeight: 34
                    MouseArea { anchors.fill: parent; onPressed: memoWindow.startSystemMove() }
                }
                MemoButton { iconName: memoController.editing ? "edit" : "eye"; tip: memoController.editing ? qsTr("编辑中，点击切换为阅读") : qsTr("阅读中，点击切换为编辑"); Accessible.name: qsTr("切换编辑或阅读"); onClicked: memoController.editing = !memoController.editing }
                MemoButton { label: "A−"; enabled: memoController.fontSize > 16; Accessible.name: qsTr("缩小字号"); onClicked: memoController.fontSize -= 2 }
                MemoButton { label: "A+"; enabled: memoController.fontSize < 40; Accessible.name: qsTr("放大字号"); onClicked: memoController.fontSize += 2 }
                Slider {
                    id: opacitySlider
                    Layout.preferredWidth: 70
                    from: .55; to: 1
                    value: memoController.backgroundOpacity
                    Accessible.name: qsTr("背景不透明度")
                    onMoved: memoController.backgroundOpacity = value
                    background: Rectangle { x: opacitySlider.leftPadding; y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - 2; width: opacitySlider.availableWidth; height: 4; radius: 2; color: "#33ffffff"
                        Rectangle { width: opacitySlider.visualPosition * parent.width; height: 4; radius: 2; color: "#b3ffffff" } }
                    handle: Rectangle { x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width); y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - 7; width: 14; height: 14; radius: 7; color: "white" }
                }
                MemoButton { iconName: "close"; Accessible.name: qsTr("收起备忘录"); onClicked: memoWindow.close() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: memoText
                    text: memoController.text
                    placeholderText: qsTr("写下或粘贴要讲的内容…")
                    font.pixelSize: memoController.fontSize
                    font.family: StudioTheme.fontFamily
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    readOnly: !memoController.editing
                    selectByMouse: true
                    color: "#f1f4f8"
                    placeholderTextColor: "#8491a1"
                    background: null
                    Accessible.name: qsTr("备忘录文稿")
                    onTextChanged: memoController.text = text
                }
            }
            Label {
                Layout.fillWidth: true
                text: memoController.captureExcluded ? qsTr("Windows 已确认捕获排除请求") : qsTr("捕获排除未确认，备忘录可能被录入")
                color: memoController.captureExcluded ? "#8c929b" : "#f3bd70"
                font.family: StudioTheme.fontFamily
                font.pixelSize: 11
                wrapMode: Text.Wrap
                ToolTip.visible: noticeHover.hovered
                ToolTip.text: memoController.captureNotice
                HoverHandler { id: noticeHover }
            }
        }
        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 18; height: 18
            cursorShape: Qt.SizeFDiagCursor
            onPressed: memoWindow.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }
}

