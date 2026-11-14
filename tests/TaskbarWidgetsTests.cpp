#include "stdafx.h"
#include "TaskbarWidgets.h"
#include "RemoteStartMenu.h"
#include <cstdio>
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

	// Remote Start menu: folder listing
	wchar_t temp[MAX_PATH]; GetTempPath(_countof(temp),temp);
	std::wstring root=std::wstring(temp)+L"RemoteStartTest";
	CreateDirectory(root.c_str(),NULL); CreateDirectory((root+L"\\PC1").c_str(),NULL); CreateDirectory((root+L"\\PC1\\Start Menu").c_str(),NULL);
	CreateDirectory((root+L"\\PC1\\Start Menu\\Games").c_str(),NULL); CreateDirectory((root+L"\\NoMenu").c_str(),NULL);
	for (const wchar_t *name:{L"\\PC1\\Start Menu\\zeta.lnk",L"\\PC1\\Start Menu\\Alpha.lnk",L"\\PC1\\Start Menu\\notes.txt",L"\\PC1\\Start Menu\\Item 10.lnk",L"\\PC1\\Start Menu\\Item 9.lnk"})
		CloseHandle(CreateFile((root+name).c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL));
	std::vector<RemoteStartLogic::Entry> entries=RemoteStartLogic::ListFolder(root+L"\\PC1\\Start Menu");
	Check(entries.size()==5 && entries[0].name==L"Alpha" && entries[1].name==L"Games" && entries[1].folder && entries[2].name==L"Item 9" && entries[3].name==L"Item 10" && entries[4].name==L"zeta",
		"lists shortcuts and folders together in natural name order, without other files");
	auto menus=RemoteStartLogic::FindMenus(root);
	Check(menus.size()==1 && menus[0].first==L"PC1","finds only PCs that have a Start Menu folder");
	SHFILEOPSTRUCT remove={NULL,FO_DELETE,NULL,NULL,FOF_NO_UI}; std::wstring from=root+L'\0'; remove.pFrom=from.c_str(); SHFileOperation(&remove);

	// Remote Start button beside Start on the main taskbar
	if (!HasRemoteStartMenus())
		printf("SKIP: no remote Start menu set up on this PC\n");
	else
	{
		WNDCLASS taskbarClass={0,DefWindowProc,0,0,GetModuleHandle(NULL),NULL,NULL,NULL,NULL,L"Shell_TrayWnd"};
		WNDCLASS railClass={0,DefWindowProc,0,0,GetModuleHandle(NULL),NULL,NULL,NULL,NULL,L"RAIL_WINDOW"};
		RegisterClass(&taskbarClass); RegisterClass(&railClass);
		HWND rail=CreateWindowEx(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"RAIL_WINDOW",L"Test (Remote)",WS_POPUP|WS_VISIBLE,-200,-200,10,10,NULL,NULL,GetModuleHandle(NULL),NULL);
		HWND tray=CreateWindow(L"Shell_TrayWnd",L"",WS_OVERLAPPEDWINDOW,0,0,1000,60,NULL,NULL,GetModuleHandle(NULL),NULL);
		HWND bar=CreateWindow(REBARCLASSNAME,NULL,WS_CHILD|WS_VISIBLE|CCS_NORESIZE,50,0,900,40,tray,NULL,GetModuleHandle(NULL),NULL);
		SetWindowSubclass(bar,RebarProc,1,0);
		SetWindowSubclass(bar,[]( HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR )->LRESULT
		{
			if (m==WM_WINDOWPOSCHANGING) { PrepareTaskbarWidgetPosition(GetParent(w),w,(WINDOWPOS*)lp); PositionTaskbarWidget(GetParent(w),w,(WINDOWPOS*)lp); }
			return DefSubclassProc(w,m,wp,lp);
		},2,0);
		Check(IsRemoteAppSessionActive(),"notices an open RemoteApp window");
		UpdateTaskbarWidgets(tray,bar,true,false,242,false,false,true);
		HWND remote=(HWND)GetProp(tray,L"OpenShell.RemoteStartButton"), bin=(HWND)GetProp(tray,L"OpenShell.RecycleButton");
		SetWindowPos(bar,NULL,50,0,900,40,SWP_NOZORDER|SWP_NOACTIVATE);
		RECT red, recycleRect, barRect; GetWindowRect(remote,&red); GetWindowRect(bin,&recycleRect); GetWindowRect(bar,&barRect);
		MapWindowPoints(NULL,tray,(POINT*)&red,2); MapWindowPoints(NULL,tray,(POINT*)&recycleRect,2); MapWindowPoints(NULL,tray,(POINT*)&barRect,2);
		Check(IsWindow(remote) && red.left==50 && recycleRect.left==red.right && barRect.left==recycleRect.right && barRect.right==950,
			"red Start button sits right of Start, before the Recycle Bin, and the toolbars keep the rest");
		ShowWindow(rail,SW_HIDE);
		if (IsRemoteAppSessionActive())
			printf("SKIP: real RemoteApp windows are open, so the button correctly stays\n");
		else
		{
			SendMessage(tray,WM_TIMER,'OSRS',0);
			GetWindowRect(bin,&recycleRect); GetWindowRect(bar,&barRect);
			MapWindowPoints(NULL,tray,(POINT*)&recycleRect,2); MapWindowPoints(NULL,tray,(POINT*)&barRect,2);
			Check(!IsWindow(remote) && recycleRect.left==50 && barRect.left==recycleRect.right && barRect.right==950,"red Start button leaves when the RemoteApp windows close");
		}
		UpdateTaskbarWidgets(tray,bar,false,false,242,false,true);
		DestroyWindow(tray); DestroyWindow(rail);
	}
	CoUninitialize();
	return failures?1:0;
}
