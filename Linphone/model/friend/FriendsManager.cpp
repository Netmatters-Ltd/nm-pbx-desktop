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
#include <algorithm>

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

std::string accountKey(const std::shared_ptr<const linphone::AccountParams> &params) {
	auto identity = params ? params->getIdentityAddress() : nullptr;
	return identity ? identity->asStringUriOnly() : std::string();
}

// The two account settings Account::normalizePhoneNumber reads.
std::string dialPlanOf(const std::shared_ptr<const linphone::AccountParams> &params) {
	if (!params) return std::string();
	return params->getInternationalPrefix() + (params->dialEscapePlusEnabled() ? "|+" : "|-");
}

// Everything a remembered miss depends on besides the contacts themselves: each account's dial
// plan, which the phone number lookup uses, and the default account's domain, which
// rebaseOnAccountDomain in ToolModel rewrites onto.
std::string lookupContextOf(const std::shared_ptr<linphone::Core> &core) {
	std::string context;
	auto defaultAccount = core->getDefaultAccount();
	auto defaultParams = defaultAccount ? defaultAccount->getParams() : nullptr;
	if (defaultParams) context += accountKey(defaultParams) + "|" + defaultParams->getDomain() + "\n";
	for (auto &account : core->getAccountList()) {
		auto params = account->getParams();
		context += accountKey(params) + "|" + dialPlanOf(params) + "\n";
	}
	return context;
}
} // namespace

// Hears about the changes the app does not make itself, mainly a CardDAV sync adding, changing or
// deleting contacts. The SDK keeps hold of it, so it reaches the manager through a pointer the
// manager clears when it goes, rather than keeping the manager alive.
class FriendsManager::FriendListWatcher : public linphone::FriendListListener {
public:
	FriendsManager *mOwner = nullptr;

	void onContactCreated(const std::shared_ptr<linphone::FriendList> &,
	                      const std::shared_ptr<linphone::Friend> &) override {
		if (mOwner) mOwner->onAddressBookChanged();
	}
	void onContactDeleted(const std::shared_ptr<linphone::FriendList> &,
	                      const std::shared_ptr<linphone::Friend> &linphoneFriend) override {
		if (!mOwner) return;
		mOwner->forgetFriend(linphoneFriend);
		mOwner->onAddressBookChanged();
	}
	void onContactUpdated(const std::shared_ptr<linphone::FriendList> &,
	                      const std::shared_ptr<linphone::Friend> &,
	                      const std::shared_ptr<linphone::Friend> &oldFriend) override {
		if (!mOwner) return;
		mOwner->forgetFriend(oldFriend);
		mOwner->onAddressBookChanged();
	}
	// A safety net. The contact callbacks above should already have covered whatever the sync
	// changed, and forgetting is cheap now that rebuilding the index is one pass.
	void onSyncStatusChanged(const std::shared_ptr<linphone::FriendList> &,
	                         linphone::FriendList::SyncStatus status,
	                         const std::string &) override {
		if (mOwner && status == linphone::FriendList::SyncStatus::Successful) mOwner->onAddressBookChanged();
	}
};

std::shared_ptr<FriendsManager> FriendsManager::gFriendsManager;
FriendsManager::FriendsManager(QObject *parent) : QObject(parent) {
	moveToThread(CoreModel::getInstance()->thread());
	mFriendListWatcher = std::make_shared<FriendListWatcher>();
	mFriendListWatcher->mOwner = this;

	connect(CoreModel::getInstance().get(), &CoreModel::friendRemoved, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) {
		        forgetFriend(f);
		        onAddressBookChanged();
	        });
	connect(CoreModel::getInstance().get(), &CoreModel::friendCreated, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) {
		        // A newly created friend supersedes anything the magic search turned up for it. Known
		        // entries are left alone: they already point at a real friend.
		        removeAllKeysFor(mUnknownFriends, f);
		        // Every remembered miss goes, not just this friend's own addresses. It can match a
		        // number we looked up under a quite different address, through its phone numbers.
		        onAddressBookChanged();
	        });
	connect(CoreModel::getInstance().get(), &CoreModel::friendUpdated, this,
	        [this](const std::shared_ptr<linphone::Friend> &f) {
		        forgetFriend(f);
		        // An edit can give this friend a number we have already looked up and not found.
		        onAddressBookChanged();
	        });
}

FriendsManager::~FriendsManager() {
	if (mFriendListWatcher) mFriendListWatcher->mOwner = nullptr;
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

void FriendsManager::onAddressBookChanged() {
	mOtherAddresses.clear();
	for (auto &index : mPhoneNumberIndexes)
		index.second.invalidate();
}

void FriendsManager::checkAddressBook() {
	auto core = CoreModel::getInstance()->getCore();
	if (!core) return;
	bool changed = false;

	// Held strongly, not weakly. The SDK hands back the same wrapper for a list only while
	// something holds it, so with weak references every list would look new on every call and
	// nothing would stay cached.
	auto lists = core->getFriendsLists();
	auto isIn = [](const auto &container, const std::shared_ptr<linphone::FriendList> &list) {
		return std::find(container.begin(), container.end(), list) != container.end();
	};
	for (auto it = mWatchedFriendLists.begin(); it != mWatchedFriendLists.end();) {
		if (isIn(lists, *it)) {
			++it;
			continue;
		}
		(*it)->removeListener(mFriendListWatcher);
		it = mWatchedFriendLists.erase(it);
		changed = true;
	}
	for (auto &list : lists) {
		if (isIn(mWatchedFriendLists, list)) continue;
		list->addListener(mFriendListWatcher);
		mWatchedFriendLists.push_back(list);
		changed = true;
	}

	auto context = lookupContextOf(core);
	if (context != mLookupContext) {
		mLookupContext = context;
		changed = true;
	}
	if (changed) onAddressBookChanged();
}

std::shared_ptr<linphone::Friend> FriendsManager::findFriendByPhoneNumber(const std::string &number) {
	auto core = CoreModel::getInstance()->getCore();
	if (!core) return nullptr;
	auto defaultAccount = core->getDefaultAccount();
	// The SDK copes with having no default account by falling back to default settings. That
	// never happens with a provisioned account, so leave that case to the SDK rather than copy it.
	if (!defaultAccount) return core->findFriendByPhoneNumber(number);
	// The same gate the SDK puts in front of its own search.
	if (!defaultAccount->isPhoneNumber(number)) return nullptr;

	auto enumerate = [core](const PhoneNumberIndex<std::shared_ptr<linphone::Friend>>::Visit &visit) {
		for (auto &list : core->getFriendsLists())
			for (auto &f : list->getFriends())
				visit(f, f->getPhoneNumbers());
	};
	// Each account in turn, normalising with that account's dial plan, first match wins. This is
	// the order the SDK searches in.
	for (auto &account : core->getAccountList()) {
		auto params = account->getParams();
		auto normalise = [account](const std::string &n) { return account->normalizePhoneNumber(n); };
		auto f = mPhoneNumberIndexes[accountKey(params)].find(number, dialPlanOf(params), normalise, enumerate);
		if (f) return f;
	}
	return nullptr;
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
