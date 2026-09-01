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

#include "ToolModel.hpp"
#include "core/App.hpp"
#include "core/conference/ConferenceInfoCore.hpp"
#include "core/path/Paths.hpp"
#include "model/core/CoreModel.hpp"
#include "model/friend/FriendsManager.hpp"
#include "model/setting/SettingsModel.hpp"
#include "tool/UriTools.hpp"
#include "tool/Utils.hpp"
#include <QDebug>
#include <QDirIterator>
#include <QLibrary>
#include <QTest>
#include <algorithm>

DEFINE_ABSTRACT_OBJECT(ToolModel)

ToolModel::ToolModel(QObject *parent) {
}
ToolModel::~ToolModel() {
}
std::shared_ptr<linphone::Address> ToolModel::interpretUrl(const QString &address) {
	bool usePrefix = false; // TODO
	// CoreManager::getInstance()->getAccountSettingsModel()->getUseInternationalPrefixForCallsAndChats();
	auto interpretedAddress =
	    CoreModel::getInstance()->getCore()->interpretUrl(Utils::appStringToCoreString(address), usePrefix);
	if (!interpretedAddress) { // Try by removing scheme.
		QStringList splitted = address.split(":");
		if (splitted.size() > 0 && splitted[0] == "sip") {
			splitted.removeFirst();
			interpretedAddress = CoreModel::getInstance()->getCore()->interpretUrl(
			    Utils::appStringToCoreString(splitted.join(":")), usePrefix);
		}
	}
	return interpretedAddress;
}

QString ToolModel::resolveCountryCallingCode(const std::shared_ptr<linphone::Account> &account) {
	mustBeInLinphoneThread(QString(gClassName) + " : " + Q_FUNC_INFO);
	auto core = CoreModel::getInstance()->getCore();
	auto acc = account ? account : (core ? core->getDefaultAccount() : nullptr);
	// Not cached: the dial plan can be changed in account settings at any time.
	if (acc && acc->getParams()) {
		auto prefix = Utils::coreStringToAppString(acc->getParams()->getInternationalPrefix());
		if (!prefix.isEmpty()) return prefix;
	}
	// We ship to UK customers. Deliberately not derived from the machine's region, so a laptop
	// set to another country still dials the way the PBX expects.
	return QStringLiteral("44");
}

QString ToolModel::normalizePhoneNumber(const QString &number, const std::shared_ptr<linphone::Account> &account) {
	if (number.isEmpty()) return number;
	// Anything already carrying a scheme is an address, not a dialled number, so leave it be.
	if (number.contains(QStringLiteral("://")) || number.startsWith(QStringLiteral("sip:")) ||
	    number.startsWith(QStringLiteral("sips:")) || number.startsWith(QStringLiteral("tel:")))
		return number;

	auto prefix = resolveCountryCallingCode(account);
	if (prefix.isEmpty()) return number;

	// +447771514661 and 00447771514661 both become 07771514661. Everything else is untouched,
	// including numbers already in local form and internal extensions.
	const QString plusForm = QStringLiteral("+") + prefix;
	const QString zerosForm = QStringLiteral("00") + prefix;
	if (number.startsWith(plusForm)) return QStringLiteral("0") + number.mid(plusForm.size());
	if (number.startsWith(zerosForm)) return QStringLiteral("0") + number.mid(zerosForm.size());
	return number;
}

std::shared_ptr<linphone::Call> ToolModel::getCallByRemoteAddress(const QString &remoteAddress) {
	auto linAddress = ToolModel::interpretUrl(remoteAddress);
	if (linAddress) return CoreModel::getInstance()->getCore()->getCallByRemoteAddress2(linAddress);
	else return nullptr;
}

std::shared_ptr<linphone::FriendPhoneNumber> ToolModel::makeLinphoneNumber(const QString &label,
                                                                           const QString &number) {
	auto linphoneNumber = std::make_shared<linphone::FriendPhoneNumber>(nullptr);
	linphoneNumber->setLabel(Utils::appStringToCoreString(label));
	linphoneNumber->setLabel(Utils::appStringToCoreString(number));
	return linphoneNumber;
}

std::shared_ptr<linphone::AudioDevice> ToolModel::findAudioDevice(const QString &id,
                                                                  linphone::AudioDevice::Capabilities capability) {
	std::string devId = Utils::appStringToCoreString(id);
	auto devices = CoreModel::getInstance()->getCore()->getExtendedAudioDevices();
	auto audioDevice =
	    find_if(devices.cbegin(), devices.cend(), [&](const std::shared_ptr<linphone::AudioDevice> &audioItem) {
		    return audioItem->hasCapability(capability) && audioItem->getId() == devId;
	    });
	if (audioDevice != devices.cend()) {
		return *audioDevice;
	}
	return nullptr;
}

namespace {
// Asterisk stamps its own name into the From display name of every leg it hands us, so
// "asterisk" <sip:123@...> arrives for each caller and beats the caller's own identity. Names
// listed in the provisioning file's [ui] ignored_display_names (comma separated, "asterisk" by
// default) are treated as if the header carried no display name at all.
bool isIgnoredDisplayName(const QString &displayName) {
	if (displayName.isEmpty()) return true;
	auto core = CoreModel::getInstance()->getCore();
	auto config = core ? core->getConfig() : nullptr;
	auto configured = config ? Utils::coreStringToAppString(
	                              config->getString(SettingsModel::UiSection, "ignored_display_names", "asterisk"))
	                         : QStringLiteral("asterisk");
	const auto trimmed = displayName.trimmed();
	for (const auto &ignored : configured.split(QLatin1Char(','), Qt::SkipEmptyParts))
		if (trimmed.compare(ignored.trimmed(), Qt::CaseInsensitive) == 0) return true;
	return false;
}
} // namespace

QString ToolModel::getDisplayName(const std::shared_ptr<const linphone::Address> &address) {
	QString displayName;
	if (address) {
		auto linFriend = findFriendByAddress(address);
		if (linFriend) {
			if (displayName.isEmpty()) displayName = Utils::coreStringToAppString(linFriend->getName());
		}
		if (displayName.isEmpty()) {
			displayName = Utils::coreStringToAppString(address->getDisplayName());
			if (isIgnoredDisplayName(displayName)) displayName.clear();
			if (displayName.isEmpty()) {
				auto accounts = CoreModel::getInstance()->getCore()->getAccountList();
				auto found = std::find_if(accounts.begin(), accounts.end(),
				                          [address](std::shared_ptr<linphone::Account> account) {
					                          return address->weakEqual(account->getParams()->getIdentityAddress());
				                          });
				if (found != accounts.end()) {
					displayName =
					    Utils::coreStringToAppString(found->get()->getParams()->getIdentityAddress()->getDisplayName());
				}
				if (displayName.isEmpty()) {
					displayName = Utils::coreStringToAppString(address->getUsername());
					if (displayName.isEmpty()) {
						return Utils::coreStringToAppString(address->asStringUriOnly());
					}
				}
			}
		}
		// The address is cleaned inside findFriendByAddress above, so there is nothing left to do
		// here. (This once referred to a getSipAddressEntry cache that no longer exists.)
	}
	return displayName;
}

QString ToolModel::getDisplayName(QString address) {
	mustBeInLinphoneThread(QString(gClassName) + " : " + Q_FUNC_INFO);

	QString displayName = getDisplayName(interpretUrl(address));
	if (displayName.isEmpty()) return address;
	QStringList nameSplitted = displayName.split(" ");
	for (auto &part : nameSplitted) {
		if (part.isEmpty()) continue;
		part[0] = part[0].toUpper();
	}
	return nameSplitted.join(" ");
}

QString ToolModel::encodeTextToQmlRichFormat(const QString &text,
                                             const QVariantMap &options,
                                             std::shared_ptr<linphone::ChatRoom> chatRoom) {
	QStringList formattedText;
	bool lastWasUrl = false;
	auto primaryColor = QColor::fromString("#4AA8FF");

	if (options.contains("noLink") && options["noLink"].toBool()) {
		formattedText.append(Utils::encodeEmojiToQmlRichFormat(text));
	} else {

		auto iriParsed = UriTools::parseIri(text);

		for (int i = 0; i < iriParsed.size(); ++i) {
			QString iri = iriParsed[i]
			                  .second.replace('&', "&amp;")
			                  .replace('<', "\u2063&lt;")
			                  .replace('>', "\u2063&gt;")
			                  .replace('"', "&quot;")
			                  .replace('\'', "&#039;");
			if (!iriParsed[i].first) {
				if (lastWasUrl) {
					lastWasUrl = false;
					if (iri.front() != ' ') iri.push_front(' ');
				}
				formattedText.append(Utils::encodeEmojiToQmlRichFormat(iri));
			} else {
				QString uri =
				    iriParsed[i].second.left(3) == "www" ? "http://" + iriParsed[i].second : iriParsed[i].second;
				/* TODO : preview from link
				int extIndex = iriParsed[i].second.lastIndexOf('.');
				QString ext;
				if( extIndex >= 0)
				    ext = iriParsed[i].second.mid(extIndex+1).toUpper();
				if(imageFormat.contains(ext.toLatin1())){// imagesHeight is not used because of bugs on display
				(blank image if set without width) images += "<a href=\"" + uri + "\"><img" + (
				options.contains("imagesWidth") ? QString(" width='") + options["imagesWidth"].toString() + "'" : ""
				        ) + (
				 options.contains("imagesWidth")
				 ? QString(" height='auto'")
				 : ""
			 ) + " src=\"" + iriParsed[i].second + "\" />"+uri+"</a>";
	  }else{
	  */
				formattedText.append("<a style=\"color:" + primaryColor.name() + ";\" href=\"" + uri + "\">" + iri +
				                     "</a>");
				lastWasUrl = true;
				/*}*/
			}
		}
	}
	if (lastWasUrl && formattedText.last().back() != ' ') {
		formattedText.push_back(" ");
	}
	if (chatRoom) {
		auto participants = chatRoom->getParticipants();
		auto mentionsParsed = UriTools::parseMention(formattedText.join(""));
		formattedText.clear();

		for (int i = 0; i < mentionsParsed.size(); ++i) {
			QString mention = mentionsParsed[i].second;

			if (mentionsParsed[i].first) {
				QString mentions = mentionsParsed[i].second;
				QStringList finalMentions;
				QStringList parts = mentions.split(" ");
				for (auto part : parts) {
					if (part.startsWith("@")) { // mention
						QString username = part;
						username.removeFirst();
						auto it = std::find_if(participants.begin(), participants.end(),
						                       [username](std::shared_ptr<linphone::Participant> p) {
							                       return p->getAddress() && username == p->getAddress()->getUsername();
						                       });
						if (it != participants.end()) {
							auto foundParticipant = *it;
							// participants.at(std::distance(participants.begin(), it));
							auto address = foundParticipant->getAddress();
							auto isFriend = findFriendByAddress(address);
							// Deliberately not cleaned: this string is a mention link target that has to
							// round-trip back to the same participant address, not a cache key.
							auto addressString = Utils::coreStringToAppString(address->asStringUriOnly());
							if (isFriend)
								part = "@" + Utils::coreStringToAppString(isFriend->getAddress()->getDisplayName());
							QString participantLink = "<a style=\"color:" + primaryColor.name() +
							                          ";\" href=\"mention:" + addressString + "\">" + part + "</a>";
							finalMentions.append(participantLink);
						} else {
							finalMentions.append(part);
						}
					} else {
						finalMentions.append(part);
					}
				}
				formattedText.push_back(finalMentions.join(" "));
			} else {
				formattedText.push_back(mentionsParsed[i].second);
			}
		}
	}
	return "<p style=\"white-space:pre-wrap;\">" + formattedText.join("");
}

std::shared_ptr<linphone::Friend> ToolModel::findFriendByAddress(const QString &address) {
	mustBeInLinphoneThread(QString(gClassName) + " : " + Q_FUNC_INFO);
	auto linphoneAddr = ToolModel::interpretUrl(address);
	if (linphoneAddr) return ToolModel::findFriendByAddress(linphoneAddr);
	else return nullptr;
}

namespace {
// Whether a SIP username is worth putting through the SDK's phone number matching. Mirrors the
// Android guard (leading '+' or all digits) with one addition: a length floor. Internal
// extensions are short and numeric, and are already resolved against the synced address book by
// findFriend, so letting them through would gain nothing and risk matching a contact who happens
// to store the same digits as a phone number.
bool looksLikePhoneNumber(const QString &username) {
	if (username.isEmpty()) return false;
	if (username.startsWith(QLatin1Char('+'))) return true;
	if (username.size() < 7) return false;
	return std::all_of(username.cbegin(), username.cend(), [](QChar c) { return c.isDigit(); });
}

// The same user reached through another host form. Inbound legs from the PBX arrive as
// sip:123@<server ip> while the address book holds sip:123@<account domain>, and the SDK matches
// friends on the whole URI string, so the two never meet. Returns the address rewritten onto the
// default account's domain, or null when there is nothing to gain.
std::shared_ptr<linphone::Address> rebaseOnAccountDomain(const std::shared_ptr<const linphone::Address> &address,
                                                         const std::shared_ptr<linphone::Core> &core) {
	if (!address || !core) return nullptr;
	auto account = core->getDefaultAccount();
	auto params = account ? account->getParams() : nullptr;
	if (!params) return nullptr;
	auto domain = params->getDomain();
	if (domain.empty() || address->getUsername().empty() || address->getDomain() == domain) return nullptr;
	auto rebased = address->clone();
	rebased->setDomain(domain);
	rebased->setPort(0); // Friends are stored without one, and the match is on the URI string.
	return rebased;
}
} // namespace

std::shared_ptr<linphone::Friend>
ToolModel::findFriendByAddress(const std::shared_ptr<const linphone::Address> &linphoneAddr) {
	mustBeInLinphoneThread(QString(gClassName) + " : " + Q_FUNC_INFO);
	if (!linphoneAddr) return nullptr;
	auto friendsManager = FriendsManager::getInstance();
	QString key = FriendsManager::addressKey(linphoneAddr);
	if (friendsManager->isInKnownFriends(key)) {
		return friendsManager->getKnownFriendAtKey(key);
	} else if (friendsManager->isInUnknownFriends(key)) {
		return friendsManager->getUnknownFriendAtKey(key);
	}

	auto core = CoreModel::getInstance()->getCore();
	// Strip transport and uri parameters such as ";user=phone" so the lookup sees the same address
	// the contact was stored with.
	auto cleanAddr = linphoneAddr->clone();
	cleanAddr->clean();

	// First, contacts held locally, which includes everything the CardDAV sync has pulled down.
	auto f = core->findFriend(cleanAddr);

	// Then, the same number written another way. A contact storing +447771514661 will not match an
	// inbound call from 07771514661 on address alone, but the SDK normalises both sides through the
	// account's dial plan here. That makes this tier dependent on the account carrying an
	// international prefix; see SettingsModel::applyAccountDialPlanDefault.
	if (!f) {
		auto username = Utils::coreStringToAppString(cleanAddr->getUsername());
		if (looksLikePhoneNumber(username)) {
			f = core->findFriendByPhoneNumber(Utils::appStringToCoreString(username));
		}
	}

	// Then the same address on the account's own domain. See rebaseOnAccountDomain.
	if (!f) {
		auto rebased = rebaseOnAccountDomain(cleanAddr, core);
		if (rebased) f = core->findFriend(rebased);
	}

	if (f) {
		friendsManager->removeUnknownFriend(key);
		friendsManager->removeOtherAddress(key);
		friendsManager->appendKnownFriend(cleanAddr, f);
		return f;
	}

	// Finally the remote directories. Asynchronous, so this call still returns nothing; the result
	// arrives via MagicSearchModel. Inert in the usual deployment, where the whole address book is
	// synced locally and no remote directory is registered, but harmless to keep.
	if (friendsManager->isInOtherAddresses(key)) return nullptr;
	friendsManager->appendOtherAddress(key);
	if (core->getRemoteContactDirectories().empty()) return nullptr;
	qDebug() << "Couldn't find friend" << key << "in core or in maps, use magic search";
	CoreModel::getInstance()->searchInMagicSearch(key,
	                                              (int)linphone::MagicSearch::Source::LdapServers |
	                                                  (int)linphone::MagicSearch::Source::RemoteCardDAV,
	                                              LinphoneEnums::MagicSearchAggregation::Friend, 50);
	return nullptr;
}

bool ToolModel::createCall(const QString &sipAddress,
                           const QVariantMap &options,
                           const QString &prepareTransfertAddress,
                           const QHash<QString, QString> &headers,
                           linphone::MediaEncryption mediaEncryption,
                           QString *errorMessage) {
	bool waitRegistrationForCall = true; // getSettingsModel()->getWaitRegistrationForCall()

	std::shared_ptr<linphone::Core> core = CoreModel::getInstance()->getCore();

	if (waitRegistrationForCall) {
		std::shared_ptr<linphone::Account> currentAccount = core->getDefaultAccount();
		if (!currentAccount || currentAccount->getState() != linphone::RegistrationState::Ok) {
			connect(
			    CoreModel::getInstance().get(), &CoreModel::accountRegistrationStateChanged,
			    CoreModel::getInstance().get(),
			    [sipAddress, options, prepareTransfertAddress, headers, mediaEncryption]() {
				    ToolModel::createCall(sipAddress, options, prepareTransfertAddress, headers, mediaEncryption);
			    },
			    Qt::SingleShotConnection);
			return false;
		}
	}

	bool localVideoEnabled = options.contains("localVideoEnabled") ? options["localVideoEnabled"].toBool() : false;

	// Every dialled string arrives here: the dial pad, the contacts list (which passes a contact's
	// stored number verbatim) and redial from history. interpretUrl itself is left free of PBX
	// policy because it also interprets account identities and addresses we are only looking up.
	std::shared_ptr<linphone::Address> address = interpretUrl(normalizePhoneNumber(sipAddress));

	if (!address) {
		lCritical() << "[" + QString(gClassName) + "] The calling address is not an interpretable SIP address: "
		            << sipAddress;
		if (errorMessage) {
			//: "The calling address is not an interpretable SIP address : %1
			*errorMessage = tr("call_error_uninterpretable_sip_address").arg(sipAddress);
		}
		return false;
	}
	bool isConference = !!core->findConferenceInformationFromUri(address);
	if (isConference) mediaEncryption = linphone::MediaEncryption::ZRTP;

	if (SettingsModel::dndEnabled(core->getConfig())) { // Force tones for outgoing calls when in DND mode (ringback,
		                                                // dtmf, etc … ) disabled again when no more calls are running.
		SettingsModel::getInstance()->setCallToneIndicationsEnabled(true);
	}
	std::shared_ptr<linphone::CallParams> params = core->createCallParams(nullptr);
	params->enableVideo(localVideoEnabled);
	params->enableCamera(localVideoEnabled);
	auto videoDirection = localVideoEnabled ? linphone::MediaDirection::SendRecv : linphone::MediaDirection::RecvOnly;
	params->setVideoDirection(videoDirection);

	bool micEnabled = options.contains("microEnabled") ? options["microEnabled"].toBool() : true;
	params->enableMic(micEnabled);
	params->setMediaEncryption(mediaEncryption);
	if (Utils::coreStringToAppString(params->getRecordFile()).isEmpty()) {

		params->setRecordFile(
		    Paths::getCapturesDirPath()
		        .append(Utils::generateSavedFilename(QString::fromStdString(address->getUsername()), ""))
		        .append(".mkv")
		        .toStdString());
	}

	QHashIterator<QString, QString> iterator(headers);
	while (iterator.hasNext()) {
		iterator.next();
		params->addCustomHeader(Utils::appStringToCoreString(iterator.key()),
		                        Utils::appStringToCoreString(iterator.value()));
	}

	if (core->getDefaultAccount()) params->setAccount(core->getDefaultAccount());
	auto call = core->inviteAddressWithParams(address, params);
	return call != nullptr;

	/* TODO transfer

	std::shared_ptr<linphone::Account> currentAccount = core->getDefaultAccount();
	if (currentAccount) {
	    if (!waitRegistrationForCall || currentAccount->getState() == linphone::RegistrationState::Ok) {
	        qWarning() << "prepareTransfert not impolemented";
	        // CallModel::prepareTransfert(core->inviteAddressWithParams(address, params), prepareTransfertAddress);
	    } else {
	        qWarning() << "Waiting registration not implemented";

	        // QObject *context = new QObject();
	        // QObject::connect(
	        //     CoreManager::getInstance()->getHandlers().get(), &CoreHandlers::registrationStateChanged, context,
	        //     [address, core, params, currentAccount, prepareTransfertAddress, context](
	        //         const std::shared_ptr<linphone::Account> &account, linphone::RegistrationState state) mutable {
	        //	    if (context && account == currentAccount && state == linphone::RegistrationState::Ok) {
	        //		    CallModel::prepareTransfert(core->inviteAddressWithParams(address, params),
	        //		                                prepareTransfertAddress);
	        //		    context->deleteLater();
	        //		    context = nullptr;
	        //	    }
	        //    });
	    }
	} else qWarning() << "prepareTransfert not impolemented";
	// CallModel::prepareTransfert(core->inviteAddressWithParams(address, params), prepareTransfertAddress);
	*/
}

std::shared_ptr<linphone::Conference> ToolModel::createConference(QString subject, QString *message) {
	auto core = CoreModel::getInstance()->getCore();
	auto conferenceParams = core->createConferenceParams(nullptr);
	conferenceParams->enableVideo(true);
	auto account = core->getDefaultAccount();
	if (!account) {
		qWarning() << "No default account found, can't create group call";
		*message = tr("group_call_error_no_account");
		return nullptr;
	}
	conferenceParams->setAccount(account);
	conferenceParams->setSubject(Utils::appStringToCoreString(subject));

	auto createEndToEnd = SettingsModel::getInstance()->getCreateEndToEndEncryptedMeetingsAndGroupCalls();
	conferenceParams->setSecurityLevel(createEndToEnd ? linphone::Conference::SecurityLevel::EndToEnd
	                                                  : linphone::Conference::SecurityLevel::PointToPoint);

	conferenceParams->enableChat(true);

	return core->createConferenceWithParams(conferenceParams);
}

bool ToolModel::createGroupCall(QString subject, const std::list<QString> &participantAddresses, QString *message) {
	auto conference = createConference(subject, message);
	if (conference) {
		auto core = CoreModel::getInstance()->getCore();
		auto callParams = core->createCallParams(nullptr);
		callParams->enableVideo(true);
		callParams->setVideoDirection(linphone::MediaDirection::RecvOnly);
		std::list<std::shared_ptr<linphone::Address>> participants;
		for (auto &address : participantAddresses) {
			auto linAddr = ToolModel::interpretUrl(normalizePhoneNumber(address));
			participants.push_back(linAddr);
		}
		auto status = conference->inviteParticipants(participants, callParams);
		if (status != 0) {
			qWarning() << "Failed to invite participants into group call";
			*message = tr("group_call_error_participants_invite");
		}
	} else {
		qWarning() << "Could not create group call";
		if (message->isEmpty()) *message = tr("group_call_error_creation");
	}
	return conference != nullptr;
}

std::shared_ptr<linphone::Account> ToolModel::findAccount(const std::shared_ptr<const linphone::Address> &address) {
	std::shared_ptr<linphone::Account> account;
	for (auto item : CoreModel::getInstance()->getCore()->getAccountList()) {
		if (item->getParams() && item->getParams()->getIdentityAddress()->weakEqual(address)) {
			account = item;
			break;
		}
	}
	return account;
}

std::shared_ptr<linphone::Account> ToolModel::findAccount(const QString &address) {
	auto linAddr = ToolModel::interpretUrl(address);
	return findAccount(linAddr);
}

bool ToolModel::isLocal(const QString &address) {
	auto linAddr = ToolModel::interpretUrl(address);
	if (!CoreModel::getInstance()->getCore()->getDefaultAccount()) {
		return false;
	} else {
		auto accountAddr = CoreModel::getInstance()->getCore()->getDefaultAccount()->getContactAddress();
		return linAddr && accountAddr ? accountAddr->weakEqual(linAddr) : false;
	}
}

bool ToolModel::isLocal(const std::shared_ptr<linphone::Conference> &conference,
                        const std::shared_ptr<const linphone::ParticipantDevice> &device) {
	auto deviceAddress = device->getAddress();
	auto callAddress = conference->getMe()->getAddress();
	auto gruuAddress = findAccount(callAddress)->getContactAddress();
	return deviceAddress->equal(gruuAddress);
}

bool ToolModel::isMe(const QString &address) {
	bool isMe = false;
	auto linAddr = ToolModel::interpretUrl(address);
	auto defaultAccount = CoreModel::getInstance()->getCore()->getDefaultAccount();
	if (!defaultAccount) {
		for (auto &account : CoreModel::getInstance()->getCore()->getAccountList()) {
			if (account->getContactAddress()->weakEqual(linAddr)) return true;
		}
	} else {
		auto accountAddr = defaultAccount->getContactAddress();
		isMe = linAddr && accountAddr ? accountAddr->weakEqual(linAddr) : false;
	}
	return isMe;
}

bool ToolModel::isMe(const std::shared_ptr<const linphone::Address> &address) {
	auto currentAccount = CoreModel::getInstance()->getCore()->getDefaultAccount();
	if (!currentAccount) { // Default account is selected : Me is all local accounts.
		return findAccount(address) != nullptr;
	} else return address ? currentAccount->getContactAddress()->weakEqual(address) : false;
}

std::shared_ptr<linphone::FriendList> ToolModel::getFriendList(const std::string &listName) {
	auto core = CoreModel::getInstance()->getCore();
	auto friendList = core->getFriendListByName(listName);
	if (!friendList) {
		friendList = core->createFriendList();
		friendList->setDisplayName(listName);
		if (listName == "ldap_friends") {
			friendList->setType(linphone::FriendList::Type::ApplicationCache);
		}
		core->addFriendList(friendList);
	}
	return friendList;
}

std::shared_ptr<linphone::FriendList> ToolModel::getAppFriendList() {
	return getFriendList("app_friends");
}

std::shared_ptr<linphone::FriendList> ToolModel::getLdapFriendList() {
	return getFriendList("ldap_friends");
}

bool ToolModel::friendIsInFriendList(const std::shared_ptr<linphone::FriendList> &friendList,
                                     const std::shared_ptr<linphone::Friend> &f) {
	if (!friendList) return false;
	auto friends = friendList->getFriends();
	auto it = std::find_if(friends.begin(), friends.end(),
	                       [f](std::shared_ptr<linphone::Friend> linFriend) { return linFriend == f; });
	return (it != friends.end());
}

QString ToolModel::getMessageFromContent(std::list<std::shared_ptr<linphone::Content>> contents) {
	mustBeInLinphoneThread(sLog().arg(Q_FUNC_INFO));
	QString res;
	for (auto &content : contents) {
		if (content->isText()) {
			return Utils::coreStringToAppString(content->getUtf8Text());
		} else if (content->isVoiceRecording()) {
			//: "Voice recording (%1)" : %1 is the duration formated in mm:ss
			return tr("voice_recording_duration").arg(Utils::formatDuration(content->getFileDuration()));
		} else if (content->isFile() || content->isFileTransfer() || content->isFileEncrypted()) {
			if (res.isEmpty()) res.append(Utils::coreStringToAppString(content->getName()));
			else res.append(", " + Utils::coreStringToAppString(content->getName()));
		} else if (content->isIcalendar()) {
			auto conferenceInfo = linphone::Factory::get()->createConferenceInfoFromIcalendarContent(content);
			auto conferenceInfoCore = ConferenceInfoCore::create(conferenceInfo);
			if (conferenceInfoCore->getConferenceInfoState() == LinphoneEnums::ConferenceInfoState::New)
				return tr("conference_invitation");
			if (conferenceInfoCore->getConferenceInfoState() == LinphoneEnums::ConferenceInfoState::Updated)
				return tr("conference_invitation_updated");
			if (conferenceInfoCore->getConferenceInfoState() == LinphoneEnums::ConferenceInfoState::Cancelled)
				return tr("conference_invitation_cancelled");
		} else if (content->isMultipart()) {
			return getMessageFromContent(content->getParts());
		}
	}
	return res;
}

// Load downloaded codecs like OpenH264 (needs to be after core is created and has loaded its plugins, as
// reloadMsPlugins modifies plugin path for the factory)
void ToolModel::loadDownloadedCodecs() {
	mustBeInLinphoneThread(sLog().arg(Q_FUNC_INFO));
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
	lInfo() << QStringLiteral("[ToolModel] Loading downloaded codecs in folder %1…").arg(Paths::getCodecsDirPath());
	QDirIterator it(Paths::getCodecsDirPath());
	while (it.hasNext()) {
		QFileInfo info(it.next());
		const QString filename(info.fileName());
		if (QLibrary::isLibrary(filename)) {
			qInfo() << QStringLiteral("Loading `%1` symbols…").arg(filename);
			auto library = QLibrary(info.filePath());
			if (!library.load()) // lib.load())
				lWarning() << QStringLiteral("[ToolModel] Failed to load `%1` symbols.").arg(filename)
				           << library.errorString();
			else lInfo() << QStringLiteral("[ToolModel] Loaded `%1` symbols…").arg(filename);
		} else lWarning() << QStringLiteral("[ToolModel] Found codec file `%1` that is not a library").arg(filename);
	}
	CoreModel::getInstance()->getCore()->reloadMsPlugins("");
	qInfo() << QStringLiteral("Finished loading downloaded codecs.");
#endif // if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
}

// Removes .in suffix from downloaded updates.
// Updates are downloaded with .in suffix as they can't overwrite already loaded plugin
// they are loaded at next app startup.

void ToolModel::updateCodecs() {
	mustBeInLinphoneThread(sLog().arg(Q_FUNC_INFO));
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
	static const QString codecSuffix = QStringLiteral(".%1").arg(Constants::LibraryExtension);

	QDirIterator it(Paths::getCodecsDirPath());
	while (it.hasNext()) {
		QFileInfo info(it.next());
		if (info.suffix() == QLatin1String("in")) {
			QString codecName = info.completeBaseName();
			if (codecName.endsWith(codecSuffix)) {
				QString codecPath = info.dir().path() + QDir::separator() + codecName;
				QFile::remove(codecPath);
				QFile::rename(info.filePath(), codecPath);
			}
		}
	}
#endif // if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
}

QVariantMap ToolModel::createVariant(const std::shared_ptr<const linphone::AudioDevice> &device) {
	QVariantMap map;
	map.insert("id", device ? Utils::coreStringToAppString(device->getId()) : "");
	map.insert("display_name",
	           device ? Utils::coreStringToAppString(device->getDriverName() + ": " + device->getDeviceName())
	                  //: "Unknown device"
	                  : tr("unknown_audio_device_name"));
	return map;
}

// User agent

QString ToolModel::computeUserAgent(const std::shared_ptr<linphone::Config> &config) {
	return QStringLiteral("%1 (%2) %3 Qt/%4 LinphoneSDK")
	    .arg(Utils::getApplicationProduct())
	    .arg(SettingsModel::getDeviceName(config).replace('\\', "\\\\").replace('(', "\\(").replace(')', "\\)"))
	    .arg(Utils::getOsProduct())
	    .arg(qVersion())
	    .remove("'");
}

bool ToolModel::isEndToEndEncryptedChatAvailable() {
	auto core = CoreModel::getInstance()->getCore();
	auto defaultAccount = core->getDefaultAccount();
	return core->limeX3DhEnabled() && defaultAccount && !defaultAccount->getParams()->getLimeServerUrl().empty() &&
	       !defaultAccount->getParams()->getConferenceFactoryUri().empty();
}

std::shared_ptr<linphone::ConferenceParams>
ToolModel::getChatRoomParams(std::shared_ptr<linphone::Call> call, std::shared_ptr<linphone::Address> remoteAddress) {
	auto core = call ? call->getCore() : CoreModel::getInstance()->getCore();
	auto localAddress = call ? call->getCallLog()->getLocalAddress() : nullptr;
	if (!remoteAddress && call) remoteAddress = call->getRemoteAddress()->clone();
	// remoteAddress->clean();
	auto account = findAccount(localAddress);
	if (!account) account = core->getDefaultAccount();
	if (!account) qWarning() << "failed to get account, return";
	if (!account) return nullptr;

	auto params = core->createConferenceParams(call ? call->getConference() : nullptr);
	params->enableChat(true);
	params->enableGroup(false);
	//: Dummy subject
	params->setSubject("Dummy subject");
	params->setAccount(account);
	params->enableAudio(false);
	params->enableVideo(false);

	auto chatParams = params->getChatParams();
	if (!chatParams) {
		qWarning() << "failed to get chat params from conference params, return";
		return nullptr;
	}
	chatParams->setEphemeralLifetime(0);
	auto accountParams = account->getParams();
	auto sameDomain = remoteAddress && remoteAddress->getDomain() == SettingsModel::getInstance()->getDefaultDomain() &&
	                  remoteAddress->getDomain() == accountParams->getDomain();
	if (accountParams->getInstantMessagingEncryptionMandatory() && sameDomain) {
		qDebug() << "Account is in secure mode & domain matches, requesting E2E encryption";
		chatParams->setBackend(linphone::ChatRoom::Backend::FlexisipChat);
		params->setSecurityLevel(linphone::Conference::SecurityLevel::EndToEnd);
	} else if (!accountParams->getInstantMessagingEncryptionMandatory()) {
		if (isEndToEndEncryptedChatAvailable()) {
			qDebug() << "Account is in interop mode but LIME is available, requesting E2E encryption";
			chatParams->setBackend(linphone::ChatRoom::Backend::FlexisipChat);
			params->setSecurityLevel(linphone::Conference::SecurityLevel::EndToEnd);
		} else {
			qDebug() << "Account is in interop mode and LIME is not available, disabling E2E encryption";
			chatParams->setBackend(linphone::ChatRoom::Backend::Basic);
			params->setSecurityLevel(linphone::Conference::SecurityLevel::None);
		}
	} else {
		qDebug() << "Account is in secure mode, can't chat with SIP address of different domain";
		return nullptr;
	}
	return params;
}

std::shared_ptr<linphone::ChatRoom> ToolModel::lookupCurrentCallChat(std::shared_ptr<CallModel> callModel) {
	auto call = callModel->getMonitor();
	auto conference = callModel->getConference();
	if (conference) {
		return conference->getChatRoom();
	} else {
		auto core = CoreModel::getInstance()->getCore();
		auto params = getChatRoomParams(call);
		auto remoteaddress = call->getRemoteAddress();
		auto localAddress = call->getCallLog()->getLocalAddress();
		std::list<std::shared_ptr<linphone::Address>> participants;
		participants.push_back(remoteaddress->clone());
		lInfo() << "[ToolModel] Looking for chat with local address" << localAddress->asStringUriOnly()
		        << "and participant" << remoteaddress->asStringUriOnly();
		auto existingChat = core->searchChatRoom(params, localAddress, nullptr, participants);
		if (existingChat) lInfo() << "[ToolModel] Found existing chat";
		else lInfo() << "[ToolModel] Did not find existing chat";
		return existingChat;
	}
}

std::shared_ptr<linphone::ChatRoom> ToolModel::createCurrentCallChat(std::shared_ptr<CallModel> callModel) {
	auto call = callModel->getMonitor();
	auto remoteAddress = call->getRemoteAddress();
	std::list<std::shared_ptr<linphone::Address>> participants;
	if (call->getConference()) {
		for (auto &participant : call->getConference()->getParticipantList()) {
			auto address = participant->getAddress();
			if (address) participants.push_back(address->clone());
		}
	} else {
		participants.push_back(remoteAddress->clone());
	}
	auto core = CoreModel::getInstance()->getCore();
	auto params = getChatRoomParams(call);
	if (!params) {
		qWarning() << "failed to create chatroom params for call with" << remoteAddress->asStringUriOnly();
		return nullptr;
	}
	auto chatRoom = core->createChatRoom(params, participants);
	return chatRoom;
}

std::shared_ptr<linphone::ChatRoom> ToolModel::lookupChatForAddress(std::shared_ptr<linphone::Address> remoteAddress) {
	auto core = CoreModel::getInstance()->getCore();
	auto account = core->getDefaultAccount();
	if (!account) return nullptr;
	auto localAddress = account->getParams()->getIdentityAddress()->clone();
	localAddress->clean();
	if (!localAddress || !remoteAddress) return nullptr;

	auto params = getChatRoomParams(nullptr, remoteAddress);
	std::list<std::shared_ptr<linphone::Address>> participants;
	remoteAddress->clean();
	participants.push_back(remoteAddress);

	lInfo() << "Looking for chat with local address" << localAddress->asStringUriOnly() << "and participant"
	        << remoteAddress->asStringUriOnly();
	auto existingChat = core->searchChatRoom(params, localAddress, nullptr, participants);
	if (existingChat) lInfo() << "Found existing chat";
	else lInfo() << "Did not find existing chat";
	return existingChat;
}

std::shared_ptr<linphone::ChatRoom> ToolModel::createChatForAddress(std::shared_ptr<linphone::Address> remoteAddress) {
	std::list<std::shared_ptr<linphone::Address>> participants;
	participants.push_back(remoteAddress->clone());
	auto core = CoreModel::getInstance()->getCore();
	auto params = getChatRoomParams(nullptr, remoteAddress);
	if (!params) {
		qWarning() << "failed to create chatroom params for address" << remoteAddress->asStringUriOnly();
		return nullptr;
	}
	auto chatRoom = core->createChatRoom(params, participants);
	return chatRoom;
}

std::shared_ptr<linphone::ChatRoom>
ToolModel::createGroupChatRoom(QString subject, std::list<std::shared_ptr<linphone::Address>> participantsAddresses) {
	auto core = CoreModel::getInstance()->getCore();
	auto account = core->getDefaultAccount();

	auto params = core->createConferenceParams(nullptr);
	params->enableChat(true);
	params->enableGroup(true);
	params->setSubject(Utils::appStringToCoreString(subject));
	params->setAccount(account);
	params->setSecurityLevel(linphone::Conference::SecurityLevel::EndToEnd);

	auto chatParams = params->getChatParams();
	if (!chatParams) {
		qWarning() << "failed to get chat params from conference params, return";
		return nullptr;
	}
	chatParams->setEphemeralLifetime(0);
	chatParams->setBackend(linphone::ChatRoom::Backend::FlexisipChat);

	auto accountParams = account->getParams();

	auto chatRoom = core->createChatRoom(params, participantsAddresses);
	if (!chatRoom) lWarning() << ("[ToolModel] Failed to create group chat");
	return chatRoom;
}

// Presence mapping from SDK PresenceModel/RFC 3863 <-> Linphone UI (5 statuses Online, Offline, Away, Busy, DND).
// Online 	= Basic Status open with no activity
// Busy 	= Basic Status open with activity Busy and description busy
// Away 	= Basic Status open with activity Away and description away
// Offline 	= Basic Status open with activity PermanentAbsence and description offline
// DND 		= Basic Status open with activity Other and description dnd
// Note : close status on the last 2 items would be preferrable, but they currently trigger multiple tuple NOTIFY from
// flexisip presence server Note 2 : close status with no activity triggers an unsubscribe.
LinphoneEnums::Presence
ToolModel::corePresenceModelToAppPresence(std::shared_ptr<const linphone::PresenceModel> presenceModel) {
	if (!presenceModel) {
		// lWarning() << sLog().arg("presence model is null.");
		return LinphoneEnums::Presence::Undefined;
	}

	auto presenceActivity = presenceModel->getActivity();
	if (presenceModel->getBasicStatus() == linphone::PresenceBasicStatus::Open) {
		if (!presenceActivity) return LinphoneEnums::Presence::Online;
		else if (presenceActivity->getType() == linphone::PresenceActivity::Type::Busy)
			return LinphoneEnums::Presence::Busy;
		else if (presenceActivity->getType() == linphone::PresenceActivity::Type::Away)
			return LinphoneEnums::Presence::Away;
		else if (presenceActivity->getType() == linphone::PresenceActivity::Type::PermanentAbsence)
			return LinphoneEnums::Presence::Offline;
		else if (presenceActivity->getType() == linphone::PresenceActivity::Type::Other)
			return LinphoneEnums::Presence::DoNotDisturb;
		else {
			lWarning() << sLog().arg("unhandled core activity type : ") << (int)presenceActivity->getType();
			return LinphoneEnums::Presence::Undefined;
		}
	}
	return LinphoneEnums::Presence::Undefined;
}

std::shared_ptr<linphone::PresenceModel> ToolModel::appPresenceToCorePresenceModel(LinphoneEnums::Presence presence,
                                                                                   QString presenceNote) {
	auto presenceModel = CoreModel::getInstance()->getCore()->createPresenceModel();

	switch (presence) {
		case LinphoneEnums::Presence::Online:
			presenceModel->setBasicStatus(linphone::PresenceBasicStatus::Open);
			break;
		case LinphoneEnums::Presence::Busy:
			presenceModel->setBasicStatus(linphone::PresenceBasicStatus::Open);
			presenceModel->setActivity(linphone::PresenceActivity::Type::Busy, "busy");
			break;
		case LinphoneEnums::Presence::Away:
			presenceModel->setBasicStatus(linphone::PresenceBasicStatus::Open);
			presenceModel->setActivity(linphone::PresenceActivity::Type::Away, "away");
			break;
		case LinphoneEnums::Presence::Offline:
			presenceModel->setBasicStatus(linphone::PresenceBasicStatus::Open);
			presenceModel->setActivity(linphone::PresenceActivity::Type::PermanentAbsence, "offline");
			break;
		case LinphoneEnums::Presence::DoNotDisturb:
			presenceModel->setBasicStatus(linphone::PresenceBasicStatus::Open);
			presenceModel->setActivity(linphone::PresenceActivity::Type::Other, "dnd");
			break;
		case LinphoneEnums::Presence::Undefined:
			lWarning() << sLog().arg("Trying to build PresenceModel from Undefined presence ");
			return nullptr;
	}

	if (!presenceNote.isEmpty()) {
		auto note = CoreModel::getInstance()->getCore()->createPresenceNote(
		    Utils::appStringToCoreString(presenceNote), Utils::appStringToCoreString(QLocale().name().left(2)));
		auto service = presenceModel->getNthService(0);
		service->addNote(note);
	}
	return presenceModel;
}

std::string ToolModel::configAccountSection(const std::shared_ptr<linphone::Account> &account) {
	int count = 0;
	for (auto item : CoreModel::getInstance()->getCore()->getAccountList()) {
		if (account == item) return "proxy_" + std::to_string(count);
		count++;
	}
	return "account";
}
