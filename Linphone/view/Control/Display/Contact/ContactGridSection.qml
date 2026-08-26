import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import UtilsCpp
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

Item {
	id: mainItem

	property var model
	property FriendGui highlightedContact
	property real cellWidth: Utils.getSizeWithScreenRatio(180)
	property real cellSpacing: Utils.getSizeWithScreenRatio(8)

	signal contactSelected(FriendGui contact)

	implicitHeight: flow.implicitHeight

	Flow {
		id: flow
		anchors.left: parent.left
		anchors.right: parent.right
		spacing: mainItem.cellSpacing

		Repeater {
			id: repeater
			model: mainItem.model

			ContactGridItem {
				id: gridItem
				width: mainItem.cellWidth
				searchResultItem: $modelData
				isSelected: mainItem.highlightedContact === $modelData
				focus: isSelected

				onClicked: mouse => {
					mainItem.highlightedContact = $modelData
					mainItem.contactSelected($modelData)
				}
			}
		}
	}
}
