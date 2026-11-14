#include "stdafx.h"
#include "TaskbarWidgets.h"
#include "RemoteStartMenu.h"
#include <cstdio>
#include <algorithm>
#include <map>
#include <set>
#include <oleacc.h>
#pragma comment(linker,"\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static int failures=0;
static LRESULT CALLBACK RebarProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR )
{
	if (message==RB_SETBANDINFOW || message==RB_SETBANDINFOA)
		FilterTaskbarBandInfo(GetParent(window),window,(UINT)wParam,(REBARBANDINFO*)lParam);
	return DefSubclassProc(window,message,wParam,lParam);
}
static void Check( bool passed, const char *name )
{
	printf("%s: %s\n",passed?"PASS":"FAIL",name);
	if (!passed) failures++;
}

int main( int argc, char **argv )
{
	CoInitialize(NULL);
	if (argc>1 && strcmp(argv[1],"--remote-probe")==0)
	{
		ConnectedRemoteMenus(); Sleep(3000); // the first call starts looking up the PCs' addresses
		RemoteMenuList connected=ConnectedRemoteMenus();
		printf("Connected remote PCs with a Start menu: %d\n",(int)connected.size());
		for (const auto &menu:connected) printf("  %ls -> %ls\n",menu.first.c_str(),menu.second.c_str());
		CoUninitialize(); return 0;
	}
	if (argc>1 && (strcmp(argv[1],"--shell-probe")==0 || strcmp(argv[1],"--shell-show")==0))
	{
		const CLSID everything={0x9d39b79c,0xe03c,0x4757,{0xb1,0xb6,0xec,0xce,0x84,0x37,0x48,0xf3}};
		CComPtr<ITrayDeskBand> tray;
		HRESULT result=tray.CoCreateInstance(CLSID_TrayDeskBand);
		printf("TrayDeskBand activation: 0x%08lX\n",result);
		if (SUCCEEDED(result))
		{
			printf("Everything visibility: 0x%08lX\n",tray->IsDeskBandShown(everything));
			if (strcmp(argv[1],"--shell-show")==0) printf("Show Everything: 0x%08lX\n",tray->ShowDeskBand(everything));
		}
		HWND taskbar=FindWindow(L"Shell_TrayWnd",NULL), recycle=(HWND)GetProp(taskbar,L"OpenShell.RecycleButton");
		RECT rect={0}; GetWindowRect(recycle,&rect);
		printf("Integrated recycle button: %s; bounds=%ld,%ld,%ld,%ld\n",IsWindow(recycle)?"present":"missing",rect.left,rect.top,rect.right,rect.bottom);
		tray.Release(); CoUninitialize(); return FAILED(result)?1:0;
	}
	INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_COOL_CLASSES|ICC_WIN95_CLASSES};
	InitCommonControlsEx(&controls);
	HWND parent=CreateWindow(L"STATIC",L"Taskbar widget test",WS_OVERLAPPEDWINDOW,0,0,1000,180,NULL,NULL,GetModuleHandle(NULL),NULL);
	HWND rebar=CreateWindow(REBARCLASSNAME,NULL,WS_CHILD|WS_VISIBLE|CCS_NORESIZE,50,0,900,40,parent,NULL,GetModuleHandle(NULL),NULL);
	SetWindowSubclass(rebar,RebarProc,1,0);
	HWND original=CreateWindow(L"STATIC",L"Existing toolbar",WS_CHILD|WS_VISIBLE,0,0,200,40,rebar,NULL,GetModuleHandle(NULL),NULL);
	REBARBANDINFO band={sizeof(band),RBBIM_CHILD|RBBIM_CHILDSIZE|RBBIM_SIZE|RBBIM_ID|RBBIM_STYLE};
	band.hwndChild=original; band.cxMinChild=100; band.cyMinChild=40; band.cx=200; band.wID=10; band.fStyle=RBBS_GRIPPERALWAYS;
	Check(SendMessage(rebar,RB_INSERTBAND,-1,(LPARAM)&band)!=0,"test fixture creates an existing toolbar band");
	UpdateTaskbarWidgets(parent,rebar,true,true,242,true);
	HWND button=(HWND)GetProp(parent,L"OpenShell.RecycleButton");
	Check(IsWindow(button)!=FALSE,"creates native Recycle Bin button");
	wchar_t buttonClass[64]; GetClassName(button,buttonClass,_countof(buttonClass));
	Check(_wcsicmp(buttonClass,L"Button")==0,"retains standard Button accessibility and default action");
	CComPtr<IAccessible> accessible;
	HRESULT result=AccessibleObjectFromWindow(button,OBJID_CLIENT,IID_PPV_ARGS(&accessible));
	CComVariant role;
	if (SUCCEEDED(result)) result=accessible->get_accRole(CComVariant(CHILDID_SELF),&role);
	Check(SUCCEEDED(result) && role.vt==VT_I4 && role.lVal==ROLE_SYSTEM_PUSHBUTTON,"exposes the native accessible push-button role");
	DWORD process=0; GetWindowThreadProcessId(button,&process);
	Check(process==GetCurrentProcessId(),"button lives in same process as its taskbar");
	Check(SendMessage(rebar,RB_GETBANDCOUNT,0,0)==1,"does not add a persistent or phantom toolbar band");
	WINDOWPOS position={rebar,NULL,50,0,900,40,0};
	PositionTaskbarWidget(parent,rebar,&position);
	RECT rect; GetWindowRect(button,&rect);
	Check(position.cx+(rect.right-rect.left)==900 && position.x==50+rect.right-rect.left,"reserves only button width beside Start");
	SetWindowPos(rebar,NULL,position.x,position.y,position.cx,position.cy,SWP_NOZORDER|SWP_NOACTIVATE);
	WINDOWPOS resize={rebar,NULL,0,0,1000,60,SWP_NOMOVE};
	PositionTaskbarWidget(parent,rebar,&resize);
	GetWindowRect(button,&rect);
	Check(resize.x==50+rect.right-rect.left && resize.cx+rect.right-rect.left==1000 && rect.bottom-rect.top==60,"size-only update preserves the origin and reserves one slot");
	SetWindowPos(rebar,NULL,resize.x,resize.y,resize.cx,resize.cy,SWP_NOZORDER|SWP_NOACTIVATE);
	WINDOWPOS move={rebar,NULL,70,5,0,0,SWP_NOSIZE};
	PositionTaskbarWidget(parent,rebar,&move);
	Check(move.x==70+rect.right-rect.left && move.cx==resize.cx && move.cy==60,"move-only update follows the taskbar without shrinking again");
	REBARBANDINFO read={sizeof(read),RBBIM_STYLE}; SendMessage(rebar,RB_GETBANDINFO,0,(LPARAM)&read);
	Check((read.fStyle&RBBS_NOGRIPPER) && !(read.fStyle&RBBS_GRIPPERALWAYS),"hides existing toolbar resize grips");
	REBARBANDINFO reset={sizeof(reset),RBBIM_STYLE}; reset.fStyle=RBBS_GRIPPERALWAYS;
	SendMessage(rebar,RB_SETBANDINFO,0,(LPARAM)&reset);
	SendMessage(rebar,RB_GETBANDINFO,0,(LPARAM)&read);
	Check((read.fStyle&RBBS_NOGRIPPER) && !(read.fStyle&RBBS_GRIPPERALWAYS),"shell style refresh cannot bring back hidden grips");
	UpdateTaskbarWidgets(parent,rebar,true,true,242,true);
	Check((HWND)GetProp(parent,L"OpenShell.RecycleButton")==button,"repeated updates do not duplicate the button");
	HMENU menu=CreateRecycleBinMenu(); wchar_t text[32]={0};
	GetMenuString(menu,2,text,_countof(text),MF_BYCOMMAND);
	Check(GetMenuItemCount(menu)==2 && wcscmp(text,L"Empty")==0,"right-click menu exposes Open and Empty without deleting data");
	DestroyMenu(menu);
	SHSTOCKICONINFO icon={sizeof(icon)};
	Check(SUCCEEDED(SHGetStockIconInfo(SIID_RECYCLER,SHGSI_ICON|SHGSI_SMALLICON,&icon)) && icon.hIcon,"Windows provides a real recycle icon");
	if (icon.hIcon) DestroyIcon(icon.hIcon);
	if (argc>1 && strcmp(argv[1],"--open-bin")==0) SendMessage(button,BM_CLICK,0,0);
	UpdateTaskbarWidgets(parent,rebar,false,true,242,false);
	Check(!IsWindow(button) && !GetProp(parent,L"OpenShell.RecycleButton"),"disabling the button releases its window and layout slot");
	read.fMask=RBBIM_STYLE; SendMessage(rebar,RB_GETBANDINFO,0,(LPARAM)&read);
	Check((read.fStyle&RBBS_GRIPPERALWAYS) && !(read.fStyle&RBBS_NOGRIPPER),"restores original grip style when disabled");
	DestroyWindow(parent); accessible.Release();

	// Remote Start menus: which PCs are connected
	using namespace RemoteStartLogic;
	wchar_t temp[MAX_PATH]; GetTempPath(_countof(temp),temp);
	std::wstring root=std::wstring(temp)+L"RemoteStartTest";
	for (const wchar_t *folder:{L"",L"\\PC1",L"\\PC1\\Start Menu",L"\\pc2",L"\\pc2\\Start Menu",L"\\NoMenu"})
		CreateDirectory((root+folder).c_str(),NULL);
	RemoteMenuList menus=FindMenus(root);
	Check(menus.size()==2 && menus[0].first==L"PC1" && menus[1].first==L"pc2" && menus[1].second==root+L"\\pc2\\Start Menu","finds each PC that has a Start Menu folder, in name order");
	std::wstring wide=L"\xFEFFscreen mode id:i:1\r\nfull address:s:PC1:3390\r\n";
	Check(HostFromRdp(std::string((const char*)wide.c_str(),wide.size()*sizeof(wchar_t)))==L"PC1","reads the computer from a Unicode connection file, without the port");
	Check(HostFromRdp("username:s:me\nFull Address:s:pc2.home.lan\n")==L"pc2.home.lan","reads the computer from a plain connection file");
	{
		HANDLE file=CreateFile((root+L"\\pc2\\Firefox.rdp").c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
		const char rdp[]="remoteapplicationmode:i:1\r\nfull address:s:192.168.1.20\r\n"; DWORD written;
		WriteFile(file,rdp,sizeof(rdp)-1,&written,NULL); CloseHandle(file);
	}
	std::set<std::wstring> names=HostNames(root+L"\\pc2");
	Check(names.size()==2 && names.count(L"pc2") && names.count(L"192.168.1.20"),"a PC is known by its folder name and the computers its connection files use");
	std::map<std::wstring,std::set<std::wstring>> addresses={{L"PC1",{L"192.168.1.10"}},{L"pc2",{L"192.168.1.20",L"fe80::2"}}};
	RemoteMenuList connected=ConnectedPcs(menus,addresses,{L"192.168.1.20",L"52.1.2.3"});
	Check(connected.size()==1 && connected[0].first==L"pc2","only PCs with an open connection get a button");
	connected=ConnectedPcs(menus,addresses,{L"192.168.1.10",L"fe80::2"});
	Check(connected.size()==2 && connected[0].first==L"PC1" && connected[1].first==L"pc2","several connected PCs each get a button, in name order");
	Check(ConnectedPcs(menus,{},{L"192.168.1.10"}).empty(),"a PC whose address is still unknown gets no button yet");
	SHFILEOPSTRUCT remove={NULL,FO_DELETE,NULL,NULL,FOF_NO_UI}; std::wstring from=root+L'\0'; remove.pFrom=from.c_str(); SHFileOperation(&remove);

	// One red Start button per connected PC, right of Start and before the Recycle Bin
	{
		static RemoteMenuList fake;
		static std::vector<std::wstring> opened;
		SetRemoteStartHooks([]()->RemoteMenuList { return fake; },[]( HWND, HWND, const wchar_t *folder ) { opened.push_back(folder); });
		WNDCLASS taskbarClass={0,DefWindowProc,0,0,GetModuleHandle(NULL),NULL,NULL,NULL,NULL,L"Shell_TrayWnd"};
		RegisterClass(&taskbarClass);
		HWND tray=CreateWindow(L"Shell_TrayWnd",L"",WS_OVERLAPPEDWINDOW,0,0,1000,60,NULL,NULL,GetModuleHandle(NULL),NULL);
		HWND bar=CreateWindow(REBARCLASSNAME,NULL,WS_CHILD|WS_VISIBLE|CCS_NORESIZE,50,0,900,40,tray,NULL,GetModuleHandle(NULL),NULL);
		SetWindowSubclass(bar,[]( HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR )->LRESULT
		{
			if (m==WM_WINDOWPOSCHANGING) { PrepareTaskbarWidgetPosition(GetParent(w),w,(WINDOWPOS*)lp); PositionTaskbarWidget(GetParent(w),w,(WINDOWPOS*)lp); }
			return DefSubclassProc(w,m,wp,lp);
		},2,0);
		auto buttons=[tray]()
		{
			std::vector<std::pair<std::wstring,RECT>> found;
			for (HWND child=GetWindow(tray,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
			{
				wchar_t text[100]; GetWindowText(child,text,_countof(text));
				RECT rect; GetWindowRect(child,&rect); MapWindowPoints(NULL,tray,(POINT*)&rect,2);
				if (*text) found.push_back(std::make_pair(std::wstring(text),rect));
			}
			std::sort(found.begin(),found.end(),[]( const std::pair<std::wstring,RECT> &a, const std::pair<std::wstring,RECT> &b ) { return a.second.left<b.second.left; });
			return found;
		};
		fake={{L"PC1",L"C:\\Menus\\PC1"},{L"pc2",L"C:\\Menus\\pc2"}};
		UpdateTaskbarWidgets(tray,bar,true,false,242,false,false,true);
		SetWindowPos(bar,NULL,50,0,900,40,SWP_NOZORDER|SWP_NOACTIVATE);
		auto found=buttons();
		RECT barRect; GetWindowRect(bar,&barRect); MapWindowPoints(NULL,tray,(POINT*)&barRect,2);
		Check(found.size()==3 && found[0].first==L"Start menu on PC1" && found[1].first==L"Start menu on pc2" && found[2].first==L"Recycle Bin"
			&& found[0].second.left==50 && found[1].second.left==found[0].second.right && found[2].second.left==found[1].second.right && barRect.left==found[2].second.right && barRect.right==950,
			"a red Start button for each PC sits right of Start, in order, before the Recycle Bin");
		HWND second=GetWindow(tray,GW_CHILD);
		while (second) { wchar_t text[100]; GetWindowText(second,text,_countof(text)); if (wcscmp(text,L"Start menu on pc2")==0) break; second=GetWindow(second,GW_HWNDNEXT); }
		SendMessage(tray,WM_COMMAND,MAKEWPARAM(0,BN_CLICKED),(LPARAM)second);
		Check(opened.size()==1 && opened[0]==L"C:\\Menus\\pc2","each button opens its own PC's menu");
		fake={{L"pc2",L"C:\\Menus\\pc2"}};
		SendMessage(tray,WM_TIMER,'OSRS',0);
		found=buttons(); GetWindowRect(bar,&barRect); MapWindowPoints(NULL,tray,(POINT*)&barRect,2);
		Check(found.size()==2 && found[0].first==L"Start menu on pc2" && found[0].second.left==50 && barRect.left==found[1].second.right && barRect.right==950,"a PC's button leaves when its RemoteApps close");
		fake.clear();
		SendMessage(tray,WM_TIMER,'OSRS',0);
		found=buttons(); GetWindowRect(bar,&barRect); MapWindowPoints(NULL,tray,(POINT*)&barRect,2);
		Check(found.size()==1 && found[0].second.left==50 && barRect.left==found[0].second.right,"with no remote PCs only the Recycle Bin stays");
		UpdateTaskbarWidgets(tray,bar,false,false,242,false,true);
		DestroyWindow(tray);
		SetRemoteStartHooks(NULL,NULL);
	}
	CoUninitialize();
	return failures?1:0;
}
