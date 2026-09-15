# Building

Requirements:

- Windows 10 or later
- Visual Studio 2022 or later with Desktop development with C++
- MSVC x86 toolchain and Windows SDK
- PowerShell 5.1 or later

Run from a PowerShell prompt:

```powershell
.\tools\Build-PublicRelease.ps1
```

The script rebuilds the Win32 Release targets, runs the native unit tests,
creates a redistributable ZIP under `dist`, and verifies that no game-owned
resource or executable is present in the package.
