#ifndef NATIVE_DESKTOP_CLIPBOARD_MAC_FORMATS_H
#define NATIVE_DESKTOP_CLIPBOARD_MAC_FORMATS_H

#import <Cocoa/Cocoa.h>
#include <native_desktop/Desktop.h>


// The clipboard's formats on macOS, read from any pasteboard: the general one, or a drag's
namespace native_desktop {


	bool MacPasteboardHasFormat (NSPasteboard* pasteboard, int format);
	bool MacPasteboardGetText (NSPasteboard* pasteboard, int format, std::string* text);
	bool MacPasteboardGetFiles (NSPasteboard* pasteboard, std::vector<std::string>* files);
	bool MacPasteboardGetImage (NSPasteboard* pasteboard, std::vector<unsigned char>* pixels, int* width, int* height);


}


#endif
