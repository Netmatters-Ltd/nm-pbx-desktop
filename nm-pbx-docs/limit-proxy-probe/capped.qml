import QtQuick
import Probe

ListView {
    id: mainItem
    width: 400; height: 660
    clip: true
    property real scrollTo: -1
    onScrollToChanged: if (scrollTo >= 0) contentY = scrollTo
    property int displayLimit: 100
    model: LimitProxyType {
        id: callHistoryProxy
        displayLimit: mainItem.displayLimit
        initialDisplayItems: mainItem.displayLimit
    }
    spacing: 10
    Component.onCompleted: cacheBuffer = Math.max(mainItem.height, 0)
    onContentHeightChanged: Qt.callLater(function () {
        if (mainItem) mainItem.cacheBuffer = Math.max(contentHeight, 0) || 0
    })
    onCountChanged: {
        if (currentIndex < 0 && count > 0) mainItem.currentIndex = 0
        if (atYBeginning) positionViewAtBeginning()
    }
    function moveToCurrentItem() {
        if (currentIndex < 0) return
        var item = itemAtIndex(currentIndex) || currentItem
        if (!item) return
        if (item.y < contentY) contentY = item.y
        else if (item.y + item.height > contentY + height) contentY = item.y + item.height - height
    }
    onCurrentItemChanged: moveToCurrentItem()
    Behavior on contentY {
        NumberAnimation { id: moveAnimation; duration: 500; easing.type: Easing.OutExpo; alwaysRunToEnd: true }
    }
    footer: Text {
        width: mainItem.width
        visible: callHistoryProxy.haveMore
        height: visible ? implicitHeight + 16 : 0
        text: "Showing your most recent " + mainItem.displayLimit + " calls."
    }
    delegate: Item { width: mainItem.width; height: 56; Text { text: model.display } }
}
