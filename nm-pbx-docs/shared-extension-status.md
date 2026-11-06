# Shared extension status

The Windows half of the `shared-extension-status` change, designed in the NMPBX Portal repository
(`openspec/changes/shared-extension-status/`, tasks 9.0 to 9.5). The handover brief we worked from
is `windows-app-handover.md` at the root of this repository. The Portal repository is the source of
truth if anything here disagrees with it.

Implemented November 2026, against SDK 5.4.114.

## The idea in one paragraph

One extension can be signed in on several apps. The phone server now keeps one **shared status** per
extension and publishes it into Flexisip as a tuple of its own. Each app states what its user chose,
and when, in the id of the tuple it publishes, and every app follows the server's tuple. Customers
are switched on one at a time by a Netmatters administrator. While a customer is switched off the
server publishes nothing, and the app behaves as it always has, apart from the automatic Busy/Online
on calls, which is gone for everyone.

## Wire format

| Tuple id | Who | Meaning |
|---|---|---|
| `nmpbx-c.<status>.<chosen>.<device>` | Each app | What the user chose on that device, and when |
| `nmpbx-s.<status>.<chosen>.<source>` | The server | The shared status every app follows |

`<status>` is `available`, `away`, `busy` or `dnd`. `<chosen>` is Unix seconds, and `0` means "no
choice yet", which can never win. The SDK keeps nothing else of a tuple that we could use, which is
why the data travels in the id. The RPID activity and the `<note>` are still published exactly as
before, for older readers and switched-off customers.

The server publishes a shared Available as `closed` with no activity, and Away, Busy and DND as
`open` with the activity. A closed `nmpbx-s.` tuple does not mean offline.

## Where the code is

| File | What it does |
|---|---|
| `Linphone/model/tool/SharedStatus.hpp` | The wire format with no SDK in it: building and parsing tuple ids, cleaning notes and device tokens, reading PIDF tuples, finding the shared status, and the 30 second pending-choice rule. Unit tested. |
| `Linphone/model/account/SharedStatusModel.*` | The single owner of the user's own status on the linphone thread. Publishes, saves, watches the own extension and follows the shared status. |
| `Linphone/model/tool/ToolModel.cpp` | Builds the published model with the `nmpbx-c.` id. Reads a colleague's shared status and message from the SDK's model. |
| `Linphone/model/tool/PresenceMapping.hpp` | The colleague decision, including the shared status. |
| `Linphone/model/account/AccountModel.cpp`, `core/account/AccountCore.cpp` | Thin pass-through to `SharedStatusModel` for the GUI. |

### Why a separate model

`AccountModel` and `AccountCore` are created in several places for the same account (the account
list, notifications, meetings, `Utils::findLocalAccountByAddress`). Anything that subscribes or
keeps a pending choice in them would do it once per copy. `SharedStatusModel` is a singleton,
created in `CoreModel::start()` so it hears every registration.

### Watching the own extension

The own extension is watched with a plain `presence` SUBSCRIBE (`Core::createSubscribe`), not as a
friend in a hidden friend list. The SDK only routes a friend's NOTIFY to it if the friend is in a
friend list added to the core, and every friend list in the core feeds the contacts search,
`FriendsManager` and caller name lookups, so a hidden list would leak the user's own extension into
all of them. The SUBSCRIBE goes to the bare identity URI with `Accept: application/pidf+xml`, and
its NOTIFY bodies are read by `SharedStatus::parsePidfTuples`.

It starts when the account registers, and starts afresh each time the account registers again after
losing it (sleep, network change), so the first NOTIFY after waking brings the shared status as it is
now. `Refreshing` counts as registered, so ordinary re-registration does not restart it. If the
subscription fails or the server ends it, it is retried after 15 seconds, doubling to at most 10
minutes.

## Behaviour

### Publishing

- Every publication carries `nmpbx-c.<status>.<chosen>.<device>`, the note and today's activity.
  The SDK keeps the id on its own refreshes and on the publish it makes when the account registers,
  because it republishes the same model.
- Only what the user chose, or the shared status being followed, is ever published. The automatic
  Busy/Online on call-state changes (`CoreModel::onCallStateChanged`) is removed. Colleagues still
  see the call from the call-state bridge.
- At start-up the saved choice is published once, with its original `chosen`. The old start-up path
  in `AccountList` re-sent it as a new user choice and is removed.
- A new choice is always stamped later than the one it replaces, even within the same second or on a
  slow clock. "Away then Available" in the same second would otherwise tie, and the server would keep
  Away.
- Changing only the message is a new choice of the same status. So is deleting it.
- The message is one line of at most 80 characters (`max_presence_note_size` is capped at 80 whatever
  the config says). Line breaks typed or pasted into the box become spaces.
- The message box (`PresenceSetCustomStatus.qml`) is filled with the current message, which may have
  come from the server, each time it opens. While it is open, a new message from the server replaces
  the text unless the user has started typing.
- With no status chosen, the app publishes Available with `chosen` = 0. An older version's saved
  status, which has no time, is also published with `chosen` = 0 rather than an invented time.

### Following

On each NOTIFY for the own extension:

- With an `nmpbx-s.` tuple: show its status and message as our own, save them with its `chosen`, and
  publish `nmpbx-c.<shared status>.<shared chosen>.<our device>`. The server sees this as not newer,
  so following never makes a new choice.
- Unless the user made a choice on this device in the last 30 seconds that the shared status does not
  show yet (status, message and `chosen` compared). Then the user's choice stays on screen, and a
  timer looks again when the 30 seconds are up.
- With no `nmpbx-s.` tuple: nothing changes. That is the switched-off case.
- The server is the judge of newest. The app follows whatever its own saved `chosen` says.

### Colleagues

- With an `nmpbx-s.` tuple: the status comes from its id and the message from its note (none means
  empty). Other apps' tuples and the pooled activities are not consulted, except that "On a call"
  from the call-state bridge still outranks it (see [call-state-presence.md](call-state-presence.md)).
- The basic status still decides whether to show anything, so a shared Available with nothing
  connected shows no presence, and a shared Away with nothing connected shows Away and the message.
- Without one: exactly as before.

### Removed from the GUI

The "reset to automatic status" row under the status picker. There is no automatic status any more.

### Device token

`[nmpbx_shared_status] device_token` in the app config, a UUID generated once. The config is in the
local, not roaming, profile, but it can still be copied to another PC, and two installations with the
same token hide each other's publications in Flexisip. So the token is tied to
`QSysInfo::machineUniqueId()` (`device_machine`) and replaced if that changes. The SDK's
`+sip.instance` UUID (`[misc] uuid`) is not used: it lives in the same config file, and the SDK's
check for a cloned config only works on mobile.

### Saved choice

In the account's config section (`proxy_N`): `explicit_presence` (as before), `presence_note` (as
before) and `presence_chosen` (new, Unix seconds).

## Known limits

- **One presence for every account.** The SDK publishes a single core presence model to every
  account, so only the default account's status is published and watched. A choice made on another
  account is saved and shown but not published. NMPBX installs have one account, so this should not
  arise.
- **Not checked on a build yet.** Everything in "Spike" below was confirmed from the SDK source, not
  from a running app.

## Spike (task 9.0)

From the SDK 5.4.114 source (`liblinphone/src/presence`, `src/account/account.cpp`,
`coreapi/callbacks.c`):

1. **The id is kept.** `PresenceService::toXml` writes the service's own id, and only invents one
   when the model has no service. `PresenceModel::setBasicStatus` creates one service with a random
   id, which is why every status change before this one went out under a new random id. We now set
   the id on that service.
2. **Refreshes and registration keep it.** `Account::sendPublish` republishes the model it was given,
   on registration and on refresh. `update_presence_model_timestamp_before_publish_expires_refresh`
   (on in our factory config) only touches the timestamp.
3. **Reading other tuples works.** The parser keeps each tuple's id and notes, and
   `getNthService` / `getId` / `getNbNotes` / `getNthNote` are public in the C++ wrapper.
4. **`+sip.instance`** is `[misc] uuid` in the config. It is not used (see Device token).
5. **Publishing more than once.** Each change calls `Core::setPresenceModel` once, which sends one
   PUBLISH per account with publishing enabled. Start-up publishes once, from `SharedStatusModel`.

Still to check on a real build, against test-a:

- The tuple id and note arrive unchanged with the presence probe, including after a refresh, after
  re-registration, and after sleep or a network change.
- What the SDK sends on a clean exit and on closing to the tray (a `PUBLISH` with `Expires: 0`, or
  nothing). Not changed by this work.
- The own-extension SUBSCRIBE is accepted by Flexisip and gets the merged document (check the app log
  for `Shared status: watching own extension` and `Shared status from the server`).
- A call still shows "On a call" to colleagues.

## Logging

Everything logs with the prefix `Shared status:` in the app log (`%LOCALAPPDATA%\NMPBX\logs`):
the device token at start-up, every publish with its full tuple id, every user choice, every shared
status received, every follow, and a kept pending choice. The server side is
`journalctl -u dnd-presence -f` on test-a, where following must log no new choice.

## Tests

`Linphone/tests/tst_SharedStatus.cpp` covers `SharedStatus.hpp`. See [tests.md](tests.md). The
manual test plan is the "Testing" section of the handover.
