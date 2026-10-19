#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_LINUX

// Native menus and tray icons for Linux

#include <native_desktop/Desktop.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace native_desktop {


	// GTK 3 and AppIndicator are optional runtime dependencies.
	// X11 popup menus and desktop-exported tray menus are supported.

	typedef void (*GCallbackFunc) (void);
	typedef void (*GtkMenuPositionFunc) (void* menu, int* x, int* y, int* pushIn, void* userData);


	struct GTKLibrary {

		bool initialized;
		bool available;

		void (*gdk_set_allowed_backends) (const char*);
		int (*g_main_context_iteration) (void*, int);
		void* (*g_object_ref_sink) (void*);
		void (*g_object_unref) (void*);
		void (*g_object_get) (void*, const char*, ...);
		void (*g_free) (void*);
		unsigned int (*g_timeout_add) (unsigned int, int (*)(void*), void*);
		int (*g_source_remove) (unsigned int);
		void* (*gtk_settings_get_default) ();
		void* (*gtk_css_provider_get_named) (const char*, const char*);
		void* (*gtk_widget_get_style_context) (void*);
		void (*gtk_style_context_add_provider) (void*, void*, unsigned int);
		unsigned long (*g_signal_connect_data) (void*, const char*, GCallbackFunc, void*, void*, int);
		void (*gtk_accel_label_set_accel) (void*, unsigned int, int);
		void* (*gtk_bin_get_child) (void*);
		void* (*gtk_check_menu_item_new_with_mnemonic) (const char*);
		void (*gtk_check_menu_item_set_active) (void*, int);
		void (*gtk_disable_setlocale) (void);
		int (*gtk_events_pending) (void);
		int (*gtk_init_check) (int*, char***);
		void (*gtk_main) (void);
		int (*gtk_main_iteration) (void);
		void (*gtk_main_quit) (void);
		void* (*gtk_menu_item_new_with_mnemonic) (const char*);
		void (*gtk_menu_item_set_submenu) (void*, void*);
		void* (*gtk_menu_new) (void);
		void (*gtk_menu_popup) (void*, void*, void*, GtkMenuPositionFunc, void*, unsigned int, unsigned int);
		void (*gtk_menu_shell_append) (void*, void*);
		void* (*gtk_separator_menu_item_new) (void);
		void (*gtk_widget_destroy) (void*);
		void* (*gtk_widget_get_toplevel) (void*);
		int (*gtk_widget_get_visible) (void*);
		void (*gtk_widget_set_sensitive) (void*, int);
		void (*gtk_widget_show_all) (void*);
		int (*XFlush) (void*);
		int (*XUngrabPointer) (void*, unsigned long);

	};


	struct PopupPosition {

		int x;
		int y;

	};


	static GTKLibrary gtk;
	static std::string displayBackend;
	void Menu::SetDisplayBackend (const char* backend) {
		if (Menu::IsOwnerThread () && !gtk.initialized) displayBackend = backend ? backend : "";
	}
	static int popupSelection = 0;


	static bool LoadGTK () {

		if (gtk.initialized) return gtk.available;

		gtk.initialized = true;

		void* library = dlopen ("libgtk-3.so.0", RTLD_NOW | RTLD_LOCAL);

		if (!library) return false;

		#define LIME_GTK_REQUIRE(name) gtk.name = (decltype (gtk.name))dlsym (library, #name); if (!gtk.name) return false;
		#define LIME_GTK_OPTIONAL(name) gtk.name = (decltype (gtk.name))dlsym (library, #name);

		LIME_GTK_REQUIRE (g_main_context_iteration);
		LIME_GTK_REQUIRE (g_object_ref_sink);
		LIME_GTK_REQUIRE (g_object_unref);
		LIME_GTK_REQUIRE (g_signal_connect_data);
		LIME_GTK_REQUIRE (gtk_bin_get_child);
		LIME_GTK_REQUIRE (gtk_check_menu_item_new_with_mnemonic);
		LIME_GTK_REQUIRE (gtk_check_menu_item_set_active);
		LIME_GTK_REQUIRE (gtk_events_pending);
		LIME_GTK_REQUIRE (gtk_init_check);
		LIME_GTK_REQUIRE (gtk_main);
		LIME_GTK_REQUIRE (gtk_main_iteration);
		LIME_GTK_REQUIRE (gtk_main_quit);
		LIME_GTK_REQUIRE (gtk_menu_item_new_with_mnemonic);
		LIME_GTK_REQUIRE (gtk_menu_item_set_submenu);
		LIME_GTK_REQUIRE (gtk_menu_new);
		LIME_GTK_REQUIRE (gtk_menu_popup);
		LIME_GTK_REQUIRE (gtk_menu_shell_append);
		LIME_GTK_REQUIRE (gtk_separator_menu_item_new);
		LIME_GTK_REQUIRE (gtk_widget_destroy);
		LIME_GTK_REQUIRE (gtk_widget_get_toplevel);
		LIME_GTK_REQUIRE (gtk_widget_get_visible);
		LIME_GTK_REQUIRE (gtk_widget_set_sensitive);
		LIME_GTK_REQUIRE (gtk_widget_show_all);

		LIME_GTK_OPTIONAL (gdk_set_allowed_backends);
		LIME_GTK_OPTIONAL (gtk_accel_label_set_accel);
		LIME_GTK_OPTIONAL (gtk_disable_setlocale);
		LIME_GTK_OPTIONAL (g_object_get);
		LIME_GTK_OPTIONAL (g_free);
		LIME_GTK_OPTIONAL (g_timeout_add);
		LIME_GTK_OPTIONAL (g_source_remove);
		LIME_GTK_OPTIONAL (gtk_settings_get_default);
		LIME_GTK_OPTIONAL (gtk_css_provider_get_named);
		LIME_GTK_OPTIONAL (gtk_widget_get_style_context);
		LIME_GTK_OPTIONAL (gtk_style_context_add_provider);
		LIME_GTK_OPTIONAL (XFlush);
		LIME_GTK_OPTIONAL (XUngrabPointer);

		#undef LIME_GTK_REQUIRE
		#undef LIME_GTK_OPTIONAL

		// Keep the application's locale.
		if (gtk.gtk_disable_setlocale) gtk.gtk_disable_setlocale ();
		if (gtk.gdk_set_allowed_backends && !displayBackend.empty ()) gtk.gdk_set_allowed_backends (displayBackend.c_str ());

		if (!gtk.gtk_init_check (NULL, NULL)) return false;

		gtk.available = true;
		return true;

	}


	static unsigned int GetKeyval (int keyCode) {

		// Printable ASCII matches GDK keysyms; letter keycodes are lowercase.
		if (keyCode >= 0x20 && keyCode < 0x7F) return (unsigned int)keyCode;
		if (keyCode >= KEY_F1 && keyCode <= KEY_F12) return 0xFFBE + (keyCode - KEY_F1);
		if (keyCode >= KEY_F13 && keyCode <= KEY_F24) return 0xFFCA + (keyCode - KEY_F13);

		switch (keyCode) {

			case KEY_BACKSPACE: return 0xFF08;
			case KEY_TAB: return 0xFF09;
			case KEY_RETURN: return 0xFF0D;
			case KEY_ESCAPE: return 0xFF1B;
			case KEY_HOME: return 0xFF50;
			case KEY_LEFT: return 0xFF51;
			case KEY_UP: return 0xFF52;
			case KEY_RIGHT: return 0xFF53;
			case KEY_DOWN: return 0xFF54;
			case KEY_PAGEUP: return 0xFF55;
			case KEY_PAGEDOWN: return 0xFF56;
			case KEY_END: return 0xFF57;
			case KEY_INSERT: return 0xFF63;
			case KEY_DELETE: return 0xFFFF;
			default: return 0;

		}

	}


	static int GetModifierMask (int keyModifiers) {

		int mask = 0;

		if (keyModifiers & MOD_SHIFT) mask |= (1 << 0);
		if (keyModifiers & MOD_CTRL) mask |= (1 << 2);
		if (keyModifiers & MOD_ALT) mask |= (1 << 3);
		if (keyModifiers & MOD_GUI) mask |= (1 << 26);

		return mask;

	}


	// Menu item callbacks receive the tray icon ID (or zero) in the high bits and the item ID in the low 16 bits

	static void OnItemActivate (void* widget, void* userData) {

		popupSelection = (int)((intptr_t)userData & 0xFFFF);

	}


	static void OnTrayIconItemActivate (void* widget, void* userData) {

		intptr_t value = (intptr_t)userData;
		Menu::PushTrayIconEvent ((int)(value >> 16), TRAY_ICON_MENU_SELECT, (int)(value & 0xFFFF));

	}


	static void OnMenuDeactivate (void* widget, void* userData) {

		gtk.gtk_main_quit ();

	}
	static int AdvanceModalHost (void*) {
		if (!Menu::AdvanceModal ()) gtk.gtk_main_quit ();
		return 1;
	}


	static void PositionMenu (void* menu, int* x, int* y, int* pushIn, void* userData) {

		PopupPosition* position = (PopupPosition*)userData;
		*x = position->x;
		*y = position->y;
		*pushIn = 1;

	}


	static void ApplyMenuTheme (void* widget, void* provider) {
		if (widget && provider) gtk.gtk_style_context_add_provider (gtk.gtk_widget_get_style_context (widget), provider, 200);
	}

	static void* GetMenuTheme (Window* window) {
		int appearance = window ? window->appearance : 0;
		if (appearance != 1 && appearance != 2) return NULL;
		if (!gtk.g_object_get || !gtk.g_free || !gtk.gtk_settings_get_default || !gtk.gtk_css_provider_get_named
			|| !gtk.gtk_widget_get_style_context || !gtk.gtk_style_context_add_provider) return NULL;
		void* settings = gtk.gtk_settings_get_default ();
		if (!settings) return NULL;
		char* theme = NULL;
		gtk.g_object_get (settings, "gtk-theme-name", &theme, NULL);
		if (!theme) return NULL;
		// Preserve accessibility themes and user CSS; never change global GtkSettings.
		void* provider = strstr (theme, "HighContrast") ? NULL : gtk.gtk_css_provider_get_named (theme, appearance == 2 ? "dark" : NULL);
		gtk.g_free (theme);
		return provider; // Borrowed from GTK's theme cache.
	}

	static void AppendMenuItems (void* menu, const std::vector<MenuItem>& items, GCallbackFunc onActivate, int tag, void* provider = NULL) {
		ApplyMenuTheme (menu, provider);

		for (size_t i = 0; i < items.size (); i++) {

			const MenuItem& item = items[i];
			bool hasSubmenu = (item.flags & MENU_ITEM_SUBMENU) != 0;
			void* widget;

			if ((item.flags & MENU_ITEM_SEPARATOR) && !hasSubmenu) {

				widget = gtk.gtk_separator_menu_item_new ();

			} else {

				std::string label = Menu::GetLabel (item.label, '_');

				if ((item.flags & MENU_ITEM_CHECKED) && !hasSubmenu) {

					// Setting the active state emits "activate", so connect afterwards
					widget = gtk.gtk_check_menu_item_new_with_mnemonic (label.c_str ());
					gtk.gtk_check_menu_item_set_active (widget, 1);

				} else {

					widget = gtk.gtk_menu_item_new_with_mnemonic (label.c_str ());

				}

				gtk.gtk_widget_set_sensitive (widget, (item.flags & MENU_ITEM_ENABLED) ? 1 : 0);

				if (hasSubmenu) {

					void* submenu = gtk.gtk_menu_new ();
					AppendMenuItems (submenu, item.submenu, onActivate, tag, provider);
					gtk.gtk_menu_item_set_submenu (widget, submenu);

				} else {

					unsigned int keyval = item.keyCode != 0 ? GetKeyval (item.keyCode) : 0;
					void* accelLabel = keyval && gtk.gtk_accel_label_set_accel ? gtk.gtk_bin_get_child (widget) : NULL;

					if (accelLabel) {

						gtk.gtk_accel_label_set_accel (accelLabel, keyval, GetModifierMask (item.keyModifiers));

					}

					intptr_t userData = ((intptr_t)tag << 16) | (item.id & 0xFFFF);
					gtk.g_signal_connect_data (widget, "activate", onActivate, (void*)userData, NULL, 0);

				}

			}

			ApplyMenuTheme (widget, provider);
			ApplyMenuTheme (gtk.gtk_bin_get_child (widget), provider);
			gtk.gtk_menu_shell_append (menu, widget);

		}

	}


	int Menu::GetSupport () {
		if (!Menu::IsOwnerThread ()) return 0;

		return LoadGTK () ? MENU_SUPPORT_POPUP : 0;

	}


	int Menu::Popup (Window* window, const unsigned char* data, int length, int x, int y, bool atCursor) {
		if (!Menu::IsOwnerThread ()) return 0;

		if (!window || !window->display || !window->handle) return 0;

		std::vector<MenuItem> items;

		if (!Parse (data, length, &items) || !LoadGTK ()) return 0;

		// Release the host window's implicit pointer grab so the menu can take its own.
		if (gtk.XUngrabPointer) {

			gtk.XUngrabPointer (window->display, 0);
			if (gtk.XFlush) gtk.XFlush (window->display);

		}

		void* menu = gtk.gtk_menu_new ();
		gtk.g_object_ref_sink (menu);

		AppendMenuItems (menu, items, (GCallbackFunc)OnItemActivate, 0, GetMenuTheme (window));
		gtk.gtk_widget_show_all (menu);
		gtk.g_signal_connect_data (menu, "deactivate", (GCallbackFunc)OnMenuDeactivate, NULL, NULL, 0);

		PopupPosition position;
		position.x = window->x; position.y = window->y;
		position.x += x;
		position.y += y;

		popupSelection = 0;
		unsigned int timer = Menu::HasModalCallback () && gtk.g_timeout_add && gtk.g_source_remove ? gtk.g_timeout_add (20, AdvanceModalHost, NULL) : 0;

		gtk.gtk_menu_popup (menu, NULL, NULL, atCursor ? NULL : PositionMenu, atCursor ? NULL : &position, 0, 0);

		// GTK gives up without emitting "deactivate" if it cannot grab the pointer, so only wait for a menu that opened
		if (gtk.gtk_widget_get_visible (gtk.gtk_widget_get_toplevel (menu))) {

			gtk.gtk_main ();

		}

		if (timer) gtk.g_source_remove (timer);
		gtk.gtk_widget_destroy (menu);
		gtk.g_object_unref (menu);

		while (gtk.gtk_events_pending ()) {

			gtk.gtk_main_iteration ();

		}

		return popupSelection;

	}


	void Menu::Remove (Window* window) {
		if (!Menu::IsOwnerThread ()) return;
	}


	bool Menu::SetApplicationMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		return false;

	}


	bool Menu::SetWindowMenu (Window* window, const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		return false;

	}


	// Tray icons use AppIndicator, which publishes them to the desktop as StatusNotifierItems over
	// D-Bus, with the menu exported by dbusmenu. The desktop shows the menu, and AppIndicator does
	// not report clicks on the icon itself.

	struct AppIndicatorLibrary {

		bool initialized;
		bool available;

		void* (*app_indicator_new) (const char*, const char*, int);
		void (*app_indicator_set_icon_full) (void*, const char*, const char*);
		void (*app_indicator_set_menu) (void*, void*);
		void (*app_indicator_set_status) (void*, int);
		void (*app_indicator_set_title) (void*, const char*);

	};


	struct GTKTrayIcon {

		void* indicator;
		void* menu;
		std::string iconPath;

	};


	static const int APP_INDICATOR_CATEGORY_APPLICATION_STATUS = 0;
	static const int APP_INDICATOR_STATUS_PASSIVE = 0;
	static const int APP_INDICATOR_STATUS_ACTIVE = 1;

	static AppIndicatorLibrary appIndicator;
	static int nextTrayIconID = 0;
	static int trayIconCount = 0;


	static bool LoadAppIndicator () {

		if (appIndicator.initialized) return appIndicator.available;

		appIndicator.initialized = true;

		if (!LoadGTK ()) return false;

		void* library = dlopen ("libayatana-appindicator3.so.1", RTLD_NOW | RTLD_LOCAL);
		if (!library) library = dlopen ("libappindicator3.so.1", RTLD_NOW | RTLD_LOCAL);
		if (!library) return false;

		#define LIME_APP_INDICATOR_REQUIRE(name) appIndicator.name = (decltype (appIndicator.name))dlsym (library, #name); if (!appIndicator.name) return false;

		LIME_APP_INDICATOR_REQUIRE (app_indicator_new);
		LIME_APP_INDICATOR_REQUIRE (app_indicator_set_icon_full);
		LIME_APP_INDICATOR_REQUIRE (app_indicator_set_menu);
		LIME_APP_INDICATOR_REQUIRE (app_indicator_set_status);
		LIME_APP_INDICATOR_REQUIRE (app_indicator_set_title);

		#undef LIME_APP_INDICATOR_REQUIRE

		appIndicator.available = true;
		return true;

	}


	static void IterateMainContext () {

		for (int i = 0; i < 64 && gtk.g_main_context_iteration (NULL, 0); i++) {}

	}


	static std::string WriteIconFile (int id, Image* imageBuffer) {

		if (!imageBuffer || !imageBuffer->png || imageBuffer->pngLength <= 0) return std::string ();

		const char* directory = getenv ("XDG_RUNTIME_DIR");
		if (!directory || !directory[0]) directory = "/tmp";

		// Use a new name for each icon, since the desktop may cache icons by name
		std::string pattern = std::string (directory) + "/native-desktop-tray-XXXXXX.png";
		std::vector<char> path (pattern.begin (), pattern.end ()); path.push_back (0);
		int descriptor = mkstemps (&path[0], 4);
		if (descriptor < 0) return std::string ();
		FILE* file = fdopen (descriptor, "wb");
		if (!file) { close (descriptor); unlink (&path[0]); return std::string (); }

		size_t written = fwrite (imageBuffer->png, 1, imageBuffer->pngLength, file);
		int closeResult = fclose (file);

		if (written != (size_t)imageBuffer->pngLength || closeResult != 0) {

			unlink (&path[0]);
			return std::string ();

		}

		return std::string (&path[0]);


	}


	bool TrayIcon::IsSupported () {
		if (!Menu::IsOwnerThread ()) return false;

		return LoadAppIndicator ();

	}


	TrayIcon::TrayIcon () {
		id = 0; platform = NULL;
		if (!Menu::IsOwnerThread ()) return;

		id = ++nextTrayIconID;
		platform = NULL;

		if (!LoadAppIndicator ()) return;

		char name[64];
		snprintf (name, sizeof (name), "native-desktop-%d-%d", (int)getpid (), id);

		GTKTrayIcon* data = new GTKTrayIcon ();
		data->indicator = appIndicator.app_indicator_new (name, "application-x-executable", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
		data->menu = NULL;
		platform = data;
		trayIconCount++;

		// AppIndicator only shows icons that have a menu
		SetMenu (NULL, 0);
		appIndicator.app_indicator_set_status (data->indicator, APP_INDICATOR_STATUS_ACTIVE);
		IterateMainContext ();

	}


	TrayIcon::~TrayIcon () {

		Close ();

	}


	void TrayIcon::Close () {
		if (!Menu::IsOwnerThread ()) return;

		GTKTrayIcon* data = (GTKTrayIcon*)platform;

		if (!data) return;

		appIndicator.app_indicator_set_status (data->indicator, APP_INDICATOR_STATUS_PASSIVE);
		gtk.g_object_unref (data->indicator);

		if (data->menu) {

			gtk.gtk_widget_destroy (data->menu);
			gtk.g_object_unref (data->menu);

		}

		if (!data->iconPath.empty ()) unlink (data->iconPath.c_str ());

		delete data;
		platform = NULL;
		trayIconCount--;

		IterateMainContext ();

	}


	int TrayIcon::PopupMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return 0;

		// The desktop shows the menu exported by AppIndicator
		return 0;

	}


	void TrayIcon::SetIcon (Image* imageBuffer) {
		if (!Menu::IsOwnerThread ()) return;

		GTKTrayIcon* data = (GTKTrayIcon*)platform;

		if (!data) return;

		std::string path = WriteIconFile (id, imageBuffer);

		if (path.empty ()) return;

		appIndicator.app_indicator_set_icon_full (data->indicator, path.c_str (), "");

		if (!data->iconPath.empty ()) unlink (data->iconPath.c_str ());
		data->iconPath = path;

	}


	bool TrayIcon::SetMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		GTKTrayIcon* trayData = (GTKTrayIcon*)platform;

		if (!trayData) return false;

		std::vector<MenuItem> items;

		if (data && length > 0 && !Menu::Parse (data, length, &items)) return false;

		void* menu = gtk.gtk_menu_new ();
		gtk.g_object_ref_sink (menu);

		AppendMenuItems (menu, items, (GCallbackFunc)OnTrayIconItemActivate, id);
		gtk.gtk_widget_show_all (menu);
		appIndicator.app_indicator_set_menu (trayData->indicator, menu);

		if (trayData->menu) {

			gtk.gtk_widget_destroy (trayData->menu);
			gtk.g_object_unref (trayData->menu);

		}

		trayData->menu = menu;
		return true;

	}


	void TrayIcon::SetTooltip (const char* tooltip) {
		if (!Menu::IsOwnerThread ()) return;

		GTKTrayIcon* data = (GTKTrayIcon*)platform;

		if (!data) return;

		appIndicator.app_indicator_set_title (data->indicator, tooltip ? tooltip : "");

	}


	void TrayIcon::Update () {
		if (!Menu::IsOwnerThread ()) return;

		// AppIndicator communicates over D-Bus, which needs the GLib main context to run
		if (trayIconCount > 0) IterateMainContext ();

	}


}


#endif

#ifdef NATIVE_DESKTOP_LINUX
namespace native_desktop {
bool TrayIcon::SetIconFile (const char* path) {
		if (!Menu::IsOwnerThread ()) return false;
	if (!platform || !path || !path[0]) return false;
	auto state = (GTKTrayIcon*)platform;
	appIndicator.app_indicator_set_icon_full (state->indicator, path, ""); return true;
}
}
#endif
