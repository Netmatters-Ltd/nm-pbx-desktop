import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import UtilsCpp
import SettingsCpp
import "qrc:/qt/qml/Linphone/view/Style/buttonStyle.js" as ButtonStyle
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

FocusScope {
	id: mainItem
	property var searchResultItem
	property bool isSelected: false
	property bool isHighlighted: false

	property var displayName: searchResultItem ? searchResultItem.core.fullName : ""
	property string presenceNote: searchResultItem ? searchResultItem.core.presenceNote : ""

	signal clicked(var mouse)

	implicitWidth: Utils.getSizeWithScreenRatio(180)
	implicitHeight: Utils.getSizeWithScreenRatio(130)

	Accessible.name: displayName

	MouseArea {
		id: contactArea
		anchors.fill: parent
		hoverEnabled: true
		acceptedButtons: Qt.AllButtons

		Rectangle {
			anchors.fill: parent
			radius: Utils.getSizeWithScreenRatio(8)
			opacity: 0.7
			color: mainItem.isSelected ? DefaultStyle.main2_200
				: contactArea.containsMouse ? DefaultStyle.main2_100
				: "transparent"
			visible: mainItem.isSelected || contactArea.containsMouse
		}

		Keys.onPressed: event => {
			if (event.key == Qt.Key_Space
				|| event.key == Qt.Key_Enter
				|| event.key == Qt.Key_Return) {
				mainItem.clicked(undefined)
				event.accepted = true
			}
		}

		onClicked: mouse => {
			mainItem.forceActiveFocus()
			mainItem.clicked(mouse)
		}

		ColumnLayout {
			anchors.fill: parent
			anchors.margins: Utils.getSizeWithScreenRatio(8)
			spacing: Utils.getSizeWithScreenRatio(6)
			z: contactArea.z + 1

			Item { Layout.fillHeight: true }

			Avatar {
				Layout.preferredWidth: Utils.getSizeWithScreenRatio(60)
				Layout.preferredHeight: Utils.getSizeWithScreenRatio(60)
				Layout.alignment: Qt.AlignHCenter
				contact: mainItem.searchResultItem
				displayPresence: true
				shadowEnabled: false
			}

			Text {
				Layout.fillWidth: true
				Layout.alignment: Qt.AlignHCenter
				horizontalAlignment: Text.AlignHCenter
				text: mainItem.displayName || ""
				font {
					pixelSize: Typography.p2.pixelSize
					weight: Typography.p2.weight
					capitalization: Font.Capitalize
				}
				maximumLineCount: 1
				elide: Text.ElideRight
			}

			Text {
				Layout.fillWidth: true
				Layout.alignment: Qt.AlignHCenter
				horizontalAlignment: Text.AlignHCenter
				visible: mainItem.presenceNote.length > 0
				text: mainItem.presenceNote
				color: DefaultStyle.main2_400
				font {
					pixelSize: Utils.getSizeWithScreenRatio(11)
					weight: 300
				}
				maximumLineCount: 1
				elide: Text.ElideRight
			}

			Item { Layout.fillHeight: true }
		}
	}
}
