#pragma once

#include <string>
#include <vector>

// Adds an "End task" row next to the menu that opens when a running taskbar button is right-clicked.
void UpdateTaskbarEndTask( HWND taskbar, bool enable );

// Pure helpers, exposed for tests
namespace EndTaskLogic
{
	struct Evidence
	{
		std::wstring taskbarName, windowTitle, processBaseName, fileDescription, productName, appId, relaunchName, processPath, pinnedPath;
	};
	struct Candidate { DWORD pid; int score; bool foreground; };
	struct MenuCandidate { RECT bounds; bool knownClass; };

	std::wstring NormalizeTaskbarName( const std::wstring &accessibleName );
	int ScoreWindow( const Evidence &evidence );
	DWORD ChooseTargetPid( const std::vector<Candidate> &candidates ); // 0 when there is no single best match
	bool SelectMenuRect( const std::vector<MenuCandidate> &candidates, const RECT &workArea, int clickX, int dpi, RECT &result, int *index=NULL );
	RECT FallbackMenuRect( int clickX, const RECT &workArea, int dpi );
	RECT PlaceRow( const RECT &menu, const RECT &workArea, int rowHeight );
	std::vector<DWORD> TerminationOrder( const std::vector<std::pair<DWORD,DWORD>> &processParents, DWORD root ); // children first, root last
	bool IsProtectedProcess( const wchar_t *baseName );
}
