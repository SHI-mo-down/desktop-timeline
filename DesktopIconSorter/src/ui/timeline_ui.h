// timeline_ui.h : Timeline panel window interface
#pragma once
#include <Windows.h>

void TimelineUI_Show(HINSTANCE hInstance, HWND parentWnd, const wchar_t* baseDir);
bool TimelineUI_IsOpen();
void TimelineUI_Close();
void TimelineUI_Refresh();
