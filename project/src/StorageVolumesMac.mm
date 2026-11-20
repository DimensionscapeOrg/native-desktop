#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// The mounted volumes on macOS, as the Finder shows them

#import <Foundation/Foundation.h>
#include <sys/mount.h>


namespace native_desktop {


	bool StorageVolumes::GetVolumes (std::vector<StorageVolume>* volumes) {

		if (!volumes) return false;
		volumes->clear ();

		@autoreleasepool {

			NSArray* keys = @[ NSURLVolumeNameKey, NSURLVolumeIsRemovableKey, NSURLVolumeIsEjectableKey, NSURLVolumeIsReadOnlyKey ];
			NSArray* urls = [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:keys options:NSVolumeEnumerationSkipHiddenVolumes];
			if (!urls) return false;

			for (NSURL* url in urls) {

				NSDictionary* values = [url resourceValuesForKeys:keys error:nil];
				const char* path = [[url path] fileSystemRepresentation];
				if (!path) continue;

				StorageVolume volume;
				volume.rootDirectory = path;

				NSString* name = [values objectForKey:NSURLVolumeNameKey];
				volume.name = name ? [name UTF8String] : volume.rootDirectory;

				// Removable media, such as a memory card, or ejectable, such as a USB drive or a disk image
				volume.isRemovable = [[values objectForKey:NSURLVolumeIsRemovableKey] boolValue] || [[values objectForKey:NSURLVolumeIsEjectableKey] boolValue];
				volume.isWritable = ![[values objectForKey:NSURLVolumeIsReadOnlyKey] boolValue];

				struct statfs info;
				if (statfs (path, &info) == 0) volume.fileSystemType = info.f_fstypename;

				volumes->push_back (volume);

			}

			return true;

		}

	}


}


#endif
