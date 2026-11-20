#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_LINUX

// Stacking, moving and resizing windows on X11, through the window manager's EWMH messages. libX11 is loaded
// at run time, as an X11 host already has it, and none of this needs GTK.

#include <dlfcn.h>
#include <cstring>


namespace native_desktop {


	// The parts of XClientMessageEvent this sends, laid out as Xlib's
	struct X11ClientMessage {

		int type;
		unsigned long serial;
		int sendEvent;
		void* display;
		unsigned long window;
		unsigned long messageType;
		int format;
		long data[5];

	};


	union X11Event {

		X11ClientMessage clientMessage;
		long padding[24];

	};


	struct X11Library {

		bool loaded;
		bool available;
		unsigned long (*XInternAtom) (void*, const char*, int);
		unsigned long (*XDefaultRootWindow) (void*);
		int (*XSendEvent) (void*, unsigned long, int, long, X11Event*);
		int (*XFlush) (void*);
		int (*XUngrabPointer) (void*, unsigned long);
		int (*XLowerWindow) (void*, unsigned long);
		int (*XQueryPointer) (void*, unsigned long, unsigned long*, unsigned long*, int*, int*, int*, int*, unsigned int*);

	};


	static X11Library x11;
	static const int CLIENT_MESSAGE = 33;
	static const long SUBSTRUCTURE_REDIRECT_MASK = 1L << 20;
	static const long SUBSTRUCTURE_NOTIFY_MASK = 1L << 19;
	static const unsigned int BUTTON1_MASK = 1 << 8;


	static bool LoadX11 () {

		if (x11.loaded) return x11.available;
		x11.loaded = true;

		void* library = dlopen ("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
		if (!library) return false;

		#define LIME_X11_REQUIRE(name) x11.name = (decltype (x11.name))dlsym (library, #name); if (!x11.name) return false;
		LIME_X11_REQUIRE (XInternAtom);
		LIME_X11_REQUIRE (XDefaultRootWindow);
		LIME_X11_REQUIRE (XSendEvent);
		LIME_X11_REQUIRE (XFlush);
		LIME_X11_REQUIRE (XUngrabPointer);
		LIME_X11_REQUIRE (XLowerWindow);
		LIME_X11_REQUIRE (XQueryPointer);
		#undef LIME_X11_REQUIRE

		x11.available = true;
		return true;

	}


	static bool IsX11Window (Window* window) {

		return window && window->display && window->handle && LoadX11 ();

	}


	// A message to the window manager about the window, sent to the root window as EWMH asks
	static bool SendToWindowManager (Window* window, const char* atom, long a, long b, long c, long d, long e) {

		void* display = window->display;
		unsigned long root = x11.XDefaultRootWindow (display);

		X11Event event;
		memset (&event, 0, sizeof (event));
		event.clientMessage.type = CLIENT_MESSAGE;
		event.clientMessage.display = display;
		event.clientMessage.window = (unsigned long)(uintptr_t)window->handle;
		event.clientMessage.messageType = x11.XInternAtom (display, atom, 0);
		event.clientMessage.format = 32;
		event.clientMessage.data[0] = a;
		event.clientMessage.data[1] = b;
		event.clientMessage.data[2] = c;
		event.clientMessage.data[3] = d;
		event.clientMessage.data[4] = e;

		bool sent = x11.XSendEvent (display, root, 0, SUBSTRUCTURE_REDIRECT_MASK | SUBSTRUCTURE_NOTIFY_MASK, &event) != 0;
		x11.XFlush (display);
		return sent;

	}


	// _NET_RESTACK_WINDOW: the source (2 for an application acting for the user), the sibling, and Above (0) or Below (1)
	static bool Restack (Window* window, Window* other, long detail) {

		if (!IsX11Window (window) || !other || !other->handle || other->handle == window->handle) return false;
		return SendToWindowManager (window, "_NET_RESTACK_WINDOW", 2, (long)(uintptr_t)other->handle, detail, 0, 0);

	}


	// _NET_WM_MOVERESIZE: where the pointer is, the direction (0 to 7 for the edges and corners from the top
	// left, clockwise, and 8 to move), the button and the source. The window manager then follows the pointer.
	static bool MoveResize (Window* window, long direction) {

		if (!IsX11Window (window)) return false;

		void* display = window->display;
		unsigned long root = x11.XDefaultRootWindow (display), child;
		int rootX, rootY, windowX, windowY;
		unsigned int mask;

		if (!x11.XQueryPointer (display, root, &root, &child, &rootX, &rootY, &windowX, &windowY, &mask) || !(mask & BUTTON1_MASK)) return false;

		// SDL holds the pointer while a button is down, which would keep it from the window manager
		x11.XUngrabPointer (display, 0);
		return SendToWindowManager (window, "_NET_WM_MOVERESIZE", rootX, rootY, direction, 1, 1);

	}


	bool WindowControl::OrderToBack (Window* window) {

		if (!IsX11Window (window)) return false;

		x11.XLowerWindow (window->display, (unsigned long)(uintptr_t)window->handle);
		x11.XFlush (window->display);
		return true;

	}


	bool WindowControl::OrderAbove (Window* window, Window* other) {

		return Restack (window, other, 0);

	}


	bool WindowControl::OrderBelow (Window* window, Window* other) {

		return Restack (window, other, 1);

	}


	bool WindowControl::StartMove (Window* window) {

		return MoveResize (window, 8);

	}


	bool WindowControl::StartResize (Window* window, int edges) {

		bool top = (edges & WINDOW_EDGE_TOP) != 0, bottom = (edges & WINDOW_EDGE_BOTTOM) != 0;
		bool left = (edges & WINDOW_EDGE_LEFT) != 0, right = (edges & WINDOW_EDGE_RIGHT) != 0;
		long direction;

		if (top && left) direction = 0;
		else if (top && right) direction = 2;
		else if (bottom && right) direction = 4;
		else if (bottom && left) direction = 6;
		else if (top) direction = 1;
		else if (right) direction = 3;
		else if (bottom) direction = 5;
		else if (left) direction = 7;
		else return false;

		return MoveResize (window, direction);

	}


}


#endif
