#pragma once

#include "RemoteStartMenu.h"

// Where the red Start buttons come from (default: ConnectedRemoteMenus) and what opens a PC's menu
void SetRemoteStartHooks( RemoteMenuList (*source)( void ), void (*open)( HWND taskbar, HWND button, const wchar_t *folder ) );

void UpdateTaskbarWidgets( HWND taskbar, HWND rebar, bool recycle, bool everything, int searchWidth, bool hideGrips, bool cleanup=false, bool remoteStart=false );
void PositionTaskbarWidget( HWND taskbar, HWND rebar, WINDOWPOS *position );
void PrepareTaskbarWidgetPosition( HWND taskbar, HWND rebar, WINDOWPOS *position );
HMENU CreateRecycleBinMenu( void );
void FilterTaskbarBandInfo( HWND taskbar, HWND rebar, UINT index, REBARBANDINFO *info );
