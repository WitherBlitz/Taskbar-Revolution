#include "stdafx.h"
#include "TaskbarEndTask.h"
#include <cstdio>

using namespace EndTaskLogic;

static int failures=0;
static void Check( bool passed, const char *name )
{
	printf("%s: %s\n",passed?"PASS":"FAIL",name);
	if (!passed) failures++;
}

static bool SameRect( const RECT &a, LONG left, LONG top, LONG right, LONG bottom )
{
	return a.left==left && a.top==top && a.right==right && a.bottom==bottom;
}

static HWND OwnOverlay( void )
{
	for (HWND window=FindWindowEx(NULL,NULL,L"OpenShell.EndTaskOverlay",NULL);window;window=FindWindowEx(NULL,window,L"OpenShell.EndTaskOverlay",NULL))
	{
		DWORD pid=0; GetWindowThreadProcessId(window,&pid);
		if (pid==GetCurrentProcessId()) return window;
	}
	return NULL;
}

int main( void )
{
	// Button names
	Check(NormalizeTaskbarName(L"Google Chrome - 3 running windows")==L"Google Chrome","strips the running-window count from grouped buttons");
	Check(NormalizeTaskbarName(L"Notepad - 1 running window ")==L"Notepad","strips the single running-window suffix");
	Check(NormalizeTaskbarName(L"Report - Final")==L"Report - Final","keeps dashes that are part of the name");

	// Matching a window to the button
	Evidence evidence;
	evidence.taskbarName=L"Notepad";
	evidence.processBaseName=L"notepad.exe";
	Check(ScoreWindow(evidence)==70,"matches the executable name");
	evidence.fileDescription=L"Notepad";
	Check(ScoreWindow(evidence)==80,"file description outranks the executable name");
	evidence.windowTitle=L" notepad ";
	Check(ScoreWindow(evidence)==100,"exact window title wins, ignoring case and spaces");
	Evidence other;
	other.taskbarName=L"Notepad";
	other.processBaseName=L"chrome.exe";
	other.windowTitle=L"Notepad tips - Chrome";
	Check(ScoreWindow(other)==0,"an unrelated window does not match");
	Evidence explorer;
	explorer.taskbarName=L"File Explorer";
	explorer.processBaseName=L"explorer.exe";
	Check(ScoreWindow(explorer)==70,"File Explorer matches explorer.exe");
	Evidence uwp;
	uwp.taskbarName=L"Calculator";
	uwp.processBaseName=L"Calculator.exe";
	uwp.appId=L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
	uwp.relaunchName=L"Calculator";
	Check(ScoreWindow(uwp)==90,"relaunch display name identifies store apps");

	// Picking one process
	std::vector<Candidate> candidates={{10,80,false},{20,100,false},{20,70,false}};
	Check(ChooseTargetPid(candidates)==20,"picks the process with the best window");
	candidates={{10,100,false},{20,100,true}};
	Check(ChooseTargetPid(candidates)==20,"breaks a tie with the foreground window");
	candidates={{10,100,false},{20,100,false}};
	Check(ChooseTargetPid(candidates)==0,"refuses to guess between equal matches");
	candidates={{10,0,true}};
	Check(ChooseTargetPid(candidates)==0,"ignores windows that do not match");

	// Placing the row next to the taskbar menu
	RECT work={0,0,1920,1040};
	std::vector<MenuCandidate> menus={{{0,0,1920,1040},false},{{500,700,800,1040},false},{{520,600,820,1040},true}};
	RECT menu={0,0,0,0};
	int index=-1;
	Check(SelectMenuRect(menus,work,600,96,menu,&index) && index==2,"prefers a known menu window over a full-screen app");
	menus={{{500,700,800,1040},false}};
	Check(SelectMenuRect(menus,work,600,96,menu,&index) && index==0,"accepts an unknown window shaped like the menu");
	Check(!SelectMenuRect(menus,work,1500,96,menu),"rejects a menu far from the click");
	Check(SameRect(FallbackMenuRect(10,work,96),0,940,256,1040),"fallback menu stays on screen");
	Check(SameRect(FallbackMenuRect(600,work,192),344,840,856,1040),"fallback menu scales with DPI");
	RECT native={500,700,800,1040};
	Check(SameRect(PlaceRow(native,work,40),500,658,800,698),"row sits just above the menu");
	RECT tall={500,10,800,1040};
	Check(PlaceRow(tall,work,40).top>=work.top && PlaceRow(tall,work,40).bottom<=work.bottom,"row stays inside the work area");

	// Ending a process tree
	std::vector<std::pair<DWORD,DWORD>> tree={{100,1},{200,100},{300,200},{400,100},{500,2},{600,600}};
	std::vector<DWORD> order=TerminationOrder(tree,100);
	Check(order.size()==4 && order.back()==100,"ends every child before the app itself");
	Check(std::find(order.begin(),order.end(),300)<std::find(order.begin(),order.end(),200),"ends grandchildren before children");
	Check(std::find(order.begin(),order.end(),500)==order.end(),"leaves unrelated processes alone");
	Check(TerminationOrder(tree,600).size()==1,"survives a process listed as its own parent");
	Check(IsProtectedProcess(L"CSRSS.EXE") && IsProtectedProcess(L"dwm.exe") && !IsProtectedProcess(L"notepad.exe"),"never ends core Windows processes");

	// Attaching to a taskbar
	WNDCLASS wc={0};
	wc.lpfnWndProc=DefWindowProc;
	wc.hInstance=GetModuleHandle(NULL);
	wc.lpszClassName=L"MSTaskListWClass";
	RegisterClass(&wc);
	HWND taskbar=CreateWindow(L"STATIC",L"",WS_POPUP,0,0,100,40,NULL,NULL,NULL,NULL);
	HWND list=CreateWindow(L"MSTaskListWClass",L"",WS_CHILD,0,0,100,40,taskbar,NULL,NULL,NULL);
	UpdateTaskbarEndTask(taskbar,true);
	UpdateTaskbarEndTask(taskbar,true);
	SendMessage(list,WM_RBUTTONUP,0,MAKELPARAM(5,5)); // not a real taskbar button: must be ignored
	HWND overlay=OwnOverlay();
	Check(overlay && !IsWindowVisible(overlay),"a right-click off a running button shows nothing");
	UpdateTaskbarEndTask(taskbar,false);
	Check(!OwnOverlay(),"turning it off removes the End task window");
	DestroyWindow(taskbar);

	return failures?1:0;
}
