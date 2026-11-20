#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_LINUX

// Drag and drop on X11 through the XDND protocol, written by hand as no toolkit owns the host's window.
//
// This keeps its own connection to the X server, with a hidden window of its own. A target window gets an
// XdndProxy property naming that window, so sources send their XDND messages here rather than to the host,
// whose own file drops stop until the proxy is removed. Drags out grab the pointer on this connection, find
// the XDND window under it, and serve the data from XdndSelection. Everything is polled from Update.
// libX11 is loaded at run time, and gdk-pixbuf too, for images, when it is there.

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <map>
#include <string>
#include <vector>


namespace native_desktop {


	namespace xdnd {


		typedef unsigned long Atom;
		typedef unsigned long XWindow;

		// Xlib's event layouts, for the events used here
		struct AnyEvent { int type; unsigned long serial; int sendEvent; void* display; XWindow window; };
		struct ClientMessageEvent { int type; unsigned long serial; int sendEvent; void* display; XWindow window; Atom messageType; int format; long data[5]; };
		struct SelectionRequestEvent { int type; unsigned long serial; int sendEvent; void* display; XWindow owner; XWindow requestor; Atom selection; Atom target; Atom property; unsigned long time; };
		struct SelectionEvent { int type; unsigned long serial; int sendEvent; void* display; XWindow requestor; Atom selection; Atom target; Atom property; unsigned long time; };
		struct PointerEvent { int type; unsigned long serial; int sendEvent; void* display; XWindow window; XWindow root; XWindow subwindow; unsigned long time; int x, y, xRoot, yRoot; unsigned int state; unsigned int detail; int sameScreen; };

		union Event {

			int type;
			AnyEvent any;
			ClientMessageEvent client;
			SelectionRequestEvent request;
			SelectionEvent selection;
			PointerEvent pointer;
			long padding[24];

		};

		static const int KEY_PRESS = 2;
		static const int BUTTON_RELEASE = 5;
		static const int MOTION_NOTIFY = 6;
		static const int SELECTION_REQUEST = 30;
		static const int SELECTION_NOTIFY = 31;
		static const int CLIENT_MESSAGE = 33;
		static const long KEY_PRESS_MASK = 1L << 0;
		static const long BUTTON_RELEASE_MASK = 1L << 3;
		static const long POINTER_MOTION_MASK = 1L << 6;
		static const int GRAB_MODE_ASYNC = 1;
		static const int PROP_MODE_REPLACE = 0;
		static const Atom XA_ATOM = 4;
		static const Atom XA_WINDOW = 33;
		static const int XDND_VERSION = 5;


		struct Library {

			bool loaded;
			bool available;
			void* (*XOpenDisplay) (const char*);
			XWindow (*XDefaultRootWindow) (void*);
			Atom (*XInternAtom) (void*, const char*, int);
			char* (*XGetAtomName) (void*, Atom);
			XWindow (*XCreateSimpleWindow) (void*, XWindow, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long);
			int (*XChangeProperty) (void*, XWindow, Atom, Atom, int, int, const unsigned char*, int);
			int (*XDeleteProperty) (void*, XWindow, Atom);
			int (*XGetWindowProperty) (void*, XWindow, Atom, long, long, int, Atom, Atom*, int*, unsigned long*, unsigned long*, unsigned char**);
			int (*XFree) (void*);
			int (*XPending) (void*);
			int (*XNextEvent) (void*, Event*);
			int (*XCheckTypedEvent) (void*, int, Event*);
			int (*XCheckTypedWindowEvent) (void*, XWindow, int, Event*);
			int (*XSendEvent) (void*, XWindow, int, long, Event*);
			int (*XFlush) (void*);
			int (*XConvertSelection) (void*, Atom, Atom, Atom, XWindow, unsigned long);
			int (*XSetSelectionOwner) (void*, Atom, XWindow, unsigned long);
			int (*XQueryPointer) (void*, XWindow, XWindow*, XWindow*, int*, int*, int*, int*, unsigned int*);
			int (*XTranslateCoordinates) (void*, XWindow, XWindow, int, int, int*, int*, XWindow*);
			int (*XGrabPointer) (void*, XWindow, int, unsigned int, int, int, XWindow, unsigned long, unsigned long);
			int (*XUngrabPointer) (void*, unsigned long);
			int (*XGrabKeyboard) (void*, XWindow, int, int, int, unsigned long);
			int (*XUngrabKeyboard) (void*, unsigned long);
			unsigned char (*XKeysymToKeycode) (void*, unsigned long);

		};


		// gdk-pixbuf, for the PNG images drags carry
		struct Pixbuf {

			bool loaded;
			bool available;
			void* (*gdk_pixbuf_loader_new) ();
			int (*gdk_pixbuf_loader_write) (void*, const unsigned char*, size_t, void**);
			int (*gdk_pixbuf_loader_close) (void*, void**);
			void* (*gdk_pixbuf_loader_get_pixbuf) (void*);
			void* (*gdk_pixbuf_new_from_data) (const unsigned char*, int, int, int, int, int, int, void*, void*);
			int (*gdk_pixbuf_save_to_buffer) (void*, char**, size_t*, const char*, void**, ...);
			int (*gdk_pixbuf_get_width) (void*);
			int (*gdk_pixbuf_get_height) (void*);
			int (*gdk_pixbuf_get_rowstride) (void*);
			int (*gdk_pixbuf_get_n_channels) (void*);
			unsigned char* (*gdk_pixbuf_get_pixels) (void*);
			void (*g_object_unref) (void*);
			void (*g_free) (void*);

		};


		static Library x;
		static Pixbuf pixbuf;
		static void* display = NULL;
		static XWindow helper = 0;
		static std::map<XWindow, uint32_t> targets;


		static bool Load () {

			if (x.loaded) return x.available;
			x.loaded = true;

			void* library = dlopen ("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
			if (!library) return false;

			#define NATIVE_DESKTOP_X11(name) x.name = (decltype (x.name))dlsym (library, #name); if (!x.name) return false;
			NATIVE_DESKTOP_X11 (XOpenDisplay);
			NATIVE_DESKTOP_X11 (XDefaultRootWindow);
			NATIVE_DESKTOP_X11 (XInternAtom);
			NATIVE_DESKTOP_X11 (XGetAtomName);
			NATIVE_DESKTOP_X11 (XCreateSimpleWindow);
			NATIVE_DESKTOP_X11 (XChangeProperty);
			NATIVE_DESKTOP_X11 (XDeleteProperty);
			NATIVE_DESKTOP_X11 (XGetWindowProperty);
			NATIVE_DESKTOP_X11 (XFree);
			NATIVE_DESKTOP_X11 (XPending);
			NATIVE_DESKTOP_X11 (XNextEvent);
			NATIVE_DESKTOP_X11 (XCheckTypedEvent);
			NATIVE_DESKTOP_X11 (XCheckTypedWindowEvent);
			NATIVE_DESKTOP_X11 (XSendEvent);
			NATIVE_DESKTOP_X11 (XFlush);
			NATIVE_DESKTOP_X11 (XConvertSelection);
			NATIVE_DESKTOP_X11 (XSetSelectionOwner);
			NATIVE_DESKTOP_X11 (XQueryPointer);
			NATIVE_DESKTOP_X11 (XTranslateCoordinates);
			NATIVE_DESKTOP_X11 (XGrabPointer);
			NATIVE_DESKTOP_X11 (XUngrabPointer);
			NATIVE_DESKTOP_X11 (XGrabKeyboard);
			NATIVE_DESKTOP_X11 (XUngrabKeyboard);
			NATIVE_DESKTOP_X11 (XKeysymToKeycode);
			#undef NATIVE_DESKTOP_X11

			// Wayland sessions run X11 applications through XWayland, which has XDND too
			display = x.XOpenDisplay (NULL);
			if (!display) return false;

			XWindow root = x.XDefaultRootWindow (display);
			helper = x.XCreateSimpleWindow (display, root, -10, -10, 1, 1, 0, 0, 0);
			if (!helper) return false;

			// The helper is the proxy for every target, so it names itself as XDND asks
			Atom version = XDND_VERSION;
			x.XChangeProperty (display, helper, x.XInternAtom (display, "XdndAware", 0), XA_ATOM, 32, PROP_MODE_REPLACE, (const unsigned char*)&version, 1);
			x.XChangeProperty (display, helper, x.XInternAtom (display, "XdndProxy", 0), XA_WINDOW, 32, PROP_MODE_REPLACE, (const unsigned char*)&helper, 1);
			x.XFlush (display);

			x.available = true;
			return true;

		}


		static bool LoadPixbuf () {

			if (pixbuf.loaded) return pixbuf.available;
			pixbuf.loaded = true;

			void* library = dlopen ("libgdk_pixbuf-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
			void* gobject = dlopen ("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
			void* glib = dlopen ("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
			if (!library || !gobject || !glib) return false;

			#define NATIVE_DESKTOP_PIXBUF(lib, name) pixbuf.name = (decltype (pixbuf.name))dlsym (lib, #name); if (!pixbuf.name) return false;
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_loader_new);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_loader_write);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_loader_close);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_loader_get_pixbuf);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_new_from_data);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_save_to_buffer);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_get_width);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_get_height);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_get_rowstride);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_get_n_channels);
			NATIVE_DESKTOP_PIXBUF (library, gdk_pixbuf_get_pixels);
			NATIVE_DESKTOP_PIXBUF (gobject, g_object_unref);
			NATIVE_DESKTOP_PIXBUF (glib, g_free);
			#undef NATIVE_DESKTOP_PIXBUF

			pixbuf.available = true;
			return true;

		}


		static Atom GetAtom (const char* name) {

			return x.XInternAtom (display, name, 0);

		}


		static std::string GetAtomName (Atom atom) {

			char* name = atom ? x.XGetAtomName (display, atom) : NULL;
			std::string result = name ? name : "";
			if (name) x.XFree (name);
			return result;

		}


		static bool GetProperty (XWindow window, Atom property, Atom type, std::vector<unsigned char>* data, Atom* actualType = NULL, int* actualFormat = NULL) {

			Atom returnedType = 0;
			int format = 0;
			unsigned long count = 0, remaining = 0;
			unsigned char* bytes = NULL;

			if (x.XGetWindowProperty (display, window, property, 0, 0x7FFFFFFF, 0, type, &returnedType, &format, &count, &remaining, &bytes) != 0) return false;

			if (bytes) {

				// Xlib hands format 32 values over as longs
				size_t size = format == 32 ? count * sizeof (long) : count * (format / 8);
				data->assign (bytes, bytes + size);
				x.XFree (bytes);

			}

			if (actualType) *actualType = returnedType;
			if (actualFormat) *actualFormat = format;
			return returnedType != 0;

		}


		static void SendClientMessage (XWindow to, XWindow window, const char* type, long a, long b, long c, long d, long e) {

			Event event;
			memset (&event, 0, sizeof (event));
			event.client.type = CLIENT_MESSAGE;
			event.client.display = display;
			event.client.window = window;
			event.client.messageType = GetAtom (type);
			event.client.format = 32;
			event.client.data[0] = a;
			event.client.data[1] = b;
			event.client.data[2] = c;
			event.client.data[3] = d;
			event.client.data[4] = e;

			x.XSendEvent (display, to, 0, 0, &event);
			x.XFlush (display);

		}


		static int ActionFromAtom (Atom atom) {

			if (atom == GetAtom ("XdndActionCopy")) return DROP_ACTION_COPY;
			if (atom == GetAtom ("XdndActionMove")) return DROP_ACTION_MOVE;
			if (atom == GetAtom ("XdndActionLink")) return DROP_ACTION_LINK;
			return DROP_ACTION_NONE;

		}


		static Atom AtomFromAction (int action) {

			if (action & DROP_ACTION_COPY) return GetAtom ("XdndActionCopy");
			if (action & DROP_ACTION_MOVE) return GetAtom ("XdndActionMove");
			if (action & DROP_ACTION_LINK) return GetAtom ("XdndActionLink");
			return 0;

		}


		static std::string DecodeURI (const std::string& text) {

			std::string result;

			for (size_t i = 0; i < text.size (); i++) {

				if (text[i] == '%' && i + 2 < text.size ()) {

					char hex[3] = { text[i + 1], text[i + 2], 0 };
					result += (char)strtol (hex, NULL, 16);
					i += 2;

				} else {

					result += text[i];

				}

			}

			return result;

		}


		static std::string EncodeURI (const std::string& path) {

			static const char* digits = "0123456789ABCDEF";
			std::string result = "file://";

			for (size_t i = 0; i < path.size (); i++) {

				unsigned char c = (unsigned char)path[i];

				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr ("/-_.~", c)) {

					result += (char)c;

				} else {

					result += '%';
					result += digits[c >> 4];
					result += digits[c & 15];

				}

			}

			return result;

		}


		// The lines of a text/uri-list, without its comments
		static std::vector<std::string> SplitURIList (const std::string& text) {

			std::vector<std::string> lines;
			size_t start = 0;

			while (start < text.size ()) {

				size_t end = text.find ('\n', start);
				if (end == std::string::npos) end = text.size ();

				std::string line = text.substr (start, end - start);
				if (!line.empty () && line[line.size () - 1] == '\r') line.erase (line.size () - 1);
				if (!line.empty () && line[0] != '#') lines.push_back (line);
				start = end + 1;

			}

			return lines;

		}


		static const char* TEXT_TYPES[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", NULL };
		static const char* HTML_TYPES[] = { "text/html", NULL };
		static const char* RICH_TEXT_TYPES[] = { "text/rtf", "application/rtf", NULL };
		static const char* URI_TYPES[] = { "text/uri-list", NULL };
		static const char* IMAGE_TYPES[] = { "image/png", NULL };


		static const char** TypesFor (int format) {

			switch (format) {

				case CLIPBOARD_TEXT: return TEXT_TYPES;
				case CLIPBOARD_HTML: return HTML_TYPES;
				case CLIPBOARD_RICH_TEXT: return RICH_TEXT_TYPES;
				case CLIPBOARD_URL:
				case CLIPBOARD_FILE_LIST: return URI_TYPES;
				case CLIPBOARD_IMAGE: return IMAGE_TYPES;
				default: return NULL;

			}

		}


		// Serves a request for the selection this owns, from a drag out or this connection's own target
		static void ServeRequest (const SelectionRequestEvent& request);


		// What a source drags here, offered as XDND types and converted from XdndSelection when read
		class X11DragData : public DragData {

			public:

				X11DragData (XWindow source, const std::vector<Atom>& types, unsigned long time) : source (source), time (time), types (types) {}

				bool HasFormat (int format) {

					if (format == CLIPBOARD_URL || format == CLIPBOARD_FILE_LIST) {

						std::string list;
						if (!Convert ("text/uri-list", &list)) return false;
						std::vector<std::string> lines = SplitURIList (list);
						bool files = !lines.empty () && lines[0].compare (0, 7, "file://") == 0;
						return format == CLIPBOARD_FILE_LIST ? files : (!lines.empty () && !files);

					}

					return FindType (format) != 0;

				}

				bool GetText (int format, std::string* text) {

					if (!text) return false;

					if (format == CLIPBOARD_URL) {

						std::string list;
						if (!Convert ("text/uri-list", &list)) return false;
						std::vector<std::string> lines = SplitURIList (list);
						if (lines.empty () || lines[0].compare (0, 7, "file://") == 0) return false;
						*text = lines[0];
						return true;

					}

					Atom type = FindType (format);
					return type && Convert (GetAtomName (type).c_str (), text);

				}

				bool GetFiles (std::vector<std::string>* files) {

					std::string list;
					if (!files || !Convert ("text/uri-list", &list)) return false;

					files->clear ();
					std::vector<std::string> lines = SplitURIList (list);

					for (size_t i = 0; i < lines.size (); i++) {

						if (lines[i].compare (0, 7, "file://") != 0) continue;

						// file://host/path, where the host is usually empty
						size_t slash = lines[i].find ('/', 7);
						if (slash != std::string::npos) files->push_back (DecodeURI (lines[i].substr (slash)));

					}

					return !files->empty ();

				}

				bool GetImage (std::vector<unsigned char>* pixels, int* width, int* height) {

					std::string png;
					if (!pixels || !width || !height || !LoadPixbuf () || !Convert ("image/png", &png) || png.empty ()) return false;

					void* loader = pixbuf.gdk_pixbuf_loader_new ();
					bool result = false;

					if (pixbuf.gdk_pixbuf_loader_write (loader, (const unsigned char*)png.data (), png.size (), NULL) && pixbuf.gdk_pixbuf_loader_close (loader, NULL)) {

						void* image = pixbuf.gdk_pixbuf_loader_get_pixbuf (loader);

						if (image) {

							int w = pixbuf.gdk_pixbuf_get_width (image);
							int h = pixbuf.gdk_pixbuf_get_height (image);
							int stride = pixbuf.gdk_pixbuf_get_rowstride (image);
							int channels = pixbuf.gdk_pixbuf_get_n_channels (image);
							const unsigned char* data = pixbuf.gdk_pixbuf_get_pixels (image);

							pixels->resize ((size_t)w * h * 4);

							for (int y = 0; y < h; y++) {

								for (int i = 0; i < w; i++) {

									const unsigned char* source = data + (size_t)y * stride + (size_t)i * channels;
									unsigned char* target = &(*pixels)[((size_t)y * w + i) * 4];
									target[0] = source[0];
									target[1] = source[1];
									target[2] = source[2];
									target[3] = channels == 4 ? source[3] : 255;

								}

							}

							*width = w;
							*height = h;
							result = w > 0 && h > 0;

						}

					}

					pixbuf.g_object_unref (loader);
					return result;

				}

			private:

				Atom FindType (int format) {

					const char** names = TypesFor (format);

					for (int i = 0; names && names[i]; i++) {

						Atom atom = GetAtom (names[i]);
						for (size_t j = 0; j < types.size (); j++) if (types[j] == atom) return atom;

					}

					return 0;

				}

				// Asks the source for the selection as type and waits for it, serving this connection's own
				// requests meanwhile, as a drag from this application to itself needs
				bool Convert (const char* typeName, std::string* result) {

					Atom type = GetAtom (typeName);
					bool offered = false;
					for (size_t i = 0; i < types.size (); i++) if (types[i] == type) offered = true;
					if (!offered) return false;

					std::map<Atom, std::string>::iterator cached = cache.find (type);
					if (cached != cache.end ()) { *result = cached->second; return true; }

					Atom property = GetAtom ("NATIVE_DESKTOP_DRAG");
					x.XConvertSelection (display, GetAtom ("XdndSelection"), type, property, helper, time);
					x.XFlush (display);

					for (int i = 0; i < 400; i++) {

						Event event;

						while (x.XCheckTypedEvent (display, SELECTION_REQUEST, &event)) ServeRequest (event.request);

						if (x.XCheckTypedWindowEvent (display, helper, SELECTION_NOTIFY, &event)) {

							if (event.selection.property == 0) return false;

							std::vector<unsigned char> data;
							Atom actualType = 0;
							GetProperty (helper, property, 0, &data, &actualType);
							x.XDeleteProperty (display, helper, property);

							// Large transfers in parts are not read
							if (actualType == GetAtom ("INCR")) return false;

							result->assign (data.begin (), data.end ());
							cache[type] = *result;
							return true;

						}

						struct timespec wait = { 0, 5000000 };
						nanosleep (&wait, NULL);

					}

					return false;

				}

				std::map<Atom, std::string> cache;
				XWindow source;
				unsigned long time;
				std::vector<Atom> types;

		};


		// A drag coming in
		struct Incoming {

			bool active;
			bool entered;
			int lastAction;
			int allowed;
			XWindow source;
			XWindow target;
			std::vector<Atom> types;
			int version;
			int x, y;

		};

		static Incoming incoming = { false, false, 0, 0, 0, 0, std::vector<Atom> (), 0, 0, 0 };


		// A drag going out
		struct Outgoing {

			bool active;
			bool dropped;
			int allowed;
			int id;
			int accepted;
			Atom acceptedAction;
			std::map<Atom, std::string> data;
			XWindow target;
			XWindow targetProxy;
			int targetVersion;
			std::vector<Atom> types;
			struct timespec droppedAt;

		};

		static Outgoing outgoing;


		static void ServeRequest (const SelectionRequestEvent& request) {

			Event reply;
			memset (&reply, 0, sizeof (reply));
			reply.selection.type = SELECTION_NOTIFY;
			reply.selection.display = display;
			reply.selection.requestor = request.requestor;
			reply.selection.selection = request.selection;
			reply.selection.target = request.target;
			reply.selection.time = request.time;
			reply.selection.property = 0;

			Atom property = request.property ? request.property : request.target;

			if (request.target == GetAtom ("TARGETS")) {

				std::vector<Atom> list = outgoing.types;
				list.push_back (GetAtom ("TARGETS"));
				x.XChangeProperty (display, request.requestor, property, XA_ATOM, 32, PROP_MODE_REPLACE, (const unsigned char*)&list[0], (int)list.size ());
				reply.selection.property = property;

			} else {

				std::map<Atom, std::string>::iterator found = outgoing.data.find (request.target);

				if (found != outgoing.data.end ()) {

					x.XChangeProperty (display, request.requestor, property, request.target, 8, PROP_MODE_REPLACE, (const unsigned char*)found->second.data (), (int)found->second.size ());
					reply.selection.property = property;

				}

			}

			x.XSendEvent (display, request.requestor, 0, 0, &reply);
			x.XFlush (display);

		}


		static int SendIncoming (int type, unsigned long time) {

			X11DragData data (incoming.source, incoming.types, time);
			DragEvent event = { type, targets.count (incoming.target) ? targets[incoming.target] : 0, incoming.x, incoming.y, incoming.allowed };
			return DragDrop::Dispatch (event, type == DRAG_LEAVE ? NULL : &data);

		}


		static void HandleIncoming (const ClientMessageEvent& message) {

			std::string name = GetAtomName (message.messageType);

			if (name == "XdndEnter") {

				incoming.active = true;
				incoming.entered = false;
				incoming.source = (XWindow)message.data[0];
				incoming.target = message.window;
				incoming.version = (int)((unsigned long)message.data[1] >> 24);
				incoming.types.clear ();

				if (message.data[1] & 1) {

					std::vector<unsigned char> list;
					GetProperty (incoming.source, GetAtom ("XdndTypeList"), XA_ATOM, &list);
					for (size_t i = 0; i + sizeof (long) <= list.size (); i += sizeof (long)) incoming.types.push_back (*(const unsigned long*)&list[i]);

				} else {

					for (int i = 2; i < 5; i++) if (message.data[i]) incoming.types.push_back ((Atom)message.data[i]);

				}

			} else if (name == "XdndPosition" && incoming.active) {

				int rootX = (int)(((unsigned long)message.data[2] >> 16) & 0xFFFF);
				int rootY = (int)((unsigned long)message.data[2] & 0xFFFF);
				XWindow child = 0;
				x.XTranslateCoordinates (display, x.XDefaultRootWindow (display), incoming.target, rootX, rootY, &incoming.x, &incoming.y, &child);

				Atom action = (Atom)message.data[4];
				incoming.allowed = ActionFromAtom (action);

				// XdndActionAsk lists what the source offers
				if (incoming.allowed == DROP_ACTION_NONE) {

					std::vector<unsigned char> list;
					GetProperty (incoming.source, GetAtom ("XdndActionList"), XA_ATOM, &list);
					for (size_t i = 0; i + sizeof (long) <= list.size (); i += sizeof (long)) incoming.allowed |= ActionFromAtom (*(const unsigned long*)&list[i]);
					if (incoming.allowed == DROP_ACTION_NONE) incoming.allowed = DROP_ACTION_COPY;

				}

				int result = SendIncoming (incoming.entered ? DRAG_OVER : DRAG_ENTER, (unsigned long)message.data[3]);
				incoming.entered = true;
				incoming.lastAction = result;

				// Accepted or not, and more positions wanted while it moves
				SendClientMessage (incoming.source, incoming.source, "XdndStatus", (long)incoming.target, (result ? 1 : 0) | 2, 0, 0, (long)AtomFromAction (result));

			} else if (name == "XdndLeave" && incoming.active) {

				if (incoming.entered) SendIncoming (DRAG_LEAVE, 0);
				incoming.active = false;

			} else if (name == "XdndDrop" && incoming.active) {

				int result = incoming.lastAction ? SendIncoming (DRAG_DROP, (unsigned long)message.data[2]) : DROP_ACTION_NONE;
				if (!incoming.lastAction && incoming.entered) SendIncoming (DRAG_LEAVE, 0);

				SendClientMessage (incoming.source, incoming.source, "XdndFinished", (long)incoming.target, result ? 1 : 0, (long)AtomFromAction (result), 0, 0);
				incoming.active = false;

			}

		}


		// The XDND window under the pointer, as a source finds it: the first window down from the root with
		// XdndAware, and the window its XdndProxy names
		static XWindow FindTarget (int rootX, int rootY, XWindow* proxy, int* version) {

			XWindow window = x.XDefaultRootWindow (display);
			Atom aware = GetAtom ("XdndAware");

			for (int depth = 0; depth < 32; depth++) {

				XWindow root = 0, child = 0;
				int rx, ry, wx, wy;
				unsigned int mask;
				if (!x.XQueryPointer (display, window, &root, &child, &rx, &ry, &wx, &wy, &mask) || !child) return 0;
				window = child;

				std::vector<unsigned char> data;

				if (GetProperty (window, aware, XA_ATOM, &data) && data.size () >= sizeof (long)) {

					*version = (int)*(const unsigned long*)&data[0];
					*proxy = window;

					std::vector<unsigned char> named;

					if (GetProperty (window, GetAtom ("XdndProxy"), XA_WINDOW, &named) && named.size () >= sizeof (long)) {

						*proxy = (XWindow)*(const unsigned long*)&named[0];

					}

					return window;

				}

			}

			return 0;

		}


		static void FinishOutgoing (int action) {

			x.XUngrabPointer (display, 0);
			x.XUngrabKeyboard (display, 0);
			x.XFlush (display);
			outgoing.active = false;
			DragDrop::PushComplete (outgoing.id, action);

		}


		static void MoveOutgoing (int rootX, int rootY, unsigned long time) {

			XWindow proxy = 0;
			int version = 0;
			XWindow target = FindTarget (rootX, rootY, &proxy, &version);

			if (target != outgoing.target) {

				if (outgoing.target) SendClientMessage (outgoing.targetProxy, outgoing.target, "XdndLeave", (long)helper, 0, 0, 0, 0);

				outgoing.target = target;
				outgoing.targetProxy = proxy;
				outgoing.targetVersion = version < XDND_VERSION ? version : XDND_VERSION;
				outgoing.accepted = 0;

				if (target) {

					long types[3] = { 0, 0, 0 };
					for (size_t i = 0; i < outgoing.types.size () && i < 3; i++) types[i] = (long)outgoing.types[i];
					long flags = ((long)outgoing.targetVersion << 24) | (outgoing.types.size () > 3 ? 1 : 0);
					SendClientMessage (proxy, target, "XdndEnter", (long)helper, flags, types[0], types[1], types[2]);

				}

			}

			if (target) {

				Atom action = AtomFromAction (outgoing.allowed);
				SendClientMessage (outgoing.targetProxy, target, "XdndPosition", (long)helper, 0, ((long)rootX << 16) | (rootY & 0xFFFF), (long)time, (long)action);

			}

		}


		static void HandleOutgoing (const Event& event) {

			if (event.type == MOTION_NOTIFY) {

				MoveOutgoing (event.pointer.xRoot, event.pointer.yRoot, event.pointer.time);

			} else if (event.type == BUTTON_RELEASE) {

				if (outgoing.target && outgoing.accepted) {

					SendClientMessage (outgoing.targetProxy, outgoing.target, "XdndDrop", (long)helper, 0, (long)event.pointer.time, 0, 0);
					outgoing.dropped = true;
					clock_gettime (CLOCK_MONOTONIC, &outgoing.droppedAt);
					x.XUngrabPointer (display, 0);
					x.XUngrabKeyboard (display, 0);

				} else {

					if (outgoing.target) SendClientMessage (outgoing.targetProxy, outgoing.target, "XdndLeave", (long)helper, 0, 0, 0, 0);
					FinishOutgoing (DROP_ACTION_NONE);

				}

			} else if (event.type == KEY_PRESS && event.pointer.detail == x.XKeysymToKeycode (display, 0xFF1B)) {

				// Escape
				if (outgoing.target) SendClientMessage (outgoing.targetProxy, outgoing.target, "XdndLeave", (long)helper, 0, 0, 0, 0);
				FinishOutgoing (DROP_ACTION_NONE);

			}

		}


	}


	using namespace xdnd;


	bool DragDrop::IsSupported () {

		return Load ();

	}


	bool DragDrop::SetTarget (Window* window, bool enabled) {

		if (!window || !window->handle || !Menu::IsOwnerThread () || !Load ()) return false;

		XWindow handle = (XWindow)(uintptr_t)window->handle;
		Atom proxy = GetAtom ("XdndProxy");

		if (enabled) {

			// Sources send XDND messages for the window to the helper instead
			x.XChangeProperty (display, handle, proxy, XA_WINDOW, 32, PROP_MODE_REPLACE, (const unsigned char*)&helper, 1);

			Atom version = XDND_VERSION;
			x.XChangeProperty (display, handle, GetAtom ("XdndAware"), XA_ATOM, 32, PROP_MODE_REPLACE, (const unsigned char*)&version, 1);
			targets[handle] = window->id;

		} else {

			x.XDeleteProperty (display, handle, proxy);
			targets.erase (handle);

		}

		x.XFlush (display);
		return true;

	}


	int DragDrop::StartDrag (Window* window, const std::vector<ClipboardItem>& items, Image* image, int offsetX, int offsetY, int allowedActions, int id) {

		if (!window || !window->handle || !Menu::IsOwnerThread () || !Load () || outgoing.active) return DROP_ACTION_NONE;

		outgoing.data.clear ();
		outgoing.types.clear ();
		std::string uris;

		for (size_t i = 0; i < items.size (); i++) {

			const ClipboardItem& item = items[i];

			switch (item.format) {

				case CLIPBOARD_TEXT:
					for (int j = 0; TEXT_TYPES[j]; j++) outgoing.data[GetAtom (TEXT_TYPES[j])] = item.text;
					break;

				case CLIPBOARD_HTML:
					outgoing.data[GetAtom ("text/html")] = item.text;
					break;

				case CLIPBOARD_RICH_TEXT:
					for (int j = 0; RICH_TEXT_TYPES[j]; j++) outgoing.data[GetAtom (RICH_TEXT_TYPES[j])] = item.text;
					break;

				case CLIPBOARD_URL:
					uris += item.text + "\r\n";
					break;

				case CLIPBOARD_FILE_LIST:
					for (size_t j = 0; j < item.files.size (); j++) uris += EncodeURI (item.files[j]) + "\r\n";
					break;

				case CLIPBOARD_IMAGE: {

					if (!LoadPixbuf () || item.width <= 0 || item.height <= 0 || item.pixels.size () < (size_t)item.width * item.height * 4) break;

					// RGBA with alpha, 8 bits a sample
					void* picture = pixbuf.gdk_pixbuf_new_from_data (&item.pixels[0], 0, 1, 8, item.width, item.height, item.width * 4, NULL, NULL);
					char* png = NULL;
					size_t size = 0;

					if (picture && pixbuf.gdk_pixbuf_save_to_buffer (picture, &png, &size, "png", NULL, NULL) && png) {

						outgoing.data[GetAtom ("image/png")] = std::string (png, size);
						pixbuf.g_free (png);

					}

					if (picture) pixbuf.g_object_unref (picture);
					break;

				}

				default: break;

			}

		}

		if (!uris.empty ()) outgoing.data[GetAtom ("text/uri-list")] = uris;
		if (outgoing.data.empty ()) return DROP_ACTION_NONE;

		for (std::map<Atom, std::string>::iterator i = outgoing.data.begin (); i != outgoing.data.end (); ++i) outgoing.types.push_back (i->first);
		x.XChangeProperty (display, helper, GetAtom ("XdndTypeList"), XA_ATOM, 32, PROP_MODE_REPLACE, (const unsigned char*)&outgoing.types[0], (int)outgoing.types.size ());

		// The host holds the pointer while its button is down, and lets it go so this connection can follow
		// it to other windows
		if (window->display) {

			x.XUngrabPointer (window->display, 0);
			x.XFlush (window->display);

		}

		XWindow root = x.XDefaultRootWindow (display);

		if (x.XGrabPointer (display, root, 0, (unsigned int)(BUTTON_RELEASE_MASK | POINTER_MOTION_MASK), GRAB_MODE_ASYNC, GRAB_MODE_ASYNC, 0, 0, 0) != 0) return DROP_ACTION_NONE;
		x.XGrabKeyboard (display, root, 0, GRAB_MODE_ASYNC, GRAB_MODE_ASYNC, 0);
		x.XSetSelectionOwner (display, GetAtom ("XdndSelection"), helper, 0);

		outgoing.active = true;
		outgoing.dropped = false;
		outgoing.allowed = allowedActions ? allowedActions : DROP_ACTION_COPY;
		outgoing.id = id;
		outgoing.accepted = 0;
		outgoing.acceptedAction = 0;
		outgoing.target = 0;
		outgoing.targetProxy = 0;

		XWindow rootReturn = 0, child = 0;
		int rootX = 0, rootY = 0, windowX, windowY;
		unsigned int mask = 0;
		x.XQueryPointer (display, root, &rootReturn, &child, &rootX, &rootY, &windowX, &windowY, &mask);
		MoveOutgoing (rootX, rootY, 0);

		// X11 shows the pointer's own cursor while dragging; the image is not drawn here
		(void)image; (void)offsetX; (void)offsetY;
		return -1;

	}


	void DragDrop::Update () {

		if (!x.available || !Menu::IsOwnerThread ()) return;

		while (x.XPending (display) > 0) {

			Event event;
			x.XNextEvent (display, &event);

			switch (event.type) {

				case CLIENT_MESSAGE: {

					std::string name = GetAtomName (event.client.messageType);

					if (name == "XdndStatus" && outgoing.active && (XWindow)event.client.data[0] == outgoing.target) {

						outgoing.accepted = (event.client.data[1] & 1) ? 1 : 0;
						outgoing.acceptedAction = (Atom)event.client.data[4];

					} else if (name == "XdndFinished" && outgoing.active && outgoing.dropped) {

						bool accepted = outgoing.targetVersion < 5 || (event.client.data[1] & 1);
						Atom action = outgoing.targetVersion >= 5 ? (Atom)event.client.data[2] : outgoing.acceptedAction;
						FinishOutgoing (accepted ? (ActionFromAtom (action) ? ActionFromAtom (action) : DROP_ACTION_COPY) : DROP_ACTION_NONE);

					} else {

						HandleIncoming (event.client);

					}

					break;

				}

				case SELECTION_REQUEST:

					ServeRequest (event.request);
					break;

				case MOTION_NOTIFY:
				case BUTTON_RELEASE:
				case KEY_PRESS:

					if (outgoing.active && !outgoing.dropped) HandleOutgoing (event);
					break;

				default: break;

			}

		}

		// A target that never finishes ends the drag after a while
		if (outgoing.active && outgoing.dropped) {

			struct timespec now;
			clock_gettime (CLOCK_MONOTONIC, &now);
			if (now.tv_sec - outgoing.droppedAt.tv_sec > 5) FinishOutgoing (outgoing.accepted ? ActionFromAtom (outgoing.acceptedAction) : DROP_ACTION_NONE);

		}

	}


}


#elif !defined (NATIVE_DESKTOP_WINDOWS) && !defined (NATIVE_DESKTOP_MAC)


namespace native_desktop {


	bool DragDrop::IsSupported () { return false; }
	bool DragDrop::SetTarget (Window*, bool) { return false; }
	int DragDrop::StartDrag (Window*, const std::vector<ClipboardItem>&, Image*, int, int, int, int) { return DROP_ACTION_NONE; }
	void DragDrop::Update () {}


}


#endif
