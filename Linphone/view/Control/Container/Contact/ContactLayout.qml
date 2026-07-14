import QtQuick
import QtQuick.Layouts
import Linphone
import UtilsCpp
import SettingsCpp
import 'qrc:/qt/qml/Linphone/view/Style/buttonStyle.js' as ButtonStyle
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

ColumnLayout {
	id: mainItem
    spacing: Utils.getSizeWithScreenRatio(30)

	property FriendGui contact

	property alias button: rightButton
	property alias content: detailLayout.data
	property alias bannerContent: bannerLayout.data
	property alias secondLineContent: verticalLayoutSecondLine.data
    property real minimumWidthForSwitchintToRowLayout: Utils.getSizeWithScreenRatio(756)
    property var useVerticalLayout: width < minimumWidthForSwitchintToRowLayout

	ColumnLayout {
		Layout.leftMargin: Utils.getSizeWithScreenRatio(64)
		Layout.rightMargin: Utils.getSizeWithScreenRatio(64)
		Layout.topMargin: Utils.getSizeWithScreenRatio(32)
		Layout.fillWidth: true
		spacing: Utils.getSizeWithScreenRatio(12)

		Avatar {
			id: avatar
			contact: mainItem.contact
			Layout.preferredWidth: Utils.getSizeWithScreenRatio(80)
			Layout.preferredHeight: Utils.getSizeWithScreenRatio(80)
			Layout.alignment: Qt.AlignHCenter
		}

		ColumnLayout {
			id: bannerLayout
			Layout.fillWidth: true
			spacing: Utils.getSizeWithScreenRatio(4)
		}

		PresenceNoteLayout {
			visible: contact?.core.presenceNote.length > 0
			friendGui: contact
			Layout.preferredWidth: Utils.getSizeWithScreenRatio(412)
			Layout.preferredHeight: Utils.getSizeWithScreenRatio(85)
			Layout.alignment: Qt.AlignHCenter
		}

		Item {
			id: verticalLayoutSecondLine
			visible: mainItem.useVerticalLayout
			Layout.alignment: Qt.AlignHCenter
			Layout.preferredWidth: childrenRect.width
			Layout.preferredHeight: childrenRect.height
		}

		MediumButton {
			id: rightButton
			Layout.alignment: Qt.AlignHCenter
			style: ButtonStyle.main
		}
	}
	StackLayout {
		id: detailLayout
		Layout.alignment: Qt.AlignCenter
        Layout.topMargin: mainItem.useVerticalLayout ? 0 : Utils.getSizeWithScreenRatio(30)
        Layout.leftMargin: Utils.getSizeWithScreenRatio(64)
        Layout.rightMargin: Utils.getSizeWithScreenRatio(64)
        Layout.bottomMargin: Utils.getSizeWithScreenRatio(53)
		Layout.fillWidth: true
		Layout.fillHeight: true
	}
}
