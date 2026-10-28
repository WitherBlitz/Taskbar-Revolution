#include "stdafx.h"
#include "TaskbarWidgets.h"
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
	DestroyWindow(parent); accessible.Release(); CoUninitialize();
	return failures?1:0;
}
