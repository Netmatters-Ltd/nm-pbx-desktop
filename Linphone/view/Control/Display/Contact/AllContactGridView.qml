import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import ContactsCpp
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

Flickable {
    id: mainItem

    flickableDirection: Flickable.VerticalFlick

    property bool showMe: true
    property bool hideSuggestions: false
    property bool showFavorites: true
    property int extensionFilter: MagicSearchProxy.ExtensionFilter.All

    property FriendGui highlightedContact

    // The address book is fetched once by ContactsCpp and shared by every view, so the only time
    // there is nothing to show is before that first fetch completes.
    readonly property bool loading: !ContactsCpp.initialLoadComplete

    // Searching is a local filter over the shared result set, which is what allows each view to
    // hold its own search text without disturbing the others.
    property string searchBarText
    readonly property string searchText: searchBarText

    property bool haveContacts: count > 0
    property int count: (favoritesSection.model ? favoritesSection.model.count || 0 : 0)
                        + (contactsProxy ? contactsProxy.count || 0 : 0)
                        + (suggestionsProxy ? suggestionsProxy.count || 0 : 0)

    property real cellWidth: Utils.getSizeWithScreenRatio(180)
    property real cellSpacing: Utils.getSizeWithScreenRatio(8)

    contentHeight: contentsLayout.implicitHeight

    signal contactSelected(FriendGui contact)

    function selectContact(address) {
        var index = contactsProxy.loadUntil(address)
        return index
    }

    function resetSelections() {
        mainItem.highlightedContact = null
    }

    // Filtering happens locally and synchronously, so scroll back to the top as the text changes
    // rather than waiting on a result callback.
    onSearchTextChanged: mainItem.contentY = 0

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

    onAtYEndChanged: if (atYEnd) {
        if (contactsProxy.haveMore)
            contactsProxy.displayMore()
        else if (suggestionsProxy.count > 0)
            suggestionsProxy.displayMore()
    }

    MagicSearchProxy {
        id: favoritesProxy
        parentProxy: ContactsCpp.rootProxy
        showMe: mainItem.showMe
        extensionFilter: mainItem.extensionFilter
        filterText: mainItem.searchText
        filterType: MagicSearchProxy.FilteringTypes.Favorites
    }

    MagicSearchProxy {
        id: contactsProxy
        parentProxy: ContactsCpp.rootProxy
        extensionFilter: mainItem.extensionFilter
        filterText: mainItem.searchText
        // CardDAV and LDAP contacts belong in the main list rather than in suggestions, whether or
        // not a search is running: they are all stored contacts as far as the user is concerned.
        filterType: MagicSearchProxy.FilteringTypes.App
                    | MagicSearchProxy.FilteringTypes.Ldap
                    | MagicSearchProxy.FilteringTypes.CardDAV
        initialDisplayItems: Math.max(20, Math.round(2 * mainItem.height / Utils.getSizeWithScreenRatio(63)))
        displayItemsStep: 3 * initialDisplayItems / 2
    }

    MagicSearchProxy {
        id: suggestionsProxy
        parentProxy: ContactsCpp.rootProxy
        extensionFilter: mainItem.extensionFilter
        filterText: mainItem.searchText
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
        }
    }
}
