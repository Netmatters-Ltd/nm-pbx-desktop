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

#ifndef CONTACTS_CORE_H_
#define CONTACTS_CORE_H_

#include "core/search/MagicSearchProxy.hpp"
#include "tool/AbstractObject.hpp"

#include <QDateTime>
#include <QObject>
#include <QTimer>

// Single owner of the contact list for the whole application, exposed to QML as ContactsCpp.
//
// Every view that shows contacts attaches to rootProxy as a parentProxy and applies its own
// filtering on top, so the address book is searched once rather than once per view. This is what
// lets the Extensions and Contacts tabs switch instantly: they read the same already-populated
// model instead of each rebuilding one.
//
// It also owns the background sync. The CardDAV address book is re-synced on a timer, and the
// results are merged into the existing list rather than replacing it, so a sync that lands while
// someone is reading the list changes nothing they can see.
class ContactsCore : public QObject, public AbstractObject {
	Q_OBJECT

	Q_PROPERTY(MagicSearchProxy *rootProxy READ getRootProxy CONSTANT)
	// False until the first search has produced results, or until the initial-load watchdog gives
	// up. Views show their busy indicator on this and nothing else, so a tab switch against an
	// already-loaded model never spins.
	Q_PROPERTY(bool initialLoadComplete READ getInitialLoadComplete NOTIFY initialLoadCompleteChanged)
	Q_PROPERTY(bool syncing READ getSyncing NOTIFY syncingChanged)
	Q_PROPERTY(QDateTime lastSyncTime READ getLastSyncTime NOTIFY lastSyncTimeChanged)
	Q_PROPERTY(bool lastSyncFailed READ getLastSyncFailed NOTIFY lastSyncFailedChanged)

public:
	static ContactsCore *getInstance();

	ContactsCore(QObject *parent = nullptr);
	~ContactsCore();

	MagicSearchProxy *getRootProxy() const;
	bool getInitialLoadComplete() const;
	bool getSyncing() const;
	QDateTime getLastSyncTime() const;
	bool getLastSyncFailed() const;

	// Syncs every CardDAV address book, then re-runs the search so the list picks up any changes.
	// Safe to call at any time: a call made while a sync is already running is ignored.
	Q_INVOKABLE void refresh();

signals:
	void initialLoadCompleteChanged();
	void syncingChanged();
	void lastSyncTimeChanged();
	void lastSyncFailedChanged();

private:
	void startPeriodicSync();
	void onSyncFinished(bool succeeded);
	void setInitialLoadComplete(bool complete);
	void setSyncing(bool syncing);
	void setLastSyncFailed(bool failed);

	// How long a sync may run before we stop waiting on it. Without this a request that never
	// completes would leave the refresh indicator spinning for the rest of the session.
	static constexpr int SyncWatchdogMs = 30000;
	// How long to wait for the very first search before showing the empty state instead of a
	// spinner.
	static constexpr int InitialLoadWatchdogMs = 15000;

	MagicSearchProxy *mRootProxy = nullptr;
	QTimer mPeriodicTimer;
	QTimer mSyncWatchdog;
	QTimer mInitialLoadWatchdog;

	bool mInitialLoadComplete = false;
	bool mSyncing = false;
	bool mLastSyncFailed = false;
	QDateTime mLastSyncTime;

	DECLARE_ABSTRACT_OBJECT
};

#endif
