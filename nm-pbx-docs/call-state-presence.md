# Call state presence ("on a call")

Implementation brief for showing when a colleague's extension is on a call, in the Windows desktop
app. This is the app half of a wider change planned in the portal repository (`nm-pbx-portal`,
OpenSpec change `extension-call-state-presence`, task group 7). Everything needed to do the app work
is summarised here, so you should not need that repository, but it is the source of truth if
anything here disagrees with it.

Written by AI 24 September 2026, based on a larger plan for implementing the call state handling
on the server. Line numbers were checked against this repository on that date.

## Where things stand

The server side is assumed to be working before this work starts:

- A call-state bridge daemon runs on each customer's FreePBX server. It watches Asterisk device
  state over AMI and, while an extension is on a call, publishes a presence document for that
  extension into the local Flexisip presence server. It removes it when the call ends.
- Flexisip is configured with `[presence-server] bypass-condition=NMPBX`, so any subscriber whose
  `User-Agent` contains `NMPBX` receives full presence bodies, including activities. This app's
  User-Agent already starts with `NMPBX-Desktop/`, so no change is needed here.
- The test server is `nm-test.nmpbx.uk` (customer 1, "Netmatters Test"). It is the only server to
  test against.

What is missing is the app. We have already confirmed from the Flexisip presence log that this app's
resource-list subscription **receives** the on-call activity today. It shows nothing because
`ToolModel::corePresenceModelToAppPresence()` has no case for it and falls through to `Undefined`.
The work below is the whole of what stands between the server working and the user seeing it.

## What the app receives

The bridge publishes this for an extension on a call:

```xml
<presence xmlns="urn:ietf:params:xml:ns:pidf"
          xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
          xmlns:rpid="urn:ietf:params:xml:ns:pidf:rpid"
          entity="sip:150@nm-test.nmpbx.uk">
  <tuple id="nmpbx-callstate">
    <status><basic>open</basic></status>
  </tuple>
  <dm:person id="nmpbx-callstate-person">
    <rpid:activities><rpid:on-the-phone/></rpid:activities>
  </dm:person>
</presence>
```

Flexisip **merges** this with whatever the extension's own app has published, rather than replacing
it. This is a real capture from the test server, for a user who had set Busy with a custom message
and was on a call:

```xml
<presence entity="sip:150@nm-test.nmpbx.uk">
  <tuple id="k9i37v">
    <status><basic>open</basic></status>
    <contact priority="0.8">sip:150@nm-test.nmpbx.uk</contact>
    <note xml:lang="en">Example custom status</note>
    <timestamp>2026-09-10T20:20:15Z</timestamp>
  </tuple>
  <tuple id="nmpbx-callstate">
    <status><basic>open</basic></status>
  </tuple>
  <p1:person id="">
    <p2:activities><p2:busy/></p2:activities>
    <p2:activities><p2:on-the-phone/></p2:activities>
    <p1:timestamp>2026-09-10T20:20:15Z</p1:timestamp>
  </p1:person>
</presence>
```

What that means for the SDK model the app sees (`linphone::PresenceModel`):

- `getBasicStatus()` is `Open` if any tuple is open. The bridge's tuple is always open, so a
  handset-only extension with nothing else published still passes the existing `Open` check.
- Each publication contributes its own `<activities>` element. The SDK parses all of them, so
  `getNbActivities()` is the total and `getNthActivity(i)` walks them in document order.
- **Document order is not stable.** In the capture above `busy` is index 0 and `on-the-phone` is
  index 1. For a handset-only extension `on-the-phone` is index 0. `getActivity()` is just
  `getNthActivity(0)`, so it gives the wrong answer in a way that varies by extension and will look
  like a race.
- `on-the-phone` parses to `linphone::PresenceActivity::Type::OnThePhone`. Nothing in this app uses
  that type today, so it is unambiguous.
- `on-the-phone` can briefly appear **twice**, if the bridge lost track of an earlier publication
  and published afresh while the old one waits to expire (up to two minutes). Treat that exactly as
  once.
- The bridge publishes **no note**. The user's custom status message sits on their own tuple and is
  unaffected, so `getNote(lang)` still returns it.
- When the call ends and the bridge withdraws its publication, the app receives a new NOTIFY. For an
  extension with an app of its own, the body goes back to their manual status. For a handset-only
  extension it goes back to a body containing only the note "No presence information available
  yet", which the current code already maps to `Undefined` (no badge).

Do not use `getConsolidatedPresence()` for this. Its `Busy` cannot tell a genuine call apart from a
user who chose Busy, which is the whole distinction this feature exists to make.

## Required behaviour

1. **Any** `OnThePhone` activity, at any index, means the extension is on a call.
2. On a call takes precedence over the manual status (Available, Away, Busy, Do Not Disturb) for
   the badge and the status label.
3. The user's custom status message is still shown alongside the on-call state.
4. On a call is its own visual state, distinguishable from all four existing states. Do not reuse
   Busy.
5. With no `OnThePhone` activity, behaviour is exactly as today.
6. An extension whose presence consists only of the bridge's publication shows as on a call, not as
   offline or nothing.
7. The app **never publishes** the on-call state. It is a fact from the server, not a status the
   user chooses. If the app published it, it would fight the bridge and would only ever report the
   app's own calls.

## Code changes

### 1. Add the enum value

`Linphone/tool/LinphoneEnums.hpp:257`

```cpp
enum class Presence { Undefined, Online, Busy, DoNotDisturb, Offline, Away };
```

Add a value, for example `OnCall`. Append it at the end rather than inserting it, so no existing
integer value moves.

`toString()` and `fromString()` in `Linphone/tool/LinphoneEnums.cpp:295-311` go through
`QMetaEnum`, so they pick up the new value automatically. The tasks file in the portal repo says
they need new cases; they do not. It is `Q_ENUM_NS`, so QML sees it as
`LinphoneEnums.Presence.OnCall`.

Be aware that `toString()` is how the user's explicitly chosen status is persisted to config
(`explicit_presence`, in `AccountModel::setPresence()`). That is one more reason the new value must
never reach the publish path.

### 2. Scan every activity in the inbound mapper

`Linphone/model/tool/ToolModel.cpp:906` `corePresenceModelToAppPresence()`

Today it reads the single `getActivity()` inside the `Open` check. Inside that same check, before the
existing Busy/Away/PermanentAbsence/Other mapping, loop `0 .. getNbActivities() - 1` over
`getNthActivity(i)` and return the new value if any activity's type is `OnThePhone`. Leave the
existing mapping after it unchanged.

Keep the `Open` guard. The bridge publishes an open tuple precisely so this path is reached for
handset-only extensions.

Checking only index 0 for `OnThePhone` is not enough, because it is not always first. The existing
manual status mapping can keep reading `getActivity()`: it only runs when no `OnThePhone` is
present, and then there is only the user's own activity to find.

The comment block above the function (`ToolModel.cpp:898-905`) explains why every published status
uses basic status `Open`. Leave that behaviour alone; the bridge is built around it. Update the
comment's list of statuses to mention the new inbound-only state.

### 3. Keep it out of the publish path and the picker

- `ToolModel::appPresenceToCorePresenceModel()` (`ToolModel.cpp:932`) builds what the app
  publishes. Do not add a case that publishes `OnThePhone`. The `switch` has no `default`, so the
  compiler will warn about the new value. Handle it the same way as `Undefined` (log a warning and
  return `nullptr`), which `AccountModel::setPresence()` already treats as "do not publish".
- `AccountModel::setPresence()` (`Linphone/model/account/AccountModel.cpp`) already refuses
  `Undefined`. Refuse the new value there too, before anything is written to config, so it can
  never be persisted as `explicit_presence` or published.
- `Linphone/view/Control/Display/Contact/Presence.qml:16-19` lists the four user-selectable
  statuses as `PresenceStatusItem` entries. Do not add the new value here.

### 4. Badge, colour and label

The display surfaces do not switch on presence themselves. They read `presenceIcon`,
`presenceColor`, `presenceStatus` and `presenceNote` from `FriendCore`
(`Linphone/core/friend/FriendCore.hpp:66-70`), which delegate to three switches in
`Linphone/tool/Utils.cpp`:

| Function | Line | Add |
|---|---|---|
| `Utils::getPresenceColor()` | 1769 | A colour token from `Linphone/view/Style/DefaultStyle.qml` |
| `Utils::getPresenceIcon()` | 1795 | An icon registered in `Linphone/view/Style/AppIcons.qml:149-154` |
| `Utils::getPresenceStatus()` | 1846 | A translatable label, e.g. `tr("contact_presence_status_on_call")` |

For the icon, add an SVG alongside the existing `presence_*.svg` files in `Linphone/data/image/` and
a matching property in `AppIcons.qml`. For the label, add the English translation (British English;
"On a call" is the suggested wording). Check `nm-pbx-docs/brand.md` before choosing a colour.

The colour and label are a design decision that has not been made yet. The only fixed constraint is
that the result is distinguishable from Available (green), Away (orange), Busy (`info_500_main`),
Do Not Disturb (`danger_500_main`) and Offline (`main2_400`). Confirm the choice with us before
finalising it.

Where it appears, via `FriendCore`, with no further change expected:

- `Linphone/view/Control/Display/Contact/Avatar.qml` and `ContactGridItem.qml` for the badge.
- `Linphone/view/Control/Display/Contact/ContactStatusPopup.qml` and `PresenceNoteLayout.qml` for
  the label and the custom message.

The custom message path is `FriendModel::getPresenceNote()` (`Linphone/model/friend/FriendModel.cpp:326`).
It returns an empty note whenever the mapped presence is `Undefined`. Since the new value is not
`Undefined`, the user's message will continue to show during a call without any change. Check it in
testing rather than assuming.

The path from the SDK to the GUI is `FriendModel::onPresenceReceived()` (`FriendModel.cpp:317`) to
`FriendCore::setPresence()` (`FriendCore.cpp:766`). No change should be needed there.

### Out of scope

- **Your own avatar.** The user's own presence badge comes from `AccountCore::mPresence`, which is
  local state, not a subscription. It will not show the bridge's on-call state, and that is fine for
  this change.
- **The existing automatic Busy.** `CoreModel.cpp:587-595` already publishes Busy for the user's own
  account while they have a call in progress, unless they have chosen a status explicitly. It stays
  as it is. The merged document then carries both `busy` and `on-the-phone`, and the precedence rule
  shows on a call. Whether to remove that automatic Busy, now that Busy could mean "chose Busy" only,
  is a separate decision for us to make later. Do not change it as part of this work.
- **Recovery after a presence server restart.** If `flexisip-presence.service` is restarted, the
  app's next publish refresh is rejected with `400 Unknown ETag`, and it keeps its old status until
  the app itself is restarted. That is a known defect, not part of this change. It does mean that if
  the server was restarted during testing, restart the app before trusting what you see.

## Tests

This repository has no unit test target at present (`Qt Test` is listed in `QT_PACKAGES` in
`Linphone/CMakeLists.txt`, but nothing uses it). Two options, in order of preference:

1. Keep the decision logic in a small pure function that takes the basic status and the list of
   activity types (not SDK objects), with `corePresenceModelToAppPresence()` as a thin adaptor over
   it. Then add a minimal Qt Test target that exercises that function without needing a core.
2. If adding a test target is not approved, document the cases below as a manual test script and run
   them against the test server.

Adding a test target changes the build, so ask before doing it. You must not build the project
yourself; ask the user to build it (see `CLAUDE.md`).

The cases to cover, whichever route is taken:

| Activities (in order) | Basic status | Expected |
|---|---|---|
| `on-the-phone` | open | On a call |
| `busy`, `on-the-phone` | open | On a call |
| `on-the-phone`, `busy` | open | On a call |
| `on-the-phone`, `on-the-phone` | open | On a call |
| `away`, `on-the-phone` | open | On a call |
| `busy` | open | Busy (unchanged) |
| `other` (dnd) | open | Do Not Disturb (unchanged) |
| none | open | Available (unchanged) |
| none, no presence model | n/a | Undefined (unchanged) |

## Verifying against the test server

You need someone with SSH access to `nm-test.nmpbx.uk`, and at least two extensions on customer 1:
one signed in on this app (the viewer) and one to observe. Extension 9 and extension 150 were used
during the earlier investigation. Ask us which extension on that server is handset-only.

### Without making a real call

`/usr/local/bin/presence-probe.py` on the server publishes exactly what the bridge publishes, so it
puts an extension into the on-call state for as long as the publication lasts. It is committed in
the portal repository under `servers/free-pbx-server/usr/local/bin/`. Check it is installed on the
server before relying on it.

```bash
# Put extension 150 on a call for five minutes; note the SIP-ETag in the response
./presence-probe.py publish --ext 150 --domain nm-test.nmpbx.uk --state on --expires 300

# Clear it, using the ETag from the previous response
./presence-probe.py publish --ext 150 --domain nm-test.nmpbx.uk --state off --etag <tag>

# See what an app would be given
./presence-probe.py subscribe --ext 150 --domain nm-test.nmpbx.uk \
    --as-ext 9 --user-agent NMPBX-Probe/1.0 --watch 20
```

The server issues a new ETag on every successful publish. Using an old one returns `412`, and the
old publication then lingers until it expires. If the daemon is also running, it may remove or
override a hand-made publication when Asterisk reports the extension idle, so for hand testing pick
an extension that is idle and expect the daemon's own reconcile to win eventually.

### With real calls

Check each of these in a development build, then again in a packaged Windows build:

1. A handset-only extension goes on a call. The viewer sees it as on a call, and it clears when the
   call ends.
2. An extension signed in on the app goes on a call. The viewer sees on a call.
3. A user who has set a manual status (for example Away) and a custom message takes a call. The
   viewer sees on a call **and** the custom message. When the call ends, the viewer sees Away and the
   message again, unchanged.
4. A user changes their manual status during a call. The viewer still sees on a call.
5. A plain ringing extension (not yet answered, not already on another call) is **not** shown as on
   a call. That is the bridge's rule, and it is deliberate.
6. The status picker offers only the four existing statuses.

If nothing appears for an extension, the two usual causes are on the server rather than in the app:

- The viewer's `User-Agent` does not contain `NMPBX` near the start (Flexisip only checks the first
  100 bytes), so it is sent no activities at all. The body then contains only "No presence
  information available yet".
- The URI does not match. Presence is keyed on exact string equality, so the address book entry,
  the registration identity and the bridge's presentity must all be `sip:<ext>@nm-test.nmpbx.uk`.
  They are known to agree on the test server.

### Older builds

As part of testing, run a build from before this change alongside the new one and note what it
shows. For an extension where `on-the-phone` lands at index 0 ahead of the user's own activity, an
old build maps the call to `Undefined`, so the user's manual status badge disappears for the length
of the call. That is a degradation rather than a wrong indicator, but we need to know whether it
happens in practice before rolling out to customers with a mix of versions. Report what you see.

## Mobile apps

Android (`nm-pbx-android`) and iOS (`nm-pbx-ios`) need the same change and follow afterwards. Nothing
in this app depends on them. Use the same precedence rules and the same test cases so the three
behave identically.
