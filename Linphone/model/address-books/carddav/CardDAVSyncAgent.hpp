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

#include <linphone++/linphone.hh>
#include <functional>
#include <memory>

// Attaches to a CardDAV FriendList as a listener, triggers a sync, and
// calls the optional onSuccess callback (on the Linphone thread) when
// the sync completes successfully. Manages its own lifetime.
class CardDAVSyncAgent : public linphone::FriendListListener,
                         public std::enable_shared_from_this<CardDAVSyncAgent> {
public:
	using SuccessCallback = std::function<void()>;

	void start(const std::shared_ptr<linphone::FriendList> &friendList,
	           SuccessCallback onSuccess = nullptr) {
		mFriendList = friendList;
		mOnSuccess = std::move(onSuccess);
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
			if (status == linphone::FriendList::SyncStatus::Successful && mOnSuccess) {
				mOnSuccess();
			}
			mKeepAlive = nullptr;
		}
	}

private:
	std::shared_ptr<linphone::FriendList> mFriendList;
	std::shared_ptr<CardDAVSyncAgent> mKeepAlive;
	SuccessCallback mOnSuccess;
};

#endif // CARDDAV_SYNC_AGENT_HPP
