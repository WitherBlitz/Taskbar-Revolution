#pragma once

#include <string>
#include <vector>

// The red Start button shows the Start menu of a PC whose RemoteApps are open on this desktop.
// Each remote PC's menu is a folder of shortcuts: %LOCALAPPDATA%\RemoteAppLaunchers\<PC>\Start Menu
// (built by Src\RemoteStart\Build-RemoteStartMenu.ps1).

bool IsRemoteAppSessionActive( void ); // a RemoteApp window from another PC is on screen
bool HasRemoteStartMenus( void );
void ShowRemoteStartMenu( HWND owner, const RECT &button );
bool HandleRemoteStartMenuMessage( HWND owner, UINT message, WPARAM wParam, LPARAM lParam ); // fills submenus as they open

// Exposed for tests
namespace RemoteStartLogic
{
	struct Entry { std::wstring name, path; bool folder; };
	std::vector<Entry> ListFolder( const std::wstring &folder ); // folders and shortcuts, sorted by name like Windows 11's All apps
	std::vector<std::pair<std::wstring,std::wstring>> FindMenus( const std::wstring &launchers ); // (PC name, menu folder)
}
