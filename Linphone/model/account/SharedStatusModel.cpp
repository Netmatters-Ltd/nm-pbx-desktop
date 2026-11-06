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

#include "SharedStatusModel.hpp"

#include "model/core/CoreModel.hpp"
#include "model/setting/SettingsModel.hpp"
#include "model/tool/ToolModel.hpp"
#include "tool/Utils.hpp"

#include <QDateTime>
#include <QPointer>
#include <QSysInfo>
#include <QUuid>

#include <algorithm>

DEFINE_ABSTRACT_OBJECT(SharedStatusModel)

namespace {
// Where the device token lives. Global, not per account: it identifies the installation.
const std::string DeviceSection = "nmpbx_shared_status";
// Kept in the account's own config section, beside the existing explicit_presence and presence_note.
const std::string ChosenKey = "presence_chosen";

constexpr int FirstRetryMs = 15 * 1000;
constexpr int MaxRetryMs = 10 * 60 * 1000;

bool isRegistered(linphone::RegistrationState state) {
	return state == linphone::RegistrationState::Ok || state == linphone::RegistrationState::Refreshing;
}

bool sameAccount(const std::shared_ptr<linphone::Account> &a, const std::shared_ptr<linphone::Account> &b) {
	return a && b && a->cPtr() == b->cPtr();
}

// The core publishes one presence model for every account, so it can only carry one account's
// choice. That is the default account's, or the only account's.
bool isPublishingAccount(const std::shared_ptr<linphone::Core> &core,
                         const std::shared_ptr<linphone::Account> &account) {
	auto defaultAccount = core->getDefaultAccount();
	if (defaultAccount) return sameAccount(defaultAccount, account);
	auto accounts = core->getAccountList();
	return accounts.size() == 1 && sameAccount(accounts.front(), account);
}
} // namespace

// Event callbacks arrive on the linphone thread, like everything else in this model.
class SharedStatusEventListener : public linphone::EventListener {
public:
	QPointer<SharedStatusModel> mOwner;

	void onNotifyReceived(const std::shared_ptr<linphone::Event> &event,
	                      const std::shared_ptr<const linphone::Content> &content) override {
		if (mOwner) mOwner->onNotify(event, content);
	}
	void onSubscribeStateChanged(const std::shared_ptr<linphone::Event> &event,
	                             linphone::SubscriptionState state) override {
		if (mOwner) mOwner->onSubscribeStateChanged(event, state);
	}
};

std::shared_ptr<SharedStatusModel> SharedStatusModel::gSharedStatusModel;

SharedStatusModel::SharedStatusModel(QObject *parent) : QObject(parent) {
	moveToThread(CoreModel::getInstance()->thread());
	mEventListener = std::make_shared<SharedStatusEventListener>();
	mEventListener->mOwner = this;

	auto coreModel = CoreModel::getInstance().get();
	connect(coreModel, &CoreModel::accountRegistrationStateChanged, this,
	        [this](const std::shared_ptr<linphone::Core> &, const std::shared_ptr<linphone::Account> &account,
	               linphone::RegistrationState state, const std::string &) {
		        onRegistrationStateChanged(account, state);
	        });
	connect(coreModel, &CoreModel::accountRemoved, this,
	        [this](const std::shared_ptr<linphone::Core> &, const std::shared_ptr<linphone::Account> &account) {
		        forget(account);
	        });
	connect(coreModel, &CoreModel::defaultAccountChanged, this,
	        [this](const std::shared_ptr<linphone::Core> &core, const std::shared_ptr<linphone::Account> &) {
		        // Only the default account is watched and published, so move both over to it. This signal is
		        // also raised when the display name or picture changes, so only republish on a real change.
		        if (!mStarted) return;
		        for (auto &entry : mStates) {
			        auto &state = *entry.second;
			        if (isPublishingAccount(core, state.account)) {
				        if (mPublishedAccount != entry.first) apply(state, false);
				        if (state.registered && !state.event) subscribe(state);
			        } else unsubscribe(state);
		        }
	        });
	connect(coreModel, &CoreModel::globalStateChanged, this,
	        [this](const std::shared_ptr<linphone::Core> &, linphone::GlobalState state, const std::string &) {
		        if (state == linphone::GlobalState::On) start();
		        else if (state == linphone::GlobalState::Shutdown || state == linphone::GlobalState::Off) stop();
	        });
}

SharedStatusModel::~SharedStatusModel() {
	if (mEventListener) mEventListener->mOwner = nullptr;
}

std::shared_ptr<SharedStatusModel> SharedStatusModel::getInstance() {
	if (!gSharedStatusModel) gSharedStatusModel = std::make_shared<SharedStatusModel>(nullptr);
	return gSharedStatusModel;
}

void SharedStatusModel::start() {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	auto core = CoreModel::getInstance()->getCore();
	if (!core || core->getGlobalState() != linphone::GlobalState::On) return;
	if (mStarted) return; // The core can report On more than once; the subscriptions are already in hand.
	mStarted = true;

	lInfo() << log().arg("Shared status: device token %1").arg(deviceToken(core->getConfig()));
	// Republish the saved choice with its original time. If the server holds something newer it ignores
	// this, and its next NOTIFY brings the shared status to follow.
	for (auto &account : core->getAccountList()) {
		auto &state = stateFor(account);
		if (isPublishingAccount(core, account)) apply(state, false);
		onRegistrationStateChanged(account, account->getState());
	}
}

void SharedStatusModel::stop() {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	mStarted = false;
	for (auto &entry : mStates)
		unsubscribe(*entry.second);
	for (auto &entry : mStates)
		entry.second->registered = false;
}

SharedStatus::Choice SharedStatusModel::getChoice(const std::shared_ptr<linphone::Account> &account) {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	if (!account) return SharedStatus::Choice();
	return stateFor(account).choice;
}

void SharedStatusModel::choose(const std::shared_ptr<linphone::Account> &account,
                               LinphoneEnums::Presence presence,
                               QString note) {
	mustBeInLinphoneThread(log().arg(Q_FUNC_INFO));
	if (!account) return;
	auto status = ToolModel::appPresenceToSharedStatus(presence);
	if (!status) {
		// Undefined, Offline and OnCall are never the user's choice. OnCall comes from the server's
		// call-state bridge and must never be published.
		lWarning() << log().arg("Shared status: refusing to choose %1").arg(LinphoneEnums::toString(presence));
		return;
	}
	auto &state = stateFor(account);

	SharedStatus::Choice choice;
	choice.status = *status;
	choice.note = SharedStatus::cleanNote(note);
	// When it was chosen, by this PC's clock. Always later than the choice it replaces, even within the
	// same second or on a slow clock, so the server takes it as the newer choice. The server treats
	// anything more than 60 seconds ahead of its own clock as "now", so a saved time far ahead of ours
	// can only be bad data and is not carried on.
	qint64 now = QDateTime::currentSecsSinceEpoch();
	choice.chosen = now;
	if (state.choice.chosen >= now && state.choice.chosen < now + 120) choice.chosen = state.choice.chosen + 1;

	lInfo() << log().arg("Shared status: user chose %1").arg(describe(choice));
	state.choice = choice;
	state.pending = choice;
	state.pendingSince.start();
	// Look again once the pending window closes, in case the server never shows this choice.
	state.pendingTimer->start(int(SharedStatus::PendingChoiceWindowMs) + 500);
	saveChoice(account, choice);
	apply(state, true);
}

QString SharedStatusModel::deviceToken(const std::shared_ptr<linphone::Config> &config) {
	static QString cached;
	if (!cached.isEmpty()) return cached;
	if (!config) return QString();

	// The token must differ on every installation, because two publications with the same tuple id hide
	// each other in Flexisip. The config is in the local (not roaming) profile, but it can still be
	// copied to another PC, so the token is tied to this machine's id and replaced if that changes.
	// The SDK's +sip.instance UUID is not used: it lives in the same config, with no such check on
	// Windows.
	auto machine = QString::fromLatin1(QSysInfo::machineUniqueId());
	auto token = SharedStatus::cleanDeviceToken(
	    Utils::coreStringToAppString(config->getString(DeviceSection, "device_token", "")));
	auto savedMachine = Utils::coreStringToAppString(config->getString(DeviceSection, "device_machine", ""));
	if (token.isEmpty() || savedMachine != machine) {
		if (!token.isEmpty())
			qInfo() << "[SharedStatusModel] Shared status: this config was made on another machine, new device token";
		token = SharedStatus::cleanDeviceToken(QUuid::createUuid().toString(QUuid::WithoutBraces));
		config->setString(DeviceSection, "device_token", Utils::appStringToCoreString(token));
		config->setString(DeviceSection, "device_machine", Utils::appStringToCoreString(machine));
		config->sync();
	}
	cached = token;
	return cached;
}

// -----------------------------------------------------------------------------

SharedStatusModel::AccountState &SharedStatusModel::stateFor(const std::shared_ptr<linphone::Account> &account) {
	void *key = account->cPtr();
	auto it = mStates.find(key);
	if (it != mStates.end()) return *it->second;

	auto state = std::make_unique<AccountState>();
	state->account = account;
	state->choice = loadChoice(account);
	state->pendingTimer = new QTimer(this);
	state->pendingTimer->setSingleShot(true);
	connect(state->pendingTimer, &QTimer::timeout, this, [this, key]() {
		auto found = mStates.find(key);
		if (found != mStates.end()) follow(*found->second);
	});
	state->retryTimer = new QTimer(this);
	state->retryTimer->setSingleShot(true);
	connect(state->retryTimer, &QTimer::timeout, this, [this, key]() {
		auto found = mStates.find(key);
		if (found == mStates.end()) return;
		auto &state = *found->second;
		if (state.registered && !state.event) subscribe(state);
	});
	auto &result = *state;
	mStates.emplace(key, std::move(state));
	return result;
}

SharedStatusModel::AccountState *SharedStatusModel::stateForEvent(const std::shared_ptr<linphone::Event> &event) {
	if (!event) return nullptr;
	for (auto &entry : mStates)
		if (entry.second->event && entry.second->event->cPtr() == event->cPtr()) return entry.second.get();
	return nullptr;
}

void SharedStatusModel::forget(const std::shared_ptr<linphone::Account> &account) {
	if (!account) return;
	auto it = mStates.find(account->cPtr());
	if (it == mStates.end()) return;
	unsubscribe(*it->second);
	it->second->pendingTimer->deleteLater();
	it->second->retryTimer->deleteLater();
	mStates.erase(it);
}

SharedStatus::Choice SharedStatusModel::loadChoice(const std::shared_ptr<linphone::Account> &account) {
	auto config = CoreModel::getInstance()->getCore()->getConfig();
	auto section = ToolModel::configAccountSection(account);
	SharedStatus::Choice choice;
	auto saved = LinphoneEnums::fromString(
	    Utils::coreStringToAppString(config->getString(section, "explicit_presence", "")));
	auto status = ToolModel::appPresenceToSharedStatus(saved);
	if (status) {
		choice.status = *status;
		// An older version saved the status without a time. That choice is not new, so it is stated as
		// "no choice yet" (0), which can never win over the shared status.
		choice.chosen = std::max<int64_t>(0, config->getInt64(section, ChosenKey, 0));
	} else {
		// Nothing chosen yet: Available, stated as no choice so it never wins.
		choice.status = SharedStatus::Status::Available;
		choice.chosen = 0;
	}
	choice.note = SharedStatus::cleanNote(Utils::coreStringToAppString(config->getString(section, "presence_note", "")));
	return choice;
}

void SharedStatusModel::saveChoice(const std::shared_ptr<linphone::Account> &account,
                                   const SharedStatus::Choice &choice) {
	auto config = CoreModel::getInstance()->getCore()->getConfig();
	auto section = ToolModel::configAccountSection(account);
	config->setString(section, "explicit_presence",
	                  Utils::appStringToCoreString(
	                      LinphoneEnums::toString(ToolModel::sharedStatusToAppPresence(choice.status))));
	config->setString(section, "presence_note", Utils::appStringToCoreString(choice.note));
	config->setInt64(section, ChosenKey, choice.chosen);
	config->sync();
}

void SharedStatusModel::apply(AccountState &state, bool statusChanged) {
	auto core = CoreModel::getInstance()->getCore();
	auto config = core->getConfig();
	auto presence = ToolModel::sharedStatusToAppPresence(state.choice.status);

	if (core->getGlobalState() != linphone::GlobalState::On) {
		lWarning() << log().arg("Shared status: core is not On, not publishing");
	} else if (!isPublishingAccount(core, state.account)) {
		lInfo() << log().arg("Shared status: not publishing for a non-default account, the SDK has one presence for "
		                     "every account");
	} else {
		auto tupleId =
		    SharedStatus::choiceTupleId(state.choice.status, state.choice.chosen, deviceToken(config));
		auto model = ToolModel::appPresenceToCorePresenceModel(presence, state.choice.note, tupleId);
		if (model) {
			lInfo() << log().arg("Shared status: publishing %1").arg(tupleId);
			core->setPresenceModel(model); // No api (yet) at the account level
			mPublishedAccount = state.account->cPtr();
		}
	}

	// What goes with a status locally, as before. Only when it changes, so republishing at start-up does not
	// undo a notification setting the user changed since.
	if (statusChanged) applyLocally(state.account, presence);
	emit choiceChanged(state.account, presence, state.choice.note);
}

void SharedStatusModel::applyLocally(const std::shared_ptr<linphone::Account> &account,
                                     LinphoneEnums::Presence presence) {
	auto config = CoreModel::getInstance()->getCore()->getConfig();
	auto section = ToolModel::configAccountSection(account);
	bool notificationsAllowed =
	    presence != LinphoneEnums::Presence::DoNotDisturb &&
	    (presence != LinphoneEnums::Presence::Away ||
	     config->getBool(section, "allow_notifications_in_presence_away", true)) &&
	    (presence != LinphoneEnums::Presence::Busy ||
	     config->getBool(section, "allow_notifications_in_presence_busy", true));
	auto identity = account->getParams()->getIdentityAddress();
	if (identity) config->setBool("ui_" + identity->asStringUriOnly(), "notifications_allowed", notificationsAllowed);
	auto settings = SettingsModel::getInstance();
	if (settings && !SettingsModel::dndEnabled(config))
		settings->enableRinging(presence != LinphoneEnums::Presence::DoNotDisturb);
}

void SharedStatusModel::follow(AccountState &state) {
	if (!state.lastShared) return; // Switched off, or nothing from the server yet: keep our own.
	auto shared = *state.lastShared;

	if (state.pending) {
		qint64 elapsed = state.pendingSince.isValid() ? state.pendingSince.elapsed() : -1;
		if (SharedStatus::keepPendingChoice(shared, *state.pending, elapsed)) {
			lInfo() << log()
			               .arg("Shared status: keeping this device's choice %1 for now, the server still shows %2")
			               .arg(describe(*state.pending))
			               .arg(describe(shared));
			return; // The pending timer looks again when the window closes.
		}
		state.pending.reset();
		state.pendingTimer->stop();
	}

	if (shared == state.choice) return;
	lInfo() << log().arg("Shared status: following %1").arg(describe(shared));
	// Republished with the shared status's own time, so the server sees it as not newer. Following never
	// makes a new choice, or two apps would chase each other.
	state.choice = shared;
	saveChoice(state.account, shared);
	apply(state, true);
}

// -----------------------------------------------------------------------------
// Watching the own extension.
// -----------------------------------------------------------------------------

void SharedStatusModel::subscribe(AccountState &state) {
	unsubscribe(state);
	auto core = CoreModel::getInstance()->getCore();
	if (!core || core->getGlobalState() != linphone::GlobalState::On) return;
	if (!isPublishingAccount(core, state.account)) return;

	auto identity = state.account->getParams()->getIdentityAddress();
	// Presence is keyed on exact string equality, so subscribe to the bare identity, as the contacts do.
	auto resource = identity ? linphone::Factory::get()->createAddress(identity->asStringUriOnly()) : nullptr;
	if (!resource) {
		lWarning() << log().arg("Shared status: account has no usable identity, not watching it");
		return;
	}
	int expires = core->getConfig()->getInt("sip", "subscribe_expires", 600);
	auto event = core->createSubscribe(resource, "presence", expires);
	if (!event) {
		lWarning() << log().arg("Shared status: could not create the subscription to %1")
		                     .arg(Utils::coreStringToAppString(resource->asStringUriOnly()));
		scheduleRetry(state);
		return;
	}
	event->addListener(mEventListener);
	event->addCustomHeader("Accept", "application/pidf+xml");
	state.event = event;
	lInfo() << log().arg("Shared status: watching own extension %1")
	                   .arg(Utils::coreStringToAppString(resource->asStringUriOnly()));
	if (event->sendSubscribe(nullptr) < 0) {
		lWarning() << log().arg("Shared status: SUBSCRIBE to own extension failed to send");
		unsubscribe(state);
		scheduleRetry(state);
	}
}

void SharedStatusModel::unsubscribe(AccountState &state) {
	if (state.retryTimer) state.retryTimer->stop();
	if (!state.event) return;
	auto event = state.event;
	state.event = nullptr;
	event->removeListener(mEventListener);
	auto subscriptionState = event->getSubscriptionState();
	if (subscriptionState == linphone::SubscriptionState::Active ||
	    subscriptionState == linphone::SubscriptionState::OutgoingProgress ||
	    subscriptionState == linphone::SubscriptionState::Pending ||
	    subscriptionState == linphone::SubscriptionState::Expiring)
		event->terminate();
}

void SharedStatusModel::scheduleRetry(AccountState &state) {
	state.retryDelayMs = state.retryDelayMs <= 0 ? FirstRetryMs : std::min(state.retryDelayMs * 2, MaxRetryMs);
	lInfo() << log().arg("Shared status: watching own extension again in %1 s").arg(state.retryDelayMs / 1000);
	state.retryTimer->start(state.retryDelayMs);
}

void SharedStatusModel::onRegistrationStateChanged(const std::shared_ptr<linphone::Account> &account,
                                                   linphone::RegistrationState registrationState) {
	if (!account || !mStarted) return;
	auto &state = stateFor(account);
	bool registered = isRegistered(registrationState);
	if (registered && !state.registered) {
		// Registered again, perhaps after sleep or a network change: start a fresh subscription, whose
		// first NOTIFY brings the shared status as it is now. The SDK republishes the presence model on
		// registration by itself, with the choice's own tuple id.
		state.registered = true;
		state.retryDelayMs = 0;
		subscribe(state);
	} else if (!registered && state.registered) {
		// Refreshing counts as registered, so this is a real loss of registration.
		state.registered = false;
		unsubscribe(state);
	}
}

void SharedStatusModel::onSubscribeStateChanged(const std::shared_ptr<linphone::Event> &event,
                                                linphone::SubscriptionState subscriptionState) {
	auto state = stateForEvent(event);
	if (!state) return;
	if (subscriptionState == linphone::SubscriptionState::Active) {
		state->retryDelayMs = 0;
	} else if (subscriptionState == linphone::SubscriptionState::Error ||
	           subscriptionState == linphone::SubscriptionState::Terminated) {
		lInfo() << log().arg("Shared status: subscription to own extension ended (%1)").arg((int)subscriptionState);
		// Not removed from the event here, inside its own callback. Callbacks from an event that is no
		// longer ours are ignored by stateForEvent().
		state->event = nullptr;
		if (state->registered) scheduleRetry(*state);
	}
}

void SharedStatusModel::onNotify(const std::shared_ptr<linphone::Event> &event,
                                 const std::shared_ptr<const linphone::Content> &content) {
	auto state = stateForEvent(event);
	if (!state || !content) return;

	std::list<std::shared_ptr<const linphone::Content>> parts;
	if (content->isMultipart())
		for (auto &part : content->getParts())
			parts.push_back(part);
	else parts.push_back(content);

	QList<SharedStatus::Tuple> tuples;
	bool sawDocument = false;
	for (auto &part : parts) {
		auto text = part ? part->getUtf8Text() : std::string();
		if (text.empty()) continue;
		sawDocument = true;
		tuples << SharedStatus::parsePidfTuples(QByteArray::fromStdString(text));
	}
	if (!sawDocument) return; // An empty NOTIFY says nothing about the status.

	auto shared = SharedStatus::findShared(tuples);
	if (!shared) {
		// No shared status: the customer is switched off. Behave as before.
		if (state->lastShared) lInfo() << log().arg("Shared status: the server no longer publishes one");
		state->lastShared.reset();
		return;
	}
	if (!state->lastShared || *state->lastShared != shared->choice)
		lInfo() << log().arg("Shared status from the server: %1, source %2").arg(describe(shared->choice)).arg(shared->source);
	state->lastShared = shared->choice;
	follow(*state);
}

QString SharedStatusModel::describe(const SharedStatus::Choice &choice) {
	return QStringLiteral("%1 chosen %2 note \"%3\"")
	    .arg(SharedStatus::statusToken(choice.status))
	    .arg(choice.chosen)
	    .arg(choice.note);
}
