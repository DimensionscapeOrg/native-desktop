#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// Clipboard formats for macOS, on the general NSPasteboard

#import <Cocoa/Cocoa.h>
#include "ClipboardMacFormats.h"


namespace native_desktop {


	static NSString* GetPasteboardType (int format) {

		switch (format) {

			case CLIPBOARD_TEXT: return NSPasteboardTypeString;
			case CLIPBOARD_HTML: return NSPasteboardTypeHTML;
			case CLIPBOARD_RICH_TEXT: return NSPasteboardTypeRTF;
			case CLIPBOARD_URL: return NSPasteboardTypeURL;
			case CLIPBOARD_FILE_LIST: return NSPasteboardTypeFileURL;
			case CLIPBOARD_IMAGE: return NSPasteboardTypeTIFF;
			default: return nil;

		}

	}


	bool Clipboard::IsSupported () {

		return true;

	}


	bool MacPasteboardHasFormat (NSPasteboard* pasteboard, int format) {

		@autoreleasepool {


			if (format == CLIPBOARD_IMAGE) {

				return [pasteboard canReadObjectForClasses:@[[NSImage class]] options:nil];

			}

			if (format == CLIPBOARD_FILE_LIST) {

				return [pasteboard canReadObjectForClasses:@[[NSURL class]] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];

			}

			NSString* type = GetPasteboardType (format);
			return type && [pasteboard availableTypeFromArray:@[type]] != nil;

		}

	}


	bool MacPasteboardGetText (NSPasteboard* pasteboard, int format, std::string* text) {

		if (!text) return false;

		@autoreleasepool {

			NSString* value = nil;

			if (format == CLIPBOARD_URL) {

				// A URL may be on the pasteboard only as an NSURL
				NSArray* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
				if ([urls count] > 0) value = [[urls objectAtIndex:0] absoluteString];

			} else if (format == CLIPBOARD_RICH_TEXT) {

				// RTF is ASCII, kept as the bytes copied
				NSData* data = [pasteboard dataForType:NSPasteboardTypeRTF];
				if (data) { text->assign ((const char*)[data bytes], [data length]); return true; }
				return false;

			} else {

				NSString* type = GetPasteboardType (format);
				if (type) value = [pasteboard stringForType:type];

			}

			if (!value) return false;
			const char* utf8 = [value UTF8String];
			*text = utf8 ? utf8 : "";
			return true;

		}

	}


	bool MacPasteboardGetFiles (NSPasteboard* pasteboard, std::vector<std::string>* files) {

		if (!files) return false;

		@autoreleasepool {

			NSArray* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
			if (!urls || [urls count] == 0) return false;

			files->clear ();

			for (NSURL* url in urls) {

				// Precomposed, as paths are given and as other platforms give them. File URLs hold names
				// decomposed, and macOS opens a file by either form.
				const char* path = [[[url path] precomposedStringWithCanonicalMapping] UTF8String];
				if (path) files->push_back (path);

			}

			return true;

		}

	}


	bool MacPasteboardGetImage (NSPasteboard* pasteboard, std::vector<unsigned char>* pixels, int* width, int* height) {

		if (!pixels || !width || !height) return false;

		@autoreleasepool {

			NSArray* images = [pasteboard readObjectsForClasses:@[[NSImage class]] options:nil];
			if (!images || [images count] == 0) return false;

			NSImage* image = [images objectAtIndex:0];
			CGImageRef cgImage = [image CGImageForProposedRect:NULL context:nil hints:nil];
			if (!cgImage) return false;

			int w = (int)CGImageGetWidth (cgImage);
			int h = (int)CGImageGetHeight (cgImage);
			if (w <= 0 || h <= 0) return false;

			// Drawn into straight RGBA bytes, after drawing into premultiplied ones, which CoreGraphics needs
			pixels->assign ((size_t)w * h * 4, 0);
			CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
			CGContextRef context = CGBitmapContextCreate (&(*pixels)[0], w, h, 8, w * 4, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
			CGColorSpaceRelease (colorSpace);
			if (!context) return false;

			CGContextDrawImage (context, CGRectMake (0, 0, w, h), cgImage);
			CGContextRelease (context);

			for (size_t i = 0; i < pixels->size (); i += 4) {

				unsigned char alpha = (*pixels)[i + 3];

				if (alpha > 0 && alpha < 255) {

					for (int c = 0; c < 3; c++) (*pixels)[i + c] = (unsigned char)(((*pixels)[i + c] * 255 + alpha / 2) / alpha);

				}

			}

			*width = w;
			*height = h;
			return true;

		}

	}


	bool Clipboard::HasFormat (int format) { return MacPasteboardHasFormat ([NSPasteboard generalPasteboard], format); }
	bool Clipboard::GetText (int format, std::string* text) { return MacPasteboardGetText ([NSPasteboard generalPasteboard], format, text); }
	bool Clipboard::GetFiles (std::vector<std::string>* files) { return MacPasteboardGetFiles ([NSPasteboard generalPasteboard], files); }
	bool Clipboard::GetImage (std::vector<unsigned char>* pixels, int* width, int* height) { return MacPasteboardGetImage ([NSPasteboard generalPasteboard], pixels, width, height); }


	bool Clipboard::SetItems (const std::vector<ClipboardItem>& items) {

		if (!Menu::IsOwnerThread ()) return false;

		@autoreleasepool {

			NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
			[pasteboard clearContents];
			bool result = true;

			NSMutableArray* objects = [NSMutableArray array];

			for (size_t i = 0; i < items.size (); i++) {

				const ClipboardItem& item = items[i];
				NSString* string = [NSString stringWithUTF8String:item.text.c_str ()];

				switch (item.format) {

					case CLIPBOARD_TEXT:
					case CLIPBOARD_HTML:
						result = [pasteboard setString:(string ? string : @"") forType:GetPasteboardType (item.format)] && result;
						break;

					case CLIPBOARD_RICH_TEXT:
						result = [pasteboard setData:[NSData dataWithBytes:item.text.data () length:item.text.size ()] forType:NSPasteboardTypeRTF] && result;
						break;

					case CLIPBOARD_URL: {

						NSURL* url = string ? [NSURL URLWithString:string] : nil;
						if (url) [objects addObject:url]; else result = false;
						break;

					}

					case CLIPBOARD_FILE_LIST:

						for (size_t j = 0; j < item.files.size (); j++) {

							NSString* path = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:item.files[j].c_str () length:item.files[j].size ()];
							if (path) [objects addObject:[NSURL fileURLWithPath:path]];

						}
						break;

					case CLIPBOARD_IMAGE: {

						if (item.width <= 0 || item.height <= 0 || item.pixels.size () < (size_t)item.width * item.height * 4) { result = false; break; }

						NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:item.width pixelsHigh:item.height
							bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
							bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:item.width * 4 bitsPerPixel:32];

						if (!rep) { result = false; break; }
						memcpy ([rep bitmapData], &item.pixels[0], (size_t)item.width * item.height * 4);

						// PNG and TIFF, which other applications read most
						NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
						if (png) result = [pasteboard setData:png forType:NSPasteboardTypePNG] && result;
						result = [pasteboard setData:[rep TIFFRepresentation] forType:NSPasteboardTypeTIFF] && result;

						#if !__has_feature(objc_arc)
						[rep release];
						#endif
						break;

					}

					default: break;

				}

			}

			// URLs and file URLs are written as objects, which gives each of them its pasteboard item
			if ([objects count] > 0) result = [pasteboard writeObjects:objects] && result;
			return result;

		}

	}


	void Clipboard::Update () {}


}


#endif
