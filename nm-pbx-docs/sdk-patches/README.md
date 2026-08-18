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

## Applying

From the repository root, after the submodules are checked out:

```
git -C external/linphone-sdk/mediastreamer2 apply ../../../nm-pbx-docs/sdk-patches/mediastreamer2-msvc-libm.patch
git -C external/linphone-sdk/liblinphone  apply ../../../nm-pbx-docs/sdk-patches/liblinphone-carddav-auth-username.patch
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
git -C external/linphone-sdk/mediastreamer2 diff > nm-pbx-docs/sdk-patches/mediastreamer2-msvc-libm.patch
```
