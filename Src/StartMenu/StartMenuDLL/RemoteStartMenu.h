#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

// A red Start button appears for each PC whose RemoteApp windows are open on this desktop.
// Each PC's Start menu is a folder of shortcuts: %LOCALAPPDATA%\RemoteAppLaunchers\<PC>\Start Menu
// (built by Src\RemoteStart\Build-RemoteStartMenu.ps1). Open-Shell shows it as its own menu.

typedef std::vector<std::pair<std::wstring,std::wstring>> RemoteMenuList; // (PC name, Start Menu folder)

RemoteMenuList ConnectedRemoteMenus( void ); // PCs that RemoteApp windows are connected to and that have a Start menu, by name

// Exposed for tests
namespace RemoteStartLogic
{
	RemoteMenuList FindMenus( const std::wstring &launchers ); // every PC folder that has a Start Menu folder
	std::wstring HostFromRdp( const std::string &contents ); // "full address" without the port
	std::set<std::wstring> HostNames( const std::wstring &pcFolder ); // the PC's folder name and the computers its .rdp files connect to
	// PCs that one of the open connections goes to; addresses are the IP addresses of each PC
	RemoteMenuList ConnectedPcs( const RemoteMenuList &menus, const std::map<std::wstring,std::set<std::wstring>> &addresses, const std::set<std::wstring> &connections );
}
