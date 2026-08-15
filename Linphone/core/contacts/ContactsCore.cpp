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

#include "ContactsCore.hpp"

#include "core/App.hpp"
#include "core/setting/SettingsCore.hpp"
#include "model/address-books/carddav/CardDAVSyncAgent.hpp"
#include "model/core/CoreModel.hpp"
#include "model/setting/SettingsModel.hpp"
#include "tool/Utils.hpp"

#include <memory>
#include <vector>

DEFINE_ABSTRACT_OBJECT(ContactsCore)

ContactsCore *ContactsCore::getInstance() {
	static auto instance = new ContactsCore();
	return instance;
}

ContactsCore::ContactsCore(QObject *parent) : QObject(parent) {
	mustBeInMainThread(getClassName());

	mRootProxy = new MagicSearchProxy(this);
	// LDAP stays in because the SDK filters it out by itself when LDAP sync is off. RemoteCardDAV is
	// deliberately absent: the background sync pulls the whole address book down, so searching is a
	// local filter over what we already hold rather than a request per keystroke.
	mRootProxy->setSourceFlags((int)LinphoneEnums::MagicSearchSource::Friends |
	                           (int)LinphoneEnums::MagicSearchSource::FavoriteFriends |
	                           (int)LinphoneEnums::MagicSearchSource::LdapServers);
	mRootProxy->setAggregationFlag(LinphoneEnums::MagicSearchAggregation::Friend);

	// The search model is built asynchronously on the linphone thread, so wait to be told it exists
	// before asking it for anything. "*" means the whole address book; individual views narrow it
	// down with their own filterText.
	connect(mRootProxy, &MagicSearchProxy::initialized, this, [this] {
		mInitialLoadWatchdog.start();
		mRootProxy->setSearchText("*");
	});
	connect(mRootProxy, &MagicSearchProxy::resultsProcessed, this, [this] {
		mInitialLoadWatchdog.stop();
		setInitialLoadComplete(true);
	});

	mSyncWatchdog.setSingleShot(true);
	mSyncWatchdog.setInterval(SyncWatchdogMs);
	connect(&mSyncWatchdog, &QTimer::timeout, this, [this] {
		lWarning() << log().arg("Address book sync did not finish within %1s, giving up on it")
		                  .arg(SyncWatchdogMs / 1000);
		// Leave the list exactly as it is. Stale contacts beat an empty page, and the next scheduled
		// sync will try again.
		onSyncFinished(false);
	});

	mInitialLoadWatchdog.setSingleShot(true);
	mInitialLoadWatchdog.setInterval(InitialLoadWatchdogMs);
	connect(&mInitialLoadWatchdog, &QTimer::timeout, this, [this] {
		lWarning() << log().arg("No contacts after %1s, showing the empty state").arg(InitialLoadWatchdogMs / 1000);
		setInitialLoadComplete(true);
	});

	connect(&mPeriodicTimer, &QTimer::timeout, this, [this] { refresh(); });

	// Single place where a completed sync turns into a refreshed list. Catches our own syncs and the
	// one the provisioning code runs at launch, which would otherwise leave the list showing
	// whatever was in the local database until the first interval elapsed.
	auto app = App::getInstance();
	if (auto settings = app->getSettings()) {
		connect(settings.get(), &SettingsCore::cardDAVAddressBookSynchronized, this, [this] {
			// The list merges results in place, so this is invisible unless something differs.
			if (mRootProxy) emit mRootProxy->forceUpdate();
		});
	}

	if (app->getCoreStarted()) startPeriodicSync();
	else
		connect(app, &App::coreStartedChanged, this, [this](bool started) {
			if (started) startPeriodicSync();
		});
}

ContactsCore::~ContactsCore() {
}

MagicSearchProxy *ContactsCore::getRootProxy() const {
	return mRootProxy;
}

bool ContactsCore::getInitialLoadComplete() const {
	return mInitialLoadComplete;
}

bool ContactsCore::getSyncing() const {
	return mSyncing;
}

QDateTime ContactsCore::getLastSyncTime() const {
	return mLastSyncTime;
}

bool ContactsCore::getLastSyncFailed() const {
	return mLastSyncFailed;
}

void ContactsCore::setInitialLoadComplete(bool complete) {
	if (mInitialLoadComplete == complete) return;
	mInitialLoadComplete = complete;
	emit initialLoadCompleteChanged();
}

void ContactsCore::setSyncing(bool syncing) {
	if (mSyncing == syncing) return;
	mSyncing = syncing;
	emit syncingChanged();
}

void ContactsCore::setLastSyncFailed(bool failed) {
	if (mLastSyncFailed == failed) return;
	mLastSyncFailed = failed;
	emit lastSyncFailedChanged();
}

void ContactsCore::startPeriodicSync() {
	// The interval lives in the provisioning file so it can be tuned per deployment.
	App::postModelAsync([]() {
		auto settings = SettingsModel::getInstance();
		int seconds = settings ? settings->getCardDAVSyncIntervalSeconds() : 0;
		App::postCoreAsync([seconds]() {
			auto self = ContactsCore::getInstance();
			if (seconds <= 0) {
				lInfo() << "[ContactsCore] Periodic address book sync is disabled by configuration";
				return;
			}
			self->mPeriodicTimer.setInterval(seconds * 1000);
			self->mPeriodicTimer.start();
			lInfo() << "[ContactsCore] Syncing the address book every" << seconds << "seconds";
			// No sync kicked off here on purpose: provisioning already syncs once at launch, and
			// doing it again would mean every client hitting the server twice on every start.
		});
	});
}

void ContactsCore::refresh() {
	mustBeInMainThread(log().arg(Q_FUNC_INFO));
	if (mSyncing) {
		lInfo() << log().arg("A sync is already running, ignoring this request");
		return;
	}
	setSyncing(true);
	mSyncWatchdog.start();

	App::postModelAsync([]() {
		auto finish = [](bool succeeded) {
			App::postCoreAsync([succeeded]() { ContactsCore::getInstance()->onSyncFinished(succeeded); });
		};

		auto core = CoreModel::getInstance()->getCore();
		if (!core) {
			finish(false);
			return;
		}

		std::vector<std::shared_ptr<linphone::FriendList>> cardDAVLists;
		for (auto &friendList : core->getFriendsLists()) {
			if (friendList->getType() == linphone::FriendList::Type::CardDAV) cardDAVLists.push_back(friendList);
		}
		if (cardDAVLists.empty()) {
			// Nothing provisioned. Not a failure, just nothing to do.
			finish(true);
			return;
		}

		// Every list has to report back before the sync counts as done. Shared state rather than
		// members because these callbacks all run on the linphone thread.
		auto pending = std::make_shared<int>((int)cardDAVLists.size());
		auto anyFailed = std::make_shared<bool>(false);
		for (auto &friendList : cardDAVLists) {
			auto agent = std::make_shared<CardDAVSyncAgent>();
			agent->start(friendList, [pending, anyFailed, finish](bool succeeded) {
				if (!succeeded) *anyFailed = true;
				if (--(*pending) == 0) finish(!*anyFailed);
			});
		}
	});
}

void ContactsCore::onSyncFinished(bool succeeded) {
	mustBeInMainThread(log().arg(Q_FUNC_INFO));
	// The watchdog may have already given up on this sync, in which case there is nothing left to
	// tidy and a late completion should not restart anything.
	bool wasSyncing = mSyncing;
	mSyncWatchdog.stop();
	setSyncing(false);
	setLastSyncFailed(!succeeded);

	if (succeeded) {
		mLastSyncTime = QDateTime::currentDateTime();
		emit lastSyncTimeChanged();
		// The SDK's friend list has changed underneath us. Announcing it here rather than calling
		// forceUpdate directly keeps one path from a completed sync to a refreshed list, and keeps
		// the views that already listen to this signal working.
		auto settings = App::getInstance()->getSettings();
		if (settings) emit settings->cardDAVAddressBookSynchronized();
	}

	// Restart so the next automatic sync is a full interval away from this one, whether it was
	// triggered by the timer or by the refresh button.
	if (wasSyncing && mPeriodicTimer.interval() > 0) mPeriodicTimer.start();
}
