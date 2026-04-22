import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import UtilsCpp 1.0
import ConstantsCpp 1.0
import SettingsCpp
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

Flickable {
    id: mainItem

    flickableDirection: Flickable.VerticalFlick

    property bool showMe: true
    property bool hideSuggestions: false
    property bool showFavorites: true
    property var sourceFlags: LinphoneEnums.MagicSearchSource.All

    property FriendGui highlightedContact

    property bool searchOnEmpty: true
    property bool loading: false
    property bool pauseSearch: false

    property string searchBarText
    property string searchText

    property bool haveContacts: count > 0
    property int count: (favoritesSection.model ? favoritesSection.model.count || 0 : 0)
                        + (contactsProxy ? contactsProxy.count || 0 : 0)
                        + (suggestionsProxy ? suggestionsProxy.count || 0 : 0)

    property real cellWidth: Utils.getSizeWithScreenRatio(180)
    property real cellSpacing: Utils.getSizeWithScreenRatio(8)

    contentHeight: contentsLayout.implicitHeight

    signal contactDeletionRequested(FriendGui contact)
    signal contactSelected(FriendGui contact)

    function selectContact(address) {
        var index = contactsProxy.loadUntil(address)
        return index
    }

    function resetSelections() {
        mainItem.highlightedContact = null
    }

    function forceFullRefresh() {
        mainItem.loading = true
        mainItem.resetSelections()
        magicSearchProxy.forceUpdate()
    }

    onSearchBarTextChanged: {
        if (!pauseSearch && (mainItem.searchOnEmpty || searchBarText != '')) {
            searchText = searchBarText.length === 0 ? "*" : searchBarText
        }
    }
    onPauseSearchChanged: {
        if (!pauseSearch && (mainItem.searchOnEmpty || searchBarText != '')) {
            searchText = searchBarText.length === 0 ? "*" : searchBarText
        }
    }
    onSearchTextChanged: {
        loading = true
    }

    Behavior on contentY {
        NumberAnimation {
            duration: 500
            easing.type: Easing.OutExpo
        }
    }

    Control.ScrollBar.vertical: ScrollBar {
        id: scrollbar
        z: 1
        active: true
        interactive: true
        visible: mainItem.contentHeight > mainItem.height
        policy: Control.ScrollBar.AsNeeded
    }

    property MagicSearchProxy mainModel: MagicSearchProxy {
        id: magicSearchProxy
        searchText: mainItem.searchText
        aggregationFlag: LinphoneEnums.MagicSearchAggregation.Friend
        sourceFlags: mainItem.sourceFlags
        onModelReset: {
            mainItem.resetSelections()
        }
        onResultsProcessed: {
            mainItem.loading = false
            mainItem.contentY = 0
        }
        onInitialized: {
            if (mainItem.searchOnEmpty || searchText != '') {
                mainItem.loading = true
                forceUpdate()
            }
        }
    }

    Connections {
        target: SettingsCpp
        onLdapConfigChanged: {
            if (SettingsCpp.syncLdapContacts)
                magicSearchProxy.forceUpdate()
        }
    }

    onAtYEndChanged: if (atYEnd) {
        if (contactsProxy.haveMore)
            contactsProxy.displayMore()
        else if (suggestionsProxy.count > 0)
            suggestionsProxy.displayMore()
    }

    MagicSearchProxy {
        id: favoritesProxy
        parentProxy: mainItem.mainModel
        showMe: mainItem.showMe
        filterType: MagicSearchProxy.FilteringTypes.Favorites
    }

    MagicSearchProxy {
        id: contactsProxy
        parentProxy: mainItem.mainModel
        filterType: MagicSearchProxy.FilteringTypes.App
                    | (mainItem.searchText != '*'
                       && mainItem.searchText != ''
                       || SettingsCpp.syncLdapContacts ? MagicSearchProxy.FilteringTypes.Ldap | MagicSearchProxy.FilteringTypes.CardDAV : 0)
        initialDisplayItems: Math.max(20, Math.round(2 * mainItem.height / Utils.getSizeWithScreenRatio(63)))
        displayItemsStep: 3 * initialDisplayItems / 2
    }

    MagicSearchProxy {
        id: suggestionsProxy
        parentProxy: mainItem.mainModel
        filterType: mainItem.hideSuggestions ? MagicSearchProxy.FilteringTypes.None : MagicSearchProxy.FilteringTypes.Other
        initialDisplayItems: contactsProxy.haveMore
            ? 0
            : Math.max(20, Math.round(2 * mainItem.height / Utils.getSizeWithScreenRatio(63)))
        onInitialDisplayItemsChanged: maxDisplayItems = initialDisplayItems
        displayItemsStep: 3 * initialDisplayItems / 2
        onModelReset: maxDisplayItems = initialDisplayItems
    }

    ColumnLayout {
        id: contentsLayout
        width: mainItem.width
        spacing: mainItem.cellSpacing

        BusyIndicator {
            visible: mainItem.loading
            width: Utils.getSizeWithScreenRatio(60)
            height: Utils.getSizeWithScreenRatio(60)
            Layout.preferredWidth: Utils.getSizeWithScreenRatio(60)
            Layout.preferredHeight: Utils.getSizeWithScreenRatio(60)
            Layout.alignment: Qt.AlignCenter | Qt.AlignVCenter
        }

        ContactGridSection {
            id: favoritesSection
            visible: model && model.count > 0
            Layout.fillWidth: true
            cellWidth: mainItem.cellWidth
            cellSpacing: mainItem.cellSpacing
            highlightedContact: mainItem.highlightedContact
            model: mainItem.showFavorites
                   && (mainItem.searchBarText == ''
                       || mainItem.searchBarText == '*') ? favoritesProxy : []

            onHighlightedContactChanged: {
                mainItem.highlightedContact = favoritesSection.highlightedContact
            }
            onContactSelected: contact => {
                mainItem.contactSelected(contact)
            }
            onContactDeletionRequested: contact => {
                mainItem.contactDeletionRequested(contact)
            }
        }

        ContactGridSection {
            id: contactsSection
            visible: model && model.count > 0
            Layout.fillWidth: true
            cellWidth: mainItem.cellWidth
            cellSpacing: mainItem.cellSpacing
            highlightedContact: mainItem.highlightedContact
            model: contactsProxy

            onHighlightedContactChanged: {
                mainItem.highlightedContact = contactsSection.highlightedContact
            }
            onContactSelected: contact => {
                mainItem.contactSelected(contact)
            }
            onContactDeletionRequested: contact => {
                mainItem.contactDeletionRequested(contact)
            }
        }

        ContactGridSection {
            id: suggestionsSection
            visible: model && model.count > 0
            Layout.fillWidth: true
            cellWidth: mainItem.cellWidth
            cellSpacing: mainItem.cellSpacing
            highlightedContact: mainItem.highlightedContact
            model: suggestionsProxy

            onHighlightedContactChanged: {
                mainItem.highlightedContact = suggestionsSection.highlightedContact
            }
            onContactSelected: contact => {
                mainItem.contactSelected(contact)
            }
            onContactDeletionRequested: contact => {
                mainItem.contactDeletionRequested(contact)
            }
        }
    }
}
