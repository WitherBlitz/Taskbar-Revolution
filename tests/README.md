# Native settings and taskbar verification

Use a Visual Studio developer PowerShell with the C++ and ATL components installed. These commands do not register build outputs or create commits.

```powershell
MSBuild Src/OpenShell.sln /t:Restore /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=x64
MSBuild Src/OpenShell.sln /t:StartMenu /p:Configuration=Release /p:Platform=x64 /p:CS_VERSION=4.4.198 /p:SpectreMitigation=Spectre /p:RegisterOutput=false
MSBuild Src/OpenShell.sln /t:StartMenu /p:Configuration=Release /p:Platform=Win32 /p:CS_VERSION=4.4.198 /p:SpectreMitigation=Spectre /p:RegisterOutput=false
MSBuild tests/TaskbarWidgetsTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:CS_VERSION=4.4.198 /p:SpectreMitigation=Spectre /p:RegisterOutput=false
./build/tests/TaskbarWidgetsTests.exe
git diff --check
```

Build the affected Explorer and IE DLL projects with the same properties for x64 and Win32:

- `Src/ClassicExplorer/ClassicExplorer.vcxproj`
- `Src/ClassicIE/ClassicIEDLL/ClassicIEDLL.vcxproj`

Build `Src/ClassicExplorer/ClassicExplorerSettings/ClassicExplorerSettings.vcxproj` and `Src/Update/Update.vcxproj` for Win32. The Explorer settings launcher intentionally uses the 32-bit DLL.

Compile and run the settings harness from the repository root:

```powershell
$framework = "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319"
New-Item -ItemType Directory -Force build/tests | Out-Null
& "$framework/csc.exe" /nologo /target:winexe /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /win32manifest:tests/SettingsSmoke.manifest /out:build/tests/SettingsSmoke.exe tests/SettingsSmoke.cs
$dll = (Resolve-Path build/bin/Release64/StartMenuDLL.dll).Path
Start-Process build/tests/SettingsSmoke.exe -ArgumentList ('"' + $dll + '" --smoke') -Wait
Get-Content build/tests/settings-smoke.log
Start-Process build/tests/SettingsSmoke.exe -ArgumentList ('"' + $dll + '" --behavior') -Wait
Get-Content build/tests/settings-smoke.log
```

The normal native test uses its own taskbar/rebar fixture, never empties the Recycle Bin, and checks the accessible button role. `--open-bin` additionally invokes the fixture button's real Open action. `--shell-probe` reads the actual shell's toolbar visibility and Recycle Bin bounds. `--shell-show` requests a real EverythingToolbar activation and is only a diagnostic command.

The settings smoke check exercises every visible category, the hidden legacy tabs, search, search restoration, and monitor bounds. It also saves native render previews beside the executable. The behavior check redirects the process's HKCU access to a temporary registry fixture, verifies keyboard editing, Save, Cancel/discard, and reopen persistence, then removes that fixture.

The smoke check also verifies sidebar font/width against the window DPI and checks that menu preview images do not overlap the controls below them. Add `--unaware`, `--thread-unaware`, or `--gdi-scaled` after `--smoke` to exercise additional DPI contexts. `--inspect-window <HWND>` runs the same scale/overlap assertions against an already-open installed settings window without editing its values. This live check reproduced the Explorer-hosted failure (27px sidebar font in a 96 DPI window); after the correction it passed with a 13px font in the same Explorer process. Settings presentation and preview bitmap selection use window DPI rather than the host process's cached display DPI.

Upstream has no automated settings UI suite. Native component builds and these executable integration checks are the affected regression coverage. Building the complete solution's MSI bootstrapper additionally requires its generated Setup32/64/ARM64 MSI payloads and checksum; those are absent from the source checkout. The local binary installation does not require an MSI.
