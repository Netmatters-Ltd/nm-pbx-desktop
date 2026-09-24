#!/usr/bin/env python3
"""Reproduce the LimitProxy scroll reset outside the app.

Background in nm-pbx-docs/call-history.md, under "Known gaps".

LimitProxy pages more rows in by raising its cap, which ends in QSortFilterProxyModel::invalidate().
That emits layoutChanged with no hint, the QML delegate model treats it as a full reset, and
QQuickItemView puts contentY back to 0. The list jumps to the top every time the user reaches the
bottom.

This script is a faithful Python port of Linphone/core/proxy/LimitProxy.cpp over a 500 row model.
It scrolls to the bottom repeatedly and prints contentY before and after each page, so the reset can
be seen without building the app. PySide6 is pinned to the same Qt the app links against, because
the behaviour lives in Qt's delegate model rather than in our code.

Setup:

    python -m venv venv
    ./venv/Scripts/python.exe -m pip install PySide6-Essentials==6.10.1

Usage:

    QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe probe.py full.qml
    QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe probe.py bare.qml

full.qml replicates CallHistoryListView including every handler, so it reproduces what the user
sees. bare.qml is a plain ListView plus the proxy, which isolates the reset to QQuickItemView and
rules out the QML handlers as the cause.

To test a candidate fix, change LimitProxy.setMax below to call self.invalidateRowsFilter() instead
of self.invalidate() and re-run. The scroll position should survive every page.
"""

import sys, os
from PySide6.QtCore import (QAbstractListModel, QModelIndex, Qt, QSortFilterProxyModel,
                            QTimer, Slot, Property, Signal, QUrl, QObject)
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtQml import qmlRegisterType

ROWS = 500

class SourceList(QAbstractListModel):
    def rowCount(self, parent=QModelIndex()):
        return ROWS
    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid(): return None
        if role == Qt.DisplayRole:
            return "call-%03d" % index.row()
        return None

class SortFilterProxy(QSortFilterProxyModel):
    pass

# Faithful re-implementation of Linphone/core/proxy/LimitProxy.cpp
class LimitProxy(QSortFilterProxyModel):
    countChanged = Signal()
    maxDisplayItemsChanged = Signal()
    initialDisplayItemsChanged = Signal()
    displayItemsStepChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._initial = -1
        self._max = -1
        self._step = 5
        self.rowsInserted.connect(self.countChanged)
        self.rowsRemoved.connect(self.countChanged)
        self.modelReset.connect(self.countChanged)

    def filterAcceptsRow(self, sourceRow, sourceParent):
        return self._max == -1 or sourceRow < self._max

    def getCount(self): return self.rowCount()
    count = Property(int, getCount, notify=countChanged)

    def _displayCount(self, listCount, maxCount=None):
        m = self._max if maxCount is None else maxCount
        return min(listCount, m) if m >= 0 else listCount

    def getInitial(self): return self._initial
    def setInitial(self, v):
        if self._initial != v:
            self._initial = v
            if self._max <= self._initial: self.setMax(v)
            if self._step <= 0: self.setStep(v)
            self.initialDisplayItemsChanged.emit()
    initialDisplayItems = Property(int, getInitial, setInitial, notify=initialDisplayItemsChanged)

    def getMax(self): return self._max
    def setMax(self, v):
        if self._max != v:
            model = self.sourceModel()
            modelCount = model.rowCount() if model else 0
            oldCount = self._displayCount(modelCount)
            self._max = v
            if self._initial > self._max: self.setInitial(v)
            if self._step <= 0: self.setStep(v)
            self.maxDisplayItemsChanged.emit()
            if model and self._displayCount(modelCount) != oldCount:
                print("  [C++] LimitProxy::invalidate()  max=%d" % self._max, flush=True)
                self.invalidate()
    maxDisplayItems = Property(int, getMax, setMax, notify=maxDisplayItemsChanged)

    def getStep(self): return self._step
    def setStep(self, v):
        if v > 0 and self._step != v:
            self._step = v
            self.displayItemsStepChanged.emit()
    displayItemsStep = Property(int, getStep, setStep, notify=displayItemsStepChanged)

    @Slot()
    def displayMore(self):
        oldCount = self.rowCount()
        model = self.sourceModel()
        newCount = self._displayCount(model.rowCount() if model else 0, self._max + self._step)
        print("  [C++] displayMore old=%d new=%d" % (oldCount, newCount), flush=True)
        if newCount != oldCount:
            self.setMax(newCount)

qmlRegisterType(LimitProxy, "Probe", 1, 0, "LimitProxyType")

app = QGuiApplication(sys.argv)

src = SourceList()
sfp = SortFilterProxy()
sfp.setSourceModel(src)

view = QQuickView()
view.setResizeMode(QQuickView.SizeRootObjectToView)
view.resize(400, 660)   # ~10 visible rows of 66px

qml = os.path.join(os.path.dirname(os.path.abspath(__file__)), sys.argv[1])
view.setSource(QUrl.fromLocalFile(qml))
root = view.rootObject()
if root is None:
    print("QML ERRORS:", [e.toString() for e in view.errors()])
    sys.exit(1)

proxy = root.property("model")
proxy.setSourceModel(sfp)
root.setProperty("ready", True)
view.show()

def step(n):
    for _ in range(n):
        app.processEvents()

def settle(ms=900):
    loop_end = [False]
    t = QTimer(); t.setSingleShot(True); t.timeout.connect(lambda: loop_end.__setitem__(0, True)); t.start(ms)
    while not loop_end[0]:
        app.processEvents()

settle(600)
print("initial: count=%d contentY=%.1f contentHeight=%.1f height=%.1f currentIndex=%d"
      % (root.property("count"), root.property("contentY"), root.property("contentHeight"),
         root.property("height"), root.property("currentIndex")), flush=True)

for page in range(6):
    ch = root.property("contentHeight"); h = root.property("height")
    target = max(0.0, ch - h)
    print("\n--- page %d: scrolling to bottom, contentY %.1f -> %.1f (count=%d) ---"
          % (page, root.property("contentY"), target, root.property("count")), flush=True)
    root.setProperty("scrollTo", target)   # QML sets contentY without the Behavior
    settle(1200)
    print("after: count=%d contentY=%.1f contentHeight=%.1f currentIndex=%d atYBeginning=%s"
          % (root.property("count"), root.property("contentY"), root.property("contentHeight"),
             root.property("currentIndex"), root.property("atYBeginning")), flush=True)
