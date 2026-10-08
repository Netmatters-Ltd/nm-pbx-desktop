import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Control

import Linphone
import UtilsCpp 1.0
import ConstantsCpp 1.0
import SettingsCpp
import ContactsCpp
import "qrc:/qt/qml/Linphone/view/Control/Tool/Helper/utils.js" as Utils

Flickable {
    id: mainItem

    flickableDirection: Flickable.VerticalFlick

    property bool showInitials: true // Display Initials of Display name.
    property bool showActions: false // Display actions layout (call buttons)
    property bool showContactMenu: true // Display the dot menu for contacts.
    property bool showFavorites: true // Display the favorites in the header
    property bool hideSuggestions: false // Hide not stored contacts (not suggestions)
    property bool showMe: true // Wether to display current account address or not (disabled for adding participants)
    property string highlightText: searchText // Bold characters in Display name.
    property var sourceFlags: LinphoneEnums.MagicSearchSource.All
    property int extensionFilter: MagicSearchProxy.ExtensionFilter.All

    property bool displayNameCapitalization: true // Capitalize display name.

    property bool selectionEnabled: true // Contact can be selected
    property bool multiSelectionEnabled: false //Multiple items can be selected.
    property list<string> selectedContacts
    // List of default address on selected contacts.
    //property FriendGui selectedContact//: model.getAt(currentIndex) || null
    property FriendGui highlightedContact

    // When set, favourites and contacts come from the address book ContactsCpp already holds in
    // memory and are filtered locally, so the list appears at once instead of waiting on a fresh
    // search. Only suggestions (addresses that are not saved contacts, such as numbers from call
    // history) still need a live search, and that runs only once something has been typed.
    // Set once at creation. searchOnEmpty and pauseSearch do not apply in this mode.
    property bool useSharedContacts: false

    // When set, and extensionFilter is All, extensions get their own section above contacts rather
    // than being mixed in with them.
    property bool separateExtensions: false
    readonly property bool _splitExtensions: separateExtensions
                                             && extensionFilter === MagicSearchProxy.ExtensionFilter.All

    property bool searchOnEmpty: true
    // In shared mode the only wait is for the app's first load of the address book.
    property bool loading: useSharedContacts && !ContactsCpp.initialLoadComplete
    property bool pauseSearch: false // true = don't search on text change

    // Model properties
    // set searchBarText without specifying a model to bold
    // matching names
    property string searchBarText
    property string searchText
    // Binding is done on searchBarTextChanged
    property ConferenceInfoGui confInfoGui

    property bool haveFavorites: false
    property bool haveContacts: count > 0
    property real sectionsPixelSize: Typography.h4.pixelSize
    property real sectionsWeight: Typography.h4.weight
    property real sectionsSpacing: Utils.getSizeWithScreenRatio(18)
    property real busyIndicatorSize: Utils.getSizeWithScreenRatio(60)

    property real itemsRightMargin: Utils.getSizeWithScreenRatio(39)
    property int count: extensionsList.count + contactsList.count + suggestionsList.count + favoritesList.count

    contentHeight: contentsLayout.height
    rightMargin: itemsRightMargin

    signal contactStarredChanged
    signal contactDeletionRequested(FriendGui contact)
    signal contactAddedToSelection(string address)
    signal contactRemovedFromSelection(string address)
    signal contactSelected(FriendGui contact)

    function selectContact(address) {
        var index = contactsProxy.loadUntil(address) // Be sure to have this address in proxy if it exists
        if (index != -1) {
            contactsList.selectIndex(index)
        }
        return index
    }
    function addContactToSelection(address) {
        if (multiSelectionEnabled) {
            var indexInSelection = selectedContacts.indexOf(address)
            if (indexInSelection == -1) {
                selectedContacts.push(address)
                contactAddedToSelection(address)
            }
        }
    }
    function removeContactFromSelection(indexInSelection) {
        var addressToRemove = selectedContacts[indexInSelection]
        if (indexInSelection != -1) {
            selectedContacts.splice(indexInSelection, 1)
            contactRemovedFromSelection(addressToRemove)
        }
    }
    function removeSelectedContactByAddress(address) {
        var index = selectedContacts.indexOf(address)
        if (index != -1) {
            selectedContacts.splice(index, 1)
            contactRemovedFromSelection(address)
        }
    }
    function haveAddress(address) {
        var index = (mainItem.useSharedContacts ? ContactsCpp.rootProxy : magicSearchProxy).findFriendIndexByAddress(address)
        return index != -1
    }

    function resetSelections() {
        mainItem.highlightedContact = null
        favoritesList.currentIndex = -1
        extensionsList.currentIndex = -1
        contactsList.currentIndex = -1
        suggestionsList.currentIndex = -1
    }

    // Lists in display order. Moving past either end wraps round, and a list can come back to
    // itself when every other list is empty.
    function findNextList(item, direction) {
        var lists = [favoritesList, extensionsList, contactsList, suggestionsList]
        var index = lists.indexOf(item)
        if (index == -1)
            return null
        for (var i = 1; i <= lists.length; ++i) {
            var nextItem = lists[(index + direction * i + lists.length) % lists.length]
            if (nextItem.model.count > 0)
                return nextItem
        }
        return null
    }

    function updatePosition(list) {
        Utils.updatePosition(mainItem, list)
    }

    onHighlightedContactChanged: {
        favoritesList.highlightedContact = highlightedContact
        extensionsList.highlightedContact = highlightedContact
        contactsList.highlightedContact = highlightedContact
        suggestionsList.highlightedContact = highlightedContact
    }

    onSearchBarTextChanged: {
        if (!pauseSearch && (mainItem.searchOnEmpty || searchBarText != '')) {
            searchText = searchBarText.length === 0 ? "*" : searchBarText
        }
        // Local filtering is synchronous, so there is no result callback to scroll back on.
        if (mainItem.useSharedContacts)
            mainItem.contentY = 0
    }
    onPauseSearchChanged: {
        if (!pauseSearch && (mainItem.searchOnEmpty || searchBarText != '')) {
            searchText = searchBarText.length === 0 ? "*" : searchBarText
        }
    }
    onSearchTextChanged: {
        if (!mainItem.useSharedContacts)
            loading = true
    }

    Keys.onPressed: event => {
        if (!event.accepted) {
            if (event.key == Qt.Key_Up
                || event.key == Qt.Key_Down) {
                var newItem
                var direction = (event.key == Qt.Key_Up ? -1 : 1)
                if (suggestionsList.activeFocus)
                newItem = findNextList(suggestionsList, direction)
                else if (contactsList.activeFocus)
                newItem = findNextList(contactsList, direction)
                else if (extensionsList.activeFocus)
                newItem = findNextList(extensionsList, direction)
                else if (favoritesList.activeFocus)
                newItem = findNextList(favoritesList, direction)
                else
                newItem = findNextList(suggestionsList, direction)
                if (newItem) {
                    newItem.selectIndex(
                        direction > 0 ? -1 : newItem.model.count - 1)
                    event.accepted = true
                }
            }
        }
    }
    Component.onCompleted: {
        if (confInfoGui) {
            for (var i = 0; i < confInfoGui.core.participants.length; ++i) {
                selectedContacts.push(
                            confInfoGui.core.getParticipantAddressAt(i))
            }
        }
    }

    Connections {
        target: SettingsCpp
        onLdapConfigChanged: {
            if (SettingsCpp.syncLdapContacts
                && (!mainItem.useSharedContacts || magicSearchProxy.searchText != ''))
                magicSearchProxy.forceUpdate()
        }
    }

    property MagicSearchProxy mainModel: MagicSearchProxy {
        id: magicSearchProxy
        // In shared mode this search only feeds suggestions, so an empty search box means no
        // search at all: an empty search text clears the list rather than fetching everything.
        searchText: mainItem.useSharedContacts ? mainItem.searchBarText : mainItem.searchText
        aggregationFlag: LinphoneEnums.MagicSearchAggregation.Friend
        sourceFlags: mainItem.sourceFlags
        onModelReset: {
            mainItem.resetSelections()
        }
        onResultsProcessed: {
            if (mainItem.useSharedContacts)
                return
            mainItem.loading = false
            mainItem.contentY = 0
        }

        onInitialized: {
            if (mainItem.useSharedContacts) {
                if (searchText != '')
                    forceUpdate()
            } else if (mainItem.searchOnEmpty || searchText != '') {
                mainItem.loading = true
                forceUpdate()
            }
        }
    }

    onAtYEndChanged: if (atYEnd) {
        if (favoritesProxy.haveMore && favoritesList.expanded && mainItem.showFavorites)
            favoritesProxy.displayMore()
        else if (mainItem._splitExtensions && extensionsProxy.haveMore && extensionsList.expanded)
            extensionsProxy.displayMore()
        else if (contactsProxy.haveMore && contactsList.expanded) {
            contactsProxy.displayMore()
        }
        else
            suggestionsProxy.displayMore()
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
        topPadding: Utils.getSizeWithScreenRatio(24) // Avoid to be on top of collapse button
        active: true
        interactive: true
        visible: mainItem.contentHeight > mainItem.height
        policy: Control.ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: contentsLayout
        width: mainItem.width
        spacing: 0

        BusyIndicator {
            id: busyIndicator
            visible: mainItem.loading
            width: mainItem.busyIndicatorSize
            height: mainItem.busyIndicatorSize
            Layout.preferredWidth: mainItem.busyIndicatorSize
            Layout.preferredHeight: mainItem.busyIndicatorSize
            Layout.alignment: Qt.AlignCenter | Qt.AlignVCenter
        }

        ContactListView {
            id: favoritesList
            visible: contentHeight > 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            sectionsWeight: mainItem.sectionsWeight
            sectionsPixelSize: mainItem.sectionsPixelSize
            interactive: false
            highlightText: mainItem.highlightText
            showActions: mainItem.showActions
            showInitials: mainItem.showInitials
            showContactMenu: mainItem.showContactMenu
            showDefaultAddress: false
            selectionEnabled: mainItem.selectionEnabled
            multiSelectionEnabled: mainItem.multiSelectionEnabled
            selectedContacts: mainItem.selectedContacts
            //: "Favoris"
            title: qsTr("car_favorites_contacts_title")
            itemsRightMargin: mainItem.itemsRightMargin

            onHighlightedContactChanged: mainItem.highlightedContact = highlightedContact
            onContactSelected: contactGui => {
                                   mainItem.contactSelected(contactGui)
                               }
            onUpdatePosition: mainItem.updatePosition(favoritesList)
            onContactDeletionRequested: contact => {
                                            mainItem.contactDeletionRequested(
                                                contact)
                                        }
            onAddContactToSelection: address => {
                                         mainItem.addContactToSelection(address)
                                     }
            onRemoveContactFromSelection: index => {
                                              mainItem.removeContactFromSelection(
                                                  index)
                                          }

            property MagicSearchProxy proxy: MagicSearchProxy {
                id: favoritesProxy
                parentProxy: mainItem.useSharedContacts ? ContactsCpp.rootProxy : mainItem.mainModel
                // showMe is set on the underlying list, so in shared mode it would change what every
                // other view sees. Match the Contacts page, which always shows it.
                showMe: mainItem.useSharedContacts || mainItem.showMe
                extensionFilter: mainItem.extensionFilter
                filterText: mainItem.useSharedContacts ? mainItem.searchBarText : ""
                filterType: MagicSearchProxy.FilteringTypes.Favorites
            }
            model: mainItem.showFavorites
                   && (mainItem.searchBarText == ''
                       || mainItem.searchBarText == '*') ? proxy : []
        }

        ContactListView {
            id: extensionsList
            visible: contentHeight > 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            Layout.topMargin: favoritesList.height > 0 ? Utils.getSizeWithScreenRatio(4) : 0
            interactive: false
            highlightText: mainItem.highlightText
            showActions: mainItem.showActions
            showInitials: mainItem.showInitials
            showContactMenu: mainItem.showContactMenu
            showDefaultAddress: false
            selectionEnabled: mainItem.selectionEnabled
            multiSelectionEnabled: mainItem.multiSelectionEnabled
            selectedContacts: mainItem.selectedContacts
            itemsRightMargin: mainItem.itemsRightMargin
            //: "Extensions"
            title: qsTr("generic_address_picker_extensions_list_title")

            onHighlightedContactChanged: mainItem.highlightedContact = highlightedContact
            onContactSelected: contactGui => {
                                   mainItem.contactSelected(contactGui)
                               }
            onUpdatePosition: mainItem.updatePosition(extensionsList)
            onContactDeletionRequested: contact => {
                                            mainItem.contactDeletionRequested(
                                                contact)
                                        }
            onAddContactToSelection: address => {
                                         mainItem.addContactToSelection(address)
                                     }
            onRemoveContactFromSelection: index => {
                                              mainItem.removeContactFromSelection(
                                                  index)
                                          }

            // Same sources as the contacts section below, limited to extensions.
            property MagicSearchProxy proxy: MagicSearchProxy {
                id: extensionsProxy
                parentProxy: mainItem.useSharedContacts ? ContactsCpp.rootProxy : mainItem.mainModel
                extensionFilter: MagicSearchProxy.ExtensionFilter.Extensions
                filterText: contactsProxy.filterText
                filterType: contactsProxy.filterType
                initialDisplayItems: contactsProxy.initialDisplayItems
                displayItemsStep: contactsProxy.displayItemsStep
                onLocalFriendCreated: (index) => {
                    extensionsList.selectIndex(index)
                }
            }
            model: mainItem._splitExtensions ? proxy : []
        }

        ContactListView {
            id: contactsList
            visible: contentHeight > 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            Layout.topMargin: (extensionsList.height + favoritesList.height) > 0 ? Utils.getSizeWithScreenRatio(4) : 0
            interactive: false
            highlightText: mainItem.highlightText
            showActions: mainItem.showActions
            showInitials: mainItem.showInitials
            showContactMenu: mainItem.showContactMenu
            showDefaultAddress: false
            selectionEnabled: mainItem.selectionEnabled
            multiSelectionEnabled: mainItem.multiSelectionEnabled
            selectedContacts: mainItem.selectedContacts
            itemsRightMargin: mainItem.itemsRightMargin
            //: 'Contacts'
            title: qsTr("generic_address_picker_contacts_list_title")

            onHighlightedContactChanged: mainItem.highlightedContact = highlightedContact
            onContactSelected: contactGui => {
                                   mainItem.contactSelected(contactGui)
                               }
            onUpdatePosition: mainItem.updatePosition(contactsList)
            onContactDeletionRequested: contact => {
                                            mainItem.contactDeletionRequested(
                                                contact)
                                        }
            onAddContactToSelection: address => {
                                         mainItem.addContactToSelection(address)
                                     }
            onRemoveContactFromSelection: index => {
                                              mainItem.removeContactFromSelection(
                                                  index)
                                          }

            model: MagicSearchProxy {
                id: contactsProxy
                parentProxy: mainItem.useSharedContacts ? ContactsCpp.rootProxy : mainItem.mainModel
                extensionFilter: mainItem._splitExtensions ? MagicSearchProxy.ExtensionFilter.Contacts
                                                           : mainItem.extensionFilter
                filterText: mainItem.useSharedContacts ? mainItem.searchBarText : ""
                // The shared list holds the whole synced address book, so CardDAV and LDAP contacts
                // always belong here, as on the Contacts page.
                filterType: MagicSearchProxy.FilteringTypes.App
                            | (mainItem.useSharedContacts
                               || mainItem.searchText != '*'
                               && mainItem.searchText != ''
                               || SettingsCpp.syncLdapContacts ? MagicSearchProxy.FilteringTypes.Ldap | MagicSearchProxy.FilteringTypes.CardDAV : 0)
                initialDisplayItems: Math.max(20, Math.round(2 * mainItem.height / Utils.getSizeWithScreenRatio(63)))
                displayItemsStep: 3 * initialDisplayItems / 2
                onLocalFriendCreated: (index) => {
                    contactsList.selectIndex(index)
                }
            }
        }
        ContactListView {
            id: suggestionsList
            visible: contentHeight > 0
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            Layout.topMargin: (contactsList.height + extensionsList.height + favoritesList.height) > 0 ? Utils.getSizeWithScreenRatio(4) : 0
            interactive: false
            showInitials: false
            highlightText: mainItem.highlightText
            showActions: mainItem.showActions
            showContactMenu: mainItem.showContactMenu
            showDefaultAddress: true
            showDisplayName: false
            selectionEnabled: mainItem.selectionEnabled
            multiSelectionEnabled: mainItem.multiSelectionEnabled
            selectedContacts: mainItem.selectedContacts
            //: "Suggestions"
            title: qsTr("generic_address_picker_suggestions_list_title")
            itemsRightMargin: mainItem.itemsRightMargin

            onHighlightedContactChanged: mainItem.highlightedContact = highlightedContact
            onContactSelected: contactGui => {
                                   mainItem.contactSelected(contactGui)
                               }
            onUpdatePosition: mainItem.updatePosition(suggestionsList)
            onContactDeletionRequested: contact => {
                                            mainItem.contactDeletionRequested(
                                                contact)
                                        }
            onAddContactToSelection: address => {
                                         mainItem.addContactToSelection(address)
                                     }
            onRemoveContactFromSelection: index => {
                                              mainItem.removeContactFromSelection(
                                                  index)
                                          }
            model: MagicSearchProxy {
                id: suggestionsProxy
                parentProxy: mainItem.mainModel
                extensionFilter: mainItem.extensionFilter
                filterType: mainItem.hideSuggestions ? MagicSearchProxy.FilteringTypes.None : MagicSearchProxy.FilteringTypes.Other
                // Hold suggestions back while either section above still has more to page in.
                initialDisplayItems: (contactsProxy.haveMore && contactsList.expanded)
                                     || (mainItem._splitExtensions && extensionsProxy.haveMore && extensionsList.expanded)
                    ? 0
                    : Math.max(20, Math.round(2 * mainItem.height / Utils.getSizeWithScreenRatio(63)))
                onInitialDisplayItemsChanged: maxDisplayItems = initialDisplayItems
                displayItemsStep: 3 * initialDisplayItems / 2
                onModelReset: maxDisplayItems = initialDisplayItems
            }
        }
    }
}
