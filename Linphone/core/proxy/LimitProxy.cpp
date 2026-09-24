/*
 * Copyright (c) 2022-2024 Belledonne Communications SARL.
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

#include "LimitProxy.hpp"

LimitProxy::LimitProxy(QObject *parent) : QSortFilterProxyModel(parent) {
	connect(this, &LimitProxy::rowsInserted, this, &LimitProxy::countChanged);
	connect(this, &LimitProxy::rowsRemoved, this, &LimitProxy::countChanged);
	connect(this, &LimitProxy::modelReset, this, &LimitProxy::countChanged);
	connect(this, &LimitProxy::countChanged, this, &LimitProxy::haveMoreChanged);
}
/*
LimitProxy::LimitProxy(QAbstractItemModel *sortFilterProxy, QObject *parent) : QSortFilterProxyModel(parent) {
    setSourceModel(sortFilterProxy);
}*/
LimitProxy::~LimitProxy() {
	//	if (mDeleteSourceModel) deleteSourceModel();
}

bool LimitProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
	return mMaxDisplayItems == -1 || sourceRow < mMaxDisplayItems;
}

void LimitProxy::setSourceModels(SortFilterProxy *firstList) {
	auto secondList = firstList->sourceModel();
	if (secondList) {
		connect(secondList, &QAbstractItemModel::rowsInserted, this, &LimitProxy::onAdded);
		connect(secondList, &QAbstractItemModel::rowsRemoved, this, &LimitProxy::onRemoved);
		connect(secondList, &QAbstractItemModel::modelReset, this, &LimitProxy::invalidate);
	}
	connect(firstList, &SortFilterProxy::filterTextChanged, this, &LimitProxy::filterTextChanged);
	connect(firstList, &SortFilterProxy::filterTypeChanged, this, &LimitProxy::filterTypeChanged);
	// A filter change in firstList shuffles its rows without touching our cap, and our own row
	// filter is index based, so rows can move into range without QSortFilterProxyModel ever
	// re-testing them. Nothing above notices: onAdded and onRemoved are wired to the list beneath
	// the filter, not to the filter itself. Lists without a ceiling resync by way of the cap moving.
	connect(firstList, &SortFilterProxy::filterTextChanged, this, &LimitProxy::resyncRowFilter);
	connect(firstList, &SortFilterProxy::filterTypeChanged, this, &LimitProxy::resyncRowFilter);

	// Restore old values
	auto oldModel = dynamic_cast<SortFilterProxy *>(sourceModel());
	if (oldModel) {
		firstList->setFilterType(oldModel->getFilterType());
		firstList->setFilterText(oldModel->getFilterText());
	}

	QSortFilterProxyModel::setSourceModel(firstList);
}

/*
void LimitProxy::setSourceModels(SortFilterProxy *firstList, QAbstractItemModel *secondList) {
    connect(secondList, &QAbstractItemModel::rowsInserted, this, &LimitProxy::invalidate);
    connect(secondList, &QAbstractItemModel::rowsRemoved, this, &LimitProxy::invalidate);
    connect(firstList, &SortFilterProxy::filterTextChanged, this, &LimitProxy::filterTextChanged);
    setSourceModel(firstList);
}*/

QVariant LimitProxy::getAt(const int &atIndex) const {
	auto modelIndex = index(atIndex, 0);
	return sourceModel()->data(mapToSource(modelIndex), 0);
}

QVariantList LimitProxy::getAll() const {
	QVariantList ret;
	for (int i = 0; i < getCount(); ++i) {
		auto modelIndex = index(i, 0);
		if (modelIndex.isValid()) ret.append(sourceModel()->data(mapToSource(modelIndex), 0));
	}
	return ret;
}

int LimitProxy::getCount() const {
	return rowCount();
}

int LimitProxy::getInitialDisplayItems() const {
	return mInitialDisplayItems;
}

void LimitProxy::setInitialDisplayItems(int initialItems) {
	if (mDisplayLimit >= 0 && initialItems > mDisplayLimit) initialItems = mDisplayLimit;
	if (mInitialDisplayItems != initialItems) {
		mInitialDisplayItems = initialItems;
		if (getMaxDisplayItems() <= mInitialDisplayItems) setMaxDisplayItems(initialItems);
		if (getDisplayItemsStep() <= 0) setDisplayItemsStep(initialItems);
		emit initialDisplayItemsChanged();
	}
}

int LimitProxy::getDisplayCount(int listCount, int maxCount) {
	return maxCount >= 0 ? qMin(listCount, maxCount) : listCount;
}

int LimitProxy::getDisplayCount(int listCount) const {
	return getDisplayCount(listCount, mMaxDisplayItems);
}

int LimitProxy::getMaxDisplayItems() const {
	return mMaxDisplayItems;
}
void LimitProxy::setMaxDisplayItems(int maxItems) {
	// Every route that raises the cap lands here: displayMore(), a row arriving in the source
	// (onAdded), the initial value and QML. Clamping once here holds the ceiling for all of them.
	if (mDisplayLimit >= 0 && maxItems > mDisplayLimit) {
		maxItems = mDisplayLimit;
		if (mMaxDisplayItems == maxItems) {
			// Already at the ceiling, so the body below would do nothing. Something still asked for
			// more rows though, which for onAdded() means a row has just been inserted in the
			// source. filterAcceptsRow() is index based and QSortFilterProxyModel only tests the
			// newly inserted row, so the row pushed past the ceiling is still mapped. Drop it.
			resyncRowFilter();
			return;
		}
	}
	if (mMaxDisplayItems != maxItems) {
		auto model = sourceModel();
		int modelCount = model ? model->rowCount() : 0;
		int oldCount = getDisplayCount(modelCount);
		mMaxDisplayItems = maxItems;
		if (getInitialDisplayItems() > mMaxDisplayItems) setInitialDisplayItems(maxItems);
		if (getDisplayItemsStep() <= 0) setDisplayItemsStep(maxItems);
		emit maxDisplayItemsChanged();

		if (model && getDisplayCount(modelCount) != oldCount) {
			invalidate();
		}
	}
}

int LimitProxy::getDisplayLimit() const {
	return mDisplayLimit;
}

void LimitProxy::setDisplayLimit(int limit) {
	if (mDisplayLimit == limit) return;
	mDisplayLimit = limit;
	emit displayLimitChanged();
	// QML may bind initialDisplayItems before displayLimit, so the cap can already be above the
	// ceiling by the time we get here.
	if (mDisplayLimit >= 0 && (mMaxDisplayItems < 0 || mMaxDisplayItems > mDisplayLimit))
		setMaxDisplayItems(mDisplayLimit);
}

void LimitProxy::resyncRowFilter() {
	if (mDisplayLimit < 0) return;
	auto model = sourceModel();
	if (!model) return;
	// Cheap: rowCount() is O(1) once the mapping exists. Skipping the re-filter when nothing is out
	// of place keeps the row signals, and the work they fan out to, down to real changes.
	if (rowCount() != getDisplayCount(model->rowCount())) reapplyRowFilter();
}

// Deliberately not invalidate(): that emits layoutChanged with no hint, which the QML delegate model
// treats as a full reset and which sends the view back to the top. This emits row insert and remove
// signals instead, so the scroll position survives. Same version guard as
// SortFilterProxy::invalidateFilter().
void LimitProxy::reapplyRowFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	beginFilterChange();
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
	invalidateRowsFilter();
#endif
}

int LimitProxy::getDisplayItemsStep() const {
	return mDisplayItemsStep;
}

void LimitProxy::setDisplayItemsStep(int step) {
	if (step > 0 && mDisplayItemsStep != step) {
		mDisplayItemsStep = step;
		emit displayItemsStepChanged();
	}
}

bool LimitProxy::getHaveMore() const {
	auto model = sourceModel();
	int modelCount = model ? model->rowCount() : 0;
	return getCount() < modelCount;
}

//--------------------------------------------------------------------------------------------------

QString LimitProxy::getFilterText() const {
	return dynamic_cast<SortFilterProxy *>(sourceModel())->getFilterText();
}

void LimitProxy::setFilterText(const QString &filter) {
	dynamic_cast<SortFilterProxy *>(sourceModel())->setFilterText(filter);
}

int LimitProxy::getFilterType() const {
	return dynamic_cast<SortFilterProxy *>(sourceModel())->getFilterType();
}

void LimitProxy::setFilterType(int filter) {
	dynamic_cast<SortFilterProxy *>(sourceModel())->setFilterType(filter);
}

//--------------------------------------------------------------------------------------------------

void LimitProxy::displayMore() {
	if (mDisplayLimit >= 0 && mMaxDisplayItems >= mDisplayLimit) return;
	int oldCount = rowCount();
	auto model = sourceModel();
	int newCount = getDisplayCount(model ? model->rowCount() : 0, mMaxDisplayItems + mDisplayItemsStep);
	if (newCount != oldCount) {
		setMaxDisplayItems(newCount);
	}
}

void LimitProxy::onAdded(const QModelIndex &parent, int first, int last) {
	int count = sourceModel()->rowCount();
	// Grow by however many rows actually arrived. Growing by one per signal meant a batched insert
	// of several contacts only ever revealed the first of them.
	int added = qMax(1, last - first + 1);
	if (mMaxDisplayItems > 0 && mMaxDisplayItems <= count) setMaxDisplayItems(mMaxDisplayItems + added);
}

void LimitProxy::onRemoved() {
	int count = sourceModel()->rowCount();
	if (mMaxDisplayItems > 0 && mMaxDisplayItems <= count) {
		invalidate();
	}
}
