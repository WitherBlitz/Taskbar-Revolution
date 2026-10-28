#include "stdafx.h"
#include "TaskbarWidgets.h"
#include "ResourceHelper.h"
#include <uxtheme.h>

static const wchar_t *RECYCLE_PROPERTY=L"OpenShell.RecycleButton";
static const CLSID EVERYTHING_TOOLBAR={0x9d39b79c,0xe03c,0x4757,{0xb1,0xb6,0xec,0xce,0x84,0x37,0x48,0xf3}};
struct WidgetState
{
	HWND rebar;
	bool recycle, everything, hideGrips, retrying;
	int searchWidth, attempts;
	DWORD searchBandId;
};

HMENU CreateRecycleBinMenu( void )
{
	HMENU menu=CreatePopupMenu();
	AppendMenu(menu,MF_STRING,1,L"Open");
	AppendMenu(menu,MF_STRING,2,L"Empty");
	SetMenuDefaultItem(menu,1,FALSE);
	return menu;
}

static LRESULT CALLBACK RecycleButtonProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR )
{
	if (message==WM_PAINT)
	{
		PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint);
		RECT rect; GetClientRect(window,&rect);
		DrawThemeParentBackground(window,dc,&rect);
		SHQUERYRBINFO info={sizeof(info)};
		bool full=SUCCEEDED(SHQueryRecycleBin(NULL,&info)) && info.i64NumItems>0;
		SHSTOCKICONINFO icon={sizeof(icon)};
		if (SUCCEEDED(SHGetStockIconInfo(full?SIID_RECYCLERFULL:SIID_RECYCLER,SHGSI_ICON|SHGSI_SMALLICON,&icon)))
		{
			int size=min(ScaleForDpi(window,24),min(rect.right,rect.bottom)-ScaleForDpi(window,8));
			DrawIconEx(dc,(rect.right-size)/2,(rect.bottom-size)/2,icon.hIcon,size,size,0,NULL,DI_NORMAL);
			DestroyIcon(icon.hIcon);
		}
		if (GetFocus()==window) { InflateRect(&rect,-3,-3); DrawFocusRect(dc,&rect); }
		EndPaint(window,&paint); return 0;
	}
	if (message==WM_ERASEBKGND) return 1;
	if (message==WM_CONTEXTMENU)
	{
		POINT point={(short)LOWORD(lParam),(short)HIWORD(lParam)};
		if (point.x==-1 && point.y==-1) { RECT rc; GetWindowRect(window,&rc); point={rc.left,rc.top}; }
		HMENU menu=CreateRecycleBinMenu();
		SetForegroundWindow(GetAncestor(window,GA_ROOT));
		int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,NULL);
		DestroyMenu(menu);
		if (command==1) ShellExecute(window,L"open",L"shell:RecycleBinFolder",NULL,NULL,SW_SHOWNORMAL);
		if (command==2) SHEmptyRecycleBin(window,NULL,0); // Keep Windows' confirmation dialog.
		PostMessage(window,WM_NULL,0,0); InvalidateRect(window,NULL,FALSE);
		return 0;
	}
	if (message==WM_TIMER) { InvalidateRect(window,NULL,FALSE); return 0; }
	if (message==WM_SETFOCUS || message==WM_KILLFOCUS) InvalidateRect(window,NULL,FALSE);
	if (message==WM_NCDESTROY) { KillTimer(window,1); RemoveWindowSubclass(window,RecycleButtonProc,'OSRB'); }
	return DefSubclassProc(window,message,wParam,lParam);
}

static LRESULT CALLBACK WidgetParentProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data )
{
	WidgetState *state=(WidgetState*)data;
	if (message==WM_TIMER && wParam=='OSWE')
	{
		state->retrying=true;
		UpdateTaskbarWidgets(window,state->rebar,state->recycle,state->everything,state->searchWidth,state->hideGrips);
		state->retrying=false;
		if (--state->attempts<=0) KillTimer(window,'OSWE');
		return 0;
	}
	if (message==WM_COMMAND && HIWORD(wParam)==BN_CLICKED && (HWND)lParam==(HWND)GetProp(window,RECYCLE_PROPERTY))
	{
		ShellExecute(window,L"open",L"shell:RecycleBinFolder",NULL,NULL,SW_SHOWNORMAL);
		return 0;
	}
	if (message==WM_NCDESTROY) { KillTimer(window,'OSWE'); RemoveWindowSubclass(window,WidgetParentProc,'OSRW'); delete state; }
	return DefSubclassProc(window,message,wParam,lParam);
}

static RECT UnreservedRect( HWND taskbar, HWND rebar, HWND button )
{
	RECT rect; GetWindowRect(rebar,&rect); MapWindowPoints(NULL,taskbar,(POINT*)&rect,2);
	if (GetProp(button,L"OpenShell.RecyclePlaced"))
	{
		RECT slot; GetWindowRect(button,&slot);
		if (rect.right-rect.left>=rect.bottom-rect.top)
		{
			if (GetWindowLong(taskbar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL) rect.right+=slot.right-slot.left;
			else rect.left-=slot.right-slot.left;
		}
		else rect.top-=slot.bottom-slot.top;
	}
	return rect;
}

void FilterTaskbarBandInfo( HWND taskbar, HWND rebar, UINT index, REBARBANDINFO *info )
{
	DWORD_PTR data=0;
	if (!info || !GetWindowSubclass(taskbar,WidgetParentProc,'OSRW',&data)) return;
	WidgetState *state=(WidgetState*)data;
	REBARBANDINFO band={sizeof(band),RBBIM_ID};
	bool found=SendMessage(rebar,RB_GETBANDINFO,index,(LPARAM)&band)!=0;
	if (state->hideGrips && (info->fMask&RBBIM_STYLE))
	{
		if (found)
		{
			wchar_t property[64]; swprintf_s(property,L"OpenShell.Grip.%u",band.wID);
			if (!GetProp(rebar,property)) SetProp(rebar,property,(HANDLE)(ULONG_PTR)((info->fStyle&(RBBS_NOGRIPPER|RBBS_GRIPPERALWAYS))+1));
		}
		info->fStyle=(info->fStyle&~RBBS_GRIPPERALWAYS)|RBBS_NOGRIPPER;
	}
	if (state->everything && found && band.wID==state->searchBandId)
	{
		int width=ScaleForDpi(taskbar,max(120,min(600,state->searchWidth)));
		if (info->fMask&RBBIM_SIZE) info->cx=width;
		if (info->fMask&RBBIM_IDEALSIZE) info->cxIdeal=width;
		if (info->fMask&RBBIM_CHILDSIZE) info->cxMinChild=width;
	}
}

void PrepareTaskbarWidgetPosition( HWND taskbar, HWND rebar, WINDOWPOS *position )
{
	HWND button=(HWND)GetProp(taskbar,RECYCLE_PROPERTY);
	if (!IsWindow(button) || (position->flags&(SWP_NOMOVE|SWP_NOSIZE))==(SWP_NOMOVE|SWP_NOSIZE)) return;
	RECT rect=UnreservedRect(taskbar,rebar,button);
	if (position->flags&SWP_NOMOVE) { position->x=rect.left; position->y=rect.top; }
	if (position->flags&SWP_NOSIZE) { position->cx=rect.right-rect.left; position->cy=rect.bottom-rect.top; }
	position->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
}

void PositionTaskbarWidget( HWND taskbar, HWND rebar, WINDOWPOS *position )
{
	HWND button=(HWND)GetProp(taskbar,RECYCLE_PROPERTY);
	if (!IsWindow(button) || (position->flags&(SWP_NOMOVE|SWP_NOSIZE))==(SWP_NOMOVE|SWP_NOSIZE)) return;
	PrepareTaskbarWidgetPosition(taskbar,rebar,position);
	int size=ScaleForDpi(taskbar,32);
	if (position->cx>=position->cy)
	{
		bool rtl=(GetWindowLong(taskbar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL)!=0;
		SetWindowPos(button,NULL,rtl?position->x+position->cx-size:position->x,position->y,size,position->cy,SWP_NOACTIVATE|SWP_NOZORDER);
		if (!rtl) position->x+=size;
		position->cx=max(0,position->cx-size);
	}
	else
	{
		SetWindowPos(button,NULL,position->x,position->y,position->cx,size,SWP_NOACTIVATE|SWP_NOZORDER);
		position->y+=size; position->cy=max(0,position->cy-size);
	}
	SetProp(button,L"OpenShell.RecyclePlaced",(HANDLE)1);
}

void UpdateTaskbarWidgets( HWND taskbar, HWND rebar, bool recycle, bool everything, int searchWidth, bool hideGrips, bool cleanup )
{
	if (!IsWindow(rebar) || GetWindowThreadProcessId(rebar,NULL)!=GetCurrentThreadId()) return;
	static bool updating=false;
	if (updating) return;
	updating=true;
	DWORD_PTR data=0;
	GetWindowSubclass(taskbar,WidgetParentProc,'OSRW',&data);
	WidgetState *state=(WidgetState*)data;
	if (!state && !cleanup)
	{
		state=new WidgetState();
		state->searchBandId=DWORD(-1);
		SetWindowSubclass(taskbar,WidgetParentProc,'OSRW',(DWORD_PTR)state);
	}
	if (state && !state->retrying && !cleanup)
	{
		state->rebar=rebar; state->recycle=recycle; state->everything=everything; state->searchWidth=searchWidth; state->hideGrips=hideGrips;
		state->attempts=3;
		// Explorer restores deskbands after the initial taskbar layout.
		SetTimer(taskbar,'OSWE',2000,NULL);
	}
	if (state && cleanup) { state->hideGrips=false; state->everything=false; }
	HWND button=(HWND)GetProp(taskbar,RECYCLE_PROPERTY);
	if (recycle && !IsWindow(button))
	{
		button=CreateWindowEx(0,L"BUTTON",L"Recycle Bin",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,32,40,taskbar,NULL,_AtlBaseModule.GetModuleInstance(),NULL);
		if (button)
		{
			SetProp(taskbar,RECYCLE_PROPERTY,button);
			SetWindowSubclass(button,RecycleButtonProc,'OSRB',0);
			SetTimer(button,1,3000,NULL);
			HWND tooltip=CreateWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,button,NULL,_AtlBaseModule.GetModuleInstance(),NULL);
			TOOLINFO tool={sizeof(tool),TTF_IDISHWND|TTF_SUBCLASS,taskbar,(UINT_PTR)button};
			tool.lpszText=const_cast<wchar_t*>(L"Recycle Bin"); SendMessage(tooltip,TTM_ADDTOOL,0,(LPARAM)&tool);
		}
	}
	else if (!recycle && IsWindow(button))
	{
		RECT rect=UnreservedRect(taskbar,rebar,button);
		RemoveProp(taskbar,RECYCLE_PROPERTY); DestroyWindow(button);
		SetWindowPos(rebar,NULL,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
	}

	wchar_t taskbarClass[64]; GetClassName(taskbar,taskbarClass,_countof(taskbarClass));
	bool primary=_wcsicmp(taskbarClass,L"Shell_TrayWnd")==0;
	CComPtr<IBandSite> site;
	if (primary && SUCCEEDED(site.CoCreateInstance(CLSID_TrayBandSiteService)))
	{
		bool found=false;
		DWORD id;
		for (UINT i=0;site->EnumBands(i,&id)==S_OK;i++)
		{
			CComPtr<IDeskBand> band; DWORD bandState; wchar_t name[128]={0};
			if (site->QueryBand(id,&band,&bandState,name,_countof(name))==S_OK && _wcsicmp(name,L"EverythingToolbar")==0)
			{
				found=true;
				if (state) state->searchBandId=id;
				if (!everything && !cleanup) { site->RemoveBand(id); break; }
				int index=(int)SendMessage(rebar,RB_IDTOINDEX,id,0);
				if (index>0 && !cleanup && SendMessage(rebar,RB_MOVEBAND,index,0)) index=0;
				if (index>=0 && searchWidth>0)
				{
					REBARBANDINFO info={sizeof(info),RBBIM_CHILDSIZE|RBBIM_SIZE|RBBIM_IDEALSIZE};
					if (SendMessage(rebar,RB_GETBANDINFO,index,(LPARAM)&info))
					{
						info.cx=info.cxIdeal=info.cxMinChild=ScaleForDpi(taskbar,max(120,min(600,searchWidth)));
						SendMessage(rebar,RB_SETBANDINFO,index,(LPARAM)&info);
						SendMessage(rebar,RB_MINIMIZEBAND,index,0);
					}
				}
			}
		}
		if (!found && everything && !cleanup)
		{
			// Create the provider on Explorer's UI thread so its WPF window lives with the shell.
			CComPtr<IUnknown> band;
			if (SUCCEEDED(band.CoCreateInstance(EVERYTHING_TOOLBAR,NULL,CLSCTX_INPROC_SERVER))) site->AddBand(band);
		}
	}
	int count=(int)SendMessage(rebar,RB_GETBANDCOUNT,0,0);
	for (int i=0;i<count;i++)
	{
		REBARBANDINFO info={sizeof(info),RBBIM_STYLE|RBBIM_ID};
		if (!SendMessage(rebar,RB_GETBANDINFO,i,(LPARAM)&info)) continue;
		wchar_t property[64]; swprintf_s(property,L"OpenShell.Grip.%u",info.wID);
		DWORD mask=RBBS_NOGRIPPER|RBBS_GRIPPERALWAYS, original=info.fStyle&mask;
		if (hideGrips)
		{
			if (!GetProp(rebar,property)) SetProp(rebar,property,(HANDLE)(ULONG_PTR)(original+1));
			info.fStyle=(info.fStyle&~mask)|RBBS_NOGRIPPER;
		}
		else if (HANDLE saved=GetProp(rebar,property))
		{
			info.fStyle=(info.fStyle&~mask)|((DWORD)(ULONG_PTR)saved-1); RemoveProp(rebar,property);
		}
		info.fMask=RBBIM_STYLE; SendMessage(rebar,RB_SETBANDINFO,i,(LPARAM)&info);
	}
	if (cleanup && state)
	{
		KillTimer(taskbar,'OSWE'); RemoveWindowSubclass(taskbar,WidgetParentProc,'OSRW'); delete state;
	}
	updating=false;
}
