#include "stdafx.h"
#include "SettingsAppearance.h"
#include "ResourceHelper.h"
#include <uxtheme.h>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")

namespace SettingsAppearance
{
HBRUSH Brush( bool card )
{
	static HBRUSH panel=CreateSolidBrush(Panel), input=CreateSolidBrush(Card);
	return card?input:panel;
}

void RoundRect( HDC dc, RECT rect, COLORREF color, int radius )
{
	HBRUSH brush=CreateSolidBrush(color);
	HGDIOBJ oldBrush=SelectObject(dc,brush), oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
	::RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
	SelectObject(dc,oldPen);
	SelectObject(dc,oldBrush);
	DeleteObject(brush);
}

static LRESULT CALLBACK PanelProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR )
{
	if (message>=WM_CTLCOLORMSGBOX && message<=WM_CTLCOLORSTATIC)
	{
		HDC dc=(HDC)wParam;
		bool input=message==WM_CTLCOLOREDIT || (message==WM_CTLCOLORLISTBOX && GetDlgCtrlID((HWND)lParam)!=30011);
		SetTextColor(dc,IsWindowEnabled((HWND)lParam)?Text:Muted);
		SetBkColor(dc,input?Card:Panel);
		return (LRESULT)Brush(input);
	}
	if (message==WM_ERASEBKGND)
	{
		RECT rect; GetClientRect(window,&rect);
		FillRect((HDC)wParam,&rect,Brush());
		return 1;
	}
	if (message==WM_NCDESTROY) RemoveWindowSubclass(window,PanelProc,'OSAP');
	return DefSubclassProc(window,message,wParam,lParam);
}

static LRESULT CALLBACK ButtonProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR )
{
	if (message==WM_PAINT)
	{
		PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint);
		int dpi=GetDpi(window);
		auto scale=[dpi](int value) { return MulDiv(value,dpi,96); };
		RECT rect; GetClientRect(window,&rect);
		FillRect(dc,&rect,Brush());
		DWORD style=GetWindowLong(window,GWL_STYLE), type=style&BS_TYPEMASK;
		bool enabled=IsWindowEnabled(window)!=FALSE;
		bool checked=SendMessage(window,BM_GETCHECK,0,0)==BST_CHECKED;
		bool hover=GetProp(window,L"OpenShell.SettingsHover")!=NULL;
		wchar_t text[512]; GetWindowText(window,text,_countof(text));
		HFONT font=(HFONT)SendMessage(window,WM_GETFONT,0,0);
		HGDIOBJ oldFont=SelectObject(dc,font);
		SetBkMode(dc,TRANSPARENT);
		SetTextColor(dc,enabled?Text:Muted);
		RECT label=rect;
		if (type==BS_AUTOCHECKBOX || type==BS_CHECKBOX || type==BS_AUTO3STATE)
		{
			int height=min(scale(18),rect.bottom-scale(4));
			RECT toggle={rect.right-scale(34),(rect.bottom-height)/2,rect.right-scale(2),(rect.bottom+height)/2};
			RoundRect(dc,toggle,checked?RGB(244,244,244):RGB(58,58,58),height);
			int diameter=max(scale(6),height-scale(6));
			int x=checked?toggle.right-diameter-scale(3):toggle.left+scale(3);
			RECT knob={x,toggle.top+scale(3),x+diameter,toggle.top+scale(3)+diameter};
			RoundRect(dc,knob,checked?RGB(18,18,18):RGB(189,189,189),diameter);
			label.right=toggle.left-scale(8);
			DrawText(dc,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
		}
		else if (type==BS_AUTORADIOBUTTON || type==BS_RADIOBUTTON)
		{
			int y=(rect.bottom-scale(14))/2;
			RECT dot={scale(1),y,scale(15),y+scale(14)}; RoundRect(dc,dot,checked?Text:RGB(70,70,70),scale(14));
			if (checked) { InflateRect(&dot,-scale(4),-scale(4)); RoundRect(dc,dot,Panel,scale(6)); }
			label.left+=scale(23);
			DrawText(dc,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
		}
		else if (type==BS_GROUPBOX)
		{
			SetTextColor(dc,Muted);
			DrawText(dc,text,-1,&label,DT_LEFT|DT_TOP|DT_SINGLELINE);
		}
		else
		{
			bool primary=GetDlgCtrlID(window)==IDOK;
			InflateRect(&rect,-1,-1);
			RoundRect(dc,rect,primary?RGB(244,244,244):(hover?RGB(56,56,56):Selected),rect.bottom-rect.top);
			SetTextColor(dc,!enabled?Muted:primary?RGB(18,18,18):Text);
			DrawText(dc,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		}
		if (GetFocus()==window) { InflateRect(&label,-4,-3); DrawFocusRect(dc,&label); }
		SelectObject(dc,oldFont); EndPaint(window,&paint);
		return 0;
	}
	if (message==WM_MOUSEMOVE)
	{
		if (!GetProp(window,L"OpenShell.SettingsHover"))
		{
			SetProp(window,L"OpenShell.SettingsHover",(HANDLE)1);
			TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track);
			InvalidateRect(window,NULL,FALSE);
		}
	}
	if (message==WM_MOUSELEAVE)
	{
		RemoveProp(window,L"OpenShell.SettingsHover"); InvalidateRect(window,NULL,FALSE);
	}
	LRESULT result=DefSubclassProc(window,message,wParam,lParam);
	if (message==BM_SETCHECK || message==WM_ENABLE || message==WM_SETFOCUS || message==WM_KILLFOCUS)
		InvalidateRect(window,NULL,FALSE);
	if (message==WM_NCDESTROY)
	{
		RemoveProp(window,L"OpenShell.SettingsHover"); RemoveWindowSubclass(window,ButtonProc,'OSAB');
	}
	return result;
}

static BOOL CALLBACK ApplyControl( HWND window, LPARAM font )
{
	wchar_t name[80]; GetClassName(window,name,_countof(name));
	if (font && (_wcsicmp(name,WC_TREEVIEW)==0 || _wcsicmp(name,WC_LISTVIEW)==0 || GetDlgCtrlID(window)==30011))
		SendMessage(window,WM_SETFONT,font,TRUE);
	if (_wcsicmp(name,WC_TREEVIEW)==0)
	{
		SetWindowTheme(window,L"DarkMode_Explorer",NULL);
		TreeView_SetBkColor(window,Panel); TreeView_SetTextColor(window,Text);
		TreeView_SetLineColor(window,Selected);
		int height=ScaleForDpi(window,36);
		TreeView_SetItemHeight(window,max(TreeView_GetItemHeight(window),height));
		SetWindowLong(window,GWL_STYLE,GetWindowLong(window,GWL_STYLE)&~WS_BORDER);
	}
	else if (_wcsicmp(name,WC_LISTVIEW)==0)
	{
		SetWindowTheme(window,L"DarkMode_Explorer",NULL);
		ListView_SetBkColor(window,Card); ListView_SetTextBkColor(window,Card); ListView_SetTextColor(window,Text);
	}
	else if (_wcsicmp(name,L"Button")==0)
	{
		DWORD style=GetWindowLong(window,GWL_STYLE);
		DWORD type=style&BS_TYPEMASK;
		if (type==BS_AUTOCHECKBOX || type==BS_CHECKBOX || type==BS_AUTORADIOBUTTON || type==BS_RADIOBUTTON)
		{
			wchar_t text[512]; GetWindowText(window,text,_countof(text));
			HDC dc=GetDC(window);
			HGDIOBJ old=SelectObject(dc,(HFONT)SendMessage(window,WM_GETFONT,0,0));
			SIZE size; GetTextExtentPoint32(dc,text,(int)wcslen(text),&size);
			int padding=ScaleForDpi(window,type==BS_AUTOCHECKBOX || type==BS_CHECKBOX?46:25);
			SelectObject(dc,old); ReleaseDC(window,dc);
			HWND parent=GetParent(window);
			RECT rect,client; GetWindowRect(window,&rect); MapWindowPoints(NULL,parent,(POINT*)&rect,2); GetClientRect(parent,&client);
			int right=client.right;
			for (HWND next=GetWindow(parent,GW_CHILD);next;next=GetWindow(next,GW_HWNDNEXT))
			{
				if (next==window || !IsWindowVisible(next)) continue;
				RECT sibling; GetWindowRect(next,&sibling); MapWindowPoints(NULL,parent,(POINT*)&sibling,2);
				if (sibling.top<rect.bottom && sibling.bottom>rect.top && sibling.left>=rect.right) right=min(right,sibling.left);
			}
			int width=min(size.cx+padding,right-rect.left);
			if (width>rect.right-rect.left) SetWindowPos(window,NULL,0,0,width,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
		}
		if (!(style&(BS_ICON|BS_BITMAP)) && (style&BS_TYPEMASK)!=BS_OWNERDRAW)
			SetWindowSubclass(window,ButtonProc,'OSAB',0);
	}
	else if (_wcsicmp(name,L"#32770")==0)
	{
		EnableThemeDialogTexture(window,ETDT_DISABLE);
		SetWindowSubclass(window,PanelProc,'OSAP',0);
	}
	else if (_wcsicmp(name,L"ComboBox")==0 || _wcsicmp(name,L"Edit")==0)
		SetWindowTheme(window,L"DarkMode_CFD",NULL);
	return TRUE;
}

void Apply( HWND window, HFONT font )
{
	BOOL dark=TRUE;
	DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
	ApplyControl(window,(LPARAM)font);
	EnumChildWindows(window,ApplyControl,(LPARAM)font);
	RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
}
