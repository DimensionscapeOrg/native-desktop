#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// Moving files and folders to the trash on macOS

#import <Foundation/Foundation.h>


namespace native_desktop {


	bool Trash::IsSupported () {

		return true;

	}


	bool Trash::MoveToTrash (const char* path) {

		if (!path || !path[0]) return false;

		@autoreleasepool {

			NSString* string = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path length:strlen (path)];
			if (!string) return false;

			// NSFileManager may be used from any thread
			NSURL* url = [NSURL fileURLWithPath:string];
			return [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:nil] == YES;

		}

	}


}


#endif
