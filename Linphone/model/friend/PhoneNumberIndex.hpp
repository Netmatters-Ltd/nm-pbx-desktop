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

#ifndef PHONE_NUMBER_INDEX_H_
#define PHONE_NUMBER_INDEX_H_

#include <functional>
#include <list>
#include <string>
#include <unordered_map>

// Finds the contact holding a phone number, giving the same answer as the SDK's
// Core::findFriendByPhoneNumber for one account.
//
// The SDK answers by putting every number of every contact through the account's dial plan, on
// every call. With a few thousand contacts that is tens of milliseconds a lookup, and the call
// history does one per row, all on the linphone thread, which is where SIP runs too. This puts
// each stored number through the dial plan once, and after that a lookup costs one normalisation
// and a hash lookup.
//
// It knows nothing about the address book changing. Whoever owns it calls invalidate() when it
// does, and the next lookup rebuilds. A change of dial plan rebuilds by itself, because every key
// was normalised under the old one.
//
// Templated on the contact so the tests can run it without the SDK.
template <typename Contact>
class PhoneNumberIndex {
public:
	using Normalise = std::function<std::string(const std::string &)>;
	// Hands one contact and its numbers, as stored, to the index.
	using Visit = std::function<void(const Contact &, const std::list<std::string> &)>;
	// Visits every contact, in the order the SDK searches them: friend lists in core order, then
	// contacts in list order.
	using Enumerate = std::function<void(const Visit &)>;

	// `dialPlan` identifies what `normalise` depends on, so the index can tell when its keys have
	// gone stale. Returns a default-constructed Contact when nobody holds the number.
	Contact find(const std::string &number,
	             const std::string &dialPlan,
	             const Normalise &normalise,
	             const Enumerate &enumerate) {
		if (!mBuilt || dialPlan != mDialPlan) build(dialPlan, normalise, enumerate);
		// An empty result means the dial plan could not make sense of it. The SDK matches nothing
		// then, rather than every contact whose own number also failed.
		auto key = normalise(number);
		if (key.empty()) return Contact();
		auto it = mByNumber.find(key);
		return it == mByNumber.end() ? Contact() : it->second;
	}

	void invalidate() {
		mBuilt = false;
		mByNumber.clear();
	}

	bool isBuilt() const {
		return mBuilt;
	}

private:
	void build(const std::string &dialPlan, const Normalise &normalise, const Enumerate &enumerate) {
		mByNumber.clear();
		enumerate([this, &normalise](const Contact &contact, const std::list<std::string> &numbers) {
			for (const auto &number : numbers) {
				auto key = normalise(number);
				if (key.empty()) continue;
				// First one wins. The SDK stops at the first contact holding the number, so when two
				// contacts share one, this has to pick the same contact it would.
				mByNumber.emplace(key, contact);
			}
		});
		mDialPlan = dialPlan;
		mBuilt = true;
	}

	bool mBuilt = false;
	std::string mDialPlan;
	std::unordered_map<std::string, Contact> mByNumber;
};

#endif
