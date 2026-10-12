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

// PhoneNumberIndex finds the contact holding a phone number. It replaces the SDK's
// Core::findFriendByPhoneNumber, which normalises every number of every contact on every call. With
// 5,000 contacts that held the linphone thread for about 28 seconds while the call history loaded,
// and an outgoing call queued behind it.
//
// Two things are checked here: that it reads the address book once rather than once per lookup,
// and that it always gives the answer the SDK's scan would. See nm-pbx-docs/tests.md.

#include "model/friend/PhoneNumberIndex.hpp"

#include <QRandomGenerator>
#include <QTest>

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

namespace {

struct Contact {
	QString name;
	std::list<std::string> numbers;
};
using ContactPtr = std::shared_ptr<Contact>;
using Index = PhoneNumberIndex<ContactPtr>;

// A rough stand-in for the SDK's dial plan normalisation, close enough to give the index the same
// kinds of input: the same number written several ways, short numbers left alone, and input it
// cannot make sense of, which comes back empty.
std::string normaliseFor(const std::string &countryCode, const std::string &number) {
	std::string digits;
	bool plus = false;
	for (char c : number) {
		if (c == '+' && digits.empty() && !plus) plus = true;
		else if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
		else if (c == ' ' || c == '-' || c == '(' || c == ')') continue;
		else return std::string();
	}
	if (digits.empty()) return std::string();
	if (plus) return "+" + digits;
	if (digits.rfind("00", 0) == 0) return "+" + digits.substr(2);
	if (digits.size() < 7) return digits;
	if (digits[0] == '0') return "+" + countryCode + digits.substr(1);
	return "+" + countryCode + digits;
}

// An address book with counters on it, so a test can see how much work a lookup did.
class AddressBook {
public:
	ContactPtr add(const QString &name, std::list<std::string> numbers) {
		auto contact = std::make_shared<Contact>(Contact{name, std::move(numbers)});
		mContacts.push_back(contact);
		return contact;
	}

	void remove(const ContactPtr &contact) {
		mContacts.erase(std::remove(mContacts.begin(), mContacts.end(), contact), mContacts.end());
	}

	ContactPtr find(const std::string &number) {
		return mIndex.find(number, mCountryCode, normaliser(), enumerator());
	}

	// What the SDK does: normalise the number searched for, then every number of every contact in
	// turn, and stop at the first match.
	ContactPtr scan(const std::string &number) const {
		auto searched = normaliseFor(mCountryCode, number);
		if (searched.empty()) return nullptr;
		for (auto &contact : mContacts)
			for (auto &stored : contact->numbers) {
				auto key = normaliseFor(mCountryCode, stored);
				if (!key.empty() && key == searched) return contact;
			}
		return nullptr;
	}

	Index mIndex;
	std::string mCountryCode = "44";
	int mEnumerations = 0;
	int mNormalisations = 0;

private:
	Index::Normalise normaliser() {
		return [this](const std::string &number) {
			++mNormalisations;
			return normaliseFor(mCountryCode, number);
		};
	}

	Index::Enumerate enumerator() {
		return [this](const Index::Visit &visit) {
			++mEnumerations;
			for (auto &contact : mContacts)
				visit(contact, contact->numbers);
		};
	}

	std::vector<ContactPtr> mContacts;
};

const QString nobody = QStringLiteral("nobody");

QString nameOf(const ContactPtr &contact) {
	return contact ? contact->name : nobody;
}

std::string randomMobile(QRandomGenerator &random) {
	return "07" + std::to_string(random.bounded(100, 1000)) + std::to_string(random.bounded(100000, 1000000));
}

} // namespace

class tst_PhoneNumberIndex : public QObject {
	Q_OBJECT

private slots:
	void findsANumberWrittenAnotherWay();
	void unknownNumberFindsNothing();
	void numbersTheDialPlanRejectsMatchNothing();
	void firstContactHoldingANumberWins();
	void nothingIsReadUntilTheFirstLookup();
	void addressBookIsReadOnceForManyLookups();
	void invalidatePicksUpChangesOnTheNextLookup();
	void dialPlanChangeRebuilds();
	void agreesWithAScanOfEveryContact();
};

void tst_PhoneNumberIndex::findsANumberWrittenAnotherWay() {
	AddressBook book;
	auto alice = book.add("Alice", {"+44 7700 900123"});
	QCOMPARE(nameOf(book.find("07700900123")), alice->name);
	QCOMPARE(nameOf(book.find("00447700900123")), alice->name);
	QCOMPARE(nameOf(book.find("+447700900123")), alice->name);
}

void tst_PhoneNumberIndex::unknownNumberFindsNothing() {
	AddressBook book;
	book.add("Alice", {"+447700900123"});
	QCOMPARE(nameOf(book.find("07700900999")), nobody);
}

// When the dial plan can make nothing of a number it comes back empty. The SDK matches nothing
// then, rather than pairing it with every contact whose number also came back empty.
void tst_PhoneNumberIndex::numbersTheDialPlanRejectsMatchNothing() {
	AddressBook book;
	book.add("Reception", {"ask for extension 4"});
	QCOMPARE(nameOf(book.find("ask for extension 4")), nobody);
	QCOMPARE(nameOf(book.find("")), nobody);
}

// The SDK stops at the first contact holding the number, so when two share one, the index has to
// pick the same contact. Otherwise the name on a call would depend on which code path found it.
void tst_PhoneNumberIndex::firstContactHoldingANumberWins() {
	AddressBook book;
	auto office = book.add("Office", {"01603 704020"});
	book.add("Office (old)", {"+441603704020"});
	QCOMPARE(nameOf(book.find("01603704020")), office->name);
	QCOMPARE(nameOf(book.find("01603704020")), nameOf(book.scan("01603704020")));
}

void tst_PhoneNumberIndex::nothingIsReadUntilTheFirstLookup() {
	AddressBook book;
	book.add("Alice", {"+447700900123"});
	QVERIFY(!book.mIndex.isBuilt());
	QCOMPARE(book.mEnumerations, 0);
	book.find("07700900123");
	QVERIFY(book.mIndex.isBuilt());
}

// The fault itself, at the size it was seen: 5,000 contacts and the 500 rows of call history the
// app loads, almost all of them numbers nobody has saved. The SDK put all 10,000 stored numbers
// through the dial plan once per row. The index does it once in total.
void tst_PhoneNumberIndex::addressBookIsReadOnceForManyLookups() {
	QRandomGenerator random(5243983);
	AddressBook book;
	const int contacts = 5000;
	const int numbersEach = 2;
	const int lookups = 500;
	for (int i = 0; i < contacts; ++i)
		book.add(QString("Contact %1").arg(i), {randomMobile(random), randomMobile(random)});

	for (int i = 0; i < lookups; ++i)
		book.find(randomMobile(random));

	QCOMPARE(book.mEnumerations, 1);
	QCOMPARE(book.mNormalisations, contacts * numbersEach + lookups);
}

void tst_PhoneNumberIndex::invalidatePicksUpChangesOnTheNextLookup() {
	AddressBook book;
	auto alice = book.add("Alice", {"+447700900123"});
	QCOMPARE(nameOf(book.find("07700900123")), alice->name);

	// Without invalidate() the index keeps answering from what it read first.
	auto bob = book.add("Bob", {"+447700900456"});
	book.remove(alice);
	QCOMPARE(nameOf(book.find("07700900456")), nobody);

	book.mIndex.invalidate();
	QVERIFY(!book.mIndex.isBuilt());
	QCOMPARE(nameOf(book.find("07700900456")), bob->name);
	QCOMPARE(nameOf(book.find("07700900123")), nobody);
	QCOMPARE(book.mEnumerations, 2);
}

// Every key was normalised under the old dial plan, so a change of plan has to rebuild even
// though nobody called invalidate().
void tst_PhoneNumberIndex::dialPlanChangeRebuilds() {
	AddressBook book;
	auto pierre = book.add("Pierre", {"+33 7700 900123"});
	QCOMPARE(nameOf(book.find("07700900123")), nobody);

	book.mCountryCode = "33";
	QCOMPARE(nameOf(book.find("07700900123")), pierre->name);
	QCOMPARE(book.mEnumerations, 2);

	// And no further rebuilds while the plan stays put.
	book.find("07700900999");
	QCOMPARE(book.mEnumerations, 2);
}

// Random address books, with the awkward cases mixed in: contacts sharing a number, numbers
// written in different forms, short numbers and numbers the dial plan rejects. Every lookup must
// give exactly what a scan of every contact gives.
void tst_PhoneNumberIndex::agreesWithAScanOfEveryContact() {
	QRandomGenerator random(20261012);
	for (int round = 0; round < 50; ++round) {
		AddressBook book;
		std::vector<std::string> pool;
		for (int i = 0; i < 40; ++i)
			pool.push_back(randomMobile(random));
		pool.push_back("999");
		pool.push_back("reception");

		auto written = [&random](const std::string &number) {
			if (number.size() < 7 || number[0] != '0') return number;
			switch (random.bounded(3)) {
				case 0:
					return number;
				case 1:
					return "+44" + number.substr(1);
				default:
					return "0044 " + number.substr(1, 4) + " " + number.substr(5);
			}
		};

		int contacts = random.bounded(1, 60);
		for (int i = 0; i < contacts; ++i) {
			std::list<std::string> numbers;
			int count = random.bounded(0, 4);
			for (int n = 0; n < count; ++n)
				numbers.push_back(written(pool[random.bounded(int(pool.size()))]));
			book.add(QString("Contact %1").arg(i), numbers);
		}

		for (int i = 0; i < 60; ++i) {
			auto number = written(pool[random.bounded(int(pool.size()))]);
			auto expected = book.scan(number);
			auto actual = book.find(number);
			if (actual != expected) {
				QFAIL(qPrintable(QString("Round %1, %2: index found %3, scan found %4")
				                     .arg(round)
				                     .arg(QString::fromStdString(number))
				                     .arg(nameOf(actual))
				                     .arg(nameOf(expected))));
			}
		}
		QCOMPARE(book.mEnumerations, 1);
	}
}

QTEST_GUILESS_MAIN(tst_PhoneNumberIndex)
#include "tst_PhoneNumberIndex.moc"
