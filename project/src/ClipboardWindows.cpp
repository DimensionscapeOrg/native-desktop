#include <native_desktop/Desktop.h>
#include "ClipboardWindowsFormats.h"

#ifdef NATIVE_DESKTOP_WINDOWS

// Clipboard formats for Windows: CF_UNICODETEXT, "HTML Format", "Rich Text Format", "UniformResourceLocatorW",
// CF_HDROP, and CF_DIBV5 or CF_DIB for images

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace native_desktop {


	static std::wstring ClipboardWide (const std::string& text) {

		if (text.empty ()) return std::wstring ();
		int size = MultiByteToWideChar (CP_UTF8, 0, text.c_str (), (int)text.size (), NULL, 0);
		std::wstring result (size > 0 ? size : 0, L'\0');
		if (size > 0) MultiByteToWideChar (CP_UTF8, 0, text.c_str (), (int)text.size (), &result[0], size);
		return result;

	}


	static std::string ClipboardUTF8 (const wchar_t* text, int length = -1) {

		if (!text) return std::string ();
		int size = WideCharToMultiByte (CP_UTF8, 0, text, length, NULL, 0, NULL, NULL);
		if (size <= 0) return std::string ();
		std::string result (size, '\0');
		WideCharToMultiByte (CP_UTF8, 0, text, length, &result[0], size, NULL, NULL);
		if (length < 0 && !result.empty () && result.back () == '\0') result.pop_back ();
		return result;

	}


	static UINT HTMLFormat () { static UINT format = RegisterClipboardFormatW (L"HTML Format"); return format; }
	static UINT RichTextFormat () { static UINT format = RegisterClipboardFormatW (L"Rich Text Format"); return format; }
	static UINT URLFormat () { static UINT format = RegisterClipboardFormatW (L"UniformResourceLocatorW"); return format; }
	static UINT ANSIURLFormat () { static UINT format = RegisterClipboardFormatW (L"UniformResourceLocator"); return format; }


	// The clipboard only takes data from an owner, so a hidden window owns what is set
	static HWND ClipboardOwner () {

		static HWND window = NULL;
		if (window) return window;

		HINSTANCE instance = NULL;
		GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ClipboardOwner, &instance);

		WNDCLASSEXW windowClass;
		ZeroMemory (&windowClass, sizeof (windowClass));
		windowClass.cbSize = sizeof (windowClass);
		windowClass.lpfnWndProc = DefWindowProcW;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = L"NativeDesktopClipboardWindow";
		RegisterClassExW (&windowClass);

		window = CreateWindowExW (0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, instance, NULL);
		return window;

	}


	// Another application may hold the clipboard for a moment
	static bool OpenWindowsClipboard (HWND owner) {

		for (int i = 0; i < 10; i++) {

			if (OpenClipboard (owner)) return true;
			Sleep (5);

		}

		return false;

	}


	static bool ReadClipboardBytes (UINT format, std::vector<unsigned char>* data) {

		HANDLE handle = GetClipboardData (format);
		if (!handle) return false;

		SIZE_T size = GlobalSize (handle);
		const unsigned char* bytes = (const unsigned char*)GlobalLock (handle);
		if (!bytes) return false;

		data->assign (bytes, bytes + size);
		GlobalUnlock (handle);
		return true;

	}


	static HGLOBAL CreateGlobal (const void* data, size_t size) {

		HGLOBAL memory = GlobalAlloc (GMEM_MOVEABLE, size);
		if (!memory) return NULL;

		void* target = GlobalLock (memory);
		if (!target) { GlobalFree (memory); return NULL; }

		memcpy (target, data, size);
		GlobalUnlock (memory);
		return memory;

	}


	static bool PutClipboardData (UINT format, const void* data, size_t size) {

		HGLOBAL memory = CreateGlobal (data, size);
		if (!memory) return false;

		// The clipboard owns the memory once it is set
		if (!SetClipboardData (format, memory)) { GlobalFree (memory); return false; }
		return true;

	}


	// The fragment of an "HTML Format" header, between StartFragment and EndFragment
	static bool GetHTMLFragment (const std::string& data, std::string* fragment) {

		size_t start = data.find ("StartFragment:");
		size_t end = data.find ("EndFragment:");
		if (start == std::string::npos || end == std::string::npos) return false;

		long from = strtol (data.c_str () + start + 14, NULL, 10);
		long to = strtol (data.c_str () + end + 12, NULL, 10);
		if (from < 0 || to < from || (size_t)to > data.size ()) return false;

		*fragment = data.substr (from, to - from);
		return true;

	}


	static std::string CreateHTMLFormat (const std::string& fragment) {

		// The offsets have a fixed width, so they can be written once the lengths are known
		const char* header = "Version:0.9\r\nStartHTML:%010u\r\nEndHTML:%010u\r\nStartFragment:%010u\r\nEndFragment:%010u\r\n";
		const std::string prefix = "<html><body>\r\n<!--StartFragment-->";
		const std::string suffix = "<!--EndFragment-->\r\n</body></html>";

		char measure[256];
		int headerLength = snprintf (measure, sizeof (measure), header, 0u, 0u, 0u, 0u);

		unsigned startHTML = (unsigned)headerLength;
		unsigned startFragment = startHTML + (unsigned)prefix.size ();
		unsigned endFragment = startFragment + (unsigned)fragment.size ();
		unsigned endHTML = endFragment + (unsigned)suffix.size ();

		char text[256];
		snprintf (text, sizeof (text), header, startHTML, endHTML, startFragment, endFragment);
		return std::string (text) + prefix + fragment + suffix;

	}


	// Reads a packed DIB of 24 or 32 bits into straight RGBA, top row first
	static bool ReadDIB (const std::vector<unsigned char>& data, std::vector<unsigned char>* pixels, int* width, int* height) {

		if (data.size () < sizeof (BITMAPINFOHEADER)) return false;

		BITMAPINFOHEADER header;
		memcpy (&header, &data[0], sizeof (header));

		int bits = header.biBitCount;
		if ((bits != 32 && bits != 24) || header.biWidth <= 0 || header.biHeight == 0) return false;
		if (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS) return false;

		DWORD redMask = 0x00FF0000, greenMask = 0x0000FF00, blueMask = 0x000000FF, alphaMask = 0;
		size_t offset = header.biSize;

		if (header.biCompression == BI_BITFIELDS) {

			if (header.biSize >= sizeof (BITMAPV4HEADER)) {

				BITMAPV4HEADER v4;
				memcpy (&v4, &data[0], sizeof (v4));
				redMask = v4.bV4RedMask; greenMask = v4.bV4GreenMask; blueMask = v4.bV4BlueMask; alphaMask = v4.bV4AlphaMask;

			} else {

				// The masks follow a plain header
				if (data.size () < offset + 12) return false;
				memcpy (&redMask, &data[offset], 4); memcpy (&greenMask, &data[offset + 4], 4); memcpy (&blueMask, &data[offset + 8], 4);
				offset += 12;

			}

		} else if (header.biSize >= sizeof (BITMAPV4HEADER) && bits == 32) {

			BITMAPV4HEADER v4;
			memcpy (&v4, &data[0], sizeof (v4));
			alphaMask = v4.bV4AlphaMask;

		}

		offset += header.biClrUsed * 4;

		int w = header.biWidth;
		int h = header.biHeight < 0 ? -header.biHeight : header.biHeight;
		bool bottomUp = header.biHeight > 0;
		size_t stride = ((size_t)w * bits / 8 + 3) & ~(size_t)3;

		if (data.size () < offset + stride * h) return false;

		// Many applications leave the alpha of a 32-bit DIB at 0, which means opaque
		if (bits == 32 && alphaMask == 0) alphaMask = 0xFF000000;

		auto shiftOf = [] (DWORD mask) { int shift = 0; if (!mask) return 0; while (!(mask & 1)) { mask >>= 1; shift++; } return shift; };
		int redShift = shiftOf (redMask), greenShift = shiftOf (greenMask), blueShift = shiftOf (blueMask), alphaShift = shiftOf (alphaMask);

		pixels->resize ((size_t)w * h * 4);
		bool anyAlpha = false;

		for (int y = 0; y < h; y++) {

			const unsigned char* row = &data[offset + stride * (bottomUp ? h - 1 - y : y)];
			unsigned char* out = &(*pixels)[(size_t)y * w * 4];

			for (int x = 0; x < w; x++) {

				if (bits == 24) {

					out[x * 4] = row[x * 3 + 2];
					out[x * 4 + 1] = row[x * 3 + 1];
					out[x * 4 + 2] = row[x * 3];
					out[x * 4 + 3] = 255;

				} else {

					DWORD value;
					memcpy (&value, row + x * 4, 4);
					out[x * 4] = (unsigned char)((value & redMask) >> redShift);
					out[x * 4 + 1] = (unsigned char)((value & greenMask) >> greenShift);
					out[x * 4 + 2] = (unsigned char)((value & blueMask) >> blueShift);
					out[x * 4 + 3] = (unsigned char)((value & alphaMask) >> alphaShift);
					if (out[x * 4 + 3]) anyAlpha = true;

				}

			}

		}

		if (bits == 32 && !anyAlpha) {

			for (size_t i = 3; i < pixels->size (); i += 4) (*pixels)[i] = 255;

		}

		*width = w;
		*height = h;
		return true;

	}


	static std::vector<unsigned char> CreateDIBV5 (const ClipboardItem& item) {

		BITMAPV5HEADER header;
		ZeroMemory (&header, sizeof (header));
		header.bV5Size = sizeof (header);
		header.bV5Width = item.width;
		header.bV5Height = item.height;
		header.bV5Planes = 1;
		header.bV5BitCount = 32;
		header.bV5Compression = BI_BITFIELDS;
		header.bV5SizeImage = (DWORD)(item.width * item.height * 4);
		header.bV5RedMask = 0x00FF0000;
		header.bV5GreenMask = 0x0000FF00;
		header.bV5BlueMask = 0x000000FF;
		header.bV5AlphaMask = 0xFF000000;
		header.bV5CSType = LCS_sRGB;
		header.bV5Intent = LCS_GM_IMAGES;

		std::vector<unsigned char> data (sizeof (header) + header.bV5SizeImage);
		memcpy (&data[0], &header, sizeof (header));

		// Bottom row first, as a positive height says, in BGRA
		for (int y = 0; y < item.height; y++) {

			const unsigned char* row = &item.pixels[(size_t)(item.height - 1 - y) * item.width * 4];
			unsigned char* out = &data[sizeof (header) + (size_t)y * item.width * 4];

			for (int x = 0; x < item.width; x++) {

				out[x * 4] = row[x * 4 + 2];
				out[x * 4 + 1] = row[x * 4 + 1];
				out[x * 4 + 2] = row[x * 4];
				out[x * 4 + 3] = row[x * 4 + 3];

			}

		}

		return data;

	}


	bool WindowsHasClipboardFormat (int format, const WindowsFormatCheck& has) {

		switch (format) {

			case CLIPBOARD_TEXT: return has (CF_UNICODETEXT);
			case CLIPBOARD_HTML: return has (HTMLFormat ());
			case CLIPBOARD_RICH_TEXT: return has (RichTextFormat ());
			case CLIPBOARD_URL: return has (URLFormat ()) || has (ANSIURLFormat ());
			case CLIPBOARD_FILE_LIST: return has (CF_HDROP);
			case CLIPBOARD_IMAGE: return has (CF_DIBV5) || has (CF_DIB);
			default: return false;

		}

	}


	bool WindowsReadClipboardText (int format, const WindowsFormatRead& read, std::string* text) {

		std::vector<unsigned char> data;

		switch (format) {

			case CLIPBOARD_TEXT:

				if (read (CF_UNICODETEXT, &data) && data.size () >= 2) {

					*text = ClipboardUTF8 ((const wchar_t*)&data[0], (int)(wcsnlen ((const wchar_t*)&data[0], data.size () / 2)));
					return true;

				}
				return false;

			case CLIPBOARD_HTML:

				if (read (HTMLFormat (), &data) && !data.empty ()) {

					std::string html ((const char*)&data[0], strnlen ((const char*)&data[0], data.size ()));
					return GetHTMLFragment (html, text);

				}
				return false;

			case CLIPBOARD_RICH_TEXT:

				if (read (RichTextFormat (), &data) && !data.empty ()) {

					text->assign ((const char*)&data[0], strnlen ((const char*)&data[0], data.size ()));
					return true;

				}
				return false;

			case CLIPBOARD_URL:

				if (read (URLFormat (), &data) && data.size () >= 2) {

					*text = ClipboardUTF8 ((const wchar_t*)&data[0], (int)(wcsnlen ((const wchar_t*)&data[0], data.size () / 2)));
					return true;

				} else if (read (ANSIURLFormat (), &data) && !data.empty ()) {

					std::string ansi ((const char*)&data[0], strnlen ((const char*)&data[0], data.size ()));
					int size = MultiByteToWideChar (CP_ACP, 0, ansi.c_str (), (int)ansi.size (), NULL, 0);
					std::wstring wide (size > 0 ? size : 0, L'\0');
					if (size > 0) MultiByteToWideChar (CP_ACP, 0, ansi.c_str (), (int)ansi.size (), &wide[0], size);
					*text = ClipboardUTF8 (wide.c_str (), (int)wide.size ());
					return true;

				}
				return false;

			default:

				return false;

		}

	}


	// CF_HDROP holds a DROPFILES header, then each path and a null, then one more null
	bool WindowsReadClipboardFiles (const WindowsFormatRead& read, std::vector<std::string>* files) {

		std::vector<unsigned char> data;
		if (!read (CF_HDROP, &data) || data.size () < sizeof (DROPFILES)) return false;

		const DROPFILES* drop = (const DROPFILES*)&data[0];
		if (drop->pFiles < sizeof (DROPFILES) || drop->pFiles > data.size ()) return false;

		files->clear ();

		if (drop->fWide) {

			const wchar_t* path = (const wchar_t*)&data[drop->pFiles];
			const wchar_t* end = (const wchar_t*)(&data[0] + data.size ());

			while (path < end && *path) {

				size_t length = wcsnlen (path, end - path);
				files->push_back (ClipboardUTF8 (path, (int)length));
				path += length + 1;

			}

		} else {

			const char* path = (const char*)&data[drop->pFiles];
			const char* end = (const char*)(&data[0] + data.size ());

			while (path < end && *path) {

				size_t length = strnlen (path, end - path);
				int size = MultiByteToWideChar (CP_ACP, 0, path, (int)length, NULL, 0);
				std::wstring wide (size > 0 ? size : 0, L'\0');
				if (size > 0) MultiByteToWideChar (CP_ACP, 0, path, (int)length, &wide[0], size);
				files->push_back (ClipboardUTF8 (wide.c_str (), (int)wide.size ()));
				path += length + 1;

			}

		}

		return true;

	}


	bool WindowsReadClipboardImage (const WindowsFormatRead& read, std::vector<unsigned char>* pixels, int* width, int* height) {

		// CF_DIBV5 keeps alpha, where Windows has it
		std::vector<unsigned char> data;
		return (read (CF_DIBV5, &data) && ReadDIB (data, pixels, width, height))
			|| (read (CF_DIB, &data) && ReadDIB (data, pixels, width, height));

	}


	bool WindowsEncodeClipboardItems (const std::vector<ClipboardItem>& items, std::vector<WindowsFormatData>* formats) {

		bool result = true;

		for (size_t i = 0; i < items.size (); i++) {

			const ClipboardItem& item = items[i];
			WindowsFormatData entry;

			switch (item.format) {

				case CLIPBOARD_TEXT: {

					std::wstring text = ClipboardWide (item.text);
					entry.format = CF_UNICODETEXT;
					entry.data.assign ((const unsigned char*)text.c_str (), (const unsigned char*)(text.c_str () + text.size () + 1));
					break;

				}

				case CLIPBOARD_HTML: {

					std::string html = CreateHTMLFormat (item.text);
					entry.format = HTMLFormat ();
					entry.data.assign ((const unsigned char*)html.c_str (), (const unsigned char*)html.c_str () + html.size () + 1);
					break;

				}

				case CLIPBOARD_RICH_TEXT:

					entry.format = RichTextFormat ();
					entry.data.assign ((const unsigned char*)item.text.c_str (), (const unsigned char*)item.text.c_str () + item.text.size () + 1);
					break;

				case CLIPBOARD_URL: {

					std::wstring url = ClipboardWide (item.text);
					entry.format = URLFormat ();
					entry.data.assign ((const unsigned char*)url.c_str (), (const unsigned char*)(url.c_str () + url.size () + 1));
					break;

				}

				case CLIPBOARD_FILE_LIST: {

					// DROPFILES, then each path and a null, then one more null
					std::wstring list;
					for (size_t j = 0; j < item.files.size (); j++) { list += ClipboardWide (item.files[j]); list += L'\0'; }
					list += L'\0';

					entry.format = CF_HDROP;
					entry.data.resize (sizeof (DROPFILES) + list.size () * sizeof (wchar_t));
					DROPFILES* drop = (DROPFILES*)&entry.data[0];
					drop->pFiles = sizeof (DROPFILES);
					drop->fWide = TRUE;
					memcpy (&entry.data[sizeof (DROPFILES)], list.c_str (), list.size () * sizeof (wchar_t));
					break;

				}

				case CLIPBOARD_IMAGE:

					if (item.width > 0 && item.height > 0 && item.pixels.size () >= (size_t)item.width * item.height * 4) {

						// Windows makes CF_DIB and CF_BITMAP from it for applications that ask for those
						entry.format = CF_DIBV5;
						entry.data = CreateDIBV5 (item);

					} else {

						result = false;
						continue;

					}
					break;

				default:

					continue;

			}

			formats->push_back (entry);

		}

		return result;

	}


	static bool HasClipboardData (UINT format) {

		return IsClipboardFormatAvailable (format) != 0;

	}


	bool Clipboard::IsSupported () {

		return true;

	}


	bool Clipboard::HasFormat (int format) {

		return WindowsHasClipboardFormat (format, HasClipboardData);

	}


	bool Clipboard::GetText (int format, std::string* text) {

		if (!text || !OpenWindowsClipboard (ClipboardOwner ())) return false;

		bool result = WindowsReadClipboardText (format, ReadClipboardBytes, text);
		CloseClipboard ();
		return result;

	}


	bool Clipboard::GetFiles (std::vector<std::string>* files) {

		if (!files || !OpenWindowsClipboard (ClipboardOwner ())) return false;

		bool result = WindowsReadClipboardFiles (ReadClipboardBytes, files);
		CloseClipboard ();
		return result;

	}


	bool Clipboard::GetImage (std::vector<unsigned char>* pixels, int* width, int* height) {

		if (!pixels || !width || !height || !OpenWindowsClipboard (ClipboardOwner ())) return false;

		bool result = WindowsReadClipboardImage (ReadClipboardBytes, pixels, width, height);
		CloseClipboard ();
		return result;

	}


	bool Clipboard::SetItems (const std::vector<ClipboardItem>& items) {

		if (!Menu::IsOwnerThread ()) return false;

		std::vector<WindowsFormatData> formats;
		bool result = WindowsEncodeClipboardItems (items, &formats);

		if (!OpenWindowsClipboard (ClipboardOwner ())) return false;

		EmptyClipboard ();

		for (size_t i = 0; i < formats.size (); i++) {

			result = PutClipboardData (formats[i].format, &formats[i].data[0], formats[i].data.size ()) && result;

		}

		CloseClipboard ();
		return result;

	}


	void Clipboard::Update () {}


}


#endif
