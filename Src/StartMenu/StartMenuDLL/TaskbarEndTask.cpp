// End task for the classic Windows 10 taskbar.
// Right-clicking a running taskbar button shows an "End task" row next to the taskbar menu.
// Clicking it force-ends the app's process tree (no save prompt). The clicked button is
// identified through its accessible name and matched against the open top-level windows.

#include "stdafx.h"
#include "TaskbarEndTask.h"
#include "ResourceHelper.h"
#include <oleacc.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <propsys.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <cwctype>

#pragma comment(lib,"version.lib")

///////////////////////////////////////////////////////////////////////////////
// Pure logic

namespace EndTaskLogic
{

static std::wstring Lower( std::wstring value )
{
	for (wchar_t &c:value) c=(wchar_t)towlower(c);
	return value;
}

static std::wstring Normalize( const std::wstring &value )
{
	size_t start=0, end=value.size();
	while (start<end && iswspace(value[start])) start++;
	while (end>start && iswspace(value[end-1])) end--;
	return Lower(value.substr(start,end-start));
}

static bool Same( const std::wstring &a, const std::wstring &b )
{
	return !a.empty() && !b.empty() && Normalize(a)==Normalize(b);
}

static std::wstring ExecutableStem( const std::wstring &value )
{
	std::wstring result=Normalize(value);
	size_t slash=result.find_last_of(L"\\/");
	if (slash!=std::wstring::npos) result.erase(0,slash+1);
	if (result.size()>=4 && result.compare(result.size()-4,4,L".exe")==0) result.resize(result.size()-4);
	return result;
}

static std::wstring AppIdTail( const std::wstring &value )
{
	std::wstring result=Normalize(value);
	size_t separator=result.find_last_of(L"!.\\/");
	if (separator!=std::wstring::npos && separator+1<result.size()) result.erase(0,separator+1);
	return result;
}

// "Chrome - 3 running windows" -> "Chrome"
std::wstring NormalizeTaskbarName( const std::wstring &accessibleName )
{
	std::wstring result=accessibleName;
	while (!result.empty() && iswspace(result.back())) result.pop_back();
	size_t separator=result.rfind(L" - ");
	if (separator==std::wstring::npos) return result;
	std::wstring suffix=Lower(result.substr(separator+3));
	size_t digits=0;
	while (digits<suffix.size() && iswdigit(suffix[digits])) digits++;
	if (digits==0) return result;
	std::wstring words=suffix.substr(digits);
	if (words!=L" running window" && words!=L" running windows") return result;
	result.resize(separator);
	while (!result.empty() && iswspace(result.back())) result.pop_back();
	return result;
}

int ScoreWindow( const Evidence &evidence )
{
	int score=0;
	std::wstring name=Normalize(evidence.taskbarName);
	if (Same(evidence.taskbarName,evidence.windowTitle)) score=max(score,100);
	if (Same(evidence.processPath,evidence.pinnedPath)) score=max(score,95);
	if (Same(evidence.taskbarName,evidence.relaunchName)) score=max(score,90);
	if (Same(evidence.taskbarName,evidence.fileDescription)) score=max(score,80);
	if (Same(evidence.taskbarName,evidence.productName)) score=max(score,75);
	if (!name.empty() && (name==ExecutableStem(evidence.processBaseName) || name==Normalize(evidence.processBaseName))) score=max(score,70);
	if ((name==L"file explorer" || name==L"windows explorer") && ExecutableStem(evidence.processBaseName)==L"explorer") score=max(score,70);
	if (!name.empty() && name==AppIdTail(evidence.appId)) score=max(score,60);
	return score;
}

DWORD ChooseTargetPid( const std::vector<Candidate> &candidates )
{
	std::map<DWORD,std::pair<int,bool>> byPid; // pid -> best score, has foreground window
	for (const Candidate &candidate:candidates)
	{
		if (candidate.pid==0 || candidate.score<=0) continue;
		std::map<DWORD,std::pair<int,bool>>::iterator it=byPid.find(candidate.pid);
		if (it==byPid.end())
			byPid[candidate.pid]=std::make_pair(candidate.score,candidate.foreground);
		else
		{
			it->second.first=max(it->second.first,candidate.score);
			it->second.second=it->second.second || candidate.foreground;
		}
	}
	int best=0;
	for (const auto &entry:byPid) best=max(best,entry.second.first);
	DWORD onlyBest=0, foregroundBest=0;
	int bestCount=0, foregroundCount=0;
	for (const auto &entry:byPid)
	{
		if (entry.second.first!=best) continue;
		bestCount++; onlyBest=entry.first;
		if (entry.second.second) { foregroundCount++; foregroundBest=entry.first; }
	}
	if (bestCount==1) return onlyBest;
	if (foregroundCount==1) return foregroundBest;
	return 0;
}

static bool LooksLikeTaskbarMenu( const RECT &bounds, const RECT &workArea, int clickX, int dpi )
{
	int width=bounds.right-bounds.left, height=bounds.bottom-bounds.top;
	return width>=MulDiv(120,dpi,96) && width<=MulDiv(640,dpi,96) && height>=MulDiv(20,dpi,96) && height<=MulDiv(800,dpi,96)
		&& abs(workArea.bottom-bounds.bottom)<=MulDiv(16,dpi,96)
		&& clickX>=bounds.left-MulDiv(32,dpi,96) && clickX<=bounds.right+MulDiv(32,dpi,96);
}

// Candidates are in z-order; menus with a known class win over other windows that fit
bool SelectMenuRect( const std::vector<MenuCandidate> &candidates, const RECT &workArea, int clickX, int dpi, RECT &result, int *index )
{
	for (int pass=0;pass<2;pass++)
		for (size_t i=0;i<candidates.size();i++)
			if ((pass==1 || candidates[i].knownClass) && LooksLikeTaskbarMenu(candidates[i].bounds,workArea,clickX,dpi))
			{
				result=candidates[i].bounds;
				if (index) *index=(int)i;
				return true;
			}
	return false;
}

RECT FallbackMenuRect( int clickX, const RECT &workArea, int dpi )
{
	int width=min(MulDiv(256,dpi,96),max(0,(int)(workArea.right-workArea.left)));
	int left=clickX-width/2;
	left=max((int)workArea.left,min(left,max((int)workArea.left,(int)workArea.right-width)));
	RECT rect={left,workArea.bottom-MulDiv(100,dpi,96),left+width,workArea.bottom};
	return rect;
}

// The row goes directly above the menu, or below it when there is no room above
RECT PlaceRow( const RECT &menu, const RECT &workArea, int rowHeight )
{
	const int gap=2;
	int width=min(max(0,(int)(workArea.right-workArea.left)),max(0,(int)(menu.right-menu.left)));
	int left=max((int)workArea.left,min((int)menu.left,max((int)workArea.left,(int)workArea.right-width)));
	int top=menu.top-rowHeight-gap;
	if (top<workArea.top) top=menu.bottom+gap;
	top=max((int)workArea.top,min(top,max((int)workArea.top,(int)workArea.bottom-rowHeight)));
	RECT rect={left,top,left+width,top+rowHeight};
	return rect;
}

std::vector<DWORD> TerminationOrder( const std::vector<std::pair<DWORD,DWORD>> &processParents, DWORD root )
{
	std::vector<DWORD> order;
	std::set<DWORD> visiting, visited;
	std::function<void(DWORD)> visit=[&]( DWORD pid )
	{
		if (pid==0 || visiting.count(pid) || visited.count(pid)) return;
		visiting.insert(pid);
		for (const auto &process:processParents)
			if (process.second==pid && process.first!=pid)
				visit(process.first);
		visiting.erase(pid);
		visited.insert(pid);
		order.push_back(pid);
	};
	visit(root);
	return order;
}

bool IsProtectedProcess( const wchar_t *baseName )
{
	static const wchar_t *names[]={L"csrss.exe",L"wininit.exe",L"winlogon.exe",L"lsass.exe",L"services.exe",L"smss.exe",L"dwm.exe"};
	for (const wchar_t *name:names)
		if (_wcsicmp(baseName,name)==0)
			return true;
	return false;
}

} // namespace EndTaskLogic

using namespace EndTaskLogic;

///////////////////////////////////////////////////////////////////////////////
// Processes

struct EndTaskTarget
{
	DWORD pid;
	ULONGLONG created; // guards against the pid being reused before the click
};

static ULONGLONG FileTimeValue( const FILETIME &time )
{
	return (((ULONGLONG)time.dwHighDateTime)<<32)|time.dwLowDateTime;
}

static bool ReadIdentity( DWORD pid, EndTaskTarget &target )
{
	HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
	if (!process) return false;
	FILETIME creation, exitTime, kernel, user;
	bool ok=GetProcessTimes(process,&creation,&exitTime,&kernel,&user)!=FALSE;
	CloseHandle(process);
	if (!ok) return false;
	target.pid=pid;
	target.created=FileTimeValue(creation);
	return target.created!=0;
}

static std::wstring ProcessPath( DWORD pid )
{
	HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
	if (!process) return std::wstring();
	wchar_t path[_MAX_PATH*4];
	DWORD size=_countof(path);
	bool ok=QueryFullProcessImageName(process,0,path,&size)!=FALSE;
	CloseHandle(process);
	return ok?std::wstring(path,size):std::wstring();
}

static bool IsAllowedTarget( const EndTaskTarget &target )
{
	if (target.pid==0 || target.pid==4 || target.created==0) return false;
	std::wstring path=ProcessPath(target.pid);
	return !path.empty() && !IsProtectedProcess(PathFindFileName(path.c_str()));
}

static DWORD TerminateTree( const EndTaskTarget &root )
{
	EndTaskTarget current;
	if (!ReadIdentity(root.pid,current) || current.created!=root.created)
		return ERROR_NOT_FOUND;

	HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
	if (snapshot==INVALID_HANDLE_VALUE) return GetLastError();
	std::vector<std::pair<DWORD,DWORD>> parents;
	std::map<DWORD,std::wstring> names;
	PROCESSENTRY32 entry={sizeof(entry)};
	for (BOOL ok=Process32First(snapshot,&entry);ok;ok=Process32Next(snapshot,&entry))
	{
		parents.push_back(std::make_pair(entry.th32ProcessID,entry.th32ParentProcessID));
		names[entry.th32ProcessID]=entry.szExeFile;
	}
	CloseHandle(snapshot);

	DWORD firstError=ERROR_SUCCESS;
	for (DWORD pid:TerminationOrder(parents,root.pid))
	{
		bool isRoot=(pid==root.pid);
		if (pid==4 || pid==GetCurrentProcessId() || IsProtectedProcess(names[pid].c_str()))
		{
			if (isRoot) return ERROR_ACCESS_DENIED;
			continue;
		}
		HANDLE process=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
		if (!process)
		{
			DWORD error=GetLastError();
			if (isRoot) return error;
			if (firstError==ERROR_SUCCESS && error!=ERROR_INVALID_PARAMETER) firstError=error;
			continue;
		}
		FILETIME creation, exitTime, kernel, user;
		if (isRoot && (!GetProcessTimes(process,&creation,&exitTime,&kernel,&user) || FileTimeValue(creation)!=root.created))
		{
			CloseHandle(process);
			return ERROR_NOT_FOUND;
		}
		if (!TerminateProcess(process,1))
		{
			DWORD error=GetLastError();
			CloseHandle(process);
			if (isRoot) return error;
			if (firstError==ERROR_SUCCESS) firstError=error;
			continue;
		}
		CloseHandle(process);
	}
	return firstError;
}

// Ending the shell (a File Explorer button) ends this taskbar too, so a separate process does it and starts the shell again
static void RestartShell( DWORD pid )
{
	wchar_t system[_MAX_PATH], windows[_MAX_PATH], command[_MAX_PATH*3];
	if (!GetSystemDirectory(system,_countof(system)) || !GetWindowsDirectory(windows,_countof(windows))) return;
	swprintf_s(command,L"\"%s\\cmd.exe\" /c \"\"%s\\taskkill.exe\" /F /PID %u & start \"\" \"%s\\explorer.exe\"\"",system,system,pid,windows);
	STARTUPINFO startup={sizeof(startup)};
	PROCESS_INFORMATION info;
	if (CreateProcess(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&info))
	{
		CloseHandle(info.hThread);
		CloseHandle(info.hProcess);
	}
}

// For apps running as administrator: Windows asks for approval, then taskkill ends them
static void EndElevated( DWORD pid )
{
	wchar_t parameters[64];
	swprintf_s(parameters,L"/F /T /PID %u",pid);
	SHELLEXECUTEINFO info={sizeof(info)};
	info.fMask=SEE_MASK_NOASYNC;
	info.lpVerb=L"runas";
	info.lpFile=L"taskkill.exe";
	info.lpParameters=parameters;
	info.nShow=SW_HIDE;
	ShellExecuteEx(&info);
}

///////////////////////////////////////////////////////////////////////////////
// Finding the app behind a taskbar button

static const PROPERTYKEY KEY_AppUserModelId={{0x9F4C2855,0x9F79,0x4B39,{0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}},5};
static const PROPERTYKEY KEY_RelaunchDisplayName={{0x9F4C2855,0x9F79,0x4B39,{0xA8,0xD0,0xE1,0xD4,0x2D,0xE1,0xD5,0xF3}},4};

static std::wstring VersionString( const std::wstring &path, const wchar_t *name )
{
	DWORD ignored=0;
	DWORD size=GetFileVersionInfoSize(path.c_str(),&ignored);
	if (!size) return std::wstring();
	std::vector<BYTE> data(size);
	if (!GetFileVersionInfo(path.c_str(),0,size,data.data())) return std::wstring();
	struct Translation { WORD language, codePage; } *translations=NULL;
	UINT length=0;
	if (!VerQueryValue(data.data(),L"\\VarFileInfo\\Translation",(void**)&translations,&length) || !translations || length<sizeof(Translation))
		return std::wstring();
	wchar_t query[128];
	swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\%s",translations[0].language,translations[0].codePage,name);
	wchar_t *value=NULL;
	if (!VerQueryValue(data.data(),query,(void**)&value,&length) || !value || length<=1) return std::wstring();
	return value;
}

static std::wstring WindowProperty( HWND window, const PROPERTYKEY &key )
{
	CComPtr<IPropertyStore> store;
	if (FAILED(SHGetPropertyStoreForWindow(window,IID_PPV_ARGS(&store))) || !store) return std::wstring();
	PROPVARIANT value;
	PropVariantInit(&value);
	std::wstring result;
	if (SUCCEEDED(store->GetValue(key,&value)))
	{
		if (value.vt==VT_LPWSTR && value.pwszVal) result=value.pwszVal;
		else if (value.vt==VT_BSTR && value.bstrVal) result.assign(value.bstrVal,SysStringLen(value.bstrVal));
	}
	PropVariantClear(&value);
	if (!result.empty() && result[0]==L'@')
	{
		wchar_t resolved[1024];
		if (SUCCEEDED(SHLoadIndirectString(result.c_str(),resolved,_countof(resolved),NULL)))
			result=resolved;
	}
	return result;
}

// The target of a pinned shortcut whose name matches the button
static std::wstring PinnedTarget( const std::wstring &name )
{
	wchar_t folder[_MAX_PATH];
	if (FAILED(SHGetFolderPath(NULL,CSIDL_APPDATA,NULL,SHGFP_TYPE_CURRENT,folder))) return std::wstring();
	std::wstring directory=std::wstring(folder)+L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\";
	WIN32_FIND_DATA found;
	HANDLE search=FindFirstFile((directory+L"*.lnk").c_str(),&found);
	if (search==INVALID_HANDLE_VALUE) return std::wstring();
	std::wstring shortcut;
	do
	{
		std::wstring stem=found.cFileName;
		stem.resize(stem.size()-4);
		if (Same(stem,name)) { shortcut=directory+found.cFileName; break; }
	} while (FindNextFile(search,&found));
	FindClose(search);
	if (shortcut.empty()) return std::wstring();

	CComPtr<IShellLink> link;
	CComQIPtr<IPersistFile> file;
	wchar_t path[_MAX_PATH*4];
	if (SUCCEEDED(link.CoCreateInstance(CLSID_ShellLink)) && (file=link)!=NULL && SUCCEEDED(file->Load(shortcut.c_str(),STGM_READ))
		&& SUCCEEDED(link->GetPath(path,_countof(path),NULL,SLGP_RAWPATH)))
		return path;
	return std::wstring();
}

static BOOL CALLBACK CollectHostedPid( HWND window, LPARAM param )
{
	std::pair<DWORD,std::set<DWORD>> &search=*(std::pair<DWORD,std::set<DWORD>>*)param;
	DWORD pid=0;
	GetWindowThreadProcessId(window,&pid);
	if (pid && pid!=search.first) search.second.insert(pid);
	return TRUE;
}

// A UWP app's frame belongs to ApplicationFrameHost; the app itself owns a child window
static DWORD HostedPid( HWND window, DWORD host )
{
	std::pair<DWORD,std::set<DWORD>> search(host,std::set<DWORD>());
	EnumChildWindows(window,CollectHostedPid,(LPARAM)&search);
	return search.second.size()==1?*search.second.begin():0;
}

static bool IsTaskWindow( HWND window )
{
	if (!IsWindowVisible(window)) return false;
	BOOL cloaked=FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) return false;
	DWORD exStyle=(DWORD)GetWindowLongPtr(window,GWL_EXSTYLE);
	if ((exStyle&WS_EX_TOOLWINDOW) && !(exStyle&WS_EX_APPWINDOW)) return false;
	return GetWindow(window,GW_OWNER)==NULL || (exStyle&WS_EX_APPWINDOW);
}

struct ResolveSearch
{
	std::wstring name, pinned;
	HWND foreground;
	std::vector<Candidate> candidates;
};

static BOOL CALLBACK CollectCandidate( HWND window, LPARAM param )
{
	ResolveSearch &search=*(ResolveSearch*)param;
	if (!IsTaskWindow(window)) return TRUE;
	DWORD pid=0;
	GetWindowThreadProcessId(window,&pid);
	if (!pid) return TRUE;
	std::wstring path=ProcessPath(pid);
	if (_wcsicmp(PathFindFileName(path.c_str()),L"ApplicationFrameHost.exe")==0)
	{
		pid=HostedPid(window,pid);
		if (!pid) return TRUE;
		path=ProcessPath(pid);
	}
	wchar_t title[512];
	int length=GetWindowText(window,title,_countof(title));

	Evidence evidence;
	evidence.taskbarName=search.name;
	evidence.windowTitle.assign(title,max(length,0));
	evidence.processBaseName=PathFindFileName(path.c_str());
	evidence.processPath=path;
	evidence.pinnedPath=search.pinned;
	evidence.fileDescription=VersionString(path,L"FileDescription");
	evidence.productName=VersionString(path,L"ProductName");
	evidence.appId=WindowProperty(window,KEY_AppUserModelId);
	evidence.relaunchName=WindowProperty(window,KEY_RelaunchDisplayName);
	int score=ScoreWindow(evidence);
	if (score>0)
	{
		Candidate candidate={pid,score,window==search.foreground};
		search.candidates.push_back(candidate);
	}
	return TRUE;
}

static bool ResolveTarget( const std::wstring &name, HWND foreground, EndTaskTarget &target )
{
	ResolveSearch search;
	search.name=name;
	search.pinned=PinnedTarget(name);
	search.foreground=foreground;
	EnumWindows(CollectCandidate,(LPARAM)&search);
	DWORD pid=ChooseTargetPid(search.candidates);
	if (!pid)
	{
		std::wstring lower=Normalize(name);
		if (lower==L"file explorer" || lower==L"windows explorer")
			GetWindowThreadProcessId(GetShellWindow(),&pid);
	}
	return pid && ReadIdentity(pid,target);
}

// Hit-tests the task list's accessible buttons; running apps are push buttons with a popup
static bool HitTestRunningButton( HWND taskList, POINT point, std::wstring &name, RECT &bounds )
{
	CComPtr<IAccessible> root;
	if (FAILED(AccessibleObjectFromWindow(taskList,OBJID_CLIENT,IID_PPV_ARGS(&root))) || !root) return false;
	CComVariant hit;
	if (FAILED(root->accHitTest(point.x,point.y,&hit))) return false;
	CComPtr<IAccessible> accessible;
	CComVariant child;
	if (hit.vt==VT_I4)
	{
		accessible=root;
		child=hit.lVal;
	}
	else if (hit.vt==VT_DISPATCH && hit.pdispVal)
	{
		if (FAILED(hit.pdispVal->QueryInterface(IID_PPV_ARGS(&accessible))) || !accessible) return false;
		child=(long)CHILDID_SELF;
	}
	else
		return false;

	CComVariant role, state;
	CComBSTR text;
	long left, top, width, height;
	if (FAILED(accessible->get_accRole(child,&role)) || role.vt!=VT_I4 || role.lVal!=ROLE_SYSTEM_PUSHBUTTON) return false;
	if (FAILED(accessible->get_accState(child,&state)) || state.vt!=VT_I4 || !(state.lVal&STATE_SYSTEM_HASPOPUP) || (state.lVal&STATE_SYSTEM_UNAVAILABLE)) return false;
	if (FAILED(accessible->get_accName(child,&text)) || !text || !text[0]) return false;
	if (FAILED(accessible->accLocation(&left,&top,&width,&height,child)) || width<=0 || height<=0) return false;
	name.assign(text,text.Length());
	SetRect(&bounds,left,top,left+width,top+height);
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Background work

typedef void (*WorkerProc)( void *param );
struct WorkerStart { WorkerProc proc; void *param; HMODULE module; };

static DWORD CALLBACK WorkerThread( void *param )
{
	WorkerStart start=*(WorkerStart*)param;
	delete (WorkerStart*)param;
	HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
	start.proc(start.param);
	if (SUCCEEDED(hr)) CoUninitialize();
	FreeLibraryAndExitThread(start.module,0); // the thread holds the DLL so it can't unload under it
}

static bool StartWorker( WorkerProc proc, void *param )
{
	HMODULE module=NULL;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)&WorkerThread,&module)) return false;
	WorkerStart *start=new WorkerStart{proc,param,module};
	HANDLE thread=CreateThread(NULL,0,WorkerThread,start,0,NULL);
	if (!thread)
	{
		delete start;
		FreeLibrary(module);
		return false;
	}
	CloseHandle(thread);
	return true;
}

static const UINT WM_ENDTASK_RESOLVED=WM_APP+0x1E7;

struct ResolveRequest
{
	HWND overlay;
	LONG request;
	std::wstring name;
	HWND foreground;
};

static void ResolveWorker( void *param )
{
	ResolveRequest *request=(ResolveRequest*)param;
	EndTaskTarget *target=new EndTaskTarget();
	if (ResolveTarget(request->name,request->foreground,*target) && IsAllowedTarget(*target)
		&& PostMessage(request->overlay,WM_ENDTASK_RESOLVED,request->request,(LPARAM)target))
		target=NULL;
	delete target;
	delete request;
}

static void EndTaskWorker( void *param )
{
	EndTaskTarget target=*(EndTaskTarget*)param;
	delete (EndTaskTarget*)param;
	DWORD shellPid=0;
	GetWindowThreadProcessId(GetShellWindow(),&shellPid);
	if (target.pid==GetCurrentProcessId() || target.pid==shellPid)
	{
		EndTaskTarget current;
		if (ReadIdentity(target.pid,current) && current.created==target.created)
			RestartShell(target.pid);
	}
	else if (TerminateTree(target)==ERROR_ACCESS_DENIED)
		EndElevated(target.pid);
}

///////////////////////////////////////////////////////////////////////////////
// The "End task" row

static const UINT_PTR SUBCLASS_ID='OSET';
static const wchar_t *OVERLAY_CLASS=L"OpenShell.EndTaskOverlay";
enum { TIMER_SHOW=1, TIMER_HIDE, TIMER_HOVER };
static const DWORD SHOW_DELAY=220; // gives the taskbar menu time to appear

static std::set<HWND> g_TaskLists;
static HWND g_Overlay;
static HFONT g_OverlayFont;
static UINT g_OverlayDpi;
static bool g_Hovered;
static bool g_HasTarget;
static EndTaskTarget g_Target;
static HWND g_SourceList, g_NativeMenu;
static RECT g_SourceButton;
static POINT g_ClickPoint;
static DWORD g_ClickTime;
static LONG g_Request;

static void HideOverlay( void )
{
	if (g_Overlay)
	{
		KillTimer(g_Overlay,TIMER_SHOW);
		KillTimer(g_Overlay,TIMER_HIDE);
		KillTimer(g_Overlay,TIMER_HOVER);
		ShowWindow(g_Overlay,SW_HIDE);
	}
	g_HasTarget=false;
	g_SourceList=g_NativeMenu=NULL;
}

static bool IsKnownMenuClass( const wchar_t *name )
{
	static const wchar_t *classes[]={L"TaskListOverlayWnd",L"#32768",L"Windows.UI.Core.CoreWindow",L"XamlExplorerHostIslandWindow",L"LauncherTipWnd"};
	for (const wchar_t *known:classes)
		if (wcscmp(name,known)==0)
			return true;
	return false;
}

static bool IsCloaked( HWND window )
{
	DWORD cloaked=0;
	return SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked;
}

struct MenuSearch
{
	std::vector<MenuCandidate> candidates;
	std::vector<HWND> windows;
};

static BOOL CALLBACK CollectMenuWindow( HWND window, LPARAM param )
{
	if (window==g_Overlay || !IsWindowVisible(window) || IsIconic(window) || IsCloaked(window)) return TRUE;
	wchar_t name[256];
	GetClassName(window,name,_countof(name));
	if (wcscmp(name,L"Shell_TrayWnd")==0 || wcscmp(name,L"Shell_SecondaryTrayWnd")==0 || wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0) return TRUE;
	RECT rect;
	if (FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&rect,sizeof(rect))) && !GetWindowRect(window,&rect)) return TRUE;
	if (rect.right<=rect.left || rect.bottom<=rect.top) return TRUE;
	MenuSearch &search=*(MenuSearch*)param;
	MenuCandidate candidate={rect,IsKnownMenuClass(name)};
	search.candidates.push_back(candidate);
	search.windows.push_back(window);
	return TRUE;
}

static void ShowOverlay( void )
{
	if (!g_HasTarget || !IsAllowedTarget(g_Target)) { HideOverlay(); return; }
	MONITORINFO info={sizeof(info)};
	if (!GetMonitorInfo(MonitorFromPoint(g_ClickPoint,MONITOR_DEFAULTTONEAREST),&info)) { HideOverlay(); return; }
	UINT dpi=g_SourceList?GetDpi(g_SourceList):GetDpi();
	MenuSearch search;
	EnumWindows(CollectMenuWindow,(LPARAM)&search);
	RECT menu;
	int index=-1;
	if (SelectMenuRect(search.candidates,info.rcWork,g_ClickPoint.x,dpi,menu,&index))
		g_NativeMenu=search.windows[index];
	else
	{
		menu=FallbackMenuRect(g_ClickPoint.x,info.rcWork,dpi);
		g_NativeMenu=NULL;
	}
	if (g_OverlayDpi!=dpi || !g_OverlayFont)
	{
		if (g_OverlayFont) DeleteObject(g_OverlayFont);
		g_OverlayFont=CreateFont(-ScaleForDpi(dpi,15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
		g_OverlayDpi=dpi;
	}
	RECT row=PlaceRow(menu,info.rcWork,ScaleForDpi(dpi,40));
	SetWindowPos(g_Overlay,HWND_TOPMOST,row.left,row.top,row.right-row.left,row.bottom-row.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
	InvalidateRect(g_Overlay,NULL,TRUE);
	SetTimer(g_Overlay,TIMER_HIDE,8000,NULL);
	SetTimer(g_Overlay,TIMER_HOVER,100,NULL);
}

// Hides the row when the menu closes or the pointer moves to another taskbar button
static void CheckOverlay( void )
{
	POINT point;
	RECT rect;
	if (!IsWindowVisible(g_Overlay) || !GetCursorPos(&point)) return;
	if (GetWindowRect(g_Overlay,&rect) && PtInRect(&rect,point)) return;
	if (g_SourceList && GetWindowRect(g_SourceList,&rect) && PtInRect(&rect,point) && !PtInRect(&g_SourceButton,point))
		HideOverlay();
	else if (g_NativeMenu && (!IsWindow(g_NativeMenu) || !IsWindowVisible(g_NativeMenu) || IsCloaked(g_NativeMenu)))
		HideOverlay();
}

static LRESULT CALLBACK OverlayProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch (uMsg)
	{
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
		case WM_MOUSEMOVE:
			if (!g_Hovered)
			{
				g_Hovered=true;
				TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,hWnd};
				TrackMouseEvent(&track);
				InvalidateRect(hWnd,NULL,FALSE);
			}
			return 0;
		case WM_MOUSELEAVE:
			g_Hovered=false;
			InvalidateRect(hWnd,NULL,FALSE);
			return 0;
		case WM_LBUTTONUP:
			if (g_HasTarget)
			{
				EndTaskTarget *target=new EndTaskTarget(g_Target);
				HideOverlay();
				if (!StartWorker(EndTaskWorker,target))
					delete target;
			}
			return 0;
		case WM_PAINT:
			{
				PAINTSTRUCT paint;
				HDC dc=BeginPaint(hWnd,&paint);
				RECT client;
				GetClientRect(hWnd,&client);
				HBRUSH brush=CreateSolidBrush(g_Hovered?RGB(0x27,0x27,0x27):RGB(0x10,0x10,0x10));
				FillRect(dc,&client,brush);
				DeleteObject(brush);
				RECT line={0,0,client.right,1};
				brush=CreateSolidBrush(RGB(0x39,0x39,0x39));
				FillRect(dc,&line,brush);
				DeleteObject(brush);
				SetBkMode(dc,TRANSPARENT);
				SetTextColor(dc,RGB(245,245,245));
				HGDIOBJ font=SelectObject(dc,g_OverlayFont);
				client.left+=ScaleForDpi(g_OverlayDpi,16);
				DrawText(dc,L"End task",-1,&client,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
				SelectObject(dc,font);
				EndPaint(hWnd,&paint);
			}
			return 0;
		case WM_TIMER:
			if (wParam==TIMER_SHOW) { KillTimer(hWnd,TIMER_SHOW); ShowOverlay(); }
			else if (wParam==TIMER_HIDE) HideOverlay();
			else if (wParam==TIMER_HOVER) CheckOverlay();
			return 0;
	}
	if (uMsg==WM_ENDTASK_RESOLVED)
	{
		EndTaskTarget *target=(EndTaskTarget*)lParam;
		if ((LONG)wParam==g_Request && g_SourceList)
		{
			g_Target=*target;
			g_HasTarget=true;
			DWORD elapsed=GetTickCount()-g_ClickTime;
			SetTimer(hWnd,TIMER_SHOW,elapsed<SHOW_DELAY?SHOW_DELAY-elapsed:USER_TIMER_MINIMUM,NULL);
		}
		delete target;
		return 0;
	}
	return DefWindowProc(hWnd,uMsg,wParam,lParam);
}

static bool CreateOverlay( void )
{
	if (g_Overlay) return true;
	WNDCLASS wc={0};
	wc.lpfnWndProc=OverlayProc;
	wc.hInstance=g_Instance;
	wc.hCursor=LoadCursor(NULL,IDC_HAND);
	wc.lpszClassName=OVERLAY_CLASS;
	RegisterClass(&wc);
	g_Overlay=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,OVERLAY_CLASS,L"End task",WS_POPUP,0,0,0,0,NULL,NULL,g_Instance,NULL);
	return g_Overlay!=NULL;
}

static void DestroyOverlay( void )
{
	HideOverlay();
	if (g_Overlay) DestroyWindow(g_Overlay);
	g_Overlay=NULL;
	UnregisterClass(OVERLAY_CLASS,g_Instance);
	if (g_OverlayFont) DeleteObject(g_OverlayFont);
	g_OverlayFont=NULL;
	g_OverlayDpi=0;
}

static void OnTaskListRightClick( HWND taskList, POINT point )
{
	if (!CreateOverlay()) return;
	HideOverlay();
	g_Request++;
	std::wstring name;
	RECT button;
	if (!HitTestRunningButton(taskList,point,name,button)) return;
	name=NormalizeTaskbarName(name);
	if (name.empty()) return;
	g_SourceList=taskList;
	g_SourceButton=button;
	g_ClickPoint=point;
	g_ClickTime=GetTickCount();
	ResolveRequest *request=new ResolveRequest{g_Overlay,g_Request,name,GetForegroundWindow()};
	if (!StartWorker(ResolveWorker,request))
		delete request;
}

static LRESULT CALLBACK SubclassTaskListProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_RBUTTONUP)
	{
		POINT point={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
		ClientToScreen(hWnd,&point);
		OnTaskListRightClick(hWnd,point);
	}
	else if (uMsg==WM_NCDESTROY)
	{
		RemoveWindowSubclass(hWnd,SubclassTaskListProc,uIdSubclass);
		g_TaskLists.erase(hWnd);
		if (g_SourceList==hWnd) HideOverlay();
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static BOOL CALLBACK FindTaskList( HWND window, LPARAM param )
{
	wchar_t name[64];
	if (GetClassName(window,name,_countof(name)) && wcscmp(name,L"MSTaskListWClass")==0)
	{
		*(HWND*)param=window;
		return FALSE;
	}
	return TRUE;
}

void UpdateTaskbarEndTask( HWND taskbar, bool enable )
{
	HWND taskList=NULL;
	if (taskbar) EnumChildWindows(taskbar,FindTaskList,(LPARAM)&taskList);
	if (taskList)
	{
		if (enable)
		{
			if (SetWindowSubclass(taskList,SubclassTaskListProc,SUBCLASS_ID,0))
				g_TaskLists.insert(taskList);
		}
		else
		{
			RemoveWindowSubclass(taskList,SubclassTaskListProc,SUBCLASS_ID);
			g_TaskLists.erase(taskList);
		}
	}
	if (!enable && g_TaskLists.empty())
		DestroyOverlay();
}
