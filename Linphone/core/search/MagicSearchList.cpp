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

#include "MagicSearchList.hpp"
#include "core/App.hpp"
#include "core/friend/FriendCore.hpp"
#include "model/tool/ToolModel.hpp"
#include "tool/Utils.hpp"
#include <QHash>
#include <QSet>
#include <QSharedPointer>
#include <functional>
#include <linphone++/linphone.hh>

// =============================================================================

DEFINE_ABSTRACT_OBJECT(MagicSearchList)

QSharedPointer<MagicSearchList> MagicSearchList::create() {
	auto model = QSharedPointer<MagicSearchList>(new MagicSearchList(), &QObject::deleteLater);
	model->moveToThread(App::getInstance()->thread());
	model->setSelf(model);
	return model;
}

MagicSearchList::MagicSearchList(QObject *parent) : ListProxy(parent) {
	mustBeInMainThread(getClassName());
	App::getInstance()->mEngine->setObjectOwnership(this, QQmlEngine::CppOwnership);
	mSourceFlags = (int)linphone::MagicSearch::Source::Friends | (int)linphone::MagicSearch::Source::LdapServers;
	mAggregationFlag = LinphoneEnums::MagicSearchAggregation::Friend;
	mSearchFilter = "*";
}

MagicSearchList::~MagicSearchList() {
	mustBeInMainThread("~" + getClassName());
}

void MagicSearchList::setSelf(QSharedPointer<MagicSearchList> me) {
	mCoreModelConnection = SafeConnection<MagicSearchList, CoreModel>::create(me, CoreModel::getInstance());
	mCoreModelConnection->makeConnectToModel(
	    &CoreModel::friendCreated, [this](const std::shared_ptr<linphone::Friend> &f) {
		    auto friendCore = FriendCore::create(f);
		    auto haveContact =
		        std::find_if(mList.begin(), mList.end(), [friendCore](const QSharedPointer<QObject> &item) {
			        auto itemCore = item.objectCast<FriendCore>();
			        auto itemModel = itemCore->getFriendModel();
			        auto friendModel = friendCore->getFriendModel();
			        return itemCore->getDefaultAddress().length() > 0 &&
			                   itemCore->getDefaultAddress() == friendCore->getDefaultAddress() ||
			               itemModel && friendModel && itemModel->getFriend() == friendModel->getFriend() &&
			                   itemModel->getFriend()->getFriendList()->getDisplayName() ==
			                       friendModel->getFriend()->getFriendList()->getDisplayName();
		        });
		    if (haveContact == mList.end()) {
			    connect(friendCore.get(), &FriendCore::removed, this, qOverload<QObject *>(&MagicSearchList::remove));
			    add(friendCore);
			    emit friendCreated(getCount() - 1, new FriendGui(friendCore));
		    }
	    });
	mCoreModelConnection->invokeToModel([this] {
		auto linphoneSearch = CoreModel::getInstance()->getCore()->createMagicSearch();
		linphoneSearch->setLimitedSearch(false);
		auto magicSearch = Utils::makeQObject_ptr<MagicSearchModel>(linphoneSearch);
		mCoreModelConnection->invokeToCore([this, magicSearch] {
			mMagicSearch = magicSearch;
			mMagicSearch->setSelf(mMagicSearch);
			mModelConnection = SafeConnection<MagicSearchList, MagicSearchModel>::create(
			    mCoreModelConnection->mCore.mQData, mMagicSearch);
			mModelConnection->makeConnectToCore(
			    &MagicSearchList::lSearch,
			    [this](QString filter, int sourceFlags, LinphoneEnums::MagicSearchAggregation aggregationFlag,
			           int maxResults) {
				    // Deliberately no resetData() here. Emptying the list up front made every
				    // refresh, including a background one, blank the contact list for as long as the
				    // search took. setResults() merges the new results in instead.
				    mModelConnection->invokeToModel([this, filter, sourceFlags, aggregationFlag, maxResults]() {
					    mMagicSearch->search(filter, sourceFlags, aggregationFlag, maxResults);
				    });
			    });
			mModelConnection->makeConnectToModel(
			    &MagicSearchModel::searchResultsReceived,
			    [this](const std::list<std::shared_ptr<linphone::SearchResult>> &results) {
				    // Building a FriendCore per result parses and re-serialises every vCard, which is
				    // real work on the linphone thread. A periodic sync almost always returns exactly
				    // what we already hold, so hash the raw results first and skip the rest when
				    // nothing has changed. Still tell the core side we finished, or anything waiting
				    // on resultsProcessed would hang.
				    auto resultsHash = hashSearchResults(results);
				    if (mLastResultsHash && resultsHash == *mLastResultsHash) {
					    lDebug() << log().arg("Search results unchanged, skipping rebuild");
					    mModelConnection->invokeToCore([this]() { emit resultsProcessed(); });
					    return;
				    }
				    mLastResultsHash = resultsHash;
				    auto *contacts = new QList<QSharedPointer<FriendCore>>();
				    auto ldapContacts = ToolModel::getLdapFriendList();
				    auto core = CoreModel::getInstance()->getCore();
				    auto userAddress = core->getDefaultAccount() && core->getDefaultAccount()->getParams()
				                           ? core->getDefaultAccount()->getParams()->getIdentityAddress()
				                           : nullptr;
				    for (auto it : results) {
					    QSharedPointer<FriendCore> contact;
					    auto linphoneFriend = it->getFriend();
					    bool isStored = false;
					    if (linphoneFriend) {
						    if (!mShowMe && userAddress && userAddress->weakEqual(linphoneFriend->getAddress())) {
							    lWarning() << log().arg("do not show my own address in this contact list");
							    continue;
						    }
						    isStored =
						        (ldapContacts->findFriendByAddress(linphoneFriend->getAddress()) != linphoneFriend);
						    contact = FriendCore::create(linphoneFriend, isStored, it->getSourceFlags());
						    contacts->append(contact);
					    } else if (auto address = it->getAddress()) {
						    if (!mShowMe && userAddress && userAddress->weakEqual(address)) {
							    lWarning() << log().arg("do not show my own address in this contact list");
							    continue;
						    }
						    auto linphoneFriend = core->createFriend();
						    linphoneFriend->setAddress(address);
						    contact = FriendCore::create(linphoneFriend, isStored, it->getSourceFlags());
						    auto displayName = Utils::coreStringToAppString(address->getDisplayName());
						    auto splitted = displayName.split(" ");
						    if (!displayName.isEmpty() && splitted.size() > 0) {
							    contact->setGivenName(splitted[0]);
							    splitted.removeFirst();
							    contact->setFamilyName(splitted.join(" "));
						    } else {
							    contact->setGivenName(displayName.isEmpty()
							                              ? Utils::coreStringToAppString(address->getUsername())
							                              : displayName);
						    }
						    contact->setDefaultFullAddress(Utils::coreStringToAppString(
						        address->asString())); // linphone Friend object remove specific address.
						    contact->setDefaultAddress(Utils::coreStringToAppString(address->asString()));
						    contacts->append(contact);
					    } else if (!it->getPhoneNumber().empty()) {
						    auto phoneNumber = it->getPhoneNumber();
						    linphoneFriend = core->createFriend();
						    linphoneFriend->addPhoneNumber(phoneNumber);
						    contact = FriendCore::create(linphoneFriend, isStored, it->getSourceFlags());
						    contact->setGivenName(Utils::coreStringToAppString(it->getPhoneNumber()));
						    contact->appendPhoneNumber(tr("device_id"),
						                               Utils::coreStringToAppString(it->getPhoneNumber()));
						    contacts->append(contact);
					    }
				    }
				    mModelConnection->invokeToCore([this, contacts]() {
					    setResults(*contacts);
					    delete contacts;
					    emit resultsProcessed();
				    });
			    });
			lDebug() << log().arg("Initialized");
			emit initialized();
		});
	});
}

void MagicSearchList::connectContact(FriendCore *data) {
	connect(data, &FriendCore::removed, this, qOverload<QObject *>(&MagicSearchList::remove));
	connect(data, &FriendCore::starredChanged, this, &MagicSearchList::friendStarredChanged);
}

size_t MagicSearchList::hashSearchResults(const std::list<std::shared_ptr<linphone::SearchResult>> &results) {
	// Cheap identity check over the raw SDK results, before any FriendCore is built. Order matters,
	// but the SDK returns results in a stable order for a stable address book, so a reorder just
	// costs us one needless rebuild.
	size_t hash = std::hash<size_t>{}(results.size());
	for (auto &result : results) {
		if (!result) continue;
		std::string key;
		if (auto address = result->getAddress()) key = address->asStringUriOnly();
		else if (auto contact = result->getFriend()) {
			if (auto contactAddress = contact->getAddress()) key = contactAddress->asStringUriOnly();
		}
		if (key.empty()) key = result->getPhoneNumber();
		if (auto contact = result->getFriend()) key += '\n' + contact->getName();
		// Same mixing constant as boost::hash_combine.
		hash ^= std::hash<std::string>{}(key) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
	}
	return hash;
}

QString MagicSearchList::contactKey(const QSharedPointer<FriendCore> &contact) {
	auto address = contact->getDefaultAddress();
	if (!address.isEmpty()) return address;
	// No SIP address, so fall back to something stable enough to match the same person across
	// refreshes. getAllAddresses() covers phone numbers too.
	auto addresses = contact->getAllAddresses();
	auto first = addresses.isEmpty() ? QString() : addresses.first().toMap()["address"].toString();
	return contact->getFullName() + '\n' + first;
}

QString MagicSearchList::contactSignature(const QSharedPointer<FriendCore> &contact) {
	// Everything the list actually draws. Presence is deliberately absent: it arrives on its own
	// async path and updates the existing FriendCore in place.
	return contact->getFullName() + '\n' + contact->getDefaultAddress() + '\n' + contact->getPictureUri() + '\n' +
	       contact->getOrganization() + '\n' + contact->getJob() + '\n' + (contact->getStarred() ? '1' : '0') + '\n' +
	       QString::number(contact->getAddresses().size()) + '\n' + QString::number(contact->getPhoneNumbers().size());
}

// Merges the new result set into the existing list rather than replacing it. A wholesale
// resetData() would empty and rebuild every delegate, which drops the user's selection and scroll
// position. Here a refresh that changes nothing emits no model signals at all.
void MagicSearchList::setResults(const QList<QSharedPointer<FriendCore>> &contacts) {
	lDebug() << log().arg("SetResults: %1").arg(contacts.size());

	QHash<QString, int> existingRows;
	existingRows.reserve(mList.size());
	for (int row = 0; row < mList.size(); ++row) {
		auto existing = mList[row].objectCast<FriendCore>();
		if (existing) existingRows.insert(contactKey(existing), row);
	}

	QSet<int> keptRows;
	QList<QSharedPointer<FriendCore>> toAppend;
	for (const auto &contact : contacts) {
		auto row = existingRows.value(contactKey(contact), -1);
		if (row == -1) {
			toAppend.append(contact);
			continue;
		}
		keptRows.insert(row);
		auto existing = mList[row].objectCast<FriendCore>();
		if (!existing || contactSignature(existing) == contactSignature(contact)) continue;
		// Same person, changed details. Swap the item and let the views re-read just that row.
		disconnect(existing.get(), nullptr, this, nullptr);
		mList[row] = contact;
		connectContact(contact.get());
		emit dataChanged(index(row, 0), index(row, 0));
	}

	// Walk descending so indices stay valid, and remove contiguous runs in one go rather than
	// emitting a signal per row.
	int runEnd = -1;
	for (int row = mList.size() - 1; row >= -1; --row) {
		bool drop = row >= 0 && !keptRows.contains(row);
		if (drop) {
			if (runEnd == -1) runEnd = row;
			auto stale = mList[row].objectCast<FriendCore>();
			if (stale) disconnect(stale.get(), nullptr, this, nullptr);
		} else if (runEnd != -1) {
			removeRows(row + 1, runEnd - row);
			runEnd = -1;
		}
	}

	if (!toAppend.isEmpty()) {
		for (const auto &contact : toAppend) {
			connectContact(contact.get());
		}
		// Qualified because MagicSearchList::add hides the base class overloads.
		ListProxy::add<FriendCore>(toAppend);
	}
}

void MagicSearchList::add(QSharedPointer<FriendCore> contact) {
	connectContact(contact.get());
	ListProxy::add(contact);
}

void MagicSearchList::setSearch(const QString &search) {
	mSearchFilter = search;
	if (!search.isEmpty()) {
		emit lSearch(search, mSourceFlags, mAggregationFlag, mMaxResults);
	} else {
		beginResetModel();
		mList.clear();
		endResetModel();
		// The list no longer reflects the last results, so the next search must rebuild even if the
		// server returns exactly what it did before. Cleared on the linphone thread, which is the
		// only thread that reads it.
		if (mModelConnection) mModelConnection->invokeToModel([this]() { mLastResultsHash.reset(); });
	}
}

int MagicSearchList::getSourceFlags() const {
	return mSourceFlags;
}

void MagicSearchList::setSourceFlags(int flags) {
	if (mSourceFlags != flags) {
		mSourceFlags = flags;
		emit sourceFlagsChanged(mSourceFlags);
	}
}

int MagicSearchList::getMaxResults() const {
	return mMaxResults;
}

void MagicSearchList::setMaxResults(int maxResults) {
	if (mMaxResults != maxResults) {
		mMaxResults = maxResults;
		emit maxResultsChanged(mMaxResults);
	}
}

bool MagicSearchList::getShowMe() const {
	return mShowMe;
}

void MagicSearchList::setShowMe(bool showMe) {
	if (mShowMe != showMe) {
		mShowMe = showMe;
		emit showMeChanged(mShowMe);
	}
}

LinphoneEnums::MagicSearchAggregation MagicSearchList::getAggregationFlag() const {
	return mAggregationFlag;
}

void MagicSearchList::setAggregationFlag(LinphoneEnums::MagicSearchAggregation flags) {
	if (mAggregationFlag != flags) {
		mAggregationFlag = flags;
		emit aggregationFlagChanged(mAggregationFlag);
	}
}

QHash<int, QByteArray> MagicSearchList::roleNames() const {
	QHash<int, QByteArray> roles;
	roles[Qt::DisplayRole] = "$modelData";
	roles[Qt::DisplayRole + 1] = "isStored";
	return roles;
}

QVariant MagicSearchList::data(const QModelIndex &index, int role) const {
	int row = index.row();
	if (!index.isValid() || row < 0 || row >= mList.count()) return QVariant();
	if (role == Qt::DisplayRole) {
		return QVariant::fromValue(new FriendGui(mList[row].objectCast<FriendCore>()));
	} else if (role == Qt::DisplayRole + 1) {
		return mList[row].objectCast<FriendCore>()->getIsStored() || mList[row].objectCast<FriendCore>()->isLdap() ||
		       mList[row].objectCast<FriendCore>()->isCardDAV();
	}
	return QVariant();
}

int MagicSearchList::findFriendIndexByAddress(const QString &address) {
	for (int i = 0; i < getCount(); ++i) {
		auto friendCore = getAt<FriendCore>(i);
		if (!friendCore) continue;
		for (auto &friendAddress : friendCore->getAllAddresses()) {
			auto map = friendAddress.toMap();
			if (map["address"].toString() == address) {
				return i;
			}
		}
	}
	return -1;
}

QSharedPointer<FriendCore> MagicSearchList::findFriendByAddress(const QString &address) {
	for (int i = 0; i < getCount(); ++i) {
		auto friendCore = getAt<FriendCore>(i);
		if (!friendCore) continue;
		for (auto &friendAddress : friendCore->getAllAddresses()) {
			auto map = friendAddress.toMap();
			if (map["address"].toString() == address) {
				return friendCore;
			}
		}
	}
	return nullptr;
}
