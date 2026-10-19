#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_WINDOWS

// Native menus and tray icons for Windows

#include <native_desktop/Desktop.h>
#include <native_desktop/WindowsAppearance.h>


#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <map>
#include <vector>
#undef CreateWindow

// Older MinGW SDK configurations may omit the Vista notification icon values.
#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif


namespace native_desktop {


	struct MenuBar {

		HMENU menu;
		uint32_t windowID;

	};


	// Keep menu commands clear of the standard dialog command IDs (IDOK, IDCANCEL...)
	static const int MENU_COMMAND_BASE = 0x1000;
	static const int MENU_COMMAND_MAX = 0xFFFF;

	static std::map<HWND, MenuBar> menuBars;


	static HWND GetWindowHandle (Window* window) {
		HWND handle = window ? (HWND)window->handle : NULL;
		return handle && GetWindowThreadProcessId (handle, NULL) == GetCurrentThreadId () ? handle : NULL;
	}

	static std::wstring ToWide (const std::string& text) {

		if (text.empty ()) return std::wstring ();

		int size = MultiByteToWideChar (CP_UTF8, 0, text.c_str (), (int)text.size (), NULL, 0);
		if (size <= 0) return std::wstring ();

		std::wstring result (size, L'\0');
		MultiByteToWideChar (CP_UTF8, 0, text.c_str (), (int)text.size (), &result[0], size);
		return result;

	}


	static std::string GetKeyEquivalentText (int keyCode, int keyModifiers) {

		const char* keyName = Menu::KeyName (keyCode);

		if (!keyName || !keyName[0]) return std::string ();

		std::string text;

		if (keyModifiers & MOD_CTRL) text += "Ctrl+";
		if (keyModifiers & MOD_SHIFT) text += "Shift+";
		if (keyModifiers & MOD_ALT) text += "Alt+";
		if (keyModifiers & MOD_GUI) text += "Win+";

		return text + keyName;

	}


	static bool AppendMenuItems (HMENU menu, const std::vector<MenuItem>& items) {

		for (size_t i = 0; i < items.size (); i++) {

			const MenuItem& item = items[i];

			UINT itemFlags = MF_STRING;
			if (!(item.flags & MENU_ITEM_ENABLED)) itemFlags |= MF_GRAYED;

			if (item.flags & MENU_ITEM_SUBMENU) {

				HMENU submenu = CreatePopupMenu ();

				if (!submenu) return false;

				if (!AppendMenuItems (submenu, item.submenu) || !AppendMenuW (menu, itemFlags | MF_POPUP, (UINT_PTR)submenu, ToWide (item.label).c_str ())) {

					DestroyMenu (submenu);
					return false;

				}

			} else if (item.flags & MENU_ITEM_SEPARATOR) {

				AppendMenuW (menu, MF_SEPARATOR, 0, NULL);

			} else {

				std::string label = item.label;

				if (item.keyCode != 0) {

					std::string keyText = GetKeyEquivalentText (item.keyCode, item.keyModifiers);
					if (!keyText.empty ()) label += "\t" + keyText;

				}

				if (item.flags & MENU_ITEM_CHECKED) itemFlags |= MF_CHECKED;

				// WM_COMMAND only carries the low word of a menu item identifier
				int command = MENU_COMMAND_BASE + item.id;

				if (command > MENU_COMMAND_MAX) {

					command = 0;
					itemFlags |= MF_GRAYED;

				}

				AppendMenuW (menu, itemFlags, (UINT_PTR)command, ToWide (label).c_str ());

			}

		}

		return true;

	}


	static HMENU CreateMenuFromData (const unsigned char* data, int length, bool popup) {

		std::vector<MenuItem> items;

		if (!Menu::Parse (data, length, &items)) return NULL;

		HMENU menu = popup ? CreatePopupMenu () : CreateMenu ();

		if (menu && !AppendMenuItems (menu, items)) {

			DestroyMenu (menu);
			return NULL;

		}

		return menu;

	}



	static LRESULT CALLBACK MenuWindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
		if (message == WM_COMMAND && HIWORD (wParam) == 0 && lParam == 0) {
			auto found = menuBars.find (hwnd);
			int command = LOWORD (wParam);
			if (found != menuBars.end () && command > MENU_COMMAND_BASE)
				Menu::PushSelection (found->second.windowID, command - MENU_COMMAND_BASE);
		} else if (message == WM_NCDESTROY) {
			menuBars.erase (hwnd);
			RemoveWindowSubclass (hwnd, MenuWindowProc, id);
		}
		return DefSubclassProc (hwnd, message, wParam, lParam);
	}

	int Menu::GetSupport () {
		if (!Menu::IsOwnerThread ()) return 0;

		return MENU_SUPPORT_POPUP | MENU_SUPPORT_WINDOW;

	}


	int Menu::Popup (Window* window, const unsigned char* data, int length, int x, int y, bool atCursor) {
		if (!Menu::IsOwnerThread ()) return 0;

		HWND hwnd = GetWindowHandle (window);

		if (!hwnd) return 0;

		HMENU menu = CreateMenuFromData (data, length, true);

		if (!menu) return 0;
		PrepareWindowsPopupAppearance (hwnd, menu, WindowsMenuDark (hwnd));

		POINT point;

		if (atCursor) {

			GetCursorPos (&point);

		} else {

			point.x = x;
			point.y = y;
			ClientToScreen (hwnd, &point);

		}

		UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON;
		flags |= GetSystemMetrics (SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

		if (!BeginWindowsMenuTracking (hwnd)) { ReleaseWindowsMenuAppearance (menu); DestroyMenu (menu); return 0; }
		int command = (int)TrackPopupMenuEx (menu, flags, point.x, point.y, hwnd, NULL);
		EndWindowsMenuTracking (hwnd);

		ReleaseWindowsMenuAppearance (menu);
		DestroyMenu (menu);
		return command > MENU_COMMAND_BASE ? command - MENU_COMMAND_BASE : 0;

	}


	void Menu::Remove (Window* window) {
		if (!Menu::IsOwnerThread ()) return;

		HWND hwnd = GetWindowHandle (window);
		if (hwnd) {
			auto found = menuBars.find (hwnd);
			if (found != menuBars.end ()) {
				if (found->second.windowID != window->id) return;
				HMENU owned = found->second.menu;
				if (GetMenu (hwnd) == owned) ::SetMenu (hwnd, NULL);
				ReleaseWindowsMenuAppearance (owned);
				DestroyMenu (owned);
				menuBars.erase (found);
			}
			ClearWindowsMenuAppearance (hwnd);
			RemoveWindowSubclass (hwnd, MenuWindowProc, 0x4E444D45);
		}

	}


	bool Menu::SetApplicationMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		return false;

	}


	bool Menu::SetWindowMenu (Window* window, const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		HWND hwnd = GetWindowHandle (window);

		if (!hwnd) return false;

		HMENU menu = NULL;

		if (data && length > 0) {

			menu = CreateMenuFromData (data, length, false);
			if (!menu) return false;

		}

		std::map<HWND, MenuBar>::iterator it = menuBars.find (hwnd);
		if (it != menuBars.end () && it->second.windowID != window->id) { if (menu) DestroyMenu (menu); return false; }
		HMENU previous = (it != menuBars.end ()) ? it->second.menu : NULL;
		if (menu && !SetWindowSubclass (hwnd, MenuWindowProc, 0x4E444D45, 0)) { DestroyMenu (menu); return false; }

		if (!::SetMenu (hwnd, menu)) {

			if (menu) DestroyMenu (menu);
			return false;

		}

		if (previous) {
			ReleaseWindowsMenuAppearance (previous);
			DestroyMenu (previous);
		}

		if (menu) {

			MenuBar menuBar;
			menuBar.menu = menu;
			menuBar.windowID = window->id;
			menuBars[hwnd] = menuBar;

		} else {

			menuBars.erase (hwnd);

		}

		RefreshWindowsMenuAppearance (hwnd, WindowsMenuDark (hwnd));

		return true;

	}


	bool TrayIcon::IsSupported () {
		if (!Menu::IsOwnerThread ()) return false;

		return true;

	}


	// Tray icons use Shell_NotifyIcon, with a hidden window that receives their notifications
	// and owns their context menus. It is a top-level window (not message-only) so that it can
	// take the foreground, which a tray context menu needs to close when clicking elsewhere.

	struct Win32TrayIcon {

		HICON icon;
		POINT anchor;
		bool hasAnchor;
		std::wstring tooltip;

	};


	static const UINT TRAY_ICON_MESSAGE = WM_APP + 1;
	static const wchar_t* TRAY_ICON_WINDOW_CLASS = L"NativeDesktopTrayIconWindow";

	static int nextTrayIconID = 0;
	static UINT taskbarCreatedMessage = 0;
	static HWND trayIconWindow = NULL;
	static std::map<int, TrayIcon*> trayIcons;


	static bool UpdateNotifyIcon (TrayIcon* trayIcon, DWORD action);


	static LRESULT CALLBACK TrayIconWindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {

		if (message == TRAY_ICON_MESSAGE) {

			// With NOTIFYICON_VERSION_4, the event is in the low word and the icon ID in the high word
			int id = HIWORD (lParam);
			std::map<int, TrayIcon*>::iterator it = trayIcons.find (id);

			if (it != trayIcons.end () && it->second->platform) {

				Win32TrayIcon* data = (Win32TrayIcon*)it->second->platform;

				switch (LOWORD (lParam)) {

					case NIN_SELECT:
					case NIN_KEYSELECT:

						Menu::PushTrayIconEvent (id, TRAY_ICON_CLICK, 0);
						break;

					case WM_CONTEXTMENU:

						data->anchor.x = GET_X_LPARAM (wParam);
						data->anchor.y = GET_Y_LPARAM (wParam);
						data->hasAnchor = true;
						Menu::PushTrayIconEvent (id, TRAY_ICON_RIGHT_CLICK, 0);
						break;

					default: break;

				}

			}

			return 0;

		}

		if (taskbarCreatedMessage != 0 && message == taskbarCreatedMessage) {

			// Explorer restarted, so the icons have to be added again
			for (std::map<int, TrayIcon*>::iterator it = trayIcons.begin (); it != trayIcons.end (); ++it) {

				UpdateNotifyIcon (it->second, NIM_ADD);

			}

			return 0;

		}

		return DefWindowProcW (hwnd, message, wParam, lParam);

	}


	static HWND GetTrayIconWindow () {

		if (trayIconWindow) return trayIconWindow;

		HINSTANCE instance = NULL;
		GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&TrayIconWindowProc, &instance);

		WNDCLASSEXW windowClass;
		ZeroMemory (&windowClass, sizeof (windowClass));
		windowClass.cbSize = sizeof (windowClass);
		windowClass.lpfnWndProc = TrayIconWindowProc;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = TRAY_ICON_WINDOW_CLASS;
		RegisterClassExW (&windowClass);

		trayIconWindow = CreateWindowExW (WS_EX_TOOLWINDOW, TRAY_ICON_WINDOW_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance, NULL);
		taskbarCreatedMessage = RegisterWindowMessageW (L"TaskbarCreated");

		return trayIconWindow;

	}


	static HICON GetDefaultIcon () {

		int size = GetSystemMetrics (SM_CXSMICON);

		// Prefer the application icon embedded as resource 1.
		HICON icon = (HICON)LoadImageW (GetModuleHandleW (NULL), MAKEINTRESOURCEW (1), IMAGE_ICON, size, size, LR_SHARED);
		return icon ? icon : LoadIcon (NULL, IDI_APPLICATION);

	}


	static HICON CreateIconFromImage (Image* imageBuffer) {

		if (!imageBuffer || !imageBuffer->Valid ()) {

			return NULL;

		}

		int width = imageBuffer->width;
		int height = imageBuffer->height;


		BITMAPV5HEADER header;
		ZeroMemory (&header, sizeof (header));
		header.bV5Size = sizeof (header);
		header.bV5Width = width;
		header.bV5Height = -height;
		header.bV5Planes = 1;
		header.bV5BitCount = 32;
		header.bV5Compression = BI_BITFIELDS;
		header.bV5RedMask = 0x00FF0000;
		header.bV5GreenMask = 0x0000FF00;
		header.bV5BlueMask = 0x000000FF;
		header.bV5AlphaMask = 0xFF000000;

		void* bits = NULL;
		HDC dc = GetDC (NULL);
		HBITMAP color = CreateDIBSection (dc, (BITMAPINFO*)&header, DIB_RGB_COLORS, &bits, NULL, 0);
		ReleaseDC (NULL, dc);

		if (!color) return NULL;

		// Host pixels are RGBA; Windows bitmaps are BGRA.
		const unsigned char* source = imageBuffer->data;
		unsigned char* dest = (unsigned char*)bits;
		int stride = imageBuffer->stride;

		for (int y = 0; y < height; y++) {

			const unsigned char* sourceRow = source + y * stride;
			unsigned char* destRow = dest + y * width * 4;

			for (int x = 0; x < width; x++) {

				destRow[x * 4 + 0] = sourceRow[x * 4 + 2];
				destRow[x * 4 + 1] = sourceRow[x * 4 + 1];
				destRow[x * 4 + 2] = sourceRow[x * 4 + 0];
				destRow[x * 4 + 3] = sourceRow[x * 4 + 3];

			}

		}

		// The alpha channel is used for transparency, so the mask only needs to exist
		std::vector<unsigned char> maskBits (((width + 15) / 16) * 2 * height, 0);
		HBITMAP mask = CreateBitmap (width, height, 1, 1, &maskBits[0]);

		ICONINFO info;
		info.fIcon = TRUE;
		info.xHotspot = 0;
		info.yHotspot = 0;
		info.hbmMask = mask;
		info.hbmColor = color;

		HICON icon = CreateIconIndirect (&info);

		DeleteObject (mask);
		DeleteObject (color);
		return icon;

	}


	static bool UpdateNotifyIcon (TrayIcon* trayIcon, DWORD action) {

		Win32TrayIcon* data = (Win32TrayIcon*)trayIcon->platform;

		if (!data) return false;

		NOTIFYICONDATAW notifyIcon;
		ZeroMemory (&notifyIcon, sizeof (notifyIcon));
		notifyIcon.cbSize = sizeof (notifyIcon);
		notifyIcon.hWnd = GetTrayIconWindow ();
		notifyIcon.uID = trayIcon->id;
		notifyIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
		notifyIcon.uCallbackMessage = TRAY_ICON_MESSAGE;
		notifyIcon.hIcon = data->icon ? data->icon : GetDefaultIcon ();
		wcsncpy (notifyIcon.szTip, data->tooltip.c_str (), sizeof (notifyIcon.szTip) / sizeof (wchar_t) - 1);

		if (!Shell_NotifyIconW (action, &notifyIcon)) return false;

		if (action == NIM_ADD) {

			notifyIcon.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW (NIM_SETVERSION, &notifyIcon);

		}

		return true;

	}


	TrayIcon::TrayIcon () {
		id = 0; platform = NULL;
		if (!Menu::IsOwnerThread ()) return;

		platform = NULL;
		id = 0;
		for (int attempt = 0; attempt < 65535; ++attempt) {
			nextTrayIconID = nextTrayIconID % 65535 + 1;
			if (trayIcons.find (nextTrayIconID) == trayIcons.end ()) { id = nextTrayIconID; break; }
		}
		if (id == 0) return;

		Win32TrayIcon* data = new Win32TrayIcon ();
		data->icon = NULL;
		data->hasAnchor = false;
		platform = data;

		trayIcons[id] = this;
		if (!UpdateNotifyIcon (this, NIM_ADD)) Close ();

	}


	TrayIcon::~TrayIcon () {

		Close ();

	}


	void TrayIcon::Close () {
		if (!Menu::IsOwnerThread ()) return;

		Win32TrayIcon* data = (Win32TrayIcon*)platform;

		if (!data) return;

		NOTIFYICONDATAW notifyIcon;
		ZeroMemory (&notifyIcon, sizeof (notifyIcon));
		notifyIcon.cbSize = sizeof (notifyIcon);
		notifyIcon.hWnd = GetTrayIconWindow ();
		notifyIcon.uID = id;
		Shell_NotifyIconW (NIM_DELETE, &notifyIcon);

		if (data->icon) DestroyIcon (data->icon);

		delete data;
		platform = NULL;
		trayIcons.erase (id);
		if (trayIcons.empty () && trayIconWindow) {
			HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW (trayIconWindow, GWLP_HINSTANCE);
			DestroyWindow (trayIconWindow);
			trayIconWindow = NULL;
			UnregisterClassW (TRAY_ICON_WINDOW_CLASS, instance);
			taskbarCreatedMessage = 0;
		}

	}


	int TrayIcon::PopupMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return 0;

		Win32TrayIcon* trayData = (Win32TrayIcon*)platform;

		if (!trayData) return 0;

		HMENU menu = CreateMenuFromData (data, length, true);

		if (!menu) return 0;

		HWND hwnd = GetTrayIconWindow ();
		PrepareWindowsPopupAppearance (hwnd, menu, WindowsMenuDark (NULL));
		POINT point = trayData->anchor;

		if (!trayData->hasAnchor) GetCursorPos (&point);
		trayData->hasAnchor = false;

		// Without the foreground, the menu would stay open when clicking elsewhere
		SetForegroundWindow (hwnd);

		UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN;
		flags |= GetSystemMetrics (SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

		if (!BeginWindowsMenuTracking (hwnd)) { ReleaseWindowsMenuAppearance (menu); DestroyMenu (menu); return 0; }
		int command = (int)TrackPopupMenuEx (menu, flags, point.x, point.y, hwnd, NULL);
		EndWindowsMenuTracking (hwnd);

		// Lets the menu close properly after it is dismissed (see TrackPopupMenu)
		PostMessageW (hwnd, WM_NULL, 0, 0);

		ReleaseWindowsMenuAppearance (menu);
		DestroyMenu (menu);
		return command > MENU_COMMAND_BASE ? command - MENU_COMMAND_BASE : 0;

	}


	void TrayIcon::SetIcon (Image* imageBuffer) {
		if (!Menu::IsOwnerThread ()) return;

		Win32TrayIcon* data = (Win32TrayIcon*)platform;

		if (!data) return;

		HICON previous = data->icon;
		data->icon = CreateIconFromImage (imageBuffer);
		UpdateNotifyIcon (this, NIM_MODIFY);

		if (previous) DestroyIcon (previous);

	}


	bool TrayIcon::SetMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		// The menu is shown by PopupMenu when the icon is right-clicked
		return false;

	}


	void TrayIcon::SetTooltip (const char* tooltip) {
		if (!Menu::IsOwnerThread ()) return;

		Win32TrayIcon* data = (Win32TrayIcon*)platform;

		if (!data) return;

		data->tooltip = ToWide (tooltip ? std::string (tooltip) : std::string ());
		UpdateNotifyIcon (this, NIM_MODIFY);

	}


	void TrayIcon::Update () {
		if (!Menu::IsOwnerThread ()) return;
		if (!trayIconWindow) return;
		MSG message;
		// Only our window is pumped; leave the host application's messages alone.
		for (int i = 0; i < 64 && PeekMessageW (&message, trayIconWindow, 0, 0, PM_REMOVE); ++i) {
			TranslateMessage (&message); DispatchMessageW (&message);
		}
	}


}


#endif

#ifdef NATIVE_DESKTOP_WINDOWS
namespace native_desktop {
bool TrayIcon::SetIconFile (const char* path) {
		if (!Menu::IsOwnerThread ()) return false;
	if (!platform || !path) return false;
	auto state = (Win32TrayIcon*)platform;
	HICON icon = (HICON)LoadImageW (NULL, ToWide (path).c_str (), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
	if (!icon) return false;
	HICON previous = state->icon; state->icon = icon;
	bool result = UpdateNotifyIcon (this, NIM_MODIFY);
	if (previous) DestroyIcon (previous);
	return result;
}
}
#endif
