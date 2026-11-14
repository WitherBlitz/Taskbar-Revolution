#pragma once

void UpdateTaskbarWidgets( HWND taskbar, HWND rebar, bool recycle, bool everything, int searchWidth, bool hideGrips, bool cleanup=false, bool remoteStart=false );
void PositionTaskbarWidget( HWND taskbar, HWND rebar, WINDOWPOS *position );
void PrepareTaskbarWidgetPosition( HWND taskbar, HWND rebar, WINDOWPOS *position );
HMENU CreateRecycleBinMenu( void );
void FilterTaskbarBandInfo( HWND taskbar, HWND rebar, UINT index, REBARBANDINFO *info );
