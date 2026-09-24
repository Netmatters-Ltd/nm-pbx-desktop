# LimitProxy probe

Two small harnesses that exercise `Linphone/core/proxy/LimitProxy.cpp` outside the app.

They exist because the behaviour that matters here lives in Qt's QML delegate model, not in our
code, so reasoning about it from the source is unreliable and building the whole app to try one
change is slow. PySide6 is pinned to the same Qt the app links against, so what these scripts show
is what the app does.

| File | What it is |
| --- | --- |
| `probe.py` | Port of `LimitProxy` as the paging lists still use it, with no `displayLimit`. Reproduces the scroll reset described in `nm-pbx-docs/call-history.md` under "Known gaps". |
| `full.qml` | Replica of `CallHistoryListView` with every handler, for reproducing what the user sees. |
| `bare.qml` | A plain `ListView` plus the proxy. Isolates the reset to `QQuickItemView` and rules out the QML handlers. |
| `cap.py` | Port of `LimitProxy` as it stands now, including `displayLimit`. Checks the ceiling holds. |
| `capped.qml` | The capped call history view that `cap.py` drives. |

## Setup

```sh
python -m venv venv
./venv/Scripts/python.exe -m pip install PySide6-Essentials==6.10.1
```

Match the version to the Qt in `nm-pbx-docs/setup.md`. If the app moves to a newer Qt, move this
too, because a large part of what is being tested is Qt's own behaviour.

## Reproducing the scroll reset

```sh
QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe probe.py full.qml
QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe probe.py bare.qml
```

`QT_ASSUME_STDERR_HAS_CONSOLE=1` is what makes `console.log` from the QML reach the terminal on
Windows. Without it you get the C++ side only.

Current code, on either QML file. `contentY` is back at zero after every page:

```
--- page 1: scrolling to bottom, contentY -0.0 -> 3290.0 (count=60) ---
  [QML] atYEnd -> displayMore()  contentY=3290.0
  [C++] LimitProxy::invalidate()  max=96
  [QML] onCurrentItemChanged currentItem=null contentY=3290.0
  [QML] onCountChanged count=96 contentY=0.0  atYBeginning=true
  [QML]   -> positionViewAtBeginning()
after: count=96 contentY=-0.0 atYBeginning=True
```

`contentY` is already 0 by the time `onCountChanged` runs, which is how you know the reset comes
from `QQuickItemView` itself rather than from the QML handlers. `bare.qml` confirms it: no handlers
at all, same reset.

To try the fix, change `self.invalidate()` to `self.invalidateRowsFilter()` in `LimitProxy.setMax`
in `probe.py` and re-run. The scroll position should survive every page:

```
--- page 1: scrolling to bottom, contentY 914.0 -> 3290.0 (count=60) ---
  [C++] LimitProxy::invalidateRowsFilter()  max=96
after: count=96 contentY=3290.0 atYBeginning=False
```

## Checking the display ceiling

```sh
QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen ./venv/Scripts/python.exe cap.py
```

Expected, against a 500 row source with the cap at 100:

```
1. initial load (source 500)       count=100   contentY=-0.0      haveMore=True
2. scrolled to bottom              count=100   contentY=5958.0    haveMore=True
  [C++] reapplyRowFilter()
3. after a call arrives            count=100   contentY=5892.0    haveMore=True
  [C++] reapplyRowFilter()
4. after a second call             count=100   contentY=5826.0    haveMore=True

-- search narrowing below the cap: matches 100 of 502 --
  [C++] reapplyRowFilter()
5. searched, 100 matches           count=100   contentY=-0.0      haveMore=False

-- search matching more than the cap --
  [C++] reapplyRowFilter()
6. searched, 500 matches           count=100   contentY=5930.0    haveMore=True
  [C++] reapplyRowFilter()
7. search cleared                  count=100   contentY=5826.0    haveMore=True

-- binding order --
   max=-1 then displayLimit=100        -> count=100 (want 100)
   initial=400 then displayLimit=100   -> count=100 initial=100 (want 100/100)
   displayLimit=100 then initial=400   -> count=100 initial=100 (want 100/100)
   no displayLimit, displayMore()      -> count=35 (paging still works)
```

Steps 3 and 4 are the ones worth watching. Without `resyncRowFilter()` the count creeps to 101 then
102, because `filterAcceptsRow()` is index based and `QSortFilterProxyModel` only tests the newly
inserted row. Step 5 is the other one: without the resync on a source filter change, the search
returns only the matches that happened to sit inside the cap before you typed.

The small `contentY` drift in steps 3 and 4 is one row per arriving call while pinned at the bottom.
That is the view re-anchoring, not the reset this directory exists to catch.
