# Contacts list

How the contact and extension lists are limited, and why. Task 5243983.

## The limits

Each view limits contacts and suggestions separately. The limits are set in `ContactsCore.hpp` and
exposed on `ContactsCpp`. Extensions are never limited. They are contacts behind the scenes, but
there are never many and they must always all be shown.

| Limit | Value | Applies to |
| --- | --- | --- |
| `contactDisplayLimit` | 100 | Contacts on the Contacts tab and in the top search bar. Favourites too, separately. |
| `callFormContactDisplayLimit` | 50 | Contacts in the new call, transfer and second call lists, which also hold every extension and the suggestions. |
| `suggestionDisplayLimit` | 20 | Suggestions, in every view. |

When a section reaches its limit and more match, the view shows "Additional contacts hidden,
search to find them." at the bottom of the list.

Searching still covers the whole address book. The search filter (`MagicSearchProxy::SortFilterList`)
sits below the limit (`LimitProxy`), so the limit only decides how many of the matches are drawn.

| Where | View | Contacts | Suggestions | Extensions |
| --- | --- | --- | --- | --- |
| Contacts tab | `AllContactGridView` with `extensionFilter: Contacts` | 100 | Hidden | Not shown |
| Extensions tab | `AllContactGridView` with `extensionFilter: Extensions` | Not shown | Hidden | All, no paging |
| New call, transfer, second call | `AllContactListView` via `CreationFormLayout` | 50 | 20 | All, in their own section |
| Search bar at the top of the main window | `AllContactListView` | 100 | 20 | All, in their own section |
| Chat forward, meeting participants (not reachable in NMPBX) | `AllContactListView` | 100 | 20 | All, in their own section |

Limiting suggestions never hides an extension. Suggestions are unsaved addresses, and the SDK drops
any call history address that belongs to a saved contact, extensions included.

`AllContactListView` always gives extensions their own section when it shows both. Mixed into the
contacts section, they would be cut off by the limit along with everything else.

`AllContactGridView` only limits a contacts-only grid. An unfiltered grid would mix extensions in,
so it is left unlimited. Nothing uses one at the moment.

## The first fetch

Straight after provisioning, the address book has to be downloaded before anything can be shown,
and with a large one that takes a while. 1,600 contacts took around 25 seconds, most of it the SDK
saving each contact to the database in turn on the linphone thread. The first search runs before
any of that arrives and comes back empty.

So that this does not look like an empty address book, `ContactsCpp.fetching` is true while it is
under way. The Contacts and Extensions tabs show "Fetching Contacts..." or "Fetching Extensions..."
with a spinner in place of the empty state, and the refresh button is held off.

- `CardDAVSyncAgent` counts a sync as a first fetch when its list holds no contacts as it starts.
  The syncs from provisioning, at launch, on a timer and from the refresh button all go through it.
  An address book added by hand in the settings syncs its own way and is not counted.
- `CoreModel` keeps a count of first fetches on the linphone thread and signals when it goes
  between none and some.
- `ContactsCore` clears `fetching` only once the search that follows the fetch has landed, so the
  empty state does not flash up in between. If a fetch has not finished after five minutes it
  gives up and shows the list as it is.

An address book that is genuinely empty on the server shows "Fetching" briefly on every sync,
before settling on the empty state.

## Why there is a limit

Neither layout recycles rows. The grid is a `Flow` with a `Repeater`, and the list is a `ListView`
sized to its content inside a `Flickable`. Every row on show is a live item with an avatar, and each
avatar makes its own lookups on the linphone thread. With 3,000 contacts the app took minutes to
switch tabs.

The lists did page, about 20 rows at a time, but `LimitProxy::onAdded()` grows the cap by however
many rows arrive so that a newly saved contact appears. The address book arrives in one batch, so
the cap jumped to the size of the whole address book. The limit is a ceiling (`displayLimit`) that
holds against that.

## Suggestions

Suggestions are search results that are neither saved contacts nor from LDAP. With every source
searched, the SDK can suggest the typed text as an address on the account's domain, addresses from
call history, chat room participants and meeting participants. In NMPBX that is mostly the typed
number and call history.

The Contacts and Extensions tabs never show them (`hideSuggestions`). In the combined lists they come
from a search that each `AllContactListView` runs for itself, and only once something is typed.

An empty search text means no search. `MagicSearchProxy::forceUpdate()` does nothing while it is
empty, because the SDK treats an empty filter as "everything". A view that should list everything
with nothing typed (`searchOnEmpty`) searches for `"*"` instead. Before this, each `ContactListView`
section re-ran its parent's search on every CardDAV sync, regardless of what was typed. That filled
an open new call or transfer list with the whole call history, and cost the top search bar four
full searches per sync. `AllContactListView` now refreshes its search once per sync, and only when
it has one.

## `limitReached`

`LimitProxy::limitReached` is true once the list is at its ceiling and more rows match. It differs
from `haveMore`, which is also true while a list is still paging in. The views use it to:

- show the note,
- stop asking for another page at the end of the list,
- stop holding suggestions back, which they do while contacts are still paging in.

## Editing and deleting

Only contacts held locally in the app can be edited or deleted. Anything from CardDAV or LDAP is
read-only, so no change made in the app can flow back to the CardDAV server. The rule is
`FriendCore::readOnly`, which takes CardDAV from the list a contact actually belongs to rather than
from the search that found it. The Contacts page shows Edit and Delete only when a contact is saved
and not read-only.

Contacts cannot be created in the app. "Add to contacts" is gone from the call history menu, and the
other ways in (the empty state button and the chat screens) are not reachable. This matters because
a new contact goes to the CardDAV list named by `friend_list_to_store_newly_created_contacts` when
that is set, and would be pushed to the server. Favourites and sharing are hidden too.
