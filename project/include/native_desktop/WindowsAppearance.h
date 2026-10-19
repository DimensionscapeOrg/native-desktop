#ifndef NATIVE_DESKTOP_WINDOWS_APPEARANCE_H
#define NATIVE_DESKTOP_WINDOWS_APPEARANCE_H
#include <windows.h>

namespace native_desktop {
	void ClearWindowsMenuAppearance (HWND window);
	bool BeginWindowsMenuTracking (HWND window);
	void EndWindowsMenuTracking (HWND window);
	void SetWindowsMenuAppearance (HWND window, int appearance);
	bool WindowsMenuDark (HWND window);
	void RefreshWindowsMenuAppearance (HWND window, bool dark);
	void PrepareWindowsPopupAppearance (HWND window, HMENU menu, bool dark);
	void ReleaseWindowsMenuAppearance (HMENU menu);
}
#endif
