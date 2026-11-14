# Open-Shell desktop customizations

This fork adds a redesigned settings interface and compact taskbar controls to [Open-Shell](https://github.com/Open-Shell/Open-Shell-Menu). It is based on upstream **v4.4.198** and preserves the upstream history and MIT license.

## Changes in this fork

- Dark sidebar navigation, rounded settings rows, switches, and a white Save button, with formatting inspired by [Top Bar Revolution](https://github.com/WitherBlitz/Top-Bar-Revolution).
- Shared settings presentation for the Start Menu, Classic Explorer, and Classic IE components, while preserving the existing editors, settings search, backup, and Save/Cancel behavior.
- A compact native Recycle Bin taskbar button: click to open the bin; right-click for **Open** and **Empty**. Empty retains Windows' confirmation.
- Controls for the existing [EverythingToolbar](https://github.com/srwi/EverythingToolbar) deskband's visibility and width. EverythingToolbar must already be installed and registered.
- Optional removal of the classic taskbar's resize grips, including after Explorer refreshes its toolbar styles.
- **End task** on taskbar buttons: right-click a running app and an End task row appears next to its menu. It force-ends the app and its child processes; apps running as administrator ask for approval first. Ending File Explorer restarts the shell.
- **Red Start buttons for remote PCs**: for each PC whose RemoteApp windows are open, a red Start button appears right of Start (one per PC, by name) and opens that PC's own Open-Shell Start menu in your Start menu's style: its programs list, a search box that searches only that PC, and an item for everything on it, with no power button or local folders. Picking an item opens it on that PC as a RemoteApp. A PC counts as connected when a RemoteApp client has a connection open to its address. Setup per PC is in [Src/RemoteStart](Src/RemoteStart): publish `RemoteStart.exe` as a RemoteApp on the PC, run `Export-RemoteStartMenu.ps1` there, then `Build-RemoteStartMenu.ps1` here to build `%LOCALAPPDATA%\RemoteAppLaunchers\<PC>\Start Menu`.
- Typing while the Start menu is open always goes to the search box, so letters no longer trigger items such as Sleep or Restart. Alt+letter still uses the item shortcuts.
- Window-based DPI sizing so Explorer-hosted settings use the same scale as their native controls and preview images.

The taskbar integrations target the classic Windows 10 taskbar. They run inside Open-Shell's Explorer DLL and use its existing startup mechanism.

## Build and verification

See [native build and test instructions](tests/README.md). The fork includes executable widget tests and settings UI checks for navigation, DPI sizing, preview overlap, keyboard editing, and Save/Cancel persistence.

The customization branch is `desktop-customizations`. Keep `upstream` pointed at `https://github.com/Open-Shell/Open-Shell-Menu.git` and use `origin` for this fork when cloning for development.

The upstream download links below contain upstream binaries; they do not include this fork's changes. A signed installer for this fork has not been published.

## Upstream project documentation

<a href="#"><img src=/Src/Setup/OpenShell.ico width="80" align="left"/></a>


# Open-Shell

A collection of utilities bringing back classic features to Windows.

*Originally* **[Classic Shell](http://www.classicshell.net)** *by [Ivo Beltchev](https://sourceforge.net/u/ibeltchev/profile/)*

[![GitHub Release](https://img.shields.io/github/release/Open-Shell/Open-Shell-Menu.svg?style=flat-square)](https://github.com/Open-Shell/Open-Shell-Menu/releases/latest)&nbsp;&nbsp;[![GitHub Pre-Release](https://img.shields.io/github/release/Open-Shell/Open-Shell-Menu/all.svg?style=flat-square)](https://github.com/Open-Shell/Open-Shell-Menu/releases)&nbsp;&nbsp;[![Build status](https://img.shields.io/appveyor/build/passionate-coder/Open-Shell-Menu?logo=appveyor&style=flat-square)](https://ci.appveyor.com/project/passionate-coder/open-shell-menu/branch/master)&nbsp;&nbsp;[![GitQ](https://img.shields.io/badge/gitq-discussions-1577fa?style=flat-square)](https://gitq.com/passionate-coder/Classic-Start)&nbsp;&nbsp;[![Gitter chat](https://img.shields.io/gitter/room/badges/shields.svg?color=lightseagreen&logo=gitter&style=flat-square)](https://gitter.im/open-shell/Lobby)&nbsp;&nbsp;[![Discord](https://img.shields.io/discord/757701054782636082?color=mediumslateblue&label=Discord&logo=discord&logoColor=white&style=flat-square)](https://discord.gg/7H6arr5)

[Open-Shell Homepage](https://open-shell.github.io/Open-Shell-Menu)  

### Features
- Classic style Start menu for Windows 7, 8, 8.1, 10, and 11
- Toolbar for Windows Explorer
- Explorer status bar with file size and disk space
- Classic copy UI (Windows 7 only)
- Title bar and status bar for Internet Explorer

### Download
You can find the latest stable version here:

[![GitHub All Releases](https://img.shields.io/github/downloads/Open-Shell/Open-Shell-Menu/total?style=for-the-badge&color=4bc2ee&logo=github)](https://github.com/Open-Shell/Open-Shell-Menu/releases/latest)

> [!IMPORTANT]
> #### Windows for ARM compatibility
> Open-Shell is compatible with Windows for ARM since version [4.4.196](https://github.com/Open-Shell/Open-Shell-Menu/releases/tag/v4.4.196).
>
> If you install older one on a Windows for ARM installation (ex. using Parallels Desktop on an Apple Silicon Mac), you will no longer be able to log into your account the next time you reboot. Please refrain from installing Open-Shell on Windows for ARM.

### Temporary Translation/Language Solution
1. Download [language DLL](https://coddec.github.io/Classic-Shell/www.classicshell.net/translations/index.html)  
2. Place it either in the Open-Shell's __install folder__ or in the `%ALLUSERSPROFILE%\OpenShell\Languages` folder

----

*For archival reasons, we have a mirror of `www.classicshell.net` [here](https://coddec.github.io/Classic-Shell/www.classicshell.net/).*

[How To Skin a Start Menu](https://coddec.github.io/Classic-Shell/www.classicshell.net/tutorials/skintutorial.html)  
[Classic Shell: Custom Start Buttons](https://coddec.github.io/Classic-Shell/www.classicshell.net/tutorials/buttontutorial.html)  
[Questions? Ask on the Discussions section](https://github.com/Open-Shell/Open-Shell-Menu/discussions) or on [Discord](https://discord.gg/7H6arr5)  
[Submit a bug report/feature request](https://github.com/Open-Shell/Open-Shell-Menu/issues)
