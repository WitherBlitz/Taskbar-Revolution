// Built without the precompiled header: winsock2.h has to come before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include "RemoteStartMenu.h"
#include <algorithm>
#include <mutex>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"iphlpapi.lib")

namespace RemoteStartLogic
{

RemoteMenuList FindMenus( const std::wstring &launchers )
{
	RemoteMenuList menus;
	WIN32_FIND_DATA data;
	HANDLE find=FindFirstFile((launchers+L"\\*").c_str(),&data);
	if (find==INVALID_HANDLE_VALUE) return menus;
	do
	{
		if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0]==L'.') continue;
		std::wstring menu=launchers+L"\\"+data.cFileName+L"\\Start Menu";
		DWORD attributes=GetFileAttributes(menu.c_str());
		if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY))
			menus.push_back(std::make_pair(std::wstring(data.cFileName),menu));
	} while (FindNextFile(find,&data));
	FindClose(find);
	std::sort(menus.begin(),menus.end(),[]( const std::pair<std::wstring,std::wstring> &a, const std::pair<std::wstring,std::wstring> &b )
	{
		return CompareString(LOCALE_USER_DEFAULT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,a.first.c_str(),-1,b.first.c_str(),-1)==CSTR_LESS_THAN;
	});
	return menus;
}

std::wstring HostFromRdp( const std::string &contents )
{
	std::wstring text;
	bool bom=contents.size()>=2 && (unsigned char)contents[0]==0xFF && (unsigned char)contents[1]==0xFE;
	if (bom || (contents.size()>=2 && contents[1]==0))
	{
		size_t start=bom?2:0;
		text.assign((const wchar_t*)(contents.data()+start),(contents.size()-start)/2);
	}
	else if (!contents.empty())
	{
		int length=MultiByteToWideChar(CP_UTF8,0,contents.data(),(int)contents.size(),NULL,0);
		text.resize(length);
		if (length) MultiByteToWideChar(CP_UTF8,0,contents.data(),(int)contents.size(),&text[0],length);
	}
	for (size_t start=0;start<text.size();)
	{
		size_t end=text.find(L'\n',start);
		if (end==std::wstring::npos) end=text.size();
		std::wstring line=text.substr(start,end-start);
		start=end+1;
		while (!line.empty() && iswspace(line.back())) line.pop_back();
		if (line.size()>15 && _wcsnicmp(line.c_str(),L"full address:s:",15)==0)
		{
			std::wstring host=line.substr(15);
			size_t colon=host.rfind(L':');
			if (colon!=std::wstring::npos && host.find(L':')==colon) host.resize(colon); // drop the port
			return host;
		}
	}
	return std::wstring();
}

static std::string ReadSmallFile( const std::wstring &path )
{
	std::string contents;
	HANDLE file=CreateFile(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
	if (file==INVALID_HANDLE_VALUE) return contents;
	DWORD size=GetFileSize(file,NULL), read=0;
	if (size!=INVALID_FILE_SIZE && size<65536)
	{
		contents.resize(size);
		if (!ReadFile(file,&contents[0],size,&read,NULL)) read=0;
		contents.resize(read);
	}
	CloseHandle(file);
	return contents;
}

std::set<std::wstring> HostNames( const std::wstring &pcFolder )
{
	std::set<std::wstring> names;
	size_t slash=pcFolder.find_last_of(L"\\/");
	names.insert(slash==std::wstring::npos?pcFolder:pcFolder.substr(slash+1));
	WIN32_FIND_DATA data;
	HANDLE find=FindFirstFile((pcFolder+L"\\*.rdp").c_str(),&data);
	if (find!=INVALID_HANDLE_VALUE)
	{
		do
		{
			std::wstring host=HostFromRdp(ReadSmallFile(pcFolder+L"\\"+data.cFileName));
			if (!host.empty()) names.insert(host);
		} while (FindNextFile(find,&data));
		FindClose(find);
	}
	return names;
}

RemoteMenuList ConnectedPcs( const RemoteMenuList &menus, const std::map<std::wstring,std::set<std::wstring>> &addresses, const std::set<std::wstring> &connections )
{
	RemoteMenuList connected;
	for (const auto &menu:menus)
	{
		std::map<std::wstring,std::set<std::wstring>>::const_iterator it=addresses.find(menu.first);
		if (it==addresses.end()) continue;
		for (const std::wstring &address:it->second)
			if (connections.count(address))
			{
				connected.push_back(menu);
				break;
			}
	}
	return connected;
}

} // namespace RemoteStartLogic

using namespace RemoteStartLogic;

static std::wstring LaunchersFolder( void )
{
	wchar_t path[_MAX_PATH];
	if (FAILED(SHGetFolderPath(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,path))) return std::wstring();
	return std::wstring(path)+L"\\RemoteAppLaunchers";
}

///////////////////////////////////////////////////////////////////////////////
// IP addresses of each PC, looked up in the background (a lookup can take seconds)

static std::mutex s_AddressLock;
static std::map<std::wstring,std::set<std::wstring>> s_Addresses; // PC name -> IP addresses
static std::map<std::wstring,std::set<std::wstring>> s_LookupNames; // what s_Addresses was looked up from
static DWORD s_LookupTime;
static bool s_LookupRunning;

static std::wstring AddressText( const sockaddr *address )
{
	wchar_t text[64]={0};
	if (address->sa_family==AF_INET)
		InetNtop(AF_INET,&((const sockaddr_in*)address)->sin_addr,text,_countof(text));
	else if (address->sa_family==AF_INET6)
		InetNtop(AF_INET6,&((const sockaddr_in6*)address)->sin6_addr,text,_countof(text));
	return text;
}

static DWORD CALLBACK LookupThread( void *param )
{
	std::map<std::wstring,std::set<std::wstring>> *names=(std::map<std::wstring,std::set<std::wstring>>*)param;
	std::map<std::wstring,std::set<std::wstring>> addresses;
	WSADATA wsa;
	bool started=WSAStartup(MAKEWORD(2,2),&wsa)==0;
	for (const auto &pc:*names)
	{
		std::set<std::wstring> &found=addresses[pc.first];
		for (const std::wstring &name:pc.second)
		{
			ADDRINFOW hints={0}, *result=NULL;
			hints.ai_family=AF_UNSPEC;
			hints.ai_socktype=SOCK_STREAM;
			if (GetAddrInfoW(name.c_str(),NULL,&hints,&result)==0)
			{
				for (ADDRINFOW *entry=result;entry;entry=entry->ai_next)
					found.insert(AddressText(entry->ai_addr));
				FreeAddrInfoW(result);
			}
		}
	}
	if (started) WSACleanup();
	{
		std::lock_guard<std::mutex> lock(s_AddressLock);
		s_Addresses.swap(addresses);
		s_LookupTime=GetTickCount();
		s_LookupRunning=false;
	}
	delete names;
	HMODULE module=NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&LookupThread,&module);
	FreeLibraryAndExitThread(module,0); // releases the reference taken when the thread started
}

static void UpdateAddresses( const RemoteMenuList &menus )
{
	std::map<std::wstring,std::set<std::wstring>> names;
	for (const auto &menu:menus)
	{
		std::wstring folder=menu.second.substr(0,menu.second.find_last_of(L'\\'));
		names[menu.first]=HostNames(folder);
	}
	{
		std::lock_guard<std::mutex> lock(s_AddressLock);
		bool fresh=s_LookupTime && GetTickCount()-s_LookupTime<5*60*1000;
		if (s_LookupRunning || (fresh && names==s_LookupNames)) return;
		s_LookupRunning=true;
		s_LookupNames=names;
	}
	HMODULE module=NULL;
	std::map<std::wstring,std::set<std::wstring>> *param=new std::map<std::wstring,std::set<std::wstring>>(names);
	HANDLE thread=NULL;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)&LookupThread,&module)) // keeps the DLL loaded while the thread runs
		thread=CreateThread(NULL,0,LookupThread,param,0,NULL);
	if (thread)
		CloseHandle(thread);
	else
	{
		if (module) FreeLibrary(module);
		delete param;
		std::lock_guard<std::mutex> lock(s_AddressLock);
		s_LookupRunning=false;
	}
}

// Remote addresses of the connections that RemoteApp clients have open
static std::set<std::wstring> ClientConnections( const std::set<DWORD> &clients )
{
	std::set<std::wstring> connections;
	for (ULONG family:{AF_INET,AF_INET6})
	{
		DWORD size=0;
		GetExtendedTcpTable(NULL,&size,FALSE,family,TCP_TABLE_OWNER_PID_CONNECTIONS,0);
		std::vector<BYTE> buffer(size+1024);
		size=(DWORD)buffer.size();
		if (GetExtendedTcpTable(buffer.data(),&size,FALSE,family,TCP_TABLE_OWNER_PID_CONNECTIONS,0)!=NO_ERROR) continue;
		if (family==AF_INET)
		{
			const MIB_TCPTABLE_OWNER_PID *table=(const MIB_TCPTABLE_OWNER_PID*)buffer.data();
			for (DWORD i=0;i<table->dwNumEntries;i++)
				if (table->table[i].dwState==MIB_TCP_STATE_ESTAB && clients.count(table->table[i].dwOwningPid))
				{
					sockaddr_in address={AF_INET};
					address.sin_addr.s_addr=table->table[i].dwRemoteAddr;
					connections.insert(AddressText((const sockaddr*)&address));
				}
		}
		else
		{
			const MIB_TCP6TABLE_OWNER_PID *table=(const MIB_TCP6TABLE_OWNER_PID*)buffer.data();
			for (DWORD i=0;i<table->dwNumEntries;i++)
				if (table->table[i].dwState==MIB_TCP_STATE_ESTAB && clients.count(table->table[i].dwOwningPid))
				{
					sockaddr_in6 address={AF_INET6};
					memcpy(&address.sin6_addr,table->table[i].ucRemoteAddr,sizeof(address.sin6_addr));
					connections.insert(AddressText((const sockaddr*)&address));
				}
		}
	}
	return connections;
}

RemoteMenuList ConnectedRemoteMenus( void )
{
	std::set<DWORD> clients; // processes showing RemoteApp windows
	for (HWND window=FindWindowEx(NULL,NULL,L"RAIL_WINDOW",NULL);window;window=FindWindowEx(NULL,window,L"RAIL_WINDOW",NULL))
	{
		DWORD pid=0;
		if (IsWindowVisible(window) && GetWindowThreadProcessId(window,&pid) && pid)
			clients.insert(pid);
	}
	if (clients.empty()) return RemoteMenuList();
	RemoteMenuList menus=FindMenus(LaunchersFolder());
	if (menus.empty()) return RemoteMenuList();
	UpdateAddresses(menus);
	std::set<std::wstring> connections=ClientConnections(clients);
	std::lock_guard<std::mutex> lock(s_AddressLock);
	return ConnectedPcs(menus,s_Addresses,connections);
}
