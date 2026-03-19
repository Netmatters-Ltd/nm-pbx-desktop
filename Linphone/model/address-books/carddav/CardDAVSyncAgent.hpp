/*
 * Copyright (c) 2010-2024 Belledonne Communications SARL.
 *
 * This file is part of linphone-desktop
 * (see https://www.linphone.org).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CARDDAV_SYNC_AGENT_HPP
#define CARDDAV_SYNC_AGENT_HPP

#include "core/App.hpp"
#include "core/setting/SettingsCore.hpp"
#include <linphone++/linphone.hh>
#include <QMetaObject>
#include <memory>

// Attaches to a CardDAV FriendList as a listener, triggers a sync, and
// emits cardDAVAddressBookSynchronized on the main thread when done so
// that the contact list UI refreshes. Manages its own lifetime via a
// self-referential shared_ptr that is cleared on completion.
class CardDAVSyncAgent : public linphone::FriendListListener,
                         public std::enable_shared_from_this<CardDAVSyncAgent> {
public:
	void start(const std::shared_ptr<linphone::FriendList> &friendList) {
		mFriendList = friendList;
		mKeepAlive = shared_from_this();
		mFriendList->addListener(shared_from_this());
		mFriendList->synchronizeFriendsFromServer();
	}

	void onSyncStatusChanged(const std::shared_ptr<linphone::FriendList> &,
	                         linphone::FriendList::SyncStatus status,
	                         const std::string &) override {
		if (status == linphone::FriendList::SyncStatus::Successful ||
		    status == linphone::FriendList::SyncStatus::Failure) {
			mFriendList->removeListener(shared_from_this());
			mFriendList = nullptr;
			if (status == linphone::FriendList::SyncStatus::Successful) {
				QMetaObject::invokeMethod(
				    App::getInstance()->getSettings().get(),
				    []() { emit App::getInstance()->getSettings()->cardDAVAddressBookSynchronized(); },
				    Qt::QueuedConnection);
			}
			mKeepAlive = nullptr;
		}
	}

private:
	std::shared_ptr<linphone::FriendList> mFriendList;
	std::shared_ptr<CardDAVSyncAgent> mKeepAlive;
};

#endif // CARDDAV_SYNC_AGENT_HPP
