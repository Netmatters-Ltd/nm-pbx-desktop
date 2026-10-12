# Unit tests

Upstream linphone-desktop has no tests of its own. Ours live in `Linphone/tests` and use Qt Test,
which the app already links against. They compile the real source files, so they test exactly what
ships, not a copy.

They only cover code that needs nothing beyond QtCore. Anything that touches the SDK, `App` or a
`*Core` class (for example `MagicSearchProxy` itself) would pull most of the app into the test, so
for those we test the generic piece underneath and stand in for the rest with a small model.

## Building

The tests are built with everything else by `cmake --build`. To turn them off, configure with
`-DENABLE_APP_TESTS=OFF`. They build into `bin\RelWithDebInfo` beside the app, but they have no
install rule, so they never reach `OUTPUT` or the installer. Install copies named targets, not the
whole folder.

Adding or changing a `CMakeLists.txt` needs a configure. The Visual Studio generator re-runs it for
you at the start of the next build, so the usual commands in [setup.md](setup.md) are enough.

To build just the tests, from the build directory:

```cmd
cmake --build . --config RelWithDebInfo --target LimitProxyTest
```

A compile error in a test stops the whole build, the same as an error anywhere else in the app.

## Running

From the build directory, in the same command prompt you build in:

```cmd
ctest --test-dir Linphone -C RelWithDebInfo --output-on-failure
```

`ctest` puts Qt's `bin` directory on the path for the tests. To run one directly, which gives Qt
Test's full output and lets you pick single test functions, `qtenv2.bat` must have been run in that
prompt:

```cmd
bin\RelWithDebInfo\LimitProxyTest.exe
bin\RelWithDebInfo\LimitProxyTest.exe narrowingTheSearchShowsMatchesFromLowerDown
```

## What is covered

### `tst_LimitProxy.cpp`

`LimitProxy` caps how many rows a list shows, on top of a `SortFilterProxy` that does the search.
Every test checks the same rule: filter first, then cap. After any change, the list should show
exactly the first `maxDisplayItems` rows of the filtered list, in order.

That's harder to keep than it sounds. `LimitProxy::filterAcceptsRow()` decides by row number, and
`QSortFilterProxyModel` only re-tests rows that are inserted, never rows that just change position.
When a search removes rows above a contact, the contact moves up into range but is never re-tested,
so it stays hidden. When deleting characters brings rows back, the rows they push past the cap stay
on show. `LimitProxy::onSourceFilterChanged()` deals with this by re-testing every row whenever the
search text or filter type changes. Before that, the first four tests and the three main contacts
list tests failed (task 5243983).

| Test | Checks |
| --- | --- |
| `narrowingTheSearchShowsMatchesFromLowerDown` | A contact that sorts last of 300 appears once the search narrows to it. |
| `wideningTheSearchKeepsTheCap` | Deleting characters doesn't leave more rows on show than the cap. |
| `changingTheFilterTypeKeepsTheListInStep` | The same for the filter type, for example favourites only. |
| `randomSearches` | 300 random lists, each searched 12 times, with and without the call history ceiling. A single change often removes some rows and inserts others, which is the case a row count check alone misses. |
| `searchingDoesNotResetTheView` | A search only inserts and removes rows. It never emits a layout change or a reset, which would send the list back to the top (see [call-history.md](call-history.md)). |
| `displayMorePagesIn` | Paging still adds rows. |
| `arrivingRowIsShownOnAPagingList` | A contact added while the list is open appears without pushing another off. |
| `ceilingHoldsWhenRowsArrive` | Call history stays at its ceiling as calls arrive. |
| `mainListSearchAfterPagingShowsAFullPageOfMatches` | Main contacts list: after scrolling has raised the cap, a search fills it. |
| `mainListPagingDuringASearchDoesNotShrinkTheList` | Main contacts list: a search with few matches makes the grid ask for another page at once. That mustn't leave the cap lowered once the search is cleared. |
| `mainListHaveMoreIsFalseOnceEveryMatchIsShown` | `haveMore`, which decides whether the grid asks for another page, is right after a search. |
| `addressBookArrivingInOneBatchStopsAtTheLimit` | The whole address book arriving at once stops at the 100 contact limit. Without the limit, this is what drew all 3,000 contacts (see [contacts-list.md](contacts-list.md)). |
| `limitIsNotReachedWhilePaging` | `limitReached`, which shows the "contacts hidden" note, stays false while the list is still paging in. |
| `searchingBelowTheLimitClearsLimitReached` | A search still finds a contact past the limit, and the note goes once every match is on show. |
| `unlimitedListShowsEverything` | A list with no limit, as extensions use, shows every row and never the note. |

`displayMorePagesIn`, `arrivingRowIsShownOnAPagingList` and `ceilingHoldsWhenRowsArrive` describe
behaviour that already works and must keep working.

The tests marked "main contacts list" use the cap and page size `AllContactGridView.qml` uses for a
grid about 700 pixels tall. They test the model under that list, not its QML. A QML test would need
`MagicSearchProxy` and so the SDK, and the faults are all in the model.

#### Why the test lists act like a view

Qt builds a proxy's row mapping only when something asks for rows. In the app the view asks at
once, and again after every layout change or reset. A test that doesn't ask can apply a filter
change before any mapping exists, so there is nothing to go wrong and the test passes by accident.
`Lists` in the test file asks the same way a view does, so keep that in place.

### `tst_PhoneNumberIndex.cpp`

`PhoneNumberIndex` finds the contact holding a phone number, in place of the SDK's
`Core::findFriendByPhoneNumber`. The SDK puts every number of every contact through the dial plan
on every lookup. With 5,000 contacts, loading the call history held the linphone thread for about
28 seconds (see "Lookup cost" in [caller-identification.md](caller-identification.md)).

The index is templated on the contact, so the tests use a small stand-in address book and a rough
UK dial plan instead of the SDK. The address book counts how often it is read and how many numbers
are normalised, which is how the tests see the cost.

| Test | Checks |
| --- | --- |
| `findsANumberWrittenAnotherWay` | `07...`, `0044...` and `+44...` all find a contact stored as `+44 ...`. |
| `unknownNumberFindsNothing` | A number nobody holds finds nobody. |
| `numbersTheDialPlanRejectsMatchNothing` | A number the dial plan can't read never matches, even another unreadable one. |
| `firstContactHoldingANumberWins` | When two contacts share a number, the one the SDK would find is the one returned. |
| `nothingIsReadUntilTheFirstLookup` | The address book isn't read until something asks. |
| `addressBookIsReadOnceForManyLookups` | The fault at the size it was seen: 5,000 contacts and 500 lookups read the address book once, and normalise each stored number once. |
| `invalidatePicksUpChangesOnTheNextLookup` | After `invalidate()`, added and removed contacts are reflected. Without it, the old answers stand. |
| `dialPlanChangeRebuilds` | A change of dial plan rebuilds on its own, and only once. |
| `agreesWithAScanOfEveryContact` | 50 random address books with shared numbers, mixed formats, short numbers and unreadable numbers. Every lookup gives exactly what a scan of every contact gives. |

Not covered here: the negative cache in `FriendsManager`, and the signals that clear it and the
index. Both need the SDK. Check them by hand as described in
[caller-identification.md](caller-identification.md). Open the Calls page so an unknown number is
looked up and remembered as a miss. Then add a contact holding that number in a different form
(`+44...` for a `07...` caller), either in the app or on the CardDAV server followed by a sync.
Leave the Calls page and come back: the row should now show the contact's name. The page is
rebuilt each time it opens, so this tests the lookup, not a live refresh. An open row only
refreshes by itself when the new contact holds the caller's SIP address, not a phone number.

## Relationship to `limit-proxy-probe`

[limit-proxy-probe](limit-proxy-probe/README.md) holds PySide6 copies of `LimitProxy` driving real
QML views. Those are for behaviour that lives in Qt Quick, such as where a `ListView` scrolls to,
which a QtCore test can't see. They're copies, so they can drift from the C++. Prefer a test here
for anything the model alone can show.

## Adding a test

- Add a new `tst_<Class>.cpp` beside the existing one, with its own `qt6_add_executable`,
  `add_test` and `set_tests_properties` block in `Linphone/tests/CMakeLists.txt`. One executable
  per class keeps a crash in one from hiding the others.
- List the real source files and their headers rather than linking the app.
- Use a fixed seed for anything random, so a failure happens the same way every time.
