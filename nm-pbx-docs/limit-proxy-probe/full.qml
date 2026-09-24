import QtQuick
import Probe

ListView {
    id: mainItem
    width: 400; height: 660
    clip: true
    property bool ready: false
    property real scrollTo: -1
    onScrollToChanged: if (scrollTo >= 0) contentY = scrollTo

    model: LimitProxyType {
        id: callHistoryProxy
        initialDisplayItems: Math.max(20, Math.round(2 * mainItem.height / 56))
        displayItemsStep: 3 * initialDisplayItems / 2
        onModelReset: console.log("  [QML] proxy modelReset")
    }
    flickDeceleration: 10000
    spacing: 10

    Component.onCompleted: cacheBuffer = Math.max(mainItem.height, 0)
    onContentHeightChanged: Qt.callLater(function () {
        if (mainItem) mainItem.cacheBuffer = Math.max(contentHeight, 0) || 0
    })

    onCountChanged: {
        console.log("  [QML] onCountChanged count=" + count + " contentY=" + contentY.toFixed(1)
                    + " currentIndex=" + currentIndex + " atYBeginning=" + atYBeginning)
        if (currentIndex < 0 && count > 0) mainItem.currentIndex = 0
        if (atYBeginning) { console.log("  [QML]   -> positionViewAtBeginning()"); positionViewAtBeginning() }
    }
    onAtYEndChanged: {
        if (atYEnd && count > 0) {
            console.log("  [QML] atYEnd -> displayMore()  contentY=" + contentY.toFixed(1))
            callHistoryProxy.displayMore()
        }
    }
    function moveToCurrentItem() {
        if (mainItem.currentIndex >= 0) {
            var item = mainItem.itemAtIndex(mainItem.currentIndex) || mainItem.currentItem
            if (!item) { console.log("  [QML]   updatePosition: no item"); return }
            var topItemPos = item.y
            var bottomItemPos = topItemPos + item.height
            if (topItemPos < mainItem.contentY) {
                console.log("  [QML]   *** updatePosition SNAPS contentY " + mainItem.contentY.toFixed(1)
                            + " -> " + topItemPos.toFixed(1))
                mainItem.contentY = topItemPos
            } else if (bottomItemPos > mainItem.contentY + mainItem.height) {
                mainItem.contentY = bottomItemPos - mainItem.height
            }
        }
    }
    onCurrentItemChanged: {
        console.log("  [QML] onCurrentItemChanged currentItem=" + currentItem + " contentY=" + contentY.toFixed(1))
        moveToCurrentItem()
    }
    property var _currentItemY: currentItem ? currentItem.y : undefined
    on_CurrentItemYChanged: if (_currentItemY && moveAnimation.running) moveToCurrentItem()
    Behavior on contentY {
        NumberAnimation { id: moveAnimation; duration: 500; easing.type: Easing.OutExpo; alwaysRunToEnd: true }
    }
    delegate: Item {
        width: mainItem.width
        height: 56
        Text { text: model.display }
    }
}
