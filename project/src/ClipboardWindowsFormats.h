#ifndef NATIVE_DESKTOP_CLIPBOARD_WINDOWS_FORMATS_H
#define NATIVE_DESKTOP_CLIPBOARD_WINDOWS_FORMATS_H

#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_WINDOWS

#include <windows.h>
#include <functional>
#include <string>
#include <vector>


// The clipboard's formats on Windows, read from and written to whatever holds them: the clipboard, or the
// IDataObject of a drag
namespace native_desktop {


	typedef std::function<bool (UINT format)> WindowsFormatCheck;
	typedef std::function<bool (UINT format, std::vector<unsigned char>* data)> WindowsFormatRead;

	struct WindowsFormatData {

		UINT format = 0;
		std::vector<unsigned char> data;

	};

	bool WindowsHasClipboardFormat (int format, const WindowsFormatCheck& has);
	bool WindowsReadClipboardText (int format, const WindowsFormatRead& read, std::string* text);
	bool WindowsReadClipboardFiles (const WindowsFormatRead& read, std::vector<std::string>* files);
	bool WindowsReadClipboardImage (const WindowsFormatRead& read, std::vector<unsigned char>* pixels, int* width, int* height);
	// The native formats for each item. False when an item cannot be written, as an image without its pixels.
	bool WindowsEncodeClipboardItems (const std::vector<ClipboardItem>& items, std::vector<WindowsFormatData>* formats);


}


#endif
#endif
