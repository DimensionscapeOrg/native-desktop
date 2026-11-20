#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_LINUX

// Clipboard formats for Linux, on GTK 3's CLIPBOARD selection, loaded at run time like the menus

#include <dlfcn.h>
#include <cstring>
#include <string>
#include <vector>


namespace native_desktop {


	bool LoadDesktopGTK ();


	struct GtkTargetEntryData {

		char* target;
		unsigned int flags;
		unsigned int info;

	};


	typedef void (*ClipboardGetFunc) (void* clipboard, void* selection, unsigned int info, void* data);
	typedef void (*ClipboardClearFunc) (void* clipboard, void* data);


	struct GTKClipboardLibrary {

		bool loaded;
		bool available;

		void* (*gdk_atom_intern) (const char*, int);
		void* (*gtk_clipboard_get) (void*);
		int (*gtk_clipboard_wait_is_target_available) (void*, void*);
		void* (*gtk_clipboard_wait_for_contents) (void*, void*);
		char* (*gtk_clipboard_wait_for_text) (void*);
		char** (*gtk_clipboard_wait_for_uris) (void*);
		void* (*gtk_clipboard_wait_for_image) (void*);
		int (*gtk_clipboard_set_with_data) (void*, const GtkTargetEntryData*, unsigned int, ClipboardGetFunc, ClipboardClearFunc, void*);
		void (*gtk_clipboard_store) (void*);
		const unsigned char* (*gtk_selection_data_get_data) (void*);
		int (*gtk_selection_data_get_length) (void*);
		void (*gtk_selection_data_free) (void*);
		void (*gtk_selection_data_set) (void*, void*, int, const unsigned char*, int);
		int (*gtk_selection_data_set_uris) (void*, char**);
		int (*gtk_selection_data_set_pixbuf) (void*, void*);
		int (*gtk_events_pending) (void);
		int (*gtk_main_iteration_do) (int);
		int (*gdk_pixbuf_get_width) (void*);
		int (*gdk_pixbuf_get_height) (void*);
		int (*gdk_pixbuf_get_rowstride) (void*);
		int (*gdk_pixbuf_get_n_channels) (void*);
		int (*gdk_pixbuf_get_has_alpha) (void*);
		unsigned char* (*gdk_pixbuf_get_pixels) (void*);
		void* (*gdk_pixbuf_new_from_data) (const unsigned char*, int, int, int, int, int, int, void*, void*);
		void (*g_object_unref) (void*);
		void (*g_free) (void*);
		void (*g_strfreev) (char**);
		char* (*g_filename_from_uri) (const char*, char**, void**);
		char* (*g_filename_to_uri) (const char*, const char*, void**);

	};


	static GTKClipboardLibrary clipboardGTK;
	// GDK_SELECTION_CLIPBOARD, a predefined atom
	static void* const SELECTION_CLIPBOARD = (void*)(intptr_t)69;


	static bool LoadClipboardGTK () {

		if (clipboardGTK.loaded) return clipboardGTK.available;
		clipboardGTK.loaded = true;

		if (!Menu::IsOwnerThread () || !LoadDesktopGTK ()) return false;

		// Already open for the menus, so this only adds a reference
		void* library = dlopen ("libgtk-3.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!library) return false;

		#define LIME_CLIPBOARD_REQUIRE(name) clipboardGTK.name = (decltype (clipboardGTK.name))dlsym (library, #name); if (!clipboardGTK.name) return false;
		LIME_CLIPBOARD_REQUIRE (gdk_atom_intern);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_get);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_wait_is_target_available);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_wait_for_contents);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_wait_for_text);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_wait_for_uris);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_wait_for_image);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_set_with_data);
		LIME_CLIPBOARD_REQUIRE (gtk_clipboard_store);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_get_data);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_get_length);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_free);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_set);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_set_uris);
		LIME_CLIPBOARD_REQUIRE (gtk_selection_data_set_pixbuf);
		LIME_CLIPBOARD_REQUIRE (gtk_events_pending);
		LIME_CLIPBOARD_REQUIRE (gtk_main_iteration_do);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_width);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_height);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_rowstride);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_n_channels);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_has_alpha);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_get_pixels);
		LIME_CLIPBOARD_REQUIRE (gdk_pixbuf_new_from_data);
		LIME_CLIPBOARD_REQUIRE (g_object_unref);
		LIME_CLIPBOARD_REQUIRE (g_free);
		LIME_CLIPBOARD_REQUIRE (g_strfreev);
		LIME_CLIPBOARD_REQUIRE (g_filename_from_uri);
		LIME_CLIPBOARD_REQUIRE (g_filename_to_uri);
		#undef LIME_CLIPBOARD_REQUIRE

		clipboardGTK.available = true;
		return true;

	}


	static void* GetClipboard () {

		return LoadClipboardGTK () ? clipboardGTK.gtk_clipboard_get (SELECTION_CLIPBOARD) : NULL;

	}


	// The MIME types each format is offered and read as, the preferred one first
	static const char* const* GetTargets (int format) {

		static const char* const html[] = { "text/html", NULL };
		static const char* const rtf[] = { "text/rtf", "application/rtf", NULL };
		static const char* const url[] = { "text/x-moz-url", "text/uri-list", NULL };
		static const char* const none[] = { NULL };

		switch (format) {

			case CLIPBOARD_HTML: return html;
			case CLIPBOARD_RICH_TEXT: return rtf;
			case CLIPBOARD_URL: return url;
			default: return none;

		}

	}


	static bool ReadTarget (void* clipboard, const char* target, std::string* text) {

		void* selection = clipboardGTK.gtk_clipboard_wait_for_contents (clipboard, clipboardGTK.gdk_atom_intern (target, 0));
		if (!selection) return false;

		const unsigned char* data = clipboardGTK.gtk_selection_data_get_data (selection);
		int length = clipboardGTK.gtk_selection_data_get_length (selection);
		bool result = data && length >= 0;

		if (result) {

			text->assign ((const char*)data, length);

			// Firefox writes text/x-moz-url as UTF-16, the URL and then its title on the next line
			if (strcmp (target, "text/x-moz-url") == 0 && length >= 2 && data[1] == 0) {

				std::string ascii;
				for (int i = 0; i + 1 < length && data[i] != '\n'; i += 2) ascii += (char)data[i];
				*text = ascii;

			}

			// A URI list ends each line with CRLF, and a URL is its first line
			if (strcmp (target, "text/uri-list") == 0) {

				size_t end = text->find_first_of ("\r\n");
				if (end != std::string::npos) text->resize (end);

			}

		}

		clipboardGTK.gtk_selection_data_free (selection);
		return result;

	}


	bool Clipboard::IsSupported () {

		return GetClipboard () != NULL;

	}


	bool Clipboard::HasFormat (int format) {

		void* clipboard = GetClipboard ();
		if (!clipboard) return false;

		switch (format) {

			case CLIPBOARD_TEXT: return clipboardGTK.gtk_clipboard_wait_is_target_available (clipboard, clipboardGTK.gdk_atom_intern ("UTF8_STRING", 0)) != 0;
			case CLIPBOARD_FILE_LIST: return clipboardGTK.gtk_clipboard_wait_is_target_available (clipboard, clipboardGTK.gdk_atom_intern ("text/uri-list", 0)) != 0;
			case CLIPBOARD_IMAGE: return clipboardGTK.gtk_clipboard_wait_is_target_available (clipboard, clipboardGTK.gdk_atom_intern ("image/png", 0)) != 0;

			default:

				for (const char* const* target = GetTargets (format); *target; target++) {

					if (clipboardGTK.gtk_clipboard_wait_is_target_available (clipboard, clipboardGTK.gdk_atom_intern (*target, 0))) return true;

				}

				return false;

		}

	}


	bool Clipboard::GetText (int format, std::string* text) {

		void* clipboard = GetClipboard ();
		if (!clipboard || !text) return false;

		if (format == CLIPBOARD_TEXT) {

			char* value = clipboardGTK.gtk_clipboard_wait_for_text (clipboard);
			if (!value) return false;
			*text = value;
			clipboardGTK.g_free (value);
			return true;

		}

		for (const char* const* target = GetTargets (format); *target; target++) {

			if (ReadTarget (clipboard, *target, text)) return true;

		}

		return false;

	}


	bool Clipboard::GetFiles (std::vector<std::string>* files) {

		void* clipboard = GetClipboard ();
		if (!clipboard || !files) return false;

		char** uris = clipboardGTK.gtk_clipboard_wait_for_uris (clipboard);
		if (!uris) return false;

		files->clear ();

		for (char** uri = uris; *uri; uri++) {

			char* path = clipboardGTK.g_filename_from_uri (*uri, NULL, NULL);
			if (path) { files->push_back (path); clipboardGTK.g_free (path); }

		}

		clipboardGTK.g_strfreev (uris);
		return true;

	}


	bool Clipboard::GetImage (std::vector<unsigned char>* pixels, int* width, int* height) {

		void* clipboard = GetClipboard ();
		if (!clipboard || !pixels || !width || !height) return false;

		void* pixbuf = clipboardGTK.gtk_clipboard_wait_for_image (clipboard);
		if (!pixbuf) return false;

		int w = clipboardGTK.gdk_pixbuf_get_width (pixbuf);
		int h = clipboardGTK.gdk_pixbuf_get_height (pixbuf);
		int stride = clipboardGTK.gdk_pixbuf_get_rowstride (pixbuf);
		int channels = clipboardGTK.gdk_pixbuf_get_n_channels (pixbuf);
		const unsigned char* data = clipboardGTK.gdk_pixbuf_get_pixels (pixbuf);

		// GdkPixbuf is straight RGB or RGBA, top row first
		pixels->resize ((size_t)w * h * 4);

		for (int y = 0; y < h; y++) {

			for (int x = 0; x < w; x++) {

				const unsigned char* in = data + (size_t)y * stride + (size_t)x * channels;
				unsigned char* out = &(*pixels)[((size_t)y * w + x) * 4];
				out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
				out[3] = channels == 4 ? in[3] : 255;

			}

		}

		clipboardGTK.g_object_unref (pixbuf);
		*width = w;
		*height = h;
		return true;

	}


	// What this application offers until another one takes the clipboard
	struct ClipboardOffer {

		std::vector<ClipboardItem> items;
		std::vector<std::string> targets;
		std::vector<int> formats;

	};


	static ClipboardOffer* currentOffer = NULL;


	static void ProvideClipboardData (void* clipboard, void* selection, unsigned int info, void* data) {

		ClipboardOffer* offer = (ClipboardOffer*)data;
		if (!offer || info >= offer->targets.size ()) return;

		const std::string& target = offer->targets[info];
		int format = offer->formats[info];

		for (size_t i = 0; i < offer->items.size (); i++) {

			const ClipboardItem& item = offer->items[i];
			if (item.format != format) continue;

			if (format == CLIPBOARD_FILE_LIST) {

				std::vector<char*> uris;

				for (size_t j = 0; j < item.files.size (); j++) {

					char* uri = clipboardGTK.g_filename_to_uri (item.files[j].c_str (), NULL, NULL);
					if (uri) uris.push_back (uri);

				}

				uris.push_back (NULL);
				clipboardGTK.gtk_selection_data_set_uris (selection, &uris[0]);
				for (size_t j = 0; j + 1 < uris.size (); j++) clipboardGTK.g_free (uris[j]);

			} else if (format == CLIPBOARD_IMAGE) {

				// GTK encodes the pixbuf as the image type asked for
				void* pixbuf = clipboardGTK.gdk_pixbuf_new_from_data (&item.pixels[0], 0, 1, 8, item.width, item.height, item.width * 4, NULL, NULL);
				if (pixbuf) { clipboardGTK.gtk_selection_data_set_pixbuf (selection, pixbuf); clipboardGTK.g_object_unref (pixbuf); }

			} else {

				std::string value = item.text;
				if (target == "text/uri-list") value += "\r\n";
				clipboardGTK.gtk_selection_data_set (selection, clipboardGTK.gdk_atom_intern (target.c_str (), 0), 8, (const unsigned char*)value.data (), (int)value.size ());

			}

			return;

		}

	}


	static void ClearClipboardData (void* clipboard, void* data) {

		ClipboardOffer* offer = (ClipboardOffer*)data;
		if (offer == currentOffer) currentOffer = NULL;
		delete offer;

	}


	bool Clipboard::SetItems (const std::vector<ClipboardItem>& items) {

		void* clipboard = GetClipboard ();
		if (!clipboard) return false;

		ClipboardOffer* offer = new ClipboardOffer ();
		offer->items = items;

		for (size_t i = 0; i < items.size (); i++) {

			const ClipboardItem& item = items[i];
			std::vector<const char*> targets;

			switch (item.format) {

				case CLIPBOARD_TEXT:
					targets.push_back ("UTF8_STRING"); targets.push_back ("text/plain;charset=utf-8"); targets.push_back ("STRING"); targets.push_back ("TEXT");
					break;

				case CLIPBOARD_FILE_LIST:
					targets.push_back ("text/uri-list");
					break;

				case CLIPBOARD_IMAGE:
					if (item.width <= 0 || item.height <= 0 || item.pixels.size () < (size_t)item.width * item.height * 4) continue;
					targets.push_back ("image/png");
					break;

				case CLIPBOARD_URL:
					targets.push_back ("text/uri-list");
					break;

				default:
					for (const char* const* target = GetTargets (item.format); *target; target++) targets.push_back (*target);
					break;

			}

			for (size_t j = 0; j < targets.size (); j++) {

				offer->targets.push_back (targets[j]);
				offer->formats.push_back (item.format);

			}

		}

		std::vector<GtkTargetEntryData> entries (offer->targets.size ());

		for (size_t i = 0; i < entries.size (); i++) {

			entries[i].target = (char*)offer->targets[i].c_str ();
			entries[i].flags = 0;
			entries[i].info = (unsigned int)i;

		}

		// GTK frees the last offer, with ClearClipboardData, as this one takes its place
		if (!clipboardGTK.gtk_clipboard_set_with_data (clipboard, entries.empty () ? NULL : &entries[0], (unsigned int)entries.size (), ProvideClipboardData, ClearClipboardData, offer)) {

			delete offer;
			return false;

		}

		currentOffer = offer;

		// A clipboard manager keeps a copy, so it can be pasted after the application quits
		clipboardGTK.gtk_clipboard_store (clipboard);
		return true;

	}


	void Clipboard::Update () {

		// Other applications ask for the clipboard through GTK's events, which nothing else runs
		if (!currentOffer || !clipboardGTK.available) return;

		for (int i = 0; i < 64 && clipboardGTK.gtk_events_pending (); i++) {

			clipboardGTK.gtk_main_iteration_do (0);

		}

	}


}


#endif
