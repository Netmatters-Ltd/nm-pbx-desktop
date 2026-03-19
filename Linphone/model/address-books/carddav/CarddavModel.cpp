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

#include "CarddavModel.hpp"
#include "model/core/CoreModel.hpp"
#include "model/setting/SettingsModel.hpp"
#include "tool/Utils.hpp"

DEFINE_ABSTRACT_OBJECT(CarddavModel)

using namespace std;

std::shared_ptr<CarddavModel> CarddavModel::create(const std::shared_ptr<linphone::FriendList> &carddavFriendList,
                                                   QObject *parent) {
	auto model = std::make_shared<CarddavModel>(carddavFriendList, parent);
	model->setSelf(model);
	return model;
}

CarddavModel::CarddavModel(const std::shared_ptr<linphone::FriendList> &carddavFriendList, QObject *parent)
    : ::Listener<linphone::FriendList, linphone::FriendListListener>(nullptr, parent) {
	mustBeInLinphoneThread(getClassName());
	mCarddavFriendList = carddavFriendList;
	mStoreNewFriendsInIt = storeNewFriendsInIt();
	mCreatedAuthInfo = nullptr;
	mRemovedAuthInfo = nullptr;
}

CarddavModel::~CarddavModel() {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
}

bool CarddavModel::storeNewFriendsInIt() {
	if (!mCarddavFriendList) return false;
	auto carddavListForNewFriends = SettingsModel::getCardDAVListForNewFriends();
	return carddavListForNewFriends != nullptr &&
	       mCarddavFriendList->getDisplayName() == carddavListForNewFriends->getDisplayName();
}

void CarddavModel::save(
    string displayName, string uri, string username, string password, string realm, bool storeNewFriendsInIt) {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	auto core = CoreModel::getInstance()->getCore();

	// Auth info handled in lazy mode, if provided handle otherwise ignore.
	// TODO: add dialog to ask user before removing existing auth info if existing already - (comment from Android)
	if (!username.empty() && !realm.empty()) {
		// Extract hostname from the CardDAV server URI so we can store it
		// as the domain on the auth info. This allows the SDK's CardDAV HTTP
		// layer to look up the correct auth info by matching the request
		// host to the auth info domain, avoiding ambiguity when multiple
		// auth infos share the same realm (e.g. SIP and CardDAV).
		std::string serverHost;
		auto schemePos = uri.find("://");
		if (schemePos != std::string::npos) {
			auto hostStart = schemePos + 3;
			auto hostEnd = uri.find_first_of(":/", hostStart);
			if (hostEnd == std::string::npos) hostEnd = uri.length();
			serverHost = uri.substr(hostStart, hostEnd - hostStart);
		}

		mRemovedAuthInfo = core->findAuthInfo(realm, username, "");
		if (mRemovedAuthInfo != nullptr) {
			lWarning() << log().arg("Auth info with username ") << username << " already exists, removing it first.";
			core->removeAuthInfo(mRemovedAuthInfo);
		}
		lInfo() << log().arg("Adding auth info with username") << username;
		mCreatedAuthInfo = linphone::Factory::get()->createAuthInfo(username, "", password, "", realm, serverHost);
		// Disable store_ha1_passwd so the plaintext password is preserved in memory
		// and on disk. The default behaviour computes an MD5 HA1 hash and then clears
		// the plaintext password, which breaks HTTP Basic authentication as used by
		// CardDAV servers.
		auto config = core->getConfig();
		config->setInt("sip", "store_ha1_passwd", 0);
		core->addAuthInfo(mCreatedAuthInfo);
	} else {
		lInfo() << log().arg("No auth info provided upon saving.");
	}

	if (!mCarddavFriendList) {
		mCarddavFriendList = CoreModel::getInstance()->getCore()->createFriendList();
		mCarddavFriendList->setType(linphone::FriendList::Type::CardDAV);
		mCarddavFriendList->enableDatabaseStorage(true);
		core->addFriendList(mCarddavFriendList);
	}
	mCarddavFriendList->setDisplayName(displayName);
	mCarddavFriendList->setUri(uri);
	mStoreNewFriendsInIt = storeNewFriendsInIt;
	setMonitor(mCarddavFriendList);
	mCarddavFriendList->synchronizeFriendsFromServer();
}

void CarddavModel::remove() {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	CoreModel::getInstance()->getCore()->removeFriendList(mCarddavFriendList);
	lInfo() << log().arg("Friend list removed:") << mCarddavFriendList->getUri();
	emit removed();
}

void CarddavModel::onSyncStatusChanged(const std::shared_ptr<linphone::FriendList> &friendList,
                                       linphone::FriendList::SyncStatus status,
                                       const std::string &message) {
	lInfo() << log().arg("carddav sync status changed :") << message;
	if (status == linphone::FriendList::SyncStatus::Successful) {
		lInfo() << log().arg("Successfully synchronized:") << mCarddavFriendList->getUri();
		setMonitor(nullptr);
		SettingsModel::setCardDAVListForNewFriends(mStoreNewFriendsInIt ? friendList->getDisplayName() : "");
		emit saved(true, Utils::coreStringToAppString(message));
	}
	if (status == linphone::FriendList::SyncStatus::Failure) {
		lWarning() << log().arg("Synchronization failure:") << mCarddavFriendList->getUri();
		setMonitor(nullptr);
		emit saved(false, Utils::coreStringToAppString(message));
	}
}
