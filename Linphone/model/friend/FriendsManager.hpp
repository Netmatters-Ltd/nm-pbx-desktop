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

#ifndef FRIENDS_MANAGER_H_
#define FRIENDS_MANAGER_H_

#include "PhoneNumberIndex.hpp"
#include "tool/AbstractObject.hpp"
#include <QObject>
#include <QSharedPointer>
#include <QThread>
#include <QVariantMap>
#include <linphone++/linphone.hh>
#include <list>
#include <map>
#include <string>

class FriendsManager : public QObject, public AbstractObject {
	Q_OBJECT

public:
	FriendsManager(QObject *parent);
	~FriendsManager();
	static std::shared_ptr<FriendsManager> create(QObject *parent);
	static std::shared_ptr<FriendsManager> getInstance();

	// The one place an address becomes a cache key. Cleaning here keeps the same person on the
	// same key however their address reached us, so a call carrying ";user=phone" or a transport
	// parameter still matches the contact that a plain lookup would have found.
	static QString addressKey(const std::shared_ptr<const linphone::Address> &address);

	QVariantMap getKnownFriends() const;
	QVariantMap getUnknownFriends() const;
	QStringList getOtherAddresses() const;

	void clearMaps();

	std::shared_ptr<linphone::Friend> getKnownFriendAtKey(const QString &key);
	std::shared_ptr<linphone::Friend> getUnknownFriendAtKey(const QString &key);

	bool isInKnownFriends(const QString &key);
	bool isInUnknownFriends(const QString &key);
	bool isInOtherAddresses(const QString &key);

	void appendKnownFriend(std::shared_ptr<const linphone::Address> address, std::shared_ptr<linphone::Friend> f);
	void appendUnknownFriend(std::shared_ptr<const linphone::Address> address, std::shared_ptr<linphone::Friend> f);
	void appendOtherAddress(QString address);

	void removeUnknownFriend(const QString &key);
	void removeOtherAddress(const QString &key);

	// Drops every trace of a friend from all three caches. A contact can be cached under several
	// keys, one per address, so forgetting only the first would leave a stale name behind.
	void forgetFriend(const std::shared_ptr<linphone::Friend> &f);

	// Same answer as Core::findFriendByPhoneNumber, from an index rather than a scan of every
	// contact. See PhoneNumberIndex.
	std::shared_ptr<linphone::Friend> findFriendByPhoneNumber(const std::string &number);

	// Catches the changes nothing signals: a friend list added or removed, or an account whose dial
	// plan or domain has changed. Either can turn a remembered miss into a match. Cheap, so it is
	// meant to run before every lookup.
	void checkAddressBook();

	// The address book has changed, so anything we concluded from it may be wrong. Forgets every
	// remembered miss and the phone number index. Known friends are forgotten one by one, by
	// forgetFriend, since a change to one contact does not affect the others.
	void onAddressBookChanged();

private:
	class FriendListWatcher;

	static std::shared_ptr<FriendsManager> gFriendsManager;
	QVariantMap mKnownFriends;
	QVariantMap mUnknownFriends;
	// Addresses we looked up and found no contact for. Also marks that a remote directory search
	// has been asked for, so it is only asked once.
	QStringList mOtherAddresses;

	// One per account, keyed on its identity, as the SDK normalises with each account in turn.
	std::map<std::string, PhoneNumberIndex<std::shared_ptr<linphone::Friend>>> mPhoneNumberIndexes;
	std::shared_ptr<FriendListWatcher> mFriendListWatcher;
	// Dropped by checkAddressBook once a list is no longer in the core.
	std::list<std::shared_ptr<linphone::FriendList>> mWatchedFriendLists;
	std::string mLookupContext;
	DECLARE_ABSTRACT_OBJECT
};

#endif
