#!/usr/bin/env python3
"""Check that LimitProxy's displayLimit ceiling holds.

Background in nm-pbx-docs/call-history.md.

Mirrors the current Linphone/core/proxy/LimitProxy.cpp, including displayLimit, the clamp in
setMaxDisplayItems(), resyncRowFilter() and the onAdded/onRemoved wiring, over a 500 row source
behind a text filter. Drives the same shape of view as the capped CallHistoryListView and checks:

  - the cap holds at 100 on load and after scrolling to the bottom
  - an arriving call does not push the count past the cap, and does not reset the scroll
  - a search narrowing the source below the cap still shows every match
  - displayLimit and initialDisplayItems settle correctly whichever order they arrive in
  - a proxy with no displayLimit still pages, so the other lists are untouched

Setup and run as for probe.py in this directory:

    QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe cap.py
"""
import sys, os
from PySide6.QtCore import (QAbstractListModel, QModelIndex, Qt, QSortFilterProxyModel,
                            QTimer, Slot, Property, Signal, QUrl)
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtQml import qmlRegisterType


class SourceList(QAbstractListModel):
    def __init__(self, n=500):
        super().__init__()
        self._rows = ["call-%04d" % i for i in range(n)]

    def rowCount(self, parent=QModelIndex()):
        return len(self._rows)

    def data(self, index, role=Qt.DisplayRole):
        return self._rows[index.row()] if index.isValid() and role == Qt.DisplayRole else None

    def prepend(self, label):
        self.beginInsertRows(QModelIndex(), 0, 0)
        self._rows.insert(0, label)
        self.endInsertRows()


class SortFilterProxy(QSortFilterProxyModel):
    """Mirrors SortFilterProxy: text filter applied with begin/endFilterChange."""
    filterTextChanged = Signal()

    def __init__(self):
        super().__init__()
        self._text = ""

    def filterAcceptsRow(self, row, parent):
        if not self._text:
            return True
        src = self.sourceModel()
        return self._text in (src.data(src.index(row, 0)) or "")

    def setFilterText(self, t):
        if self._text == t:
            return
        self.beginFilterChange()
        self._text = t
        self.endFilterChange(QSortFilterProxyModel.Direction.Rows)
        self.filterTextChanged.emit()


class LimitProxy(QSortFilterProxyModel):
    countChanged = Signal()
    maxDisplayItemsChanged = Signal()
    initialDisplayItemsChanged = Signal()
    displayItemsStepChanged = Signal()
    displayLimitChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._initial, self._max, self._step, self._limit = -1, -1, 5, -1
        self.rowsInserted.connect(self.countChanged)
        self.rowsRemoved.connect(self.countChanged)
        self.modelReset.connect(self.countChanged)

    def filterAcceptsRow(self, row, parent):
        return self._max == -1 or row < self._max

    def getCount(self):
        return self.rowCount()
    count = Property(int, getCount, notify=countChanged)

    def getHaveMore(self):
        m = self.sourceModel()
        return self.rowCount() < (m.rowCount() if m else 0)
    haveMore = Property(bool, getHaveMore, notify=countChanged)

    def _displayCount(self, listCount, maxCount=None):
        m = self._max if maxCount is None else maxCount
        return min(listCount, m) if m >= 0 else listCount

    def setSourceModels(self, first):
        second = first.sourceModel()
        if second:
            second.rowsInserted.connect(self.onAdded)
            second.rowsRemoved.connect(self.onRemoved)
            second.modelReset.connect(self.invalidate)
        first.filterTextChanged.connect(self.resyncRowFilter)
        self.setSourceModel(first)

    @Slot()
    def onAdded(self, parent, first, last):
        count = self.sourceModel().rowCount()
        added = max(1, last - first + 1)
        if self._max > 0 and self._max <= count:
            self.setMax(self._max + added)

    @Slot()
    def onRemoved(self):
        count = self.sourceModel().rowCount()
        if self._max > 0 and self._max <= count:
            self.invalidate()

    def getInitial(self):
        return self._initial

    def setInitial(self, v):
        if self._limit >= 0 and v > self._limit:
            v = self._limit
        if self._initial != v:
            self._initial = v
            if self._max <= self._initial:
                self.setMax(v)
            if self._step <= 0:
                self.setStep(v)
            self.initialDisplayItemsChanged.emit()
    initialDisplayItems = Property(int, getInitial, setInitial, notify=initialDisplayItemsChanged)

    def getMax(self):
        return self._max

    def setMax(self, v):
        if self._limit >= 0 and v > self._limit:
            v = self._limit
            if self._max == v:
                self.resyncRowFilter()
                return
        if self._max != v:
            model = self.sourceModel()
            mc = model.rowCount() if model else 0
            old = self._displayCount(mc)
            self._max = v
            if self._initial > self._max:
                self.setInitial(v)
            if self._step <= 0:
                self.setStep(v)
            self.maxDisplayItemsChanged.emit()
            if model and self._displayCount(mc) != old:
                print("  [C++] invalidate() max=%d" % self._max, flush=True)
                self.invalidate()
    maxDisplayItems = Property(int, getMax, setMax, notify=maxDisplayItemsChanged)

    def getStep(self):
        return self._step

    def setStep(self, v):
        if v > 0 and self._step != v:
            self._step = v
            self.displayItemsStepChanged.emit()
    displayItemsStep = Property(int, getStep, setStep, notify=displayItemsStepChanged)

    def getLimit(self):
        return self._limit

    def setLimit(self, v):
        if self._limit == v:
            return
        self._limit = v
        self.displayLimitChanged.emit()
        if self._limit >= 0 and (self._max < 0 or self._max > self._limit):
            self.setMax(self._limit)
    displayLimit = Property(int, getLimit, setLimit, notify=displayLimitChanged)

    @Slot()
    def reapplyRowFilter(self):
        print("  [C++] reapplyRowFilter()", flush=True)
        self.beginFilterChange()
        self.endFilterChange(QSortFilterProxyModel.Direction.Rows)

    @Slot()
    def resyncRowFilter(self):
        if self._limit < 0:
            return
        model = self.sourceModel()
        if not model:
            return
        if self.rowCount() != self._displayCount(model.rowCount()):
            self.reapplyRowFilter()

    @Slot()
    def displayMore(self):
        if self._limit >= 0 and self._max >= self._limit:
            return
        old = self.rowCount()
        model = self.sourceModel()
        new = self._displayCount(model.rowCount() if model else 0, self._max + self._step)
        if new != old:
            self.setMax(new)


qmlRegisterType(LimitProxy, "Probe", 1, 0, "LimitProxyType")

app = QGuiApplication(sys.argv)
src = SourceList()
sfp = SortFilterProxy()
sfp.setSourceModel(src)

view = QQuickView()
view.setResizeMode(QQuickView.SizeRootObjectToView)
view.resize(400, 660)
view.setSource(QUrl.fromLocalFile(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "capped.qml")))
root = view.rootObject()
if root is None:
    print("QML ERRORS:", [e.toString() for e in view.errors()])
    sys.exit(1)
proxy = root.property("model")
proxy.setSourceModels(sfp)
view.show()


def settle(ms=800):
    done = [False]
    t = QTimer()
    t.setSingleShot(True)
    t.timeout.connect(lambda: done.__setitem__(0, True))
    t.start(ms)
    while not done[0]:
        app.processEvents()


def report(tag):
    print("%-34s count=%-5d contentY=%-9.1f haveMore=%s" % (
        tag, root.property("count"), root.property("contentY"), proxy.getHaveMore()), flush=True)


settle(600)
report("1. initial load (source 500)")

root.setProperty("scrollTo", max(0.0, root.property("contentHeight") - root.property("height")))
settle(1200)
report("2. scrolled to bottom")

src.prepend("call-NEW-1")
settle(500)
report("3. after a call arrives")
src.prepend("call-NEW-2")
settle(500)
report("4. after a second call")

print("")
print("-- search narrowing below the cap: matches 100 of 502 --")
sfp.setFilterText("call-01")
settle(600)
report("5. searched, 100 matches")

print("")
print("-- search matching more than the cap --")
sfp.setFilterText("call-0")
settle(600)
report("6. searched, 500 matches")
sfp.setFilterText("")
settle(600)
report("7. search cleared")

print("")
print("-- binding order --")
p2 = LimitProxy()
p2.setSourceModels(sfp)
p2.setLimit(100)
print("   max=-1 then displayLimit=100        -> count=%d (want 100)" % p2.rowCount())
p3 = LimitProxy()
p3.setSourceModels(sfp)
p3.setInitial(400)
p3.setLimit(100)
print("   initial=400 then displayLimit=100   -> count=%d initial=%d (want 100/100)"
      % (p3.rowCount(), p3.getInitial()))
p4 = LimitProxy()
p4.setSourceModels(sfp)
p4.setLimit(100)
p4.setInitial(400)
print("   displayLimit=100 then initial=400   -> count=%d initial=%d (want 100/100)"
      % (p4.rowCount(), p4.getInitial()))
p5 = LimitProxy()
p5.setSourceModels(sfp)
p5.setInitial(30)
p5.displayMore()
print("   no displayLimit, displayMore()      -> count=%d (want 60, paging still works)"
      % p5.rowCount())
