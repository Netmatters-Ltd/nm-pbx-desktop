# Caller Identification

How an incoming or outgoing call is matched to a contact, and how a dialled number is converted
before it becomes a SIP address. This mirrors the Android app's behaviour; where the two differ
deliberately, it is called out below.

## The problem

Two separate things were failing.

**Dialling.** Contacts routinely store numbers in international form (`+447771514661`). Passed
straight to `interpretUrl`, that produced `sip:+447771514661@pbx.example.com`. The PBX only routes
numbers in local format, so the call came back as a SIP 404 "user has not been found".

**Identification.** `core->findFriend()` only compares SIP addresses. An inbound call from
`07771514661` never matched a contact holding `+447771514661`, so external callers rang in as a
bare number with no name.

## Number normalisation

`ToolModel::normalizePhoneNumber` converts a number from international to local form before it is
turned into an address.

| Stored or typed | Dialled       |
|-----------------|---------------|
| `+447771514661` | `07771514661` |
| `00447771514661`| `07771514661` |
| `07771514661`   | unchanged     |
| `01953438555`   | unchanged     |
| `201` (extension) | unchanged   |
| `sip:someone@…` | unchanged     |

If the number does not begin with the resolved prefix in either form, it is returned unchanged.
The rule is never applied forcibly.

### Resolving the prefix

`ToolModel::resolveCountryCallingCode` returns the country calling code without the `+`:

1. The default account's `internationalPrefix`, set by provisioning or in account settings.
2. Otherwise `44`.

Unlike Android there is no middle step reading the device region. Android reads the SIM's network
country; a desktop has no SIM, and deriving it from the Windows region setting would mean a laptop
set to another country silently stopped normalising. These are UK deployments, so UK is the
default.

### Where it is applied

At the dialling sites only, never inside `interpretUrl` itself. `interpretUrl` also interprets
account identities and addresses we are merely looking up, and rewriting numbers there would reach
account registration and the CLI, which Android never does.

- `ToolModel::createCall` — the dial pad, the contacts list (which passes a contact's stored
  number verbatim) and redial from history all reach this one function.
- `ToolModel::createGroupCall` — participant invites.
- `CallCore::lTransferCall` — blind transfer target.
- `CliModel` `tel:` / `callto:` handler. This branch needs its own call because it hands on a
  fully interpreted SIP URI as a string, by which point it no longer looks like a dialled number.

Deliberately left alone: `AccountModel`'s direct `core->interpretUrl(..., false)` calls (account
identities, not dialled numbers), and `CliModel::cliBye` / `cliAccept` (lookups, not dialling).
`usePrefix` in `interpretUrl` stays `false`; the SDK's `useInternationalPrefixForCallsAndChats`
does not reliably perform this conversion, and enabling it now that the account carries a prefix
would apply a second, different transform on top of ours.

## Matching a call to a contact

`ToolModel::findFriendByAddress` is the single lookup, used by `CallCore`, `CallHistoryCore`,
`Notifier`, `ParticipantDeviceCore` and `ToolModel::getDisplayName`. It runs on the linphone thread and has three tiers.

1. **Cache.** `FriendsManager`'s known and unknown maps, keyed by `FriendsManager::addressKey`.
2. **Local friends.** `core->findFriend()`, which covers everything the CardDAV sync has pulled
   down, plus locally created contacts.
3. **Phone number.** If the username looks like a phone number,
   `core->findFriendByPhoneNumber()`. The SDK normalises both the inbound number and each
   contact's stored numbers through the account's dial plan, so `07771514661` matches a contact
   holding `+447771514661`.

Only if all three miss does the existing remote-directory magic search fire. That path is
asynchronous and returns nothing to the caller. It is inert in the usual deployment: the whole
address book is synced locally and no remote contact directory is registered. We keep it rather
than delete it, but we do not rely on it, and `CallCore::findRemoteFriend` is likewise left as it
is.

The two synchronous tiers run **before** the negative cache is consulted. That ordering matters:
the negative cache is written before the async search returns, so a phone tier placed after it
would be short-circuited on the second lookup of the same address.

### Address cleaning

`FriendsManager::addressKey` is the one place an address becomes a cache key. It clones the
address and calls `clean()`, stripping transport and URI parameters such as `;user=phone`, so the
same person lands on the same key however their address reached us. Cloning is not optional:
`clean()` mutates in place, and the address passed in is often the SDK friend's own.

All three writers go through it — `FriendsManager`'s append and invalidation paths,
`ToolModel::findFriendByAddress`, and `MagicSearchModel::onSearchResultsReceived`. They have to
agree on the key format or the caches silently stop hitting.

The caches are in-memory only and are rebuilt from the core on each launch. Nothing serialises
them.

### Cache invalidation

A contact is cached under one key per address, so eviction iterates rather than removing the first
match, and clears every one of the contact's addresses from the negative cache. Without this, a
contact added or edited mid-session kept showing its old name, or stayed unidentified, on any
address but the first.

## Dependency: the account must carry a dial plan

The phone-number tier depends entirely on this. `linphone_account_normalize_phone_number` looks up
`DialPlan::findByCcc(internationalPrefix)`; with an empty prefix it falls back to a generic plan
where a UK national number never reaches E.164, and nothing matches.

`SettingsModel::applyAccountDialPlanDefault` fills the gap. It runs alongside
`applyCardDAVProvisioning`, on global state ready, on successful provisioning, and when the
default account changes. Order of precedence:

1. `international_prefix` in the provisioning file's `[ui]` section.
2. Whatever the account already has, from provisioning or from account settings. Never overwritten.
3. `44`.

It sets both `internationalPrefix` and `internationalPrefixIsoCountryCode`, so the dial plan
dropdown in account settings shows United Kingdom rather than a blank entry.

Expect a side effect: once the prefix is populated, SDK phone matching wakes up across the app, so
searching contacts by number behaves differently from before.

## Conference participants

Merging calls produces a local conference, and its tiles were showing people as
`127.0.0.1:5080`. Two addresses are in play and the wrong one was being used.

A `linphone::ParticipantDevice` takes its address from the SIP **Contact** header of the leg
(`ParticipantDevice::ParticipantDevice(participant, session, name)` in the SDK). On our PBX that
Contact is Asterisk's internal socket behind Flexisip, so it identifies nothing. The friend lookup
missed it and `getDisplayName` fell through to its last resort, the raw URI.

The participant address is the useful one. `Conference::addParticipantDevice` builds the device from
`call->getRemoteAddress()`, so `device->getParticipant()->getAddress()` is the same address
`CallCore` uses for `remoteName`. `ParticipantDeviceCore` now identifies from that, running the same
tiers as a one-to-one call: `findFriendByAddress`, then the SIP display name, then
`getDisplayName`. The device address is used only if there is no participant.

`mName`, `mAddress` and `mUniqueAddress` stay on the device address. They are identity keys rather
than labels: `ParticipantDeviceList::findDeviceByUniqueAddress` uses `uniqueAddress` when a device
leaves, and `ActiveSpeakerLayout.qml` compares `address` to hide the active speaker from the strip.
One participant can have several devices in a server-hosted conference, and those keys have to stay
distinct.

One-to-one calls are untouched. `ParticipantDeviceCore` is only built from `ConferenceCore` and
`ParticipantDeviceList`, both driven by a conference; without one, the sticker falls through to
`call.core.remoteName`.

Still outstanding: conference tiles pass no address to their `Avatar`, so contact photos never load
there, and `displayName` is a `CONSTANT` property resolved once, so a contact edited mid-conference
does not refresh. `CallHistoryCore` shows the refresh pattern if we want it.

## Known gaps

**Short extensions are excluded from the phone tier.** `looksLikePhoneNumber` requires a leading
`+` or at least 7 digits. Android has no such floor. Internal extensions are short and numeric and
are already resolved by tier 2 against the synced address book, so letting them reach
`findFriendByPhoneNumber` would gain nothing while risking a match against a contact who happens to
store the same digits as a phone number, putting a wrong name on a ringing call.

**A negative-cache entry can outlive its usefulness.** If a remote search returns a friend whose
addresses do not include the address that was queried, the queried key stays marked as a known
failure. Fixing it properly means plumbing the query string through
`CoreModel::searchInMagicSearch` so the result can be correlated with the request. Left alone
because the remote path is inert in this deployment and tier 3 covers the real cases.

## Related

- `nm-pbx-android/docs/phone-number-normalisation.md` — the Android side of the same problem.
- Extensions versus external contacts is a separate concern, handled by `FriendCore::mIsInternal`
  (a friend with a SIP address on the default account's domain) and `MagicSearchProxy`'s
  `ExtensionFilter`. Unchanged by this work.
