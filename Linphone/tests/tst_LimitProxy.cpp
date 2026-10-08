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

// LimitProxy caps how many rows a list shows, on top of a SortFilterProxy that does the searching.
// The rule every test here checks: filter first, then cap. Whatever has happened to the list, it
// should show exactly the first maxDisplayItems rows of the filtered list, in order.
//
// That is harder to keep than it looks, because LimitProxy's row filter is index based and
// QSortFilterProxyModel only re-tests rows that are inserted, never rows that merely shift. See
// nm-pbx-docs/tests.md.

#include "core/proxy/LimitProxy.hpp"
#include "core/proxy/SortFilterProxy.hpp"

#include <QAbstractListModel>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

namespace {

// Stands in for MagicSearchList or CallHistoryList: a flat list whose rows arrive with their data
// already in place, as the real lists do.
class NameList : public QAbstractListModel {
public:
	explicit NameList(const QStringList &names) : mNames(names) {
	}

	int rowCount(const QModelIndex &parent = QModelIndex()) const override {
		return parent.isValid() ? 0 : mNames.size();
	}

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
		if (!index.isValid() || role != Qt::DisplayRole) return QVariant();
		return mNames.at(index.row());
	}

	void prepend(const QString &name) {
		beginInsertRows(QModelIndex(), 0, 0);
		mNames.prepend(name);
		endInsertRows();
	}

private:
	QStringList mNames;
};

// Stands in for MagicSearchProxy::SortFilterList: sorted, and filtered on both the search text and
// a filter type. The matching rules are simplified, the shape is the same.
class NameFilterList : public SortFilterProxy {
public:
	enum FilterType { All = 0, FromM = 1 };

	explicit NameFilterList(QAbstractItemModel *list) : SortFilterProxy(list, Qt::AscendingOrder) {
	}

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
		const QString name = sourceModel()->index(sourceRow, 0, sourceParent).data().toString();
		if (mFilterType == FromM && name.compare(QString("M"), Qt::CaseInsensitive) < 0) return false;
		return mFilterText.isEmpty() || name.contains(mFilterText, Qt::CaseInsensitive);
	}
};

// The two proxies wired together as MagicSearchProxy::setList() and the QML views do it.
struct Lists {
	NameList source;
	NameFilterList *filtered; // Owned by source, as in the app.
	LimitProxy limit;

	Lists(const QStringList &names, int initialDisplayItems, int displayLimit = -1) : source(names) {
		filtered = new NameFilterList(&source);
		limit.setSourceModels(filtered);
		// Same order as CallHistoryListView binds them.
		if (displayLimit >= 0) limit.setDisplayLimit(displayLimit);
		limit.setInitialDisplayItems(initialDisplayItems);

		// Act as the view on top would. Qt builds a proxy's row mapping only when something asks for
		// rows, and a view asks straight away and again after every layout change or reset. Without
		// a mapping, a filter change has nothing to go wrong in, so tests could pass by accident.
		QObject::connect(&limit, &QAbstractItemModel::layoutChanged, &limit, [this] { limit.rowCount(); });
		QObject::connect(&limit, &QAbstractItemModel::modelReset, &limit, [this] { limit.rowCount(); });
		limit.rowCount();
	}
};

// Configured as contactsProxy in AllContactGridView.qml for a grid about 700 pixels tall. The cap
// starts at max(20, 2 * height / 63) and each page adds half as much again.
constexpr int MainListPage = 22;
constexpr int MainListStep = 33;

QStringList rows(const QAbstractItemModel &model, int count = -1) {
	const int total = model.rowCount();
	const int n = count < 0 ? total : qMin(count, total);
	QStringList result;
	for (int row = 0; row < n; ++row)
		result << model.index(row, 0).data().toString();
	return result;
}

// Empty when the list shows the first maxDisplayItems rows of the filtered list, otherwise a
// description of what is wrong.
QString problemWith(const Lists &lists, const QString &context) {
	const QStringList want = rows(*lists.filtered, lists.limit.getMaxDisplayItems());
	const QStringList got = rows(lists.limit);
	if (got == want) return QString();
	QStringList missing, extra;
	for (const QString &name : want)
		if (!got.contains(name)) missing << name;
	for (const QString &name : got)
		if (!want.contains(name)) extra << name;
	return QString("%1: showing %2 rows, expected %3 of %4 matches (cap %5). Missing: [%6]. Extra: [%7].")
	    .arg(context)
	    .arg(got.size())
	    .arg(want.size())
	    .arg(lists.filtered->rowCount())
	    .arg(lists.limit.getMaxDisplayItems())
	    .arg(missing.mid(0, 5).join(", "))
	    .arg(extra.mid(0, 5).join(", "));
}

QString randomWord(QRandomGenerator &random, const QString &letters, int length) {
	QString word;
	for (int i = 0; i < length; ++i)
		word += letters.at(random.bounded(int(letters.size())));
	return word;
}

// A list rather than a set, so the source order, and with it the order Qt reports changes in, is
// the same on every run.
QStringList randomNames(QRandomGenerator &random, int count, const QString &letters, int length) {
	QStringList names;
	while (names.size() < count) {
		QString name = randomWord(random, letters, length);
		name[0] = name[0].toUpper();
		if (!names.contains(name)) names << name;
	}
	return names;
}

// 300 contacts. Nothing but Zoe Walker contains a "z", and she sorts last, so she starts well
// outside any cap.
QStringList addressBook() {
	QRandomGenerator random(5243983);
	QStringList names = randomNames(random, 299, "abcdefghijklmnopqrstuvwxy", 7);
	names << "Zoe Walker";
	return names;
}

} // namespace

class tst_LimitProxy : public QObject {
	Q_OBJECT

private slots:
	void narrowingTheSearchShowsMatchesFromLowerDown();
	void wideningTheSearchKeepsTheCap();
	void changingTheFilterTypeKeepsTheListInStep();
	void randomSearches_data();
	void randomSearches();
	void searchingDoesNotResetTheView();
	void displayMorePagesIn();
	void arrivingRowIsShownOnAPagingList();
	void ceilingHoldsWhenRowsArrive();

	// The main Contacts and Extensions pages (AllContactGridView.qml), which page as you scroll.
	void mainListSearchAfterPagingShowsAFullPageOfMatches();
	void mainListPagingDuringASearchDoesNotShrinkTheList();
	void mainListHaveMoreIsFalseOnceEveryMatchIsShown();
};

// The reported fault: matches outside the first rows of the unfiltered list never appear.
void tst_LimitProxy::narrowingTheSearchShowsMatchesFromLowerDown() {
	Lists lists(addressBook(), 20);
	QVERIFY(!rows(lists.limit).contains("Zoe Walker"));

	// One character at a time, as typed.
	for (const QString &text : QStringList{"z", "zo", "zoe"}) {
		lists.limit.setFilterText(text);
		const QString problem = problemWith(lists, "searching for '" + text + "'");
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
	}
	QCOMPARE(rows(lists.limit), QStringList{"Zoe Walker"});
}

// Deleting characters brings rows back in, and the rows they push past the cap have to go.
void tst_LimitProxy::wideningTheSearchKeepsTheCap() {
	Lists lists(addressBook(), 20);
	for (const QString &text : QStringList{"zo", "z", "", "a", ""}) {
		lists.limit.setFilterText(text);
		const QString problem = problemWith(lists, "searching for '" + text + "'");
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
	}
	QCOMPARE(lists.limit.rowCount(), 20);
}

// The filter type, for example favourites only, takes the same route as the search text.
void tst_LimitProxy::changingTheFilterTypeKeepsTheListInStep() {
	Lists lists(addressBook(), 20);
	const QList<std::pair<QString, int>> steps{{"", NameFilterList::FromM},
	                                           {"", NameFilterList::All},
	                                           {"e", NameFilterList::FromM},
	                                           {"e", NameFilterList::All}};
	for (const auto &[text, filterType] : steps) {
		lists.limit.setFilterText(text);
		lists.limit.setFilterType(filterType);
		const QString problem =
		    problemWith(lists, QString("searching for '%1' with filter type %2").arg(text).arg(filterType));
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
	}
}

void tst_LimitProxy::randomSearches_data() {
	QTest::addColumn<bool>("ceiling");
	QTest::newRow("paging list") << false;
	QTest::newRow("call history ceiling") << true;
}

// Short lists over a small alphabet, so a single change to the search both removes and inserts
// rows. That mix is what a row count check alone cannot catch: the count can come out right while
// the wrong rows are on show.
void tst_LimitProxy::randomSearches() {
	QFETCH(bool, ceiling);
	const QString letters = "abcdef";
	QRandomGenerator random(1);
	for (int trial = 0; trial < 300; ++trial) {
		const QStringList names = randomNames(random, random.bounded(5, 121), letters, 5);
		const int cap = random.bounded(1, 26);
		Lists lists(names, cap, ceiling ? cap : -1);
		for (int step = 0; step < 12; ++step) {
			const QString text = randomWord(random, letters, random.bounded(0, 3));
			lists.limit.setFilterText(text);
			const QString problem =
			    problemWith(lists, QString("trial %1, step %2, searching for '%3'").arg(trial).arg(step).arg(text));
			QVERIFY2(problem.isEmpty(), qPrintable(problem));
		}
	}
}

// A layout change or a reset sends a QML ListView back to the top (see nm-pbx-docs/call-history.md),
// so a search may only ever insert and remove rows.
void tst_LimitProxy::searchingDoesNotResetTheView() {
	Lists lists(addressBook(), 20);
	QSignalSpy layoutChanges(&lists.limit, &QAbstractItemModel::layoutChanged);
	QSignalSpy resets(&lists.limit, &QAbstractItemModel::modelReset);
	for (const QString &text : QStringList{"z", "zo", "zoe", "zo", "z", "", "a", ""})
		lists.limit.setFilterText(text);
	QCOMPARE(layoutChanges.count(), 0);
	QCOMPARE(resets.count(), 0);
}

// Existing behaviour the fix must keep: scrolling to the end pages more rows in.
void tst_LimitProxy::displayMorePagesIn() {
	Lists lists(addressBook(), 20);
	lists.limit.setDisplayItemsStep(30);
	lists.limit.displayMore();
	QCOMPARE(lists.limit.rowCount(), 50);
	const QString problem = problemWith(lists, "after displayMore()");
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

// Existing behaviour the fix must keep: a contact added while the list is open raises the cap by
// one, so it appears without pushing another contact off.
void tst_LimitProxy::arrivingRowIsShownOnAPagingList() {
	Lists lists(addressBook(), 20);
	lists.source.prepend("Aaron Able");
	QCOMPARE(lists.limit.rowCount(), 21);
	QVERIFY(rows(lists.limit).contains("Aaron Able"));
	const QString problem = problemWith(lists, "after a contact arrives");
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

// Existing behaviour the fix must keep: call history has a fixed ceiling, so an arriving call
// pushes the oldest one off the end rather than growing the list.
void tst_LimitProxy::ceilingHoldsWhenRowsArrive() {
	Lists lists(addressBook(), 100, 100);
	lists.source.prepend("Aaron Able");
	lists.source.prepend("Abbie Adams");
	QCOMPARE(lists.limit.rowCount(), 100);
	const QString problem = problemWith(lists, "after two calls arrive");
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

// Once the user has scrolled, the cap is above its starting point, and a search should fill it.
void tst_LimitProxy::mainListSearchAfterPagingShowsAFullPageOfMatches() {
	Lists lists(addressBook(), MainListPage);
	lists.limit.setDisplayItemsStep(MainListStep);
	lists.limit.displayMore();
	QCOMPARE(lists.limit.rowCount(), MainListPage + MainListStep);

	for (const QString &text : QStringList{"a", "ab", "a", ""}) {
		lists.limit.setFilterText(text);
		const QString problem = problemWith(lists, "searching for '" + text + "' after paging");
		QVERIFY2(problem.isEmpty(), qPrintable(problem));
	}
	QCOMPARE(lists.limit.getMaxDisplayItems(), MainListPage + MainListStep);
}

// A search with few matches leaves the grid short, so it reaches its end at once and, while
// haveMore is true, calls displayMore(). If the list is wrong at that moment, displayMore() drops
// the cap to the number of matches, and it stays there after the search is cleared: the page then
// shows only a few contacts until the user scrolls.
void tst_LimitProxy::mainListPagingDuringASearchDoesNotShrinkTheList() {
	Lists lists(addressBook(), MainListPage);
	lists.limit.setDisplayItemsStep(MainListStep);
	lists.limit.setFilterText("zoe");
	lists.limit.displayMore();
	lists.limit.setFilterText("");

	QCOMPARE(lists.limit.getMaxDisplayItems(), MainListPage);
	const QString problem = problemWith(lists, "after clearing the search");
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

// haveMore is what makes the grid ask for another page when it reaches its end. In the new call
// window it also holds suggestions back while it is true.
void tst_LimitProxy::mainListHaveMoreIsFalseOnceEveryMatchIsShown() {
	Lists lists(addressBook(), MainListPage);
	lists.limit.setFilterText("zoe");
	QCOMPARE(rows(lists.limit), QStringList{"Zoe Walker"});
	QVERIFY(!lists.limit.getHaveMore());
}

QTEST_GUILESS_MAIN(tst_LimitProxy)
#include "tst_LimitProxy.moc"
