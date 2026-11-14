#include "stdafx.h"
#include "RemoteStartMenu.h"
#include "ResourceHelper.h"
#include <algorithm>
#include <map>

namespace RemoteStartLogic
{

std::vector<Entry> ListFolder( const std::wstring &folder )
{
	std::vector<Entry> entries;
	WIN32_FIND_DATA data;
	HANDLE find=FindFirstFile((folder+L"\\*").c_str(),&data);
	if (find==INVALID_HANDLE_VALUE) return entries;
	do
	{
		std::wstring name=data.cFileName;
		if (name==L"." || name==L".." || (data.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))) continue;
		bool isFolder=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
		if (!isFolder)
		{
			size_t dot=name.rfind(L'.');
			if (dot==std::wstring::npos || _wcsicmp(name.c_str()+dot,L".lnk")!=0) continue;
			name.resize(dot);
		}
		Entry entry={name,folder+L"\\"+data.cFileName,isFolder};
		entries.push_back(entry);
	} while (FindNextFile(find,&data));
	FindClose(find);
	std::sort(entries.begin(),entries.end(),[]( const Entry &a, const Entry &b )
	{
		return CompareString(LOCALE_USER_DEFAULT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,a.name.c_str(),-1,b.name.c_str(),-1)==CSTR_LESS_THAN;
	});
	return entries;
}

std::vector<std::pair<std::wstring,std::wstring>> FindMenus( const std::wstring &launchers )
{
	std::vector<std::pair<std::wstring,std::wstring>> menus;
	for (const Entry &entry:ListFolder(launchers))
	{
		std::wstring menu=entry.path+L"\\Start Menu";
		DWORD attributes=GetFileAttributes(menu.c_str());
		if (entry.folder && attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY))
			menus.push_back(std::make_pair(entry.name,menu));
	}
	return menus;
}

} // namespace RemoteStartLogic

using namespace RemoteStartLogic;

static std::map<HMENU,std::wstring> s_PendingFolders; // submenus filled when they open
static std::vector<std::wstring> s_Commands; // command id-1 -> shortcut
static std::vector<HBITMAP> s_Bitmaps;

static std::wstring LaunchersFolder( void )
{
	wchar_t path[_MAX_PATH];
	if (FAILED(SHGetFolderPath(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,path))) return std::wstring();
	return std::wstring(path)+L"\\RemoteAppLaunchers";
}

bool IsRemoteAppSessionActive( void )
{
	for (HWND window=FindWindowEx(NULL,NULL,L"RAIL_WINDOW",NULL);window;window=FindWindowEx(NULL,window,L"RAIL_WINDOW",NULL))
		if (IsWindowVisible(window))
			return true;
	return false;
}

bool HasRemoteStartMenus( void )
{
	return !FindMenus(LaunchersFolder()).empty();
}

static HBITMAP IconBitmap( const std::wstring &path )
{
	SHFILEINFO info={0};
	if (!SHGetFileInfo(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON) || !info.hIcon) return NULL;
	HBITMAP bitmap=BitmapFromIcon(info.hIcon,GetSystemMetrics(SM_CXSMICON),NULL,true);
	if (bitmap) s_Bitmaps.push_back(bitmap);
	return bitmap;
}

static void AddItem( HMENU menu, const std::wstring &name, const std::wstring &path, bool folder )
{
	std::wstring text;
	for (wchar_t c:name)
	{
		if (c==L'&') text+=L'&'; // show "&" instead of underlining the next letter
		text+=c;
	}
	MENUITEMINFO item={sizeof(item),MIIM_STRING|MIIM_BITMAP};
	item.dwTypeData=const_cast<wchar_t*>(text.c_str());
	item.hbmpItem=IconBitmap(path);
	if (folder)
	{
		item.fMask|=MIIM_SUBMENU;
		item.hSubMenu=CreatePopupMenu();
		s_PendingFolders[item.hSubMenu]=path;
	}
	else
	{
		s_Commands.push_back(path);
		item.fMask|=MIIM_ID;
		item.wID=(UINT)s_Commands.size();
	}
	InsertMenuItem(menu,GetMenuItemCount(menu),TRUE,&item);
}

static void AddFolder( HMENU menu, const std::wstring &folder )
{
	for (const Entry &entry:ListFolder(folder))
		AddItem(menu,entry.name,entry.path,entry.folder);
	if (GetMenuItemCount(menu)==0)
		AppendMenu(menu,MF_STRING|MF_GRAYED,0,L"(Empty)");
}

bool HandleRemoteStartMenuMessage( HWND owner, UINT message, WPARAM wParam, LPARAM lParam )
{
	if (message!=WM_INITMENUPOPUP) return false;
	std::map<HMENU,std::wstring>::iterator it=s_PendingFolders.find((HMENU)wParam);
	if (it==s_PendingFolders.end()) return false;
	std::wstring folder=it->second;
	s_PendingFolders.erase(it);
	AddFolder((HMENU)wParam,folder);
	return true;
}

void ShowRemoteStartMenu( HWND owner, const RECT &button )
{
	std::vector<std::pair<std::wstring,std::wstring>> menus=FindMenus(LaunchersFolder());
	HMENU menu=CreatePopupMenu();
	if (menus.empty())
		AppendMenu(menu,MF_STRING|MF_GRAYED,0,L"No remote Start menu is set up");
	else if (menus.size()==1)
	{
		AppendMenu(menu,MF_STRING|MF_GRAYED,0,(menus[0].first+L" (remote)").c_str());
		AppendMenu(menu,MF_SEPARATOR,0,NULL);
		AddFolder(menu,menus[0].second);
	}
	else
	{
		for (const auto &pc:menus)
			AddItem(menu,pc.first,pc.second,true);
	}

	TPMPARAMS params={sizeof(params),button};
	SetForegroundWindow(owner); // lets the menu close when clicking elsewhere
	int command=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_LEFTALIGN|TPM_BOTTOMALIGN|TPM_VERTICAL,button.left,button.top,owner,&params);
	PostMessage(owner,WM_NULL,0,0);

	std::wstring target;
	if (command>0 && command<=(int)s_Commands.size())
		target=s_Commands[command-1];
	DestroyMenu(menu);
	s_PendingFolders.clear();
	s_Commands.clear();
	for (HBITMAP bitmap:s_Bitmaps) DeleteObject(bitmap);
	s_Bitmaps.clear();

	if (!target.empty())
		ShellExecute(NULL,NULL,target.c_str(),NULL,NULL,SW_SHOWNORMAL);
}
