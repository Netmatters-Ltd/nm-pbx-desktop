# Bringing upstream 6.2 transfer work into our fork

Task: 5185585 — attended transfer
Branch: `feature/5185585-attended-transfer`
Investigated: 18 August 2026

## 1. Where we sit relative to upstream

| | Commit / ref | Date |
|---|---|---|
| Fork point (merge base) | `7e9e3e6f5` | 9 January 2026 |
| Upstream 6.1.0 tag | `a51de0ff7` | 27 January 2026 |
| Our `main` | `d9e20d4a2` | 14 July 2026 |
| Our branch head | `4c11c510f` | — |
| Upstream 6.2 branch | `upstream/release/6.2` (`ffebc007b`) | 18 August 2026 |

We branched **18 days before the 6.1.0 tag**, so we are actually on a pre-6.1.0 base. Anything
released in 6.1.0, 6.1.1, 6.1.2 and 6.2.0 is missing from our tree.

Volume of upstream work we do not have: **410 commits**, 308 files, roughly 11,000 added and
3,500 removed lines once translation files are excluded.

Note on tags: we have our own local tag named `6.2.0` (our version bump commit), which collides
with upstream's `6.2.0` tag. `git fetch upstream --tags` therefore refuses that one tag. Use
`upstream/release/6.2` as the reference for upstream 6.2, or fetch upstream's tag under an alias:

```
git fetch upstream refs/tags/6.2.0:refs/tags/upstream-6.2.0
```

## 2. Headline finding on transfers

**Upstream 6.2 did not add attended transfer, and it did not meaningfully rework transfers in the
desktop app.** The transfer code in `upstream/release/6.2` is functionally the same as the code we
already have:

- Blind transfer — `CallCore::lTransferCall` → `CallModel::transferTo` → `linphone::Call::transferTo`
  (SIP REFER).
- Attended transfer — `CallCore::lTransferCallToAnother` → `CallModel::transferToAnother` →
  `linphone::Call::transferToAnother` (SIP REFER with Replaces).

Both already exist in our fork, both are wired to the UI, and the `transferState` handling in
`CallsWindow.qml` is byte-for-byte identical between our tree and upstream 6.2. The 6.2 changelog
makes no mention of transfers at all.

Attended transfer itself has been in liblinphone since 2013 (`4edaa02cd1 implement attended
transfert`), so there was nothing for 6.2 to add.

**What 6.2 does bring is (a) three small transfer-panel UI fixes and (b) an SDK bump that carries a
real attended-transfer reliability fix.** The SDK fix is the valuable part.

We have not touched `Linphone/core/call/*` or `Linphone/model/call/*` at all since forking, and our
edits to `CallsWindow.qml` amount to 11 lines. The merge surface for call code is therefore very
small.

## 3. The SDK fix — this is the important one

liblinphone commit `f49e4540a` (21 January 2026), "Add call transfer to the list of pending actions
if it is not possible to execute it immediately".

Before this fix, `CallSession::transfer()` — used by **both** blind and attended transfer — ignored
the return value of the REFER send and unconditionally set the transfer state to `OutgoingInit`. If
the REFER could not be sent right then (for example a re-INVITE was in flight because the call was
being paused or resumed), the transfer silently failed while the UI showed "transfer in progress".

After the fix, a failed REFER is queued as a pending action and retried when the session reaches a
state that allows it. The same commit also adds a null-dialog guard in
`SalCallOp::notifyReferState`, which fixes a crash path.

This is exactly the failure mode you hit in practice with attended transfer, because an attended
transfer is normally performed while one leg is on hold.

### Availability

| SDK version | Contains the fix |
|---|---|
| 5.4.73 (what we ship today) | No |
| 5.4.100 and later | **Yes** — backported to the 5.4 line |
| 5.4.104 (shipped by upstream 6.1.2) | Yes |
| 5.4.124 (5.4 line tip) | Yes |
| 5.5.x (shipped by upstream 6.2.0) | Yes |

**We do not need to jump to SDK 5.5 to get this.** Bumping the `external/linphone-sdk` submodule
within the 5.4 line is enough, and is far less disruptive.

### Why avoiding 5.5 matters

The 5.5 SDK is a structural change, not just a version bump: liblinphone and the other components
stop being git submodules and become directories inlined in the `linphone-sdk` repository. Upstream
6.2 also relies on 5.5-only APIs, notably the Jabra headset callbacks
(`onHeadsetAnswerCallRequested` and friends) that do not exist anywhere in the 5.4 line.

Good news on the rest: `linphone::Conference::SecurityLevel`, `getTransferState`,
`getTransferTargetCall`, `getTransfererCall`, `acceptTransfer` and `hasTransferPending` all exist
already in 5.4.73, so most of upstream's other call changes will compile against a 5.4 SDK.

## 4. Desktop-side changes worth taking

### Directly about transfers

| Commit | What it does | Notes |
|---|---|---|
| `a4b84891a` | Hides the transfer button in the current-calls list for calls that are not yet connected (incoming ringing, outgoing init/progress/ringing, early media) | `CallListView.qml`. Prevents attempting an attended transfer to a leg that cannot accept a REFER-with-Replaces yet. Clean take. |
| `b54359285` | Hides the play/pause button in the current-calls list when that list is the transfer picker | `CallListView.qml`, one line. Clean take. |
| `76f4f5525` | Closes the right panel (including the transfer panel) when a conference starts | `CallsWindow.qml`. Small, self-contained. |
| `6d9b5efcc` | Hides the current call in the transfer target list; reworks `CallProxy` from `LimitProxy` to `SortFilterProxy` and adds the `showCurrentCall` property | **Already in our tree** — it landed 15 December 2025, before our fork point. |
| NewCallForm cleanup | Removes the dead `groupCallVisible` property and switches the transfer panel to the real `startGroupButtonVisible` | Our fork still carries the dead property. Harmless for us today because we set `disable_meetings_feature=1`, but worth tidying while we are in the file. |

### Multi-call handling that attended transfer depends on

| Commit | What it does | Why it matters here |
|---|---|---|
| `58297e22c` | Sets a new call as current when it enters `OutgoingProgress` (LINQT-2660) | This is the consultation-call step of an attended transfer. Without it the "current call" does not follow the call you just dialled. Touches `CallList.cpp`. |
| `37fb24ed6` | `CallModel::setPaused` now either leaves the conference **or** pauses, rather than doing both | Hold is the first step of an attended transfer, so this affects the transfer path directly. |
| `2d3d1bd79` | Syncs microphone and speaker mute state on `Connected` (LINQT-2588) | Multi-call correctness — mute state was stale after switching legs. |
| `6970a8cc1` | Forces model and connection teardown in the `CallCore` destructor | Crash fix on call end. |
| `0db1c6774`, `057fddafa`, `83dfaccf1` | Merge-calls fixes, error message in the calls window, hide merge button for non-admin meetings | Adjacent multi-call work. `83dfaccf1` adds `haveNonAdminMeeting` to `CallList`/`CallProxy`. Take if we keep the merge feature; the meeting parts are less relevant to us. |

### Deliberately skip

- `befa4a263` — Jabra headset support via hidapi, plus the six `onHeadset*` callbacks in
  `CallModel`. **Needs SDK 5.5.** Drop these hunks when porting `CallModel` and `CallCore`.
- Anything in `Linphone/core/chat`, `Linphone/core/recording`, video layout, screen sharing and
  meetings — we do not ship those features.
- Most of `MainWindow.qml` (163 lines changed) — it is an SSO/OIDC page, a splash-screen loader
  restructure and an H.264 codec downloader, none of it transfer-related, and it collides with our
  login-page changes.

## 5. Suggested order of work

1. **Bump the SDK submodule within the 5.4 line** to 5.4.104 (matching upstream 6.1.2) or the 5.4
   tip. Build, then test blind and attended transfer. This alone is likely to fix the real-world
   transfer failures, and it is independent of everything else.
2. **Cherry-pick the four transfer-panel UI changes** — `a4b84891a`, `b54359285`, `76f4f5525` and
   the `NewCallForm` cleanup. Low risk, low conflict.
3. **Cherry-pick the multi-call fixes** — `58297e22c`, `37fb24ed6`, `2d3d1bd79`, `6970a8cc1`. These
   touch `CallList.cpp`, `CallModel.cpp` and `CallCore.cpp`, which we have not modified, so they
   should apply cleanly.
4. **Decide on the merge-calls fixes** (`0db1c6774`, `057fddafa`, `83dfaccf1`) depending on whether
   we keep the merge button.
5. Treat a full SDK 5.5 and upstream 6.2 catch-up as a separate, larger piece of work.

## 6. Gaps that upstream does not fix

Worth flagging, because if the goal is "attended transfer works well" then upstream 6.2 will not get
us all the way there:

- **No consultation-then-transfer flow.** The user has to open the New call panel, dial, then open
  the Transfer panel and pick the call from the current-calls list. There is no single "consult then
  complete transfer" affordance, and no explicit "complete transfer" button on the consultation leg.
- **`transferState` is only tracked for the current call.** `CallsWindow.qml` binds
  `property var transferState: call && call.core.transferState`. When the transferred call ends, the
  binding switches to a different call, which makes the success and failure toasts unreliable.
- **Incoming REFER is never surfaced.** `hasTransferPending()` and `acceptTransfer()` exist in the
  SDK we already ship but are unused, so transfers directed at us are always auto-accepted with no
  user visibility.

These would be our own additions rather than a port, so they need estimating separately.

## 7. What was actually done on this branch

Decisions taken: stay on the SDK 5.4 line, take the transfer work plus the multi-call fixes, and fix
the `transferState` binding. The consult-then-complete flow was left out for now.

| Commit | Change |
|---|---|
| `ada7a42ed` | Capture the local SDK patches as files under `nm-pbx-docs/sdk-patches` before touching the submodule |
| `089624d9d` | `.gitattributes` marking `*.patch` as binary so `core.autocrlf` cannot corrupt them |
| `5f4f7ea74` | SDK bumped from 5.4.73 to **5.4.114**, carrying liblinphone `f49e4540a` |
| `bbc57d0cd` | Upstream `37fb24ed6` — pause or leave the conference, not both |
| `af7887578` | Upstream `6970a8cc1` — force model teardown in the `CallCore` destructor |
| `cbfa459a4` | Upstream `2d3d1bd79` — sync mic and speaker mute on `Connected` |
| `7f4ef94e0` | Upstream `58297e22c` — new call becomes current at `OutgoingProgress` |
| `b0b8e1bf4` | Upstream `a4b84891a` — hide the transfer button for calls not yet connected |
| `7cb1d5482` | Upstream `b54359285` — hide pause in the transfer picker |
| `0377cf4e6` | Upstream `76f4f5525` — close the right panel when a conference starts |
| `30244a1cb` | Drop the dead `groupCallVisible` property, matching upstream |
| `419f92489` | Track the transferred call rather than the current one |

### The SDK patch trap

`external/linphone-sdk` carried three uncommitted edits in its nested submodule working trees:
a CardDAV auth fix in `liblinphone` and the MSVC `libm` fix in `mediastreamer2`. Nothing tracked
them, so a fresh clone, a `git submodule update --force` or any SDK bump would have destroyed them
silently. The CardDAV one was not documented anywhere.

They are now saved as patch files with a README under `nm-pbx-docs/sdk-patches`. Both reapplied
cleanly to 5.4.114. **Check that directory whenever the SDK moves.**

The pre-bump state is also still sitting in a git stash inside each nested submodule
(`git -C external/linphone-sdk/liblinphone stash list`) as a belt-and-braces backup. Safe to drop
once a build has been verified.

### transferState rework

`CallsWindow.qml` bound `property var transferState: call && call.core.transferState`, which follows
whichever call is current. A transfer ends the call being transferred, so the current call moves on
mid-transfer and the binding lands on a different call reporting `Idle`.

Replaced with a latch in `AbstractWindow.qml`: `transferringCall` plus `transferInProgress`, set by
`beginTransfer(call)` at each initiation point and cleared by `endTransferTracking()`. The latch is
set before the flag deliberately, so the binding settles before we start reporting, otherwise the
stale state left on a call by a previous failed transfer reads as a fresh failure.

Three further defects fixed at the same time:

- Transfers we did not start no longer raise a popup. `Notifier.cpp` transfers a declined call to
  voicemail in C++, which used to trigger the "transfer in progress" popup and its toasts.
- If the transferred call disappears before reporting a result, the progress popup is now closed
  rather than left on screen.
- The terminal state is snapshotted and tracking stopped before any popup is shown, so the handler
  cannot re-enter for the same transfer.

### Build note

`clang-format` is not on `PATH` on this machine, and the repo's `pre-commit` hook refuses to run
without it. It ships with Qt Creator:

```
export PATH="/c/Qt/Tools/QtCreator/bin/clang/bin:$PATH"
```

`cmake` is likewise not on `PATH`; it is at `C:\Qt\Tools\CMake_64\bin`.

## 8. Still to test by hand

The code changes are done and the tree builds, but transfers cannot be verified without a PBX and
two endpoints. Worth walking through:

1. **Blind transfer** to a contact with one address, and to a contact with several (the address
   chooser path).
2. **Attended transfer** — answer a call, dial a consultation call from the New call panel, confirm
   the consultation call becomes the current call, talk, then transfer the first call to it from the
   Transfer panel.
3. **Transfer while a leg is mid-hold.** This is the case the SDK fix addresses, so it is the most
   valuable test. Put a call on hold and transfer immediately, before the re-INVITE settles.
4. **Failure path** — transfer to an address that rejects, and confirm the progress popup closes and
   the failure message appears exactly once.
5. **Transfer picker contents** — confirm the current call is hidden, ringing and early-media calls
   show no transfer button, and no pause button appears in the picker.
6. **Decline to voicemail** — confirm no transfer popup appears now.
