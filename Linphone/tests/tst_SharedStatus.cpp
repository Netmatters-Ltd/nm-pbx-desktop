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

// SharedStatus holds the wire format of the shared extension status: the tuple ids the app publishes
// and reads, the PIDF it reads for its own extension, and the rule for a choice the server has not
// taken up yet. See nm-pbx-docs/shared-extension-status.md and nm-pbx-docs/tests.md.

#include "model/tool/SharedStatus.hpp"

#include <QTest>

using namespace SharedStatus;

class SharedStatusTest : public QObject {
	Q_OBJECT

private slots:
	void choiceTupleIdMatchesTheServersPattern();
	void parsesTheServersTuple();
	void parsesAnAppsTuple();
	void ignoresAnythingElse_data();
	void ignoresAnythingElse();
	void cleansDeviceTokens();
	void notesAreOneLineOfAtMost80();
	void readsTheServersDocument();
	void readsAMergedDocument();
	void sharedAvailableIsClosedAndHasNoNote();
	void noSharedTupleMeansSwitchedOff();
	void newerSharedTupleWins();
	void pendingChoiceWinsForThirtySeconds();
};

void SharedStatusTest::choiceTupleIdMatchesTheServersPattern() {
	QCOMPARE(choiceTupleId(Status::Away, 1793963980, "3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003"),
	         QString("nmpbx-c.away.1793963980.3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003"));
	QCOMPARE(choiceTupleId(Status::Available, 0, "abc"), QString("nmpbx-c.available.0.abc"));
	QCOMPARE(choiceTupleId(Status::Dnd, 5, "abc"), QString("nmpbx-c.dnd.5.abc"));
	QCOMPARE(choiceTupleId(Status::Busy, -3, "abc"), QString("nmpbx-c.busy.0.abc"));

	// The exact pattern from the handover, as the server matches it.
	QRegularExpression server("^nmpbx-c\\.(available|away|busy|dnd)\\.(\\d{1,12})\\.([A-Za-z0-9-]{1,64})$");
	QVERIFY(server.match(choiceTupleId(Status::Busy, 1793963980, cleanDeviceToken(
	                                                                 "{3F2A9C1E-7B4D-4E0A-9D1B-55C0A1E2F003}")))
	            .hasMatch());
}

void SharedStatusTest::parsesTheServersTuple() {
	auto parsed = parseTupleId("nmpbx-s.away.1793963980.portal-12");
	QVERIFY(parsed);
	QVERIFY(parsed->shared);
	QVERIFY(parsed->status == Status::Away);
	QCOMPARE(parsed->chosen, qint64(1793963980));
	QCOMPARE(parsed->origin, QString("portal-12"));

	parsed = parseTupleId("nmpbx-s.dnd.1793963981.fc");
	QVERIFY(parsed);
	QVERIFY(parsed->status == Status::Dnd);
	QCOMPARE(parsed->origin, QString("fc"));
}

void SharedStatusTest::parsesAnAppsTuple() {
	auto parsed = parseTupleId("nmpbx-c.busy.0.abc-123");
	QVERIFY(parsed);
	QVERIFY(!parsed->shared);
	QVERIFY(parsed->status == Status::Busy);
	QCOMPARE(parsed->chosen, qint64(0));
	QCOMPARE(parsed->origin, QString("abc-123"));
}

void SharedStatusTest::ignoresAnythingElse_data() {
	QTest::addColumn<QString>("id");
	QTest::newRow("random SDK id") << "k9i37v";
	QTest::newRow("call-state bridge") << "nmpbx-callstate";
	QTest::newRow("unknown status") << "nmpbx-s.offline.1.fc";
	QTest::newRow("upper case status") << "nmpbx-s.Away.1.fc";
	QTest::newRow("13 digit time") << "nmpbx-s.away.1234567890123.fc";
	QTest::newRow("no source") << "nmpbx-s.away.1.";
	QTest::newRow("bad source character") << "nmpbx-s.away.1.portal_12";
	QTest::newRow("source too long") << "nmpbx-s.away.1." + QString(65, 'a');
	QTest::newRow("trailing text") << "nmpbx-s.away.1.fc.x";
	QTest::newRow("other prefix") << "nmpbx-x.away.1.fc";
}

void SharedStatusTest::ignoresAnythingElse() {
	QFETCH(QString, id);
	QVERIFY(!parseTupleId(id));
}

void SharedStatusTest::cleansDeviceTokens() {
	QCOMPARE(cleanDeviceToken("{3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003}"),
	         QString("3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003"));
	QCOMPARE(cleanDeviceToken("<urn:uuid:3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003>"),
	         QString("3f2a9c1e-7b4d-4e0a-9d1b-55c0a1e2f003"));
	QCOMPARE(cleanDeviceToken("urn:uuid:3f2a9c1e-7b4d"), QString("3f2a9c1e-7b4d"));
	QCOMPARE(cleanDeviceToken(QString(100, 'a')).size(), qsizetype(MaxDeviceTokenLength));
	QCOMPARE(cleanDeviceToken("!!!"), QString());
}

void SharedStatusTest::notesAreOneLineOfAtMost80() {
	QCOMPARE(cleanNote("Holiday, back 3 Feb"), QString("Holiday, back 3 Feb"));
	QCOMPARE(cleanNote("  On site\r\nback at 2  "), QString("On site back at 2"));
	QCOMPARE(cleanNote(QString(81, 'x')).size(), qsizetype(MaxNoteLength));
	QCOMPARE(cleanNote(QString(80, 'x')).size(), qsizetype(80));
	QCOMPARE(cleanNote(""), QString());
	// A character outside the BMP is never cut in half.
	QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
	QString note = QString(79, 'x') + emoji;
	QCOMPARE(cleanNote(note), QString(79, 'x'));
}

void SharedStatusTest::readsTheServersDocument() {
	// The document from the handover, as it reaches subscribers.
	QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<presence xmlns="urn:ietf:params:xml:ns:pidf"
          xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
          xmlns:rpid="urn:ietf:params:xml:ns:pidf:rpid"
          entity="sip:102@example.nmpbx.uk">
  <tuple id="nmpbx-s.away.1793963980.portal-12">
    <status><basic>open</basic></status>
    <note xml:lang="en">Holiday, back 3 Feb</note>
  </tuple>
  <dm:person id="nmpbx-s-person">
    <rpid:activities><rpid:away/></rpid:activities>
  </dm:person>
</presence>)";
	auto tuples = parsePidfTuples(xml);
	QCOMPARE(tuples.size(), qsizetype(1));
	QCOMPARE(tuples[0].id, QString("nmpbx-s.away.1793963980.portal-12"));
	QVERIFY(tuples[0].open);
	QCOMPARE(tuples[0].note, QString("Holiday, back 3 Feb"));

	auto shared = findShared(tuples);
	QVERIFY(shared);
	QVERIFY(shared->choice.status == Status::Away);
	QCOMPARE(shared->choice.chosen, qint64(1793963980));
	QCOMPARE(shared->choice.note, QString("Holiday, back 3 Feb"));
	QCOMPARE(shared->source, QString("portal-12"));
}

void SharedStatusTest::readsAMergedDocument() {
	// Flexisip merges every publisher's tuples and pools their activities into one person, with
	// prefixes of its own choosing. Another app's choice, an older app and the call-state bridge are
	// all here, and the shared status is still the one taken.
	QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<presence xmlns:p1="urn:ietf:params:xml:ns:pidf:data-model" xmlns:p2="urn:ietf:params:xml:ns:pidf:rpid"
          xmlns="urn:ietf:params:xml:ns:pidf" entity="sip:150@nm-test.nmpbx.uk">
  <tuple id="k9i37v">
    <status><basic>open</basic></status>
    <contact priority="0.8">sip:150@nm-test.nmpbx.uk</contact>
    <note xml:lang="en">Old message</note>
    <timestamp>2026-09-10T20:20:15Z</timestamp>
  </tuple>
  <tuple id="nmpbx-c.busy.1793963999.android-1">
    <status><basic>open</basic></status>
    <note xml:lang="en">On site</note>
  </tuple>
  <tuple id="nmpbx-callstate">
    <status><basic>open</basic></status>
  </tuple>
  <tuple id="nmpbx-s.dnd.1793963990.fc">
    <status><basic>open</basic></status>
  </tuple>
  <p1:person id="">
    <p2:activities><p2:busy/></p2:activities>
    <p2:activities><p2:on-the-phone/></p2:activities>
    <p2:activities><p2:other>dnd</p2:other></p2:activities>
  </p1:person>
</presence>)";
	auto tuples = parsePidfTuples(xml);
	QCOMPARE(tuples.size(), qsizetype(4));
	QCOMPARE(tuples[1].note, QString("On site"));

	auto shared = findShared(tuples);
	QVERIFY(shared);
	QVERIFY(shared->choice.status == Status::Dnd);
	QCOMPARE(shared->choice.chosen, qint64(1793963990));
	// No <note> means an empty message, not another tuple's.
	QCOMPARE(shared->choice.note, QString());
	QCOMPARE(shared->source, QString("fc"));
}

void SharedStatusTest::sharedAvailableIsClosedAndHasNoNote() {
	QByteArray xml = R"(<presence xmlns="urn:ietf:params:xml:ns:pidf" entity="sip:102@example.nmpbx.uk">
  <tuple id="nmpbx-s.available.1793963980.app-abc"><status><basic>closed</basic></status></tuple>
</presence>)";
	auto tuples = parsePidfTuples(xml);
	QCOMPARE(tuples.size(), qsizetype(1));
	QVERIFY(!tuples[0].open);
	auto shared = findShared(tuples);
	QVERIFY(shared);
	// Closed does not mean offline: the status is in the id.
	QVERIFY(shared->choice.status == Status::Available);
	QCOMPARE(shared->source, QString("app-abc"));
}

void SharedStatusTest::noSharedTupleMeansSwitchedOff() {
	QByteArray xml = R"(<presence xmlns="urn:ietf:params:xml:ns:pidf" entity="sip:102@example.nmpbx.uk">
  <tuple id="nmpbx-c.away.1793963980.abc"><status><basic>open</basic></status></tuple>
  <tuple id="k9i37v"><status><basic>open</basic></status><note>No presence information available yet</note></tuple>
</presence>)";
	QVERIFY(!findShared(parsePidfTuples(xml)));
	QVERIFY(!findShared(parsePidfTuples(QByteArray())));
	QVERIFY(!findShared(parsePidfTuples("not xml at all")));
}

void SharedStatusTest::newerSharedTupleWins() {
	QList<Tuple> tuples;
	Tuple older;
	older.id = "nmpbx-s.away.100.fc";
	Tuple newer;
	newer.id = "nmpbx-s.busy.200.portal-1";
	newer.note = "In a meeting";
	tuples << newer << older;
	auto shared = findShared(tuples);
	QVERIFY(shared);
	QVERIFY(shared->choice.status == Status::Busy);
	QCOMPARE(shared->choice.note, QString("In a meeting"));

	tuples.clear();
	tuples << older << newer;
	QVERIFY(findShared(tuples)->choice.status == Status::Busy);
}

void SharedStatusTest::pendingChoiceWinsForThirtySeconds() {
	Choice mine{Status::Available, QString(), 200};
	Choice serverOld{Status::Away, QString(), 100};

	// The server has not shown my choice yet: keep it, but only for 30 seconds.
	QVERIFY(keepPendingChoice(serverOld, mine, 0));
	QVERIFY(keepPendingChoice(serverOld, mine, PendingChoiceWindowMs - 1));
	QVERIFY(!keepPendingChoice(serverOld, mine, PendingChoiceWindowMs));
	// No pending choice.
	QVERIFY(!keepPendingChoice(serverOld, mine, -1));
	// The server shows my choice: follow it straight away.
	QVERIFY(!keepPendingChoice(mine, mine, 0));
	// Only the message differs: still not taken up.
	Choice serverOtherNote{Status::Available, QString("x"), 200};
	QVERIFY(keepPendingChoice(serverOtherNote, mine, 0));
}

QTEST_GUILESS_MAIN(SharedStatusTest)
#include "tst_SharedStatus.moc"
