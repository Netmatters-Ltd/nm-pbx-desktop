# Local patches to the linphone-sdk

These are changes we need inside `external/linphone-sdk` that are not in upstream's SDK. The SDK is
a git submodule, and its own components (`liblinphone`, `mediastreamer2`, ...) are nested
submodules, so these edits live in a working tree that nothing tracks. They are lost by a fresh
clone, by `git submodule update --force`, and by any SDK version bump.

Keep them here so they can be reapplied.

## Patches

### `mediastreamer2-msvc-libm.patch`

Stops mediastreamer2 linking against the Unix maths library `libm` on Windows with MSVC, where the
maths functions live in the C runtime. Without it the build fails with
`LINK : fatal error LNK1104: cannot open file 'm.lib'`.

Also described in `nm-pbx-docs/setup.md`.

### `liblinphone-carddav-auth-username.patch`

Sets the authentication credentials on the CardDAV HTTP request so the auth callback can find the
right auth info by username when the server responds 401. Without it the auth event carries no
username hint, so the lookup is ambiguous when several auth infos share a realm, which is the normal
case for us because SIP and CardDAV sit on the same deployment.

Needed by our CardDAV contacts work (task 5000570).

### `liblinphone-log-collection.patch`

Fixes two defects in the log collection writer in `coreapi/linphonecore.c`, both still present in
upstream master (as of Sep/2026). They were found while investigating the app freezes after a machine
stopped logging entirely at the point its log file crossed the 50MB rotation threshold.

- **Use-after-free.** `_open_log_collection_file_with_idx` assigned `fopen`'s result straight to the
  global `liblinphone_log_collection_file`, then called `fclose` on the oversize path without
  setting it back to `NULL`. When rotation could not clear the second file, the handler went on to
  `fprintf` through a freed `FILE*`. On Windows that happens whenever another process holds a log
  file open without `FILE_SHARE_DELETE`, because `unlink` is then refused and `rename` fails with
  the destination still present. Anti-virus, the search indexer and an engineer with the log open in
  an editor all do this. The fix opens into a local `FILE*` and only publishes it to the global once
  it is known to be usable.
- **Retry storm.** When the handle was `NULL`, every log line from every thread retried the whole
  open and rotate sequence while holding the process-wide log mutex, then discarded the message.
  At the peak rate we measured, 568 lines per second, that is roughly 5,000 filesystem metadata
  operations a second with every logging thread serialised behind one lock. The fix adds a five
  second backoff between failed attempts.

The same patch also closes a time-of-check race on the file pointer by putting the null check and
the write in one locked region, makes rotation fall back to truncating rather than giving up when
`unlink` or `rename` is refused, checks the previously unchecked `fstat` return, and takes the file
size from `ftell` rather than from `fprintf`'s return, which undercounts by one byte per line on
Windows because the stream translates `\n` to `\r\n`.

## Applying

From the repository root, after the submodules are checked out, run
`nm-pbx-docs/apply-sdk-patches.ps1`. It is safe to run more than once and reports what it did.

To do it by hand instead:

```
git -C external/linphone-sdk/mediastreamer2 apply ../../../nm-pbx-docs/sdk-patches/mediastreamer2-msvc-libm.patch
git -C external/linphone-sdk/liblinphone  apply ../../../nm-pbx-docs/sdk-patches/liblinphone-carddav-auth-username.patch
git -C external/linphone-sdk/liblinphone  apply ../../../nm-pbx-docs/sdk-patches/liblinphone-log-collection.patch
```

## Checking what is applied

```
git -C external/linphone-sdk/mediastreamer2 diff
git -C external/linphone-sdk/liblinphone  diff
```

## Refreshing a patch after an SDK bump

If a patch no longer applies cleanly, fix it by hand in the submodule working tree, then regenerate
the file:

```
git -C external/linphone-sdk/liblinphone diff src/vcard/carddav-context.cpp > nm-pbx-docs/sdk-patches/liblinphone-carddav-auth-username.patch
git -C external/linphone-sdk/liblinphone diff coreapi/linphonecore.c > nm-pbx-docs/sdk-patches/liblinphone-log-collection.patch
git -C external/linphone-sdk/mediastreamer2 diff > nm-pbx-docs/sdk-patches/mediastreamer2-msvc-libm.patch
```

Note that the two liblinphone patches touch different files, so `git -C ... diff` with no path would
merge them into one. Always pass the path.
