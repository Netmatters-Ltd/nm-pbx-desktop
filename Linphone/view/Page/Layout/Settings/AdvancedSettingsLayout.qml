
import QtQuick
import QtQuick.Layouts
import SettingsCpp 1.0
import Linphone
import 'qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js' as Utils

AbstractSettingsLayout {
	width: parent?.width
	contentModel: [
		{
            //: System
            title: qsTr("settings_system_title"),
			subTitle: "",
			contentComponent: systemComponent
		}
	]

	onSave: {
		SettingsCpp.save()
	}
	onUndo: SettingsCpp.undo()

	// System
	/////////

	Component {
		id: systemComponent
		ColumnLayout {
            spacing: Utils.getSizeWithScreenRatio(40)
			SwitchSetting {
				Layout.fillWidth: true
                //: Auto start %1
                titleText: qsTr("settings_advanced_auto_start_title").arg(applicationName)
				propertyName: "autoStart"
				propertyOwner: SettingsCpp
			}
			// These two strings are already translated under the DebugSettingsLayout
			// context, so reuse them rather than duplicating them in every .ts file.
			SwitchSetting {
				Layout.fillWidth: true
				//: "Activer les traces de débogage"
				titleText: qsTranslate("DebugSettingsLayout", "settings_debug_enable_logs_title")
				propertyName: "logsEnabled"
				propertyOwner: SettingsCpp
			}
			SwitchSetting {
				Layout.fillWidth: true
				//: "Activer les traces de débogage intégrales"
				titleText: qsTranslate("DebugSettingsLayout", "settings_debug_enable_full_logs_title")
				propertyName: "fullLogsEnabled"
				propertyOwner: SettingsCpp
			}
		}
	}
}
