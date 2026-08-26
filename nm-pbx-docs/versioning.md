# Versioning

## Short answer

There is no single file holding the version. The version is worked out at CMake configure
time from **git tags**, using `git describe` in the repository root. Everything that shows a
version, the installer title, the package filename, the exe properties, the in-app debug
screen, the SIP user agent and the crash reports, is derived from that one value.

So the single place to change is the **git tag**, with `-DLINPHONEAPP_VERSION=...` available
as a manual override when you need to build a specific version without tagging.

## How it is derived

1. `cmake/install/install.cmake:29` runs `git describe --always` in the repo root and stores
   the result in `LINPHONEAPP_VERSION`, unless that variable is already set.
2. `Linphone/CMakeLists.txt:54` does the same thing for the app target, falling back to
   `bc_compute_full_version()` (from `external/linphone-sdk/bctoolbox`), which also uses
   `git describe`.
3. `bc_parse_full_version()` splits that into `LINPHONE_MAJOR_VERSION`,
   `LINPHONE_MINOR_VERSION`, `LINPHONE_MICRO_VERSION` and the branch/pre-release part.
4. `Linphone/application_info.cmake:9` sets `APPLICATION_SEMVER` to the full version, which
   reaches C++ via `Linphone/config.h.cmake`.

On the current branch `git describe` returns `6.1.0-beta-182-gc56aa877f`, so major is 6,
minor is 1 and micro is 0.

## Where the version ends up

| Surface | Source |
| --- | --- |
| Installer title, "NMPBX 6.1" | `CPACK_NSIS_PACKAGE_NAME`, `cmake/install/install.cmake:253-257` |
| Installer filename, `NMPBX-<version>-win64.exe` | `CPACK_PACKAGE_FILE_NAME`, `cmake/install/install.cmake:228` |
| Add/Remove Programs version | `CPACK_PACKAGE_VERSION_MAJOR/MINOR/PATCH`, `cmake/install/install.cmake:187-190` |
| Windows exe file properties | `cmake/install/windows/appDetailsWindows.rc.in`, configured at `Linphone/CMakeLists.txt:142` |
| macOS `Info.plist` (`CFBundleShortVersionString`) | `@PACKAGE_VERSION@`, `cmake/install/macos/Info.plist.in` |
| In-app version, Settings > Debug | `AppCpp.applicationVersion` in `Linphone/view/Page/Layout/Settings/DebugSettingsLayout.qml:129` |
| SIP `user_agent` in `linphonerc` | `Linphone/tool/Utils.cpp:606` |
| Crash report annotation | `APPLICATION_SEMVER` in `Linphone/tool/crash_reporter/CrashReporter.cpp:45` |
| Update check | `CoreModel::checkForUpdate`, `Linphone/core/App.cpp:1552` |

## Why the installer says "NMPBX 6.1" and not "6.1.0"

`cmake/install/install.cmake:253` only appends the micro version when it is truthy. CMake
treats the string `0` as false, so a `x.y.0` version drops the micro and the installer title
becomes "NMPBX 6.1". This is expected behaviour, not a bug, and it means a 6.2.0 tag will
show as "NMPBX 6.2".

## Moving to 6.2

### Step 1, the tag (the real change)

Create an annotated tag reachable from the branch you build, matching the existing tag style
(`6.1.0-alpha`, `6.1.0-beta`, `6.2.0-alpha`):

```
git tag -a 6.2.0-beta -m "6.2.0-beta"
git push origin 6.2.0-beta
```

Note that the `6.2.0-alpha` tag already exists in the repo but sits on commit `d4c1387c4`,
which is **not** an ancestor of this branch, so it has no effect on our builds. `git describe`
only sees tags on your own history.

After tagging, reconfigure CMake from scratch (a stale cache keeps the old version) and the
installer title becomes "NMPBX 6.2".

### Step 2, a build without tagging

If you need a 6.2 build before the tag exists, pass the version explicitly at configure time.
It has to be set in the cache so both the app scope and the install scope see the same value:

```
cmake -DLINPHONEAPP_VERSION=6.2.0 ...
```

### Step 3, hardcoded values to keep in step

These do not follow the tag and should be updated by hand when the major/minor changes:

- `Linphone/CMakeLists.txt:3`, `project(Linphone VERSION 6.1.0 ...)`. Feeds `PROJECT_VERSION`,
  used only for the macOS bundle properties, but it should not be left stale.
- `cmake/install/install.cmake:43`, the `6.1.0` fallback used when git is unavailable, for
  example a source tarball build with no `.git` directory.
- `CHANGELOG.md:36`, the `## [6.1.0]` heading.
- `nm-pbx-docs/setup.md:347,354`, example signing commands referencing 6.1 filenames.

## Known quirk in the Windows exe properties

`cmake/install/windows/appDetailsWindows.rc.in:28-29` references `${FULL_VERSION}` and
`${version_major}` / `${version_minor}` / `${version_patch}`, none of which are set in that
scope. The `FileVersion` and `ProductVersion` **strings** in the exe properties therefore come
out empty. The numeric `FILEVERSION` / `PRODUCTVERSION` fields on lines 10-11 use
`LINPHONE_MAJOR_VERSION` and friends and are correct. This is inherited from upstream. If we
want the string fields populated, they should be changed to use the `LINPHONE_*_VERSION`
variables too.
