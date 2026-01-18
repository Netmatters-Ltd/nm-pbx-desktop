# Our setup guide

This is for Windows 11 64-bit and was last updated Jan/2026.

Many of these steps will trigger Windows to ask you to provide credentials for a user with admininstrator access, which you should do. Unless specifically stated, you don't need to run command prompts (or other operations) as administrator.

## Troubleshooting tips

Problems often come down to what the system is trying to run based on `Path`:

- Ultimately we need these dependencies to be available in a Windows Command Prompt.
- If you running `doxygen` in `cmd.exe` fails, it'll likely fail for the make process too.
- Use `which doxygen` to check which install location the system is using.
- If `which` shows something unexpected you may need to add things to or re-arrange the Windows system environment variable named `Path` (works very much like `PATH` in the Linux world.)
- Ordering matters in `Path`. It'll work from first to last, until it finds a matching name.
- In most cases you'll want the MSYS2 directories to be last in the `Path`.

## MSYS2

Install https://www.msys2.org/

Run it. You'll be running several commands in the console it opens. Note this console doesn't support Ctrl-V to paste. You can use Shift-Insert or the right-click menu.

```
pacman -Syu
```

Accept prompts.

When it closes, run MSYS2 again.

```
pacman -Su
```

Accept prompts.

```
pacman -Sy --needed base-devel mingw-w64-x86_64-toolchain
```
Select all. Accept prompt.

```
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf mingw-w64-x86_64-nasm mingw-w64-x86_64-yasm doxygen perl git --needed
```

In Windows' settings, add to the system `Path` environment variable, in the following order. Make sure you're editing the **System** environment variables, not user environment variables.
```
C:\msys64\mingw64\bin
C:\msys64\
C:\msys64\usr\bin
```

Prefer to keep these three entries at the end of your `Path` when adding others later.

## Python

Download and install Python 3 for Windows, using the Python install manager: https://www.python.org/downloads/windows/

It should prompt you if you want to add a directory like `C:\Users\sam.driver\AppData\Local\Python\bin` to PATH. Answer yes.

If you don't already have an up to date version of CPython (the reference implementation of Python) installed it'll prompt to install. Answer yes.

In a Command Prompt check that you're set up to use the right install of Python:

```cmd
which python
```
Should give a result like:
```
/c/Users/sam.driver/AppData/Local/Python/bin/python
```

If it doesn't, fiddle around with the ordering of the PATH environment variable for the system and/or your user to prioritise this install of Python.

Install Python dependencies this project needs:
```cmd
python -m pip install pystache six
```

You can check they're installed:
```cmd
python -c "import pystache; print(pystache.__version__)"
python -c "import six; print(six.__version__)"
```

## Doxygen

Download and install Doxygen.

Recommend using `winget` if you have it available:

```cmd
winget install DimitriVanHeesch.Doxygen
```

Can also get it via https://github.com/doxygen/doxygen although the site it links to with an installer to download is full of ads, and the installer is not signed.

You want to end up with the install location (probably `C:\Program Files\doxygen\bin`) in your system Path for Windows. The installer should do that for you - either via winget or downloaded yourself.

## Visual Studio

You don't have to use the Visual Studio IDE to write the code on the project. But you do need to install it as we'll be using various build tools it provides.

Install Visual Studio 2022 Community Edition: https://aka.ms/vs/17/release/vs_community.exe

We're intentionally installing 2022, not the latest version. Also note that Visual Studio is a completely separate product from VS Code (they're both IDEs made by Microsoft.) If you already have another version of Visual Studio installed, the Visual Studio Installer should be able to cope with that and allow you to have multiple versions.

The readme asks for "Windows Universal Platform development" but that is no longer available. During installation include workloads:
- Desktop development with C++
- WinUI application development, then on the right tick these optional items:
    - Universal Windows Platform tools
    - C++ (v143) Universal Windows Platform tools

## Qt

Install Qt Community Edition: https://www.qt.io/development/download-qt-installer-oss

The project we're building is GPLv3, so does fit the obligations.

Use the "online installer". You'll need to create a Qt account. You can use your company email address.

Install to C:\Qt and tick both "Qt 6.10 for desktop development" and "Custom Installation". At time of writing 6.10.1 is the latest version, linphone-desktop needs 6.10 or later. Make the following changes in the custom installation:
- Within Qt 6.10.1 select **"MSVC 2022 64-bit"** and make sure MinGW is un-ticked.
    We need the MSVC toolchain to work with Visual Studio, instead of MinGW.
- Within Build Tools untick MinGW.
- Within Additional Libraries under Qt 6.10.1:
    - Tick Qt Multimedia (may already be ticked)
    - Tick Qt Network Authorization
    - Tick Qt Shader Tools (may already be ticked)

There will be several other items ticked by default, which can likely be left as-is.

Add a new system environment variable in Windows:
- name "Qt6_DIR" with value: "C:\Qt\6.10.1\msvc2022_64\lib\cmake\Qt6"

Add to the Path system environment variable in Windows:
- "C:\Qt\6.10.1\msvc2022_64\bin"

If you installed a different Qt version the `6.10.1` part of the above paths will be different.

## 7-Zip

Download and install 7-zip (if you don't already have it installed) https://www.7-zip.org/

Add `C:\Program Files\7-Zip` to the system Path environment variable in Windows (or wherever you installed it.)

## Clone repository
```pwsh
git clone https://github.com/Anatta336/linphone-desktop.git --recursive
```

Note this pulls several submodules from the main project, in addition to our customised linphone-desktop project.

## Build

Run "x64 Native Tools Command Prompt for VS 2022" from Windows start menu. Run the following within that command prompt (it matters that you run it in that specific command prompt instance.)

```cmd
C:\Qt\6.10.1\msvc2022_64\bin\qtenv2.bat
```

This sets environment variables for Qt, and needs to be done **each time** you start a command prompt session to build the project. Don't worry about the "Remember to call vcvarsall.bat" that has effectively been done by using "x64 Native Tools Command Prompt for VS 2022" instead of a normal command prompt.

Navigate to where you cloned the repository, for example:
```cmd
cd C:\Users\sam.driver\Code\linphone-desktop
```

If this is the first time building, create a build directory. In the future deleting and recreating this build directory will be a useful step if you suspect you have problematic remains from previous attempts to build.

```cmd
mkdir build
cd build
```

### Known Issue: `m.lib` linker error

Both build approaches can fail with `LINK : fatal error LNK1104: cannot open file 'm.lib'`, this is because the linphone-sdk's mediastreamer2 component tries to link against the Unix math library (`libm`) which doesn't exist on Windows with MSVC (math functions are built into the C runtime).

Make two edits to prevent the build system from searching for and linking the math library on Windows with MSVC:

1. Edit `external\linphone-sdk\mediastreamer2\src\CMakeLists.txt` around line 23. Change:
   ```cmake
   find_library(LIBM NAMES m)
   ```
   To:
   ```cmake
   if(NOT MSVC)
       find_library(LIBM NAMES m)
   endif()
   ```

2. Edit `external\linphone-sdk\mediastreamer2\CMakeLists.txt` around line 462. Change:
   ```cmake
   if(LIBM)
       list(APPEND LINK_LIBS m)
   endif()
   ```
   To:
   ```cmake
   if(LIBM AND NOT MSVC)
       list(APPEND LINK_LIBS m)
   endif()
   ```

### With Visual Studio generator

From within the build directory, run this to configure the build process. This (usually) only needs to be re-run if make files have changed.

If you have built successfully (without a change in project dependencies since then) you should be able to remove `-DENABLE_WINDOWS_TOOLS_CHECK=ON`

```cmd
cmake .. -G "Visual Studio 17 2022" -A x64 -DENABLE_WINDOWS_TOOLS_CHECK=ON
```

If it complains about an old CMake version being used, add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`

This will take around 15 minutes the first time, and 5 minutes after that.

Still in the build directory, run the build: (To help identify issues remove `--parallel 10` add `--verbose`)

```cmd
cmake --build . --config RelWithDebInfo --parallel 10
```

Once the build is done successfully run the install step:

```cmd
cmake --install . --config RelWithDebInfo
```

The final executable to actually run should be: `build\OUTPUT\bin\linphone.exe`

### With Ninja generator (faster, if it works)

It should be possible to build faster using Ninja, but haven't had success. These instructions are included for reference.

From within the build directory, run this to configure the build process.

```cmd
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WINDOWS_TOOLS_CHECK=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

This will take around 15 minutes the first time, and 5 minutes after that.

- `-G "Ninja"` use the Ninja generator which should be faster.
- `-DCMAKE_BUILD_TYPE=RelWithDebInfo` to build as "debug release" which is recommended. Shouldn't be meaningfully different performance than release without debug info.
- `-DENABLE_WINDOWS_TOOLS_CHECK=ON` to install any missing tools/packages.
- `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` work around some makefiles being for earlier versions.

Still in the build directory, run the build: (To help identify issues remove `--parallel 10` add `--verbose`)

```cmd
cmake --build . --config RelWithDebInfo --parallel 10
```

Once the build is done successfully run the install step:

```cmd
cmake --install . --config RelWithDebInfo
```

The final executable to actually run should be: `build\OUTPUT\bin\linphone.exe`
