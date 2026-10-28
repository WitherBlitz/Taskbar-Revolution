#pragma once

// Shared presentation for every settings panel; the existing editors own values.
namespace SettingsAppearance
{
	const COLORREF Background=RGB(17,17,17);
	const COLORREF Panel=RGB(27,27,27);
	const COLORREF Card=RGB(38,38,38);
	const COLORREF Selected=RGB(47,47,47);
	const COLORREF Text=RGB(236,236,236);
	const COLORREF Muted=RGB(141,141,141);
	HBRUSH Brush( bool card=false );
	void Apply( HWND window, HFONT font=NULL );
	void RoundRect( HDC dc, RECT rect, COLORREF color, int radius );
}
