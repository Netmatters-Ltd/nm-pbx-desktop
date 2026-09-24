# Call history

How the call list on the Calls page is loaded, filtered and bounded, and what is still wrong with it.

## The three layers

The history passes through three models, and almost everything worth knowing follows from the order
they sit in.

```
CallHistoryList            every call log loaded, bounded by maxCallHistory
  -> SortFilterList        search text filter, sorted newest first
    -> CallHistoryProxy    display cap only
```

`CallHistoryList` (`Linphone/core/call-history/CallHistoryList.cpp`) reads the database on the
linphone thread and builds a `CallHistoryCore` per row. `SortFilterList`
(`CallHistoryProxy.cpp:62-80`) applies the search box against the display name and remote address,
and sorts by date descending. `CallHistoryProxy` derives from `LimitProxy` and does nothing but
limit how many of those rows are handed to the view.

The consequence that matters: **the search filter sits below the display cap**. Search runs across
everything loaded and the cap only limits how many matches are rendered. Capping the list does not
narrow what a user can find. Because the sort sits below the cap too, the cap always takes the most
recent calls rather than an arbitrary slice.

## The two numbers

There are two limits and they do different jobs. Reaching for the wrong one is the easy mistake.

**`maxCallHistory`, default 500.** How much history exists in the app at all. `SettingsModel`
(`SettingsModel.cpp:1082-1107`, bounds at `SettingsModel.hpp:219-221`) pushes this into the SDK's
`setMaxCallLogs`, which puts it straight into the SQL `LIMIT`, so it bounds the database read as
well as the objects built from it. It is exposed in Call settings and can be provisioned through
`[misc] history_max_size`. Raise it if a user cannot *find* an old call by searching. Lower it if
loading the history is slow.

**`displayLimit`, 100.** How many rows the list shows. A constant on the view
(`Linphone/view/Control/Display/Call/CallHistoryListView.qml`), not a user setting. It is a
rendering bound, nothing more. Raise it if users complain about scrolling; it has no effect on
search.

Note what the display cap does *not* do: every `CallHistoryCore` for the whole loaded history is
still built on the linphone thread on every rebuild, each with its own friend lookup and its own
`CoreModel` connections. If the complaint is "opening the Calls page is slow", the cap addresses the
delegate half of that only. The other half is `maxCallHistory`.

## The display ceiling

`LimitProxy` was written to page: show a screenful, then add more as the user scrolls. That paging
is what causes the scroll reset described under Known gaps, so the call history does not page. It
takes a fixed slice instead.

`LimitProxy::displayLimit` (`Linphone/core/proxy/LimitProxy.cpp`) is an absolute ceiling on the rows
a proxy will show, whatever paging or an arriving row asks for. It defaults to -1, meaning no
ceiling, so the other seventeen `LimitProxy` subclasses behave exactly as before. Only the call
history sets it.

Three details are load bearing:

**The clamp lives in `setMaxDisplayItems()`.** Every route that raises the cap goes through that one
function: `displayMore()`, `onAdded()` when a row arrives, `setInitialDisplayItems()`, and QML
assignment. Clamping once there covers all of them. In particular `onAdded()` is not virtual and is
connected by member pointer, so a subclass cannot intercept it; without the clamp, every incoming
call would raise the cap by one and reset the scroll.

**`reapplyRowFilter()` is not `invalidate()`.** `filterAcceptsRow()` is index based
(`sourceRow < mMaxDisplayItems`), so rows move into and out of range without
`QSortFilterProxyModel` ever re-testing them, and the cap has to be re-applied by hand. Doing that
with `invalidate()` would reintroduce the scroll reset, so the helper uses
`beginFilterChange()`/`endFilterChange(Direction::Rows)` instead, which emits granular row signals.
Same version guard as `SortFilterProxy::invalidateFilter()`.

**Two things trigger a re-apply**, and `resyncRowFilter()` runs the re-filter only when the rows on
show actually disagree with the cap:

- A call log is prepended. `QSortFilterProxyModel` tests only the newly inserted row and renumbers
  the rest, so the row pushed past the ceiling stays mapped and the count creeps to 101, 102, and so
  on.
- The search text changes. `onAdded`/`onRemoved` are wired to the list *beneath* the search filter,
  not to the filter itself, so nothing else notices when the filter reshuffles rows underneath the
  cap. Without this, searching returns only the matches that happened to sit inside the cap before
  the user typed.

When there are more matches than the cap allows, the list shows a footer saying so. It is gated on
`showLimitNotice`, which follows the search bar, so it appears on the main list and not on the
right-hand detail pane where there is no search to offer.

## The per-address list

The Calls page has two of these lists (`Linphone/view/Page/Main/Call/CallPage.qml`, around lines 258
and 576). The main one on the left is the whole history. The one in the right-hand detail pane sets
`peerAddress` instead of a search term, which makes `CallHistoryList` fetch through the SDK's
`getCallLogsForAddress` so the database does the filtering, rather than loading everything and
throwing most of it away.

The detail pane inherits the same 100 row cap. For a correspondent with more than 100 calls the
older ones are not reachable there, and there is no search in that pane to narrow with. That is a
deliberate trade: it is a detail panel, not an archive.

## Testing with a realistic history

A fresh profile carries a couple of hundred rows, which is not enough to show any of this.
`nm-pbx-docs/call-history-load-test.py` pads the local database out to customer scale and takes it
back out again afterwards. With the app closed:

```sh
python nm-pbx-docs/call-history-load-test.py generate    # 12000 rows
python nm-pbx-docs/call-history-load-test.py remove
```

Every row it writes is tagged, so `remove` deletes exactly what `generate` added and leaves real
call history alone.

`nm-pbx-docs/limit-proxy-probe/` exercises `LimitProxy` outside the app, which is much faster than a
build when the question is about model behaviour rather than about the UI.

## Known gaps

**Paging any `LimitProxy` list resets the scroll to the top.** This is upstream, not ours, and it is
why the call history takes a fixed slice instead of paging.

`LimitProxy::displayMore()` raises the cap through `setMaxDisplayItems()`, which ends in
`invalidate()` (`LimitProxy.cpp:122`, and again in `onRemoved()` at `:184`).
`QSortFilterProxyModel::invalidate()` emits `layoutChanged` with no hint. Qt's QML delegate model
cannot tell what moved, so it treats that as a full model reset, destroys every delegate, and
`QQuickItemView` puts `contentY` back to 0. The view's own `onCountChanged` then sees `atYBeginning`
as true and calls `positionViewAtBeginning()`, which nails it there. `contentY` is already 0 before
any QML handler runs, which is how you know the reset comes from `QQuickItemView` rather than from
our QML.

Still affected, all of them paging on scroll:

- `Linphone/view/Control/Display/Contact/AllContactListView.qml:219`
- `Linphone/view/Control/Display/Contact/AllContactGridView.qml:71`
- `Linphone/view/Control/Display/Meeting/MeetingListView.qml:73`
- `Linphone/view/Control/Display/Chat/ChatMessagesListView.qml:73`, which pages at the *top* when
  you scroll back through older messages, so the reset is more disruptive there than anywhere else

The fix is to replace both `invalidate()` calls with `reapplyRowFilter()`, which already exists in
the same file with a working precedent. Two things to confirm while doing it. `invalidate()` also
re-runs the sort, so check no `LimitProxy` subclass depends on that; at the time of writing
`LimitProxy` never calls `sort()` and has no `filterAcceptsColumn()` override, so for this class the
two differ only in the signal emitted, which makes the change lower risk than it first looks. And
`onRemoved()` is load bearing rather than incidental: removing a row below the cap shrinks the
mapping while the source still has more rows, so without a re-apply the visible count would ratchet
down permanently.

Prove it with `nm-pbx-docs/limit-proxy-probe/`, which reproduces the reset and shows the fix holding
the scroll position. Its README has the expected before and after output. Then check by hand that
each of the four views above still pages correctly, since the granular row signals reach the view
differently from a reset.

**Index-based row filtering can silently drop rows.** The same root cause as above, in a different
guise. Because `LimitProxy::filterAcceptsRow()` tests the source row *index*, any list whose source
filters underneath it can end up showing an arbitrary subset, since `QSortFilterProxyModel` never
re-tests a row it has already rejected. The call history handles this through `resyncRowFilter()`.
The other views have not been checked for the same shape, and should be when the fix above is done.
