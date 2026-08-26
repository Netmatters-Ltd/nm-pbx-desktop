# Windows installer guide for NMPBX (local workflow)

## Scope

This guide is for producing a Windows installer from a local machine only (no CI/CD), based on the current project setup.

Target output:
- Signed installer executable (.exe)
- Silent install and silent uninstall for fleet deployment

## What already exists in this repository

The project already has a complete Windows packaging path:
- CPack is enabled when ENABLE_APP_PACKAGING is ON in [CMakeLists.txt](../CMakeLists.txt#L154).
- Windows packaging uses NSIS (installer .exe) in [cmake/install/install.cmake](../cmake/install/install.cmake#L225).
- The custom NSIS template is [cmake/install/windows/NSIS.template.in](../cmake/install/windows/NSIS.template.in), with custom install/uninstall behaviour and protocol registration hooks from [cmake/install/windows/install.nsi.in](../cmake/install/windows/install.nsi.in) and [cmake/install/windows/uninstall.nsi.in](../cmake/install/windows/uninstall.nsi.in).
- Packaging is executed during install via [cmake/install/packaging.cmake.in](../cmake/install/packaging.cmake.in), and final artefacts are copied into OUTPUT/Packages.
- Optional signing is already wired into packaging via LINPHONE_WINDOWS_SIGN_TOOL, LINPHONE_WINDOWS_SIGN_TIMESTAMP_URL, and LINPHONE_WINDOWS_SIGN_HASH in [cmake/install/install.cmake](../cmake/install/install.cmake#L273).

Conclusion: the most efficient path is to keep NSIS + CPack and automate around that.

## Recommended strategy

Use the existing NSIS installer output (.exe) as the deployment package.

Reason:
- It is already integrated and maintained in this project.
- It includes app-specific registry/protocol handling.
- It supports unattended deployment mode.
- It avoids re-implementing the installer logic in WiX/MSI.

If an MSI is strictly required later, treat that as a separate phase.

## Local packaging workflow (Windows)

Run these from an x64 Visual Studio developer prompt with Qt environment loaded, as in [nm-pbx-docs/setup.md](setup.md).

1. Configure with packaging enabled (and optional built-in signing)

Example without auto-signing:

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DENABLE_WINDOWS_TOOLS_CHECK=ON ^
  -DENABLE_APP_PACKAGING=YES
```

Example with auto-signing using hardware token certificate thumbprint:

```bat
cmake .. -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DENABLE_WINDOWS_TOOLS_CHECK=ON ^
  -DENABLE_APP_PACKAGING=YES ^
  -DLINPHONE_WINDOWS_SIGN_TOOL=signtool ^
  -DLINPHONE_WINDOWS_SIGN_TIMESTAMP_URL=http://timestamp.digicert.com ^
  -DLINPHONE_WINDOWS_SIGN_HASH=YOUR_CERT_SHA1_THUMBPRINT
```

2. Build and install

```bat
cmake --build . --config RelWithDebInfo --parallel 10
cmake --install . --config RelWithDebInfo
```

3. Collect package

Expected output location:
- build\OUTPUT\Packages\NMPBX-<version>-win64.exe

Notes:
- NSIS (makensis) must be available; this is checked in [cmake/install/install.cmake](../cmake/install/install.cmake#L232).
- Packaging uses windeployqt internally in [cmake/install/cleanCPack.cmake.in](../cmake/install/cleanCPack.cmake.in#L89), so Qt runtime deployment is included in the installer payload.

## Signing with hardware token (recommended production flow)

Use SignTool with certificate thumbprint from the token-backed Windows certificate store.

1. Install token middleware (SafeNet) and verify the signing cert is visible in certlm.msc / certmgr.msc.
2. Sign installer:

```bat
signtool sign /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com /sha1 YOUR_CERT_SHA1_THUMBPRINT build\OUTPUT\Packages\NMPBX-<version>-win64.exe
```

3. Verify signature:

```bat
signtool verify /pa /v build\OUTPUT\Packages\NMPBX-<version>-win64.exe
```

You can either:
- Let CPack auto-sign (via CMake variables above), or
- Sign as a dedicated post-package step.

For operations reliability, a dedicated post-package sign step is usually easier to troubleshoot.

## Unattended deployment commands

### Silent install

NSIS supports silent mode with /S.

```bat
NMPBX-<version>-win64.exe /S
```

Optional install directory override (must be the last argument, no quotes in NSIS convention):

```bat
NMPBX-<version>-win64.exe /S /D=C:\Program Files\NMPBX
```

### Silent uninstall

```bat
"C:\Program Files\NMPBX\Uninstall.exe" /S
```

### Recommended upgrade sequence for fleet tools

The installer checks for running NMPBX and can prompt in interactive scenarios in [cmake/install/windows/NSIS.template.in](../cmake/install/windows/NSIS.template.in#L993). For unattended deployment, use a deterministic sequence:

1. Stop process if running.
2. Silent uninstall existing version (if present).
3. Silent install new version.
4. Validate exit code and signature.

Example:

```bat
taskkill /IM NMPBX.exe /F >NUL 2>&1
if exist "C:\Program Files\NMPBX\Uninstall.exe" "C:\Program Files\NMPBX\Uninstall.exe" /S
NMPBX-<version>-win64.exe /S
```

## Should we produce MSI now?

Short answer: not yet, unless there is a hard policy requirement.

Why:
- The current project is explicitly customised around NSIS.
- Recreating protocol registration and uninstall/upgrade behaviour in MSI/WiX is additional engineering and test work.
- NSIS .exe already supports silent enterprise deployment.

If MSI becomes mandatory (for example, strict GPO/Intune policy), recommended phase 2 options:
- Rework CPack configuration from NSIS to WiX and migrate custom actions.
- Or maintain a dedicated WiX project that installs the same payload and replicates registration logic.

## Security and secret-handling notes

- Do not keep token passwords, PFX passphrases, or sensitive signing details in committed markdown files.
- Prefer certificate thumbprint-based signing from the token and protected local machine context.
- Timestamp every signature to preserve trust after cert expiry.

## Practical release checklist

1. Build RelWithDebInfo with ENABLE_APP_PACKAGING=YES.
2. Produce package in OUTPUT/Packages.
3. Sign installer with hardware token.
4. Verify signature.
5. Test interactive install/uninstall on a clean VM.
6. Test silent install/uninstall and upgrade path on a clean VM.
7. Record installer hash and version for deployment tracking.

## Summary recommendation

For this repository today, the fastest and lowest-risk production path is:
- Keep NSIS/CPack as implemented.
- Sign the generated installer with your hardware token.
- Deploy using silent mode (/S) with a pre-stop and pre-uninstall sequence.

This gives you a practical, unsupervised Windows deployment path now, without introducing a separate MSI authoring project.