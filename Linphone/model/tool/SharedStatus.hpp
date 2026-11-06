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

#ifndef SHARED_STATUS_H_
#define SHARED_STATUS_H_

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamReader>

#include <optional>

// =============================================================================
// Shared extension status. The phone server keeps one status per extension and publishes it into
// Flexisip as its own tuple. Each app says what its user chose, and when, in the id of the tuple it
// publishes, and every app follows the server's tuple. The data travels in the tuple id because the
// SDK keeps nothing else of a tuple that we could use. See nm-pbx-docs/shared-extension-status.md.
//
//   What we publish:          nmpbx-c.<status>.<chosen>.<device>
//   What the server publishes: nmpbx-s.<status>.<chosen>.<source>
//
// Kept free of the SDK so it can be tested with QtCore alone (Linphone/tests).
// =============================================================================

namespace SharedStatus {

enum class Status { Available, Away, Busy, Dnd };

// The same limit as the Portal, the server and the other apps. The server trims to this as well.
constexpr int MaxNoteLength = 80;
// How long a choice made on this device wins over a shared status that does not show it yet.
constexpr qint64 PendingChoiceWindowMs = 30 * 1000;
constexpr int MaxDeviceTokenLength = 64;

inline QString statusToken(Status status) {
	switch (status) {
		case Status::Available:
			return QStringLiteral("available");
		case Status::Away:
			return QStringLiteral("away");
		case Status::Busy:
			return QStringLiteral("busy");
		case Status::Dnd:
			return QStringLiteral("dnd");
	}
	return QString();
}

inline std::optional<Status> statusFromToken(const QString &token) {
	if (token == QLatin1String("available")) return Status::Available;
	if (token == QLatin1String("away")) return Status::Away;
	if (token == QLatin1String("busy")) return Status::Busy;
	if (token == QLatin1String("dnd")) return Status::Dnd;
	return std::nullopt;
}

// What a user chose: the status, the message, and when (Unix seconds, 0 for "no choice yet").
struct Choice {
	Status status = Status::Available;
	QString note;
	qint64 chosen = 0;

	bool operator==(const Choice &other) const {
		return status == other.status && note == other.note && chosen == other.chosen;
	}
	bool operator!=(const Choice &other) const {
		return !(*this == other);
	}
};

struct TupleId {
	bool shared = false; // nmpbx-s (the server), otherwise nmpbx-c (an app)
	Status status = Status::Available;
	qint64 chosen = 0;
	QString origin; // the device token for nmpbx-c, the source for nmpbx-s
};

// Exactly the server's patterns. Anything else is somebody else's tuple and is not ours to read.
inline std::optional<TupleId> parseTupleId(const QString &id) {
	static const QRegularExpression pattern(
	    QStringLiteral("^nmpbx-([cs])\\.(available|away|busy|dnd)\\.(\\d{1,12})\\.([A-Za-z0-9-]{1,64})$"));
	auto match = pattern.match(id);
	if (!match.hasMatch()) return std::nullopt;
	TupleId result;
	result.shared = match.captured(1) == QLatin1String("s");
	result.status = *statusFromToken(match.captured(2));
	result.chosen = match.captured(3).toLongLong();
	result.origin = match.captured(4);
	return result;
}

inline QString choiceTupleId(Status status, qint64 chosen, const QString &device) {
	return QStringLiteral("nmpbx-c.%1.%2.%3").arg(statusToken(status)).arg(chosen < 0 ? 0 : chosen).arg(device);
}

// Keeps only the characters the server accepts, so a token taken from elsewhere (a UUID with braces
// or a urn: prefix) still matches. Empty if nothing usable is left.
inline QString cleanDeviceToken(const QString &raw) {
	QString token = raw.trimmed();
	while (token.startsWith(QLatin1Char('<')) || token.startsWith(QLatin1Char('{')) || token.startsWith(QLatin1Char('"')))
		token.remove(0, 1);
	if (token.startsWith(QLatin1String("urn:uuid:"), Qt::CaseInsensitive)) token = token.mid(9);
	QString clean;
	for (const QChar c : token) {
		if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
		    (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('-'))
			clean += c;
	}
	return clean.left(MaxDeviceTokenLength);
}

// One line of printable text, at most MaxNoteLength characters, as the server stores it. Line breaks
// and other control characters become spaces.
inline QString cleanNote(const QString &note) {
	QString clean;
	clean.reserve(note.size());
	for (const QChar c : note)
		clean += (c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator)
		             ? QChar(u' ')
		             : c;
	clean = clean.simplified();
	if (clean.size() > MaxNoteLength) {
		clean.truncate(MaxNoteLength);
		// Never leave half of a surrogate pair at the end.
		if (clean.size() > 0 && clean.back().isHighSurrogate()) clean.chop(1);
	}
	return clean;
}

// A <tuple> as it arrives in a PIDF document: its id, whether it is open, and its first note.
struct Tuple {
	QString id;
	bool open = false;
	QString note;
};

// Reads the tuples of a PIDF document. Only what the shared status needs, namespaces ignored, so a
// document Flexisip has merged from several publishers reads the same as a single one.
inline QList<Tuple> parsePidfTuples(const QByteArray &xml) {
	QList<Tuple> tuples;
	QXmlStreamReader reader(xml);
	std::optional<Tuple> current;
	bool inBasic = false;
	while (!reader.atEnd()) {
		reader.readNext();
		if (reader.isStartElement()) {
			auto name = reader.name();
			if (name == QLatin1String("tuple")) {
				current = Tuple();
				current->id = reader.attributes().value(QLatin1String("id")).toString();
			} else if (current && name == QLatin1String("basic")) {
				inBasic = true;
			} else if (current && name == QLatin1String("note")) {
				QString text = reader.readElementText(QXmlStreamReader::IncludeChildElements);
				if (current->note.isEmpty()) current->note = text.trimmed();
			}
		} else if (reader.isCharacters() && current && inBasic) {
			if (reader.text().trimmed() == QLatin1String("open")) current->open = true;
		} else if (reader.isEndElement()) {
			auto name = reader.name();
			if (name == QLatin1String("basic")) inBasic = false;
			else if (name == QLatin1String("tuple") && current) {
				tuples << *current;
				current.reset();
			}
		}
	}
	return tuples;
}

struct Shared {
	Choice choice;
	QString source;
};

// The server's shared status among a document's tuples. There is normally one. If a republish
// briefly leaves two, the newer choice is the one the server holds.
inline std::optional<Shared> findShared(const QList<Tuple> &tuples) {
	std::optional<Shared> result;
	for (const auto &tuple : tuples) {
		auto parsed = parseTupleId(tuple.id);
		if (!parsed || !parsed->shared) continue;
		if (result && result->choice.chosen > parsed->chosen) continue;
		Shared shared;
		shared.choice.status = parsed->status;
		shared.choice.chosen = parsed->chosen;
		shared.choice.note = cleanNote(tuple.note);
		shared.source = parsed->origin;
		result = shared;
	}
	return result;
}

// While a choice made on this device has not been taken up by the server, it wins for a short time,
// so the user does not see their own choice flick back. msSinceChoice < 0 means there is none.
inline bool keepPendingChoice(const Choice &shared, const Choice &pending, qint64 msSinceChoice) {
	return msSinceChoice >= 0 && msSinceChoice < PendingChoiceWindowMs && shared != pending;
}

} // namespace SharedStatus

#endif
