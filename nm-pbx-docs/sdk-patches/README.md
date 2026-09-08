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

### `vpx-windows-linker.patch`

Two fixes to the `Windows/vpx_configure.sh.cmake` wrapper used when building VPX for Windows/MSVC:

1. `unset CC CXX` before invoking libvpx's `configure`. libvpx's own `configure.sh` has a built-in
   escape hatch for exactly this situation — `if [ -z "$CC" ] || enabled external_build; then` —
   which skips its internal toolchain self-tests entirely (printing "Bypassing toolchain for
   environment detection.") on the reasoning that an external build system driving it (like our
   CMake `ExternalProject_Add`) already knows the toolchain works, so libvpx doesn't need to
   re-validate it. That path only triggers when `CC` is empty. When something in the build
   environment happens to have `CC` already set (observed via MSBuild's custom-build-step
   environment, though not fully root-caused which upstream tool sets it), libvpx instead runs its
   real self-tests, which fail: `check_cc()`/`check_ld()` invoke the compiler GNU-style as
   `cl -c -o file.o file.c`, but MSVC's `cl.exe` only honours `-o` for the final link step — under
   `-c` it silently writes its own default `file.obj` instead, so the object never exists at the
   path the next step (`check_ld()`, or the real Makefile's compile rules, which use the identical
   pattern) expects, and the whole configure step fails with `Toolchain is unable to link
   executables` / `LINK : fatal error LNK1181: cannot open input file`. Explicitly unsetting `CC`
   (and `CXX`, checked the same way for C++) forces the bypass every time, regardless of what the
   calling environment provides. Verified directly: with `CC`/`CXX` deliberately pre-set to `cl`
   before invoking the script, `unset CC CXX` still reliably triggers the bypass and configure
   still succeeds.

2. Pins `TMPDIR` to a fixed path inside the CMake build directory, instead of leaving libvpx's
   script to fall back to the ambiguous POSIX `/tmp`. Depending on which MSYS/MSYS2 runtime
   actually ends up running `sh` for this step, `/tmp` can resolve to a different real Windows
   directory than the one native tools write to and read from, so files written by one step aren't
   reliably found by the next. Pinning `TMPDIR` removes that ambiguity; kept as a good-hygiene
   belt-and-braces fix even though it's no longer covering for the `-o`/`/Fo:` mismatch once the
   bypass in (1) is reliably triggered.

Neither of these is specific to any particular build tool or IDE — both affect any VS 2022 build
of this exact SDK commit, run any way.

## Applying

From the repository root, after the submodules are checked out:

```
git -C external/linphone-sdk/mediastreamer2 apply ../../../nm-pbx-docs/sdk-patches/mediastreamer2-msvc-libm.patch
git -C external/linphone-sdk/liblinphone  apply ../../../nm-pbx-docs/sdk-patches/liblinphone-carddav-auth-username.patch
git -C external/linphone-sdk apply ../../nm-pbx-docs/sdk-patches/vpx-windows-linker.patch
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
