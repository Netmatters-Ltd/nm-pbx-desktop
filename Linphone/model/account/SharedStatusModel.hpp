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

#ifndef SHARED_STATUS_MODEL_H_
#define SHARED_STATUS_MODEL_H_

#include "model/tool/SharedStatus.hpp"
#include "tool/AbstractObject.hpp"
#include "tool/LinphoneEnums.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <linphone++/linphone.hh>

#include <map>
#include <memory>
#include <optional>

class SharedStatusEventListener;

// =============================================================================
// The user's own status, for each account. The single owner of it on the linphone thread.
//
// - Publishes only what the user chose, or the server's shared status it is following, always with
//   the nmpbx-c tuple id of that choice. Never changes the status by itself.
// - Keeps the choice (status, message and when it was chosen) in the account's config section.
// - Watches the account's own extension with a presence subscription of its own, so it follows the
//   server's shared status whether or not the extension is in the contacts.
//
// AccountModel and AccountCore are created in several places for the same account (the account
// list, notifications, meetings), so none of this can live in them: each copy would subscribe.
// See nm-pbx-docs/shared-extension-status.md.
// =============================================================================

class SharedStatusModel : public QObject, public AbstractObject {
	Q_OBJECT
public:
	SharedStatusModel(QObject *parent = nullptr);
	~SharedStatusModel();
	static std::shared_ptr<SharedStatusModel> getInstance();

	// The core is On. Publishes each account's saved choice with its original time, and watches the
	// own extension of every registered account.
	void start();
	// The core is shutting down. Drops every subscription.
	void stop();

	// What the app shows as this account's own status.
	SharedStatus::Choice getChoice(const std::shared_ptr<linphone::Account> &account);
	// The user chose a status or a message on this device. Both count as a new choice.
	void choose(const std::shared_ptr<linphone::Account> &account, LinphoneEnums::Presence presence, QString note);

	// This installation's token, the <device> part of every tuple id it publishes.
	static QString deviceToken(const std::shared_ptr<linphone::Config> &config);

signals:
	void choiceChanged(const std::shared_ptr<linphone::Account> &account, LinphoneEnums::Presence presence, QString note);

private:
	friend class SharedStatusEventListener;

	struct AccountState {
		std::shared_ptr<linphone::Account> account;
		SharedStatus::Choice choice;
		// A choice made on this device that the server has not shown back yet.
		std::optional<SharedStatus::Choice> pending;
		QElapsedTimer pendingSince;
		// The last shared status the server sent, if it sends one at all (the customer is switched on).
		std::optional<SharedStatus::Choice> lastShared;
		std::shared_ptr<linphone::Event> event;
		bool registered = false;
		int retryDelayMs = 0;
		QTimer *pendingTimer = nullptr;
		QTimer *retryTimer = nullptr;
	};

	AccountState &stateFor(const std::shared_ptr<linphone::Account> &account);
	AccountState *stateForEvent(const std::shared_ptr<linphone::Event> &event);
	void forget(const std::shared_ptr<linphone::Account> &account);

	SharedStatus::Choice loadChoice(const std::shared_ptr<linphone::Account> &account);
	void saveChoice(const std::shared_ptr<linphone::Account> &account, const SharedStatus::Choice &choice);
	// Publishes the account's current choice and tells the GUI. statusChanged also applies what goes with a
	// status locally (notifications, ringing in DND).
	void apply(AccountState &state, bool statusChanged);
	void applyLocally(const std::shared_ptr<linphone::Account> &account, LinphoneEnums::Presence presence);
	void follow(AccountState &state);

	void subscribe(AccountState &state);
	void unsubscribe(AccountState &state);
	void scheduleRetry(AccountState &state);
	void onRegistrationStateChanged(const std::shared_ptr<linphone::Account> &account, linphone::RegistrationState state);
	void onNotify(const std::shared_ptr<linphone::Event> &event, const std::shared_ptr<const linphone::Content> &content);
	void onSubscribeStateChanged(const std::shared_ptr<linphone::Event> &event, linphone::SubscriptionState state);

	static QString describe(const SharedStatus::Choice &choice);

	std::map<void *, std::unique_ptr<AccountState>> mStates;
	std::shared_ptr<SharedStatusEventListener> mEventListener;
	bool mStarted = false;
	// The account whose choice the core's presence model carries.
	void *mPublishedAccount = nullptr;

	static std::shared_ptr<SharedStatusModel> gSharedStatusModel;

	DECLARE_ABSTRACT_OBJECT
};

#endif
