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

#include "FriendsManager.hpp"
#include "model/core/CoreModel.hpp"
#include "tool/Utils.hpp"
#include <QDebug>
#include <QUrl>

DEFINE_ABSTRACT_OBJECT(FriendsManager)

namespace {
// QMap::key() returns only the first key holding a value, so a contact cached under several of
// its addresses would otherwise be left half-evicted, still showing its old name on the rest.
void removeAllKeysFor(QVariantMap &map, const std::shared_ptr<linphone::Friend> &f) {
	auto value = QVariant::fromValue(f);
	for (auto it = map.begin(); it != map.end();) {
		if (it.value() == value) it = map.erase(it);
		else ++it;
	}
}

// Every address a friend holds, so invalidation covers all of them rather than just the default.
std::list<std::shared_ptr<const linphone::Address>> allAddressesOf(const std::shared_ptr<linphone::Friend> &f) {
	// getAddresses hands back mutable pointers and getAddress a const one, so the list is built by
	// hand rather than appended to.
	std::list<std::shared_ptr<const linphone::Address>> addresses;
	for (auto &address : f->getAddresses())
		addresses.push_back(address);
	auto defaultAddress = f->getAddress();
	if (defaultAddress) {
		bool alreadyListed = false;
		for (auto &address : addresses)
			if (address && address->weakEqual(defaultAddress)) alreadyListed = true;
		if (!alreadyListed) addresses.push_back(defaultAddress);
	}
	return addresses;
}
} // namespace

std::shared_ptr<FriendsManager> FriendsManager::gFriendsManager;
FriendsManager::FriendsManager(QObject *parent) : QObject(parent) {
	moveToThread(CoreModel::getInstance()->thread());

	connect(CoreModel::getInstance().get(), &CoreModel::friendRemoved, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) { forgetFriend(f); });
	connect(CoreModel::getInstance().get(), &CoreModel::friendCreated, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) {
		        // A newly created friend supersedes anything the magic search turned up for it, and
		        // clears the "we looked and found nothing" marker so the next call resolves. Known
		        // entries are left alone: they already point at a real friend.
		        removeAllKeysFor(mUnknownFriends, f);
		        for (auto &address : allAddressesOf(f))
			        mOtherAddresses.removeAll(addressKey(address));
	        });
	connect(CoreModel::getInstance().get(), &CoreModel::friendUpdated, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) { forgetFriend(f); });
}

FriendsManager::~FriendsManager() {
	clearMaps();
}

std::shared_ptr<FriendsManager> FriendsManager::create(QObject *parent) {
	auto model = std::make_shared<FriendsManager>(parent);
	return model;
}

std::shared_ptr<FriendsManager> FriendsManager::getInstance() {
	if (!gFriendsManager) gFriendsManager = FriendsManager::create(nullptr);
	return gFriendsManager;
}

QString FriendsManager::addressKey(const std::shared_ptr<const linphone::Address> &address) {
	if (!address) return QString();
	// clean() mutates in place and the address handed to us is often the SDK friend's own, so this
	// has to work on a copy.
	auto cleaned = address->clone();
	cleaned->clean();
	return Utils::coreStringToAppString(cleaned->asStringUriOnly());
}

void FriendsManager::forgetFriend(const std::shared_ptr<linphone::Friend> &f) {
	if (!f) return;
	removeAllKeysFor(mKnownFriends, f);
	removeAllKeysFor(mUnknownFriends, f);
	for (auto &address : allAddressesOf(f))
		mOtherAddresses.removeAll(addressKey(address));
}

QVariantMap FriendsManager::getKnownFriends() const {
	return mKnownFriends;
}

QVariantMap FriendsManager::getUnknownFriends() const {
	return mUnknownFriends;
}

QStringList FriendsManager::getOtherAddresses() const {
	return mOtherAddresses;
}

void FriendsManager::clearMaps() {
	mKnownFriends.clear();
	mUnknownFriends.clear();
	mOtherAddresses.clear();
}

std::shared_ptr<linphone::Friend> FriendsManager::getKnownFriendAtKey(const QString &key) {
	if (isInKnownFriends(key)) {
		return mKnownFriends.value(key).value<std::shared_ptr<linphone::Friend>>();
	} else return nullptr;
}

std::shared_ptr<linphone::Friend> FriendsManager::getUnknownFriendAtKey(const QString &key) {
	if (isInUnknownFriends(key)) {
		return mUnknownFriends.value(key).value<std::shared_ptr<linphone::Friend>>();
	} else return nullptr;
}

bool FriendsManager::isInKnownFriends(const QString &key) {
	return mKnownFriends.contains(key);
}

bool FriendsManager::isInUnknownFriends(const QString &key) {
	return mUnknownFriends.contains(key);
}

bool FriendsManager::isInOtherAddresses(const QString &key) {
	return mOtherAddresses.contains(key);
}

void FriendsManager::appendKnownFriend(std::shared_ptr<const linphone::Address> address,
                                       std::shared_ptr<linphone::Friend> f) {
	auto key = addressKey(address);
	if (mKnownFriends.contains(key)) {
		qDebug() << "friend is already in konwn list, return";
		return;
	}
	mKnownFriends.insert(key, QVariant::fromValue(f));
}

void FriendsManager::appendUnknownFriend(std::shared_ptr<const linphone::Address> address,
                                         std::shared_ptr<linphone::Friend> f) {
	auto key = addressKey(address);
	if (mUnknownFriends.contains(key)) {
		qDebug() << "friend is already in unkonwn list, return";
		return;
	}
	mUnknownFriends.insert(key, QVariant::fromValue(f));
}

void FriendsManager::appendOtherAddress(QString address) {
	if (mOtherAddresses.contains(address)) {
		qDebug() << "friend is already in other addresses, return";
		return;
	}
	mOtherAddresses.append(address);
}

void FriendsManager::removeUnknownFriend(const QString &key) {
	mUnknownFriends.remove(key);
}

void FriendsManager::removeOtherAddress(const QString &key) {
	mOtherAddresses.removeAll(key);
}
