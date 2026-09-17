# NMPBX

This is a lightly modified version of the open source linephone-desktop project. It has been adapted for use within NMPBX. The changes are primarily hiding features that are not needed to streamline the experience for users.

## Technical overview

This is a cross-platform C++ project, which uses Qt6.

For our use, we're only concerned with the 64-bit Windows build.

## Documentation

[Readme](README.md) gives broad information about the project, with the technical details from the project's primary maintainers.

[Setup](/nm-pbx-docs/setup.md) has instructions specifically for building the Windows version. Assume the user is following these steps when building the project.

[Brand](/nm-pbx-docs/brand.md) has referencing for colours, images, etc., for the NMPBX branding.

[SDK patches](/nm-pbx-docs/sdk-patches/README.md) lists the local fixes we carry inside the
`external/linphone-sdk` submodules. Those live in untracked working trees, so they are lost by a
fresh clone, by `git submodule update --force`, and by any SDK version bump. If you change anything
under `external/linphone-sdk`, capture it as a patch there. Reapply them with
`nm-pbx-docs/apply-sdk-patches.ps1`.

## Language

This project supports translation into multiple languages. However we are only concerned with the English version. Use British English. Translate any hardcoded text in other languages that may be user-visible.

## Building / Compiling

When you need to build the project, you MUST ask the user to perform the build for you. DO NOT attempt to build the project yourself.

There are steps involved in building that you do not have access to, and attempting to perform a build yourself may disrupt future work.

## Logs

Crash dump will be written to `%LOCALAPPDATA%\NMPBX\crashpad\reports` and can be symbolised against `build/bin/RelWithDebInfo/NMPBX_app.pdb`

General logs in `%LOCALAPPDATA%\NMPBX\logs`
