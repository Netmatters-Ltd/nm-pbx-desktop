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

#ifndef LIMIT_PROXY_H_
#define LIMIT_PROXY_H_

#include "SortFilterProxy.hpp"
#include <QSortFilterProxyModel>

class LimitProxy : public QSortFilterProxyModel {
	Q_OBJECT
public:
	Q_PROPERTY(int count READ getCount NOTIFY countChanged)
	Q_PROPERTY(int initialDisplayItems READ getInitialDisplayItems WRITE setInitialDisplayItems NOTIFY
	               initialDisplayItemsChanged)
	Q_PROPERTY(int maxDisplayItems READ getMaxDisplayItems WRITE setMaxDisplayItems NOTIFY maxDisplayItemsChanged)
	// An absolute ceiling on the rows this proxy will show, whatever paging or an arriving row asks
	// for. -1, the default, means no ceiling.
	Q_PROPERTY(int displayLimit READ getDisplayLimit WRITE setDisplayLimit NOTIFY displayLimitChanged)
	Q_PROPERTY(int displayItemsStep READ getDisplayItemsStep WRITE setDisplayItemsStep NOTIFY displayItemsStepChanged)
	Q_PROPERTY(bool haveMore READ getHaveMore NOTIFY haveMoreChanged)

	// Propagation
	Q_PROPERTY(QString filterText READ getFilterText WRITE setFilterText NOTIFY filterTextChanged)
	Q_PROPERTY(int filterType READ getFilterType WRITE setFilterType NOTIFY filterTypeChanged)

	LimitProxy(QObject *parent = nullptr);
	virtual ~LimitProxy();
	virtual bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

	// Helper for setting the limit with sorted/filtered list
	void setSourceModels(SortFilterProxy *firstList);

	Q_INVOKABLE virtual void displayMore();
	// Re-tests every source row against filterAcceptsRow(). Needed because that test is index based,
	// so rows can move into or out of range without QSortFilterProxyModel ever re-checking them.
	Q_INVOKABLE void reapplyRowFilter();
	Q_INVOKABLE QVariant getAt(const int &index) const;
	Q_INVOKABLE QVariantList getAll() const;
	virtual int getCount() const;

	// Get the item following by what is shown from 2 lists
	template <class A, class B, class C>
	QSharedPointer<C> getItemAt(const int &atIndex) const {
		return dynamic_cast<A *>(sourceModel())->template getItemAt<B, C>(atIndex);
	}

	template <class A>
	inline A *getListModel() const {
		auto model = dynamic_cast<SortFilterProxy *>(sourceModel());
		if (model) return dynamic_cast<A *>(model->sourceModel());
		else return nullptr;
	}

	static int getDisplayCount(int listCount, int maxCount);
	int getDisplayCount(int listCount) const;
	int getInitialDisplayItems() const;
	void setInitialDisplayItems(int initialItems);

	int getMaxDisplayItems() const;
	void setMaxDisplayItems(int maxItems);

	int getDisplayLimit() const;
	void setDisplayLimit(int limit);

	int getDisplayItemsStep() const;
	void setDisplayItemsStep(int step);

	bool getHaveMore() const;

	//-------------------------------------------------------------
	QString getFilterText() const;
	void setFilterText(const QString &filter);

	virtual int getFilterType() const;
	virtual void setFilterType(int filterType);
	//-------------------------------------------------------------

	void onAdded(const QModelIndex &parent, int first, int last);
	void onRemoved();
	// Re-runs the row filter, but only for a proxy holding a ceiling and only when the rows on show
	// no longer match what the cap says they should be.
	void resyncRowFilter();

	int mInitialDisplayItems = -1;
	int mMaxDisplayItems = -1;
	int mDisplayItemsStep = 5;
	int mDisplayLimit = -1;

signals:
	void countChanged();
	void initialDisplayItemsChanged();
	void maxDisplayItemsChanged();
	void displayLimitChanged();
	void displayItemsStepChanged();
	void haveMoreChanged();
	//-----------------------------------------------------------------
	void filterTypeChanged(int filterType);
	void filterTextChanged();
};

#endif
