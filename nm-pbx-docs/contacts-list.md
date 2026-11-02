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
