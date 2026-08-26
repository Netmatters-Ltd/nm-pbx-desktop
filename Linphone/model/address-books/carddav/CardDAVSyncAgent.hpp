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

#include <QDebug>
#include <QString>
#include <linphone++/linphone.hh>
#include <functional>
#include <memory>

// Attaches to a CardDAV FriendList as a listener, triggers a sync, and calls the optional
// completion callback (on the Linphone thread) when the sync finishes, passing whether it
// succeeded. Manages its own lifetime.
class CardDAVSyncAgent : public linphone::FriendListListener,
                         public std::enable_shared_from_this<CardDAVSyncAgent> {
public:
	// Receives true on success, false on failure. Callers need the failure case so a stalled or
	// rejected sync can clear its in-progress state rather than hanging.
	using CompletionCallback = std::function<void(bool succeeded)>;

	void start(const std::shared_ptr<linphone::FriendList> &friendList,
	           CompletionCallback onComplete = nullptr) {
		mFriendList = friendList;
		mOnComplete = std::move(onComplete);
		mKeepAlive = shared_from_this();
		mFriendList->addListener(shared_from_this());
		mFriendList->synchronizeFriendsFromServer();
	}

	void onSyncStatusChanged(const std::shared_ptr<linphone::FriendList> &,
	                         linphone::FriendList::SyncStatus status,
	                         const std::string &message) override {
		if (status == linphone::FriendList::SyncStatus::Successful ||
		    status == linphone::FriendList::SyncStatus::Failure) {
			bool succeeded = status == linphone::FriendList::SyncStatus::Successful;
			if (!succeeded) {
				qWarning() << "[CardDAVSyncAgent] Address book sync failed:" << QString::fromStdString(message);
			}
			mFriendList->removeListener(shared_from_this());
			mFriendList = nullptr;
			if (mOnComplete) {
				mOnComplete(succeeded);
			}
			mKeepAlive = nullptr;
		}
	}

private:
	std::shared_ptr<linphone::FriendList> mFriendList;
	std::shared_ptr<CardDAVSyncAgent> mKeepAlive;
	CompletionCallback mOnComplete;
};

#endif // CARDDAV_SYNC_AGENT_HPP
