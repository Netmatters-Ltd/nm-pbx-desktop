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
        id: p
        initialDisplayItems: Math.max(20, Math.round(2 * mainItem.height / 56))
        displayItemsStep: 3 * initialDisplayItems / 2
    }
    spacing: 10
    onAtYEndChanged: if (atYEnd && count > 0) p.displayMore()
    delegate: Item { width: mainItem.width; height: 56; Text { text: model.display } }
}
