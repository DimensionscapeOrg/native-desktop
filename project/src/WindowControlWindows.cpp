#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_WINDOWS

// Stacking, moving and resizing windows on Windows, which SDL has no calls for

#include <windows.h>


namespace native_desktop {


	static HWND GetControlledWindow (Window* window) {

		HWND handle = window ? (HWND)window->handle : NULL;
		return handle && IsWindow (handle) ? handle : NULL;

	}


	static bool SetStack (HWND window, HWND insertAfter) {

		return SetWindowPos (window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != 0;

	}


	// The system's own move or resize loop, as when the caption or a border is pressed. It returns when the
	// button is released, which it keeps from the window, so the release is posted for the host to see.
	static bool StartSystemDrag (HWND window, WPARAM hitTest) {

		if (!(GetAsyncKeyState (GetSystemMetrics (SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000)) return false;

		POINT point;
		GetCursorPos (&point);

		ReleaseCapture ();
		SendMessageW (window, WM_NCLBUTTONDOWN, hitTest, MAKELPARAM (point.x, point.y));

		GetCursorPos (&point);
		ScreenToClient (window, &point);
		PostMessageW (window, WM_LBUTTONUP, 0, MAKELPARAM (point.x, point.y));
		return true;

	}


	bool WindowControl::OrderToBack (Window* window) {

		HWND handle = GetControlledWindow (window);
		return handle && SetStack (handle, HWND_BOTTOM);

	}


	bool WindowControl::OrderAbove (Window* window, Window* other) {

		HWND handle = GetControlledWindow (window);
		HWND otherHandle = GetControlledWindow (other);
		if (!handle || !otherHandle || handle == otherHandle) return false;

		// Just above the other window is just below the one above it
		HWND above = GetWindow (otherHandle, GW_HWNDPREV);
		if (above == handle) return true;
		return SetStack (handle, above ? above : HWND_TOP);

	}


	bool WindowControl::OrderBelow (Window* window, Window* other) {

		HWND handle = GetControlledWindow (window);
		HWND otherHandle = GetControlledWindow (other);
		if (!handle || !otherHandle || handle == otherHandle) return false;

		return SetStack (handle, otherHandle);

	}


	bool WindowControl::StartMove (Window* window) {

		HWND handle = GetControlledWindow (window);
		return handle && StartSystemDrag (handle, HTCAPTION);

	}


	bool WindowControl::StartResize (Window* window, int edges) {

		HWND handle = GetControlledWindow (window);
		if (!handle) return false;

		bool top = (edges & WINDOW_EDGE_TOP) != 0, bottom = (edges & WINDOW_EDGE_BOTTOM) != 0;
		bool left = (edges & WINDOW_EDGE_LEFT) != 0, right = (edges & WINDOW_EDGE_RIGHT) != 0;
		WPARAM hitTest;

		if (top && left) hitTest = HTTOPLEFT;
		else if (top && right) hitTest = HTTOPRIGHT;
		else if (bottom && left) hitTest = HTBOTTOMLEFT;
		else if (bottom && right) hitTest = HTBOTTOMRIGHT;
		else if (top) hitTest = HTTOP;
		else if (bottom) hitTest = HTBOTTOM;
		else if (left) hitTest = HTLEFT;
		else if (right) hitTest = HTRIGHT;
		else return false;

		return StartSystemDrag (handle, hitTest);

	}


}


#endif
