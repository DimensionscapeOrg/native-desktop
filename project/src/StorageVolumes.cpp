#include <native_desktop/Desktop.h>

// The mounted volumes: drive letters on Windows, and on Linux the block devices of /proc/self/mountinfo.
// macOS is in StorageVolumesMac.mm.

#if defined(NATIVE_DESKTOP_WINDOWS)

#include <windows.h>

namespace native_desktop {


	static std::string VolumeUTF8 (const wchar_t* text) {

		int size = WideCharToMultiByte (CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
		if (size <= 1) return std::string ();
		std::string result (size - 1, '\0');
		WideCharToMultiByte (CP_UTF8, 0, text, -1, &result[0], size, NULL, NULL);
		return result;

	}


	bool StorageVolumes::GetVolumes (std::vector<StorageVolume>* volumes) {

		if (!volumes) return false;
		volumes->clear ();

		// An empty card reader or optical drive would ask the user to insert a disk without this
		UINT previousMode = SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
		DWORD drives = GetLogicalDrives ();

		for (int i = 0; i < 26; i++) {

			if (!(drives & (1u << i))) continue;

			wchar_t root[] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
			UINT type = GetDriveTypeW (root);
			if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) continue;

			wchar_t label[MAX_PATH + 1] = {};
			wchar_t fileSystem[MAX_PATH + 1] = {};
			DWORD flags = 0;

			// A drive without a disk in it has nothing mounted
			if (!GetVolumeInformationW (root, label, MAX_PATH + 1, NULL, NULL, &flags, fileSystem, MAX_PATH + 1)) continue;

			StorageVolume volume;
			volume.rootDirectory = VolumeUTF8 (root);
			volume.drive = std::string (1, (char)('A' + i));
			volume.name = label[0] ? VolumeUTF8 (label) : volume.drive + ":";
			volume.fileSystemType = VolumeUTF8 (fileSystem);
			volume.isRemovable = type == DRIVE_REMOVABLE || type == DRIVE_CDROM;
			volume.isWritable = !(flags & FILE_READ_ONLY_VOLUME);
			volumes->push_back (volume);

		}

		SetErrorMode (previousMode);
		return true;

	}


}

#elif defined(NATIVE_DESKTOP_LINUX)

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>

namespace native_desktop {


	// mountinfo escapes spaces and other characters as \ooo
	static std::string Unescape (const std::string& text) {

		std::string result;

		for (size_t i = 0; i < text.size (); i++) {

			if (text[i] == '\\' && i + 3 < text.size ()) {

				int value = 0;
				bool octal = true;

				for (int j = 1; j <= 3; j++) {

					char c = text[i + j];
					if (c < '0' || c > '7') { octal = false; break; }
					value = value * 8 + (c - '0');

				}

				if (octal) { result += (char)value; i += 3; continue; }

			}

			result += text[i];

		}

		return result;

	}


	// The kernel's removable flag of the whole disk, as /sys/block/sdb for /dev/sdb1
	static bool IsRemovableDevice (const std::string& source) {

		if (source.compare (0, 5, "/dev/") != 0) return false;

		std::string device = source.substr (5);
		char real[4096];
		std::string link = "/sys/class/block/" + device;
		ssize_t length = readlink (link.c_str (), real, sizeof (real) - 1);
		if (length <= 0) return false;
		real[length] = 0;

		// A partition's folder is inside its disk's, and the disk has the removable file
		std::string path = std::string ("/sys/class/block/") + real;
		for (int depth = 0; depth < 2; depth++) {

			FILE* file = fopen ((path + "/removable").c_str (), "r");

			if (file) {

				int value = fgetc (file);
				fclose (file);
				return value == '1';

			}

			size_t slash = path.rfind ('/');
			if (slash == std::string::npos) break;
			path.resize (slash);

		}

		return false;

	}


	bool StorageVolumes::GetVolumes (std::vector<StorageVolume>* volumes) {

		if (!volumes) return false;
		volumes->clear ();

		FILE* file = fopen ("/proc/self/mountinfo", "r");
		if (!file) return false;

		char line[8192];

		while (fgets (line, sizeof (line), file)) {

			// id parent major:minor root mountPoint options [optional fields] - fileSystem source superOptions
			std::string text (line);
			size_t separator = text.find (" - ");
			if (separator == std::string::npos) continue;

			char root[4096], mountPoint[4096], options[1024];
			if (sscanf (line, "%*s %*s %*s %4095s %4095s %1023s", root, mountPoint, options) != 3) continue;

			char fileSystem[256], source[4096];
			if (sscanf (text.c_str () + separator + 3, "%255s %4095s", fileSystem, source) != 2) continue;

			// Block devices, which leaves out /proc, tmpfs and the like, but not their bind mounts of folders
			if (strncmp (source, "/dev/", 5) != 0 || strcmp (root, "/") != 0) continue;

			StorageVolume volume;
			volume.rootDirectory = Unescape (mountPoint);
			std::string base = volume.rootDirectory.substr (volume.rootDirectory.rfind ('/') + 1);
			volume.name = base.empty () ? volume.rootDirectory : base;
			volume.fileSystemType = fileSystem;
			volume.isRemovable = IsRemovableDevice (source);
			volume.isWritable = strncmp (options, "rw", 2) == 0 && (options[2] == ',' || options[2] == 0);
			volumes->push_back (volume);

		}

		fclose (file);
		return true;

	}


}

#endif
